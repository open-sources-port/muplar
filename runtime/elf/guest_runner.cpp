// runtime/elf/guest_runner.cpp
//
// GuestRunner::run() — wraps elfuse bootstrap and registers the HVC #6
// embedder hook added to guest_t in the forked elfuse.
//
// HVC immediate assignment:
//   #5  → elfuse Linux syscall forwarding (unchanged, handled by vcpu_run_loop)
//   #6  → muplar dispatch via g.hvc6_handler callback
//         X8 = call number, X0–X7 = arguments

#include "guest_runner.h"

#include <array>
#include <algorithm>
#include <stdexcept>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <chrono>
#include <filesystem>
#include <limits>
#include <optional>
#include <thread>
#include <vector>

#ifdef PF_R
#  undef PF_R
#  undef PF_W
#  undef PF_X
#endif
#include <elf.h>
#include <libkern/OSCacheControl.h>

extern "C" {
    #include "core/bootstrap.h"
    #include "core/guest.h"
    #include "debug/log.h"
    #include "runtime/forkipc.h"
    #include "shim_blob.h"
    #include "syscall/internal.h"
    #include "syscall/proc.h"
    extern char** environ;
}

#include <Hypervisor/Hypervisor.h>

#include "../jni/jni_env.h"
#include "../jni/jni_bridge.h"
#include "../jni/jni_onload.h"
#include "../android/android_runtime.h"

namespace muplar::runtime::elf {

#ifndef R_AARCH64_ABS64
#define R_AARCH64_ABS64     257
#define R_AARCH64_GLOB_DAT 1025
#define R_AARCH64_JUMP_SLOT 1026
#define R_AARCH64_RELATIVE 1027
#endif

static constexpr int64_t MU_DT_ANDROID_RELR    = 0x6fffe000;
static constexpr int64_t MU_DT_ANDROID_RELRSZ  = 0x6fffe001;
static constexpr int64_t MU_DT_ANDROID_RELRENT = 0x6fffe003;

static uint64_t read_guest_u64_or_zero(guest_t *g, uint64_t gpa)
{
    uint64_t value = 0;
    if (guest_read(g, gpa, &value, sizeof(value)) != 0)
        return 0;
    return value;
}

static uint64_t dyn_val(const Elf64_Dyn& dyn)
{
    return dyn.d_un.d_val;
}

static const char** to_cstrings(const std::vector<std::string>& v)
{
    auto** arr = static_cast<const char**>(std::calloc(v.size(), sizeof(const char*)));
    if (!arr) return nullptr;
    for (size_t i = 0; i < v.size(); ++i) {
        arr[i] = strdup(v[i].c_str());
        if (!arr[i]) {
            for (size_t j = 0; j < i; ++j) free(const_cast<char*>(arr[j]));
            free(arr);
            return nullptr;
        }
    }
    return arr;
}

static void free_cstrings(const char** arr, int n)
{
    if (!arr) return;
    for (int i = 0; i < n; ++i) free(const_cast<char*>(arr[i]));
    free(arr);
}

static std::string normalize_jni_class_name(std::string name)
{
    for (char& c : name) {
        if (c == '.') c = '/';
    }
    if (name.size() >= 2 && name.front() == 'L' && name.back() == ';') {
        name = name.substr(1, name.size() - 2);
    }
    return name;
}

static std::string jni_mangle(const std::string& value)
{
    std::string out;
    char escaped[8];
    for (unsigned char c : value) {
        if (std::isalnum(c)) {
            out.push_back(static_cast<char>(c));
        } else if (c == '/' || c == '.') {
            out.push_back('_');
        } else if (c == '_') {
            out += "_1";
        } else if (c == ';') {
            out += "_2";
        } else if (c == '[') {
            out += "_3";
        } else {
            std::snprintf(escaped, sizeof(escaped), "_0%04x",
                          static_cast<unsigned>(c));
            out += escaped;
        }
    }
    return out;
}

static bool jni_argument_signature(const std::string& signature,
                                   std::string* args_out)
{
    size_t open = signature.find('(');
    size_t close = signature.find(')', open == std::string::npos ? 0 : open);
    if (open == std::string::npos || close == std::string::npos || close <= open)
        return false;
    if (args_out)
        *args_out = signature.substr(open + 1, close - open - 1);
    return true;
}

static std::string jni_export_base(const std::string& class_name,
                                   const std::string& method_name)
{
    return "Java_" + jni_mangle(class_name) + "_" + jni_mangle(method_name);
}

static uint64_t resolve_jni_call_target(jni::JniEnv&        jni_env,
                                        jni::JniOnLoad&     jni_onload,
                                        uint64_t            so_load_base,
                                        const std::string&  so_path,
                                        const JniCallConfig& call,
                                        std::string*        class_out)
{
    std::string class_name = normalize_jni_class_name(call.class_name);
    if (class_out) *class_out = class_name;

    uint64_t cls = jni_env.find_class(class_name);
    if (cls) {
        uint64_t fn = jni_env.find_native(cls, call.method_name, call.signature);
        if (fn) {
            std::fprintf(stderr,
                "[Muplar] JNI target resolved from RegisterNatives: %s.%s%s → 0x%llx\n",
                class_name.c_str(), call.method_name.c_str(),
                call.signature.c_str(), (unsigned long long)fn);
            return fn;
        }
    }

    std::vector<std::string> candidates;
    std::string base = jni_export_base(class_name, call.method_name);
    candidates.push_back(base);

    std::string args_sig;
    if (jni_argument_signature(call.signature, &args_sig))
        candidates.push_back(base + "__" + jni_mangle(args_sig));

    for (const std::string& symbol : candidates) {
        uint64_t fn = jni_onload.find_symbol(so_load_base, so_path, symbol, true);
        if (fn) {
            std::fprintf(stderr,
                "[Muplar] JNI target resolved from export %s → 0x%llx\n",
                symbol.c_str(), (unsigned long long)fn);
            return fn;
        }
    }

    std::fprintf(stderr,
        "[Muplar] JNI target not found: %s.%s%s\n",
        class_name.c_str(), call.method_name.c_str(), call.signature.c_str());
    return 0;
}

static void write_guest_i32(guest_t* g, uint64_t gpa, int32_t value)
{
    guest_write(g, gpa, &value, sizeof(value));
}

static void write_guest_u64(guest_t* g, uint64_t gpa, uint64_t value)
{
    guest_write(g, gpa, &value, sizeof(value));
}

static void write_guest_string(guest_t* g, uint64_t gpa, const char* value)
{
    guest_write(g, gpa, value, std::strlen(value) + 1);
}

static uint64_t prepare_native_activity(guest_t* g,
                                        const jni::JniOnLoad& jni_onload,
                                        uint64_t scratch_gpa,
                                        uint64_t asset_manager_gpa)
{
    constexpr uint64_t kActivitySize = 0x80;
    constexpr uint64_t kCallbacksSize = 16 * 8;

    uint64_t activity_gpa = scratch_gpa;
    uint64_t callbacks_gpa = scratch_gpa + 0x100;
    uint64_t internal_path_gpa = scratch_gpa + 0x200;
    uint64_t external_path_gpa = scratch_gpa + 0x240;
    uint64_t obb_path_gpa = scratch_gpa + 0x280;

    std::vector<uint8_t> zeroes(0x300, 0);
    guest_write(g, scratch_gpa, zeroes.data(), zeroes.size());

    write_guest_string(g, internal_path_gpa, "/data/data/muplar");
    write_guest_string(g, external_path_gpa, "/sdcard/Android/data/muplar");
    write_guest_string(g, obb_path_gpa, "/sdcard/Android/obb/muplar");

    // NDK ANativeActivity layout on arm64:
    // callbacks, vm, env, clazz, internalDataPath, externalDataPath,
    // sdkVersion, padding, instance, assetManager, obbPath.
    write_guest_u64(g, activity_gpa + 0x00, callbacks_gpa);
    write_guest_u64(g, activity_gpa + 0x08, jni_onload.java_vm_ptr_gpa());
    write_guest_u64(g, activity_gpa + 0x10, jni_onload.jni_env_ptr_gpa());
    write_guest_u64(g, activity_gpa + 0x18, 0x70000002ULL);
    write_guest_u64(g, activity_gpa + 0x20, internal_path_gpa);
    write_guest_u64(g, activity_gpa + 0x28, external_path_gpa);
    write_guest_i32(g, activity_gpa + 0x30, 35);
    write_guest_u64(g, activity_gpa + 0x38, 0);
    write_guest_u64(g, activity_gpa + 0x40, asset_manager_gpa);
    write_guest_u64(g, activity_gpa + 0x48, obb_path_gpa);

    (void)kActivitySize;
    (void)kCallbacksSize;
    std::fprintf(stderr,
        "[Muplar] prepared ANativeActivity at GPA 0x%llx callbacks=0x%llx\n",
        (unsigned long long)activity_gpa,
        (unsigned long long)callbacks_gpa);
    return activity_gpa;
}

static bool read_at(FILE* f, uint64_t off, void* dst, size_t len)
{
    if (std::fseek(f, static_cast<long>(off), SEEK_SET) != 0) return false;
    return std::fread(dst, 1, len, f) == len;
}

static bool vaddr_to_file_offset(const std::vector<Elf64_Phdr>& phdrs,
                                 uint64_t vaddr,
                                 uint64_t size,
                                 uint64_t* off_out)
{
    for (const auto& ph : phdrs) {
        if (ph.p_type != PT_LOAD) continue;
        if (vaddr < ph.p_vaddr) continue;
        uint64_t rel = vaddr - ph.p_vaddr;
        if (rel > ph.p_filesz || size > ph.p_filesz - rel) continue;
        *off_out = ph.p_offset + rel;
        return true;
    }
    return false;
}

static bool read_u64_guest(const guest_t* g, uint64_t gpa, uint64_t* out)
{
    return guest_read(g, gpa, out, sizeof(*out)) == 0;
}

static void write_u64_guest(guest_t* g, uint64_t gpa, uint64_t value)
{
    guest_write(g, gpa, &value, sizeof(value));
}

struct DirectSoDyn {
    uint64_t strtab = 0;
    uint64_t strsz = 0;
    uint64_t symtab = 0;
    uint64_t rela = 0;
    uint64_t rela_size = 0;
    uint64_t jmprel = 0;
    uint64_t jmprel_size = 0;
    uint64_t relr = 0;
    uint64_t relr_size = 0;
    uint64_t soname_off = 0;
    std::vector<uint32_t> needed_off;
};

struct DirectSoObject {
    std::string path;
    std::string soname;
    uint64_t load_base = 0;
    uint64_t load_min = 0;
    uint64_t load_max = 0;
    std::vector<Elf64_Phdr> phdrs;
    DirectSoDyn dyn;
    bool mapped_by_bootstrap = false;
};

static std::string read_dyn_string(FILE* f,
                                   const std::vector<Elf64_Phdr>& phdrs,
                                   const DirectSoDyn& dyn,
                                   uint32_t off)
{
    if (!dyn.strtab || off >= dyn.strsz) return {};
    uint64_t file_off = 0;
    if (!vaddr_to_file_offset(phdrs, dyn.strtab + off, 1, &file_off)) return {};

    std::string out;
    for (uint64_t pos = file_off; off + out.size() < dyn.strsz; ++pos) {
        char c = 0;
        if (!read_at(f, pos, &c, 1) || c == 0) break;
        out.push_back(c);
    }
    return out;
}

static uint64_t align_down_u64(uint64_t value, uint64_t align)
{
    return value & ~(align - 1);
}

static uint64_t align_up_u64(uint64_t value, uint64_t align)
{
    return (value + align - 1) & ~(align - 1);
}

static int phdr_mem_perms(uint32_t flags)
{
    int perms = MEM_PERM_R;
    if (flags & PF_W) perms |= MEM_PERM_W;
    if (flags & PF_X) perms |= MEM_PERM_X;
    return perms;
}

static std::string path_filename(const std::string& path)
{
    return std::filesystem::path(path).filename().string();
}

static bool parse_direct_so_metadata(const std::string& path,
                                     DirectSoObject* out)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;

    Elf64_Ehdr ehdr{};
    if (!read_at(f, 0, &ehdr, sizeof(ehdr)) ||
        std::memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0 ||
        ehdr.e_ident[EI_CLASS] != ELFCLASS64 ||
        ehdr.e_machine != EM_AARCH64) {
        std::fclose(f);
        return false;
    }

    DirectSoObject obj;
    obj.path = path;
    obj.phdrs.resize(ehdr.e_phnum);
    if (!read_at(f, ehdr.e_phoff, obj.phdrs.data(),
                 obj.phdrs.size() * sizeof(Elf64_Phdr))) {
        std::fclose(f);
        return false;
    }

    obj.load_min = UINT64_MAX;
    obj.load_max = 0;
    for (const auto& ph : obj.phdrs) {
        if (ph.p_type != PT_LOAD) continue;
        uint64_t start = align_down_u64(ph.p_vaddr, 0x1000);
        uint64_t end = align_up_u64(ph.p_vaddr + ph.p_memsz, 0x1000);
        obj.load_min = std::min(obj.load_min, start);
        obj.load_max = std::max(obj.load_max, end);
    }
    if (obj.load_min == UINT64_MAX) {
        std::fclose(f);
        return false;
    }

    for (const auto& ph : obj.phdrs) {
        if (ph.p_type != PT_DYNAMIC) continue;
        size_t count = ph.p_filesz / sizeof(Elf64_Dyn);
        for (size_t i = 0; i < count; ++i) {
            Elf64_Dyn entry{};
            if (!read_at(f, ph.p_offset + i * sizeof(entry), &entry, sizeof(entry)))
                break;
            uint64_t val = dyn_val(entry);
            switch (entry.d_tag) {
            case DT_STRTAB:   obj.dyn.strtab = val; break;
            case DT_STRSZ:    obj.dyn.strsz = val; break;
            case DT_SYMTAB:   obj.dyn.symtab = val; break;
            case DT_RELA:     obj.dyn.rela = val; break;
            case DT_RELASZ:   obj.dyn.rela_size = val; break;
            case DT_JMPREL:   obj.dyn.jmprel = val; break;
            case DT_PLTRELSZ: obj.dyn.jmprel_size = val; break;
            case MU_DT_ANDROID_RELR:    obj.dyn.relr = val; break;
            case MU_DT_ANDROID_RELRSZ:  obj.dyn.relr_size = val; break;
            case MU_DT_ANDROID_RELRENT: break;
            case DT_SONAME:   obj.dyn.soname_off = val; break;
            case DT_NEEDED:
                obj.dyn.needed_off.push_back(static_cast<uint32_t>(val));
                break;
            case DT_NULL:     i = count; break;
            default: break;
            }
        }
        break;
    }

    if (obj.dyn.soname_off)
        obj.soname = read_dyn_string(f, obj.phdrs, obj.dyn,
                                     static_cast<uint32_t>(obj.dyn.soname_off));
    if (obj.soname.empty())
        obj.soname = path_filename(path);

    std::fclose(f);
    *out = std::move(obj);
    return true;
}

static std::vector<std::string> direct_so_needed_names(const DirectSoObject& obj)
{
    std::vector<std::string> out;
    FILE* f = std::fopen(obj.path.c_str(), "rb");
    if (!f) return out;
    for (uint32_t off : obj.dyn.needed_off) {
        std::string needed = read_dyn_string(f, obj.phdrs, obj.dyn, off);
        if (!needed.empty())
            out.push_back(needed);
    }
    std::fclose(f);
    return out;
}

static bool android_builtin_soname(const android::AndroidRuntime& art,
                                   const std::string& soname)
{
    if (!art.builtin_symbols(soname).empty())
        return true;

    for (const char* const* known = android::AndroidRuntime::KNOWN_SONAMES;
         *known; ++known) {
        if (soname == *known)
            return true;
    }

    return soname == "libdl_android.so" ||
           soname == "libc++_shared.so" ||
           soname == "libz.so";
}

static bool direct_so_already_loaded(const std::vector<DirectSoObject>& objects,
                                     const std::string& soname)
{
    for (const auto& obj : objects) {
        if (obj.soname == soname || path_filename(obj.path) == soname)
            return true;
    }
    return false;
}

static std::optional<std::string>
find_direct_so_dependency(const std::vector<std::string>& search_dirs,
                          const std::string& soname)
{
    for (const std::string& dir : search_dirs) {
        if (dir.empty()) continue;
        std::filesystem::path candidate = std::filesystem::path(dir) / soname;
        if (std::filesystem::exists(candidate))
            return candidate.string();
    }
    return std::nullopt;
}

static bool map_direct_so_object(guest_t* g,
                                 DirectSoObject& obj,
                                 uint64_t runtime_min)
{
    FILE* f = std::fopen(obj.path.c_str(), "rb");
    if (!f) return false;

    obj.load_base = runtime_min - obj.load_min;
    uint64_t runtime_end = obj.load_base + obj.load_max;
    uint64_t map_start = align_down_u64(runtime_min, BLOCK_2MIB);
    uint64_t map_end = align_up_u64(runtime_end, BLOCK_2MIB);

    if (map_end > g->guest_size) {
        std::fprintf(stderr,
            "[Muplar] direct .so dependency range exceeds guest memory: %s\n",
            obj.path.c_str());
        std::fclose(f);
        return false;
    }

    pthread_mutex_lock(&mmap_lock);
    int rc = guest_extend_page_tables(g, map_start, map_end, MEM_PERM_RW);
    pthread_mutex_unlock(&mmap_lock);
    if (rc < 0) {
        std::fclose(f);
        return false;
    }

    for (const auto& ph : obj.phdrs) {
        if (ph.p_type != PT_LOAD) continue;

        uint64_t dst_gpa = obj.load_base + ph.p_vaddr;
        auto* dst = static_cast<uint8_t*>(g->host_base) + dst_gpa;
        std::memset(dst, 0, static_cast<size_t>(ph.p_memsz));
        if (ph.p_filesz &&
            !read_at(f, ph.p_offset, dst, static_cast<size_t>(ph.p_filesz))) {
            std::fclose(f);
            return false;
        }

        if (ph.p_flags & PF_X)
            sys_icache_invalidate(dst, static_cast<size_t>(ph.p_memsz));
    }

    for (const auto& ph : obj.phdrs) {
        if (ph.p_type != PT_LOAD) continue;
        uint64_t seg_start = align_down_u64(obj.load_base + ph.p_vaddr, 0x1000);
        uint64_t seg_end = align_up_u64(obj.load_base + ph.p_vaddr + ph.p_memsz,
                                        0x1000);
        pthread_mutex_lock(&mmap_lock);
        rc = guest_update_perms(g, seg_start, seg_end, phdr_mem_perms(ph.p_flags));
        pthread_mutex_unlock(&mmap_lock);
        if (rc < 0) {
            std::fclose(f);
            return false;
        }
    }

    std::fclose(f);
    return true;
}

static uint64_t lookup_direct_symbol(const std::vector<DirectSoObject>& objects,
                                     const std::string& name)
{
    for (const auto& obj : objects) {
        if (!obj.dyn.symtab || !obj.dyn.strtab || !obj.dyn.strsz)
            continue;

        FILE* f = std::fopen(obj.path.c_str(), "rb");
        if (!f) continue;

        for (uint32_t i = 1; i < 8192; ++i) {
            uint64_t sym_off = 0;
            if (!vaddr_to_file_offset(obj.phdrs,
                                      obj.dyn.symtab + i * sizeof(Elf64_Sym),
                                      sizeof(Elf64_Sym),
                                      &sym_off)) {
                break;
            }

            Elf64_Sym sym{};
            if (!read_at(f, sym_off, &sym, sizeof(sym)))
                break;
            if (sym.st_name >= obj.dyn.strsz)
                continue;
            if (sym.st_shndx == SHN_UNDEF)
                continue;

            std::string sym_name = read_dyn_string(f, obj.phdrs, obj.dyn,
                                                   sym.st_name);
            if (sym_name == name) {
                std::fclose(f);
                return obj.load_base + sym.st_value;
            }
        }

        std::fclose(f);
    }

    return 0;
}

static bool load_direct_so_dependencies(guest_t* g,
                                        std::vector<DirectSoObject>& objects,
                                        const std::vector<std::string>& search_dirs,
                                        const android::AndroidRuntime& art)
{
    uint64_t next_base = 0x180000000ULL;
    for (const auto& obj : objects) {
        next_base = std::max(next_base,
            align_up_u64(obj.load_base + obj.load_max, BLOCK_2MIB));
    }

    for (size_t i = 0; i < objects.size(); ++i) {
        for (const std::string& needed : direct_so_needed_names(objects[i])) {
            if (direct_so_already_loaded(objects, needed))
                continue;

            auto dep_path = find_direct_so_dependency(search_dirs, needed);
            if (!dep_path) {
                if (android_builtin_soname(art, needed))
                    continue;
                std::fprintf(stderr,
                    "[Muplar] direct .so dependency not found locally: %s\n",
                    needed.c_str());
                continue;
            }

            DirectSoObject dep;
            if (!parse_direct_so_metadata(*dep_path, &dep)) {
                std::fprintf(stderr,
                    "[Muplar] failed to parse dependency %s\n",
                    dep_path->c_str());
                return false;
            }

            uint64_t runtime_min = align_up_u64(next_base, BLOCK_2MIB);
            if (!map_direct_so_object(g, dep, runtime_min)) {
                std::fprintf(stderr,
                    "[Muplar] failed to map dependency %s\n",
                    dep.path.c_str());
                return false;
            }

            next_base = align_up_u64(dep.load_base + dep.load_max, BLOCK_2MIB);
            std::fprintf(stderr,
                "[Muplar] loaded direct .so dependency %s at GPA 0x%llx\n",
                dep.soname.c_str(),
                (unsigned long long)(dep.load_base + dep.load_min));
            objects.push_back(std::move(dep));
        }
    }

    return true;
}

static uint64_t resolve_android_symbol(const android::AndroidRuntime& art,
                                       const std::string& name)
{
    for (const char* const* soname = android::AndroidRuntime::KNOWN_SONAMES;
         *soname; ++soname) {
        auto symbols = art.builtin_symbols(*soname);
        auto it = symbols.find(name);
        if (it != symbols.end()) return it->second;
    }
    return 0;
}

static void apply_direct_so_rela(FILE* f,
                                 const DirectSoObject& obj,
                                 const std::vector<DirectSoObject>& objects,
                                 guest_t* g,
                                 uint64_t rela_vaddr,
                                 uint64_t rela_size,
                                 const android::AndroidRuntime& art,
                                 size_t* applied)
{
    if (!rela_vaddr || !rela_size) return;
    uint64_t rela_off = 0;
    if (!vaddr_to_file_offset(obj.phdrs, rela_vaddr, rela_size, &rela_off)) return;

    size_t count = rela_size / sizeof(Elf64_Rela);
    for (size_t i = 0; i < count; ++i) {
        Elf64_Rela rela{};
        if (!read_at(f, rela_off + i * sizeof(rela), &rela, sizeof(rela))) break;

        uint32_t sym_idx = ELF64_R_SYM(rela.r_info);
        uint32_t type = ELF64_R_TYPE(rela.r_info);
        uint64_t slot_gpa = obj.load_base + rela.r_offset;
        uint64_t value = 0;
        bool should_write = true;

        switch (type) {
        case R_AARCH64_RELATIVE:
            value = obj.load_base + static_cast<uint64_t>(rela.r_addend);
            break;
        case R_AARCH64_GLOB_DAT:
        case R_AARCH64_JUMP_SLOT:
        case R_AARCH64_ABS64: {
            if (!sym_idx || !obj.dyn.symtab) {
                should_write = false;
                break;
            }

            uint64_t sym_off = 0;
            if (!vaddr_to_file_offset(obj.phdrs,
                                      obj.dyn.symtab + sym_idx * sizeof(Elf64_Sym),
                                      sizeof(Elf64_Sym),
                                      &sym_off)) {
                should_write = false;
                break;
            }

            Elf64_Sym sym{};
            if (!read_at(f, sym_off, &sym, sizeof(sym))) {
                should_write = false;
                break;
            }

            if (sym.st_shndx != SHN_UNDEF) {
                value = obj.load_base + sym.st_value + static_cast<uint64_t>(rela.r_addend);
            } else {
                std::string name = read_dyn_string(f, obj.phdrs, obj.dyn, sym.st_name);
                uint64_t resolved = lookup_direct_symbol(objects, name);
                if (!resolved)
                    resolved = resolve_android_symbol(art, name);
                if (!resolved) {
                    should_write = false;
                    break;
                }
                value = resolved + static_cast<uint64_t>(rela.r_addend);
            }
            break;
        }
        default:
            should_write = false;
            break;
        }

        if (should_write) {
            write_u64_guest(g, slot_gpa, value);
            ++(*applied);
        }
    }
}

static size_t apply_direct_so_relr(FILE* f,
                                   const std::vector<Elf64_Phdr>& phdrs,
                                   guest_t* g,
                                   uint64_t load_base,
                                   uint64_t relr_vaddr,
                                   uint64_t relr_size)
{
    if (!relr_vaddr || !relr_size) return 0;
    uint64_t relr_off = 0;
    if (!vaddr_to_file_offset(phdrs, relr_vaddr, relr_size, &relr_off)) return 0;

    uint64_t slot_gpa = 0;
    size_t applied = 0;
    size_t count = relr_size / sizeof(uint64_t);

    for (size_t i = 0; i < count; ++i) {
        uint64_t entry = 0;
        if (!read_at(f, relr_off + i * sizeof(entry), &entry, sizeof(entry))) break;

        if ((entry & 1) == 0) {
            slot_gpa = load_base + entry;
            uint64_t current = 0;
            if (read_u64_guest(g, slot_gpa, &current)) {
                write_u64_guest(g, slot_gpa, current + load_base);
                ++applied;
            }
            slot_gpa += 8;
        } else {
            uint64_t bitmap = entry >> 1;
            uint64_t scan_gpa = slot_gpa;
            while (bitmap) {
                if (bitmap & 1) {
                    uint64_t current = 0;
                    if (read_u64_guest(g, scan_gpa, &current)) {
                        write_u64_guest(g, scan_gpa, current + load_base);
                        ++applied;
                    }
                }
                scan_gpa += 8;
                bitmap >>= 1;
            }
            slot_gpa += 63 * 8;
        }
    }

    return applied;
}

static bool apply_direct_so_relocations(guest_t* g,
                                        const DirectSoObject& obj,
                                        const std::vector<DirectSoObject>& objects,
                                        const android::AndroidRuntime& art)
{
    FILE* f = std::fopen(obj.path.c_str(), "rb");
    if (!f) return false;

    size_t relr_applied = apply_direct_so_relr(f, obj.phdrs, g, obj.load_base,
                                               obj.dyn.relr, obj.dyn.relr_size);
    size_t rela_applied = 0;
    apply_direct_so_rela(f, obj, objects, g, obj.dyn.rela, obj.dyn.rela_size,
                         art, &rela_applied);
    apply_direct_so_rela(f, obj, objects, g, obj.dyn.jmprel,
                         obj.dyn.jmprel_size, art, &rela_applied);

    std::fprintf(stderr,
        "[Muplar] direct .so relocations applied for %s: RELR=%zu RELA/PLT=%zu\n",
        obj.soname.c_str(), relr_applied, rela_applied);
    std::fclose(f);
    return true;
}

struct MuplarCtx {
    jni::JniOnLoad*          jni_onload;
    android::AndroidRuntime* art;
};

static uint64_t hvc6_handler(uint64_t call_nr, const uint64_t args[8], void* userdata)
{
    auto* ctx = static_cast<MuplarCtx*>(userdata);
    uint64_t out = 0;

    if (call_nr >= 0x1000 && call_nr <= 0x1FFF) {
        ctx->jni_onload->try_intercept(static_cast<uint32_t>(call_nr), args, &out);
        return out;
    }
    if (call_nr >= 0x2000 && call_nr <= 0x2FFF) {
        ctx->art->try_dispatch(static_cast<uint32_t>(call_nr), args, &out);
        return out;
    }

    std::fprintf(stderr, "[muplar] unknown HVC #6 call_nr=0x%llx\n",
                 (unsigned long long)call_nr);
    return 0;
}

int GuestRunner::run(const GuestRunnerConfig& cfg)
{
    log_init();
    if (cfg.verbose) log_set_level(LOG_DEBUG);

    const char*  elf_path   = cfg.elf_path.c_str();
    const char*  sysroot    = cfg.sysroot.empty() ? nullptr : cfg.sysroot.c_str();
    int          guest_argc = static_cast<int>(cfg.argv.size());
    const char** guest_argv = to_cstrings(cfg.argv);
    if (!guest_argv) throw std::runtime_error("GuestRunner: OOM allocating argv");

    guest_t           g;
    bool              guest_initialized = false;
    guest_bootstrap_t boot;

    std::printf("[Muplar] guest_bootstrap_prepare...\n");
    int rc = guest_bootstrap_prepare(&g, elf_path, false, elf_path, sysroot,
                                      guest_argc, guest_argv, environ,
                                      shim_bin, shim_bin_len,
                                      cfg.verbose, &guest_initialized, &boot);
    free_cstrings(guest_argv, guest_argc);

    if (rc < 0) {
        if (guest_initialized) guest_destroy(&g);
        throw std::runtime_error(
            "GuestRunner: guest_bootstrap_prepare failed for: " + cfg.elf_path);
    }

    hv_vcpu_t       vcpu;
    hv_vcpu_exit_t* vexit;

    std::printf("[Muplar] guest_bootstrap_create_vcpu...\n");
    rc = guest_bootstrap_create_vcpu(&g, &boot, cfg.verbose, &vcpu, &vexit);
    if (rc < 0) {
        guest_destroy(&g);
        throw std::runtime_error("GuestRunner: guest_bootstrap_create_vcpu failed");
    }

    // ── muplar subsystems ─────────────────────────────────────────────────────
    uint64_t jni_stubs_gpa     = g.shim_data_base + 0x000000;
    uint64_t android_stubs_gpa = g.shim_data_base + 0x001000;
    uint64_t jni_table_gpa     = g.shim_data_base + 0x002000;
    uint64_t java_vm_gpa       = g.shim_data_base + 0x003000;

    jni::JniEnv    jni_env;
    jni::JniBridge jni_bridge(&g, &jni_env, jni_table_gpa, jni_stubs_gpa);
    jni_bridge.install();

    jni::JniOnLoad jni_onload(&g, &jni_bridge, &jni_env, java_vm_gpa);
    jni_onload.install();

    android::AndroidRuntime art(&g, android_stubs_gpa, cfg.host_window);
    art.set_asset_root(cfg.apk_assets_dir);
    art.install();

    // ── Register HVC #6 hook ──────────────────────────────────────────────────
    MuplarCtx ctx{ &jni_onload, &art };
    g.hvc6_handler  = hvc6_handler;
    g.hvc6_userdata = &ctx;

    // ── Patch __aarch64_have_lse_atomics ─────────────────────────────────────
    {
        uint64_t lse_flag_gpa = 0;
        FILE* ef = std::fopen(cfg.elf_path.c_str(), "rb");
        if (ef) {
            Elf64_Ehdr ehdr{};
            std::fread(&ehdr, sizeof(ehdr), 1, ef);

            std::vector<Elf64_Shdr> shdrs(ehdr.e_shnum);
            std::fseek(ef, static_cast<long>(ehdr.e_shoff), SEEK_SET);
            std::fread(shdrs.data(), sizeof(Elf64_Shdr), ehdr.e_shnum, ef);

            for (auto& shdr : shdrs) {
                if (shdr.sh_type != SHT_SYMTAB && shdr.sh_type != SHT_DYNSYM) continue;
                if (shdr.sh_link >= ehdr.e_shnum) continue;

                const Elf64_Shdr& strhdr = shdrs[shdr.sh_link];
                std::vector<Elf64_Sym> syms(shdr.sh_size / sizeof(Elf64_Sym));
                std::fseek(ef, static_cast<long>(shdr.sh_offset), SEEK_SET);
                std::fread(syms.data(), sizeof(Elf64_Sym), syms.size(), ef);

                std::vector<char> strtab(strhdr.sh_size);
                std::fseek(ef, static_cast<long>(strhdr.sh_offset), SEEK_SET);
                std::fread(strtab.data(), 1, strhdr.sh_size, ef);

                for (auto& sym : syms) {
                    if (!sym.st_name || sym.st_name >= strhdr.sh_size) continue;
                    if (std::strcmp(&strtab[sym.st_name],
                                    "__aarch64_have_lse_atomics") == 0) {
                        lse_flag_gpa = sym.st_value;
                        break;
                    }
                }
                if (lse_flag_gpa) break;
            }
            std::fclose(ef);
        }

        if (lse_flag_gpa) {
            uint8_t one = 1;
            guest_write(&g, lse_flag_gpa, &one, 1);
            std::printf("[Muplar] patched __aarch64_have_lse_atomics=1 "
                        "at GPA 0x%llx\n", (unsigned long long)lse_flag_gpa);
        } else {
            std::fprintf(stderr,
                "[Muplar] WARNING: __aarch64_have_lse_atomics not found\n");
        }
    }

    // ── Detect if the input is a shared library (ET_DYN with no _start) ──────
    bool is_shared_lib = false;
    {
        FILE* ef = std::fopen(cfg.elf_path.c_str(), "rb");
        if (ef) {
            Elf64_Ehdr ehdr{};
            std::fread(&ehdr, sizeof(ehdr), 1, ef);
            // ET_DYN with entry=0 means .so (no _start); ET_DYN with entry≠0
            // is a PIE executable.  libjnitest.so has e_entry=0.
            if (ehdr.e_type == ET_DYN && ehdr.e_entry == 0)
                is_shared_lib = true;
            std::fclose(ef);
        }
    }

    // ── Run via elfuse's own vcpu_run_loop ────────────────────────────────────
    int exit_code = 0;
    bool host_app_loop_ran = false;
    bool direct_so_ready = true;

    if (is_shared_lib) {
        // For .so files: run Android/JNI entrypoints directly.
        std::printf("[Muplar] detected shared library — running Android .so path\n");

        DirectSoObject main_so;
        if (parse_direct_so_metadata(cfg.elf_path, &main_so)) {
            main_so.load_base = g.elf_load_min - main_so.load_min;
            main_so.mapped_by_bootstrap = true;

            std::vector<DirectSoObject> direct_objects;
            direct_objects.push_back(main_so);

            std::vector<std::string> lib_search_dirs = cfg.native_lib_search_dirs;
            std::string main_dir =
                std::filesystem::path(cfg.elf_path).parent_path().string();
            if (!main_dir.empty() &&
                std::find(lib_search_dirs.begin(), lib_search_dirs.end(), main_dir) ==
                    lib_search_dirs.end()) {
                lib_search_dirs.push_back(main_dir);
            }

            if (!load_direct_so_dependencies(&g, direct_objects, lib_search_dirs, art)) {
                exit_code = 1;
                direct_so_ready = false;
            } else {
                for (const auto& obj : direct_objects) {
                    if (!apply_direct_so_relocations(&g, obj, direct_objects, art)) {
                        exit_code = 1;
                        direct_so_ready = false;
                    }
                }
            }
        } else {
            std::fprintf(stderr,
                "[Muplar] WARNING: unable to parse direct .so metadata for %s\n",
                cfg.elf_path.c_str());
            exit_code = 1;
            direct_so_ready = false;
        }

        auto run_current_vcpu = [&](hv_vcpu_t v,
                                    hv_vcpu_exit_t* ve,
                                    guest_t* gst) {
            return vcpu_run_loop(v, ve, gst, cfg.verbose, cfg.timeout_sec);
        };

        struct VcpuContext {
            std::array<uint64_t, 31> x{};
            uint64_t pc = 0;
            uint64_t fpcr = 0;
            uint64_t fpsr = 0;
            uint64_t cpsr = 0;
            uint64_t sp_el0 = 0;
            uint64_t sp_el1 = 0;
            uint64_t spsr_el1 = 0;
        };

        auto save_context = [&](VcpuContext& ctx) {
            for (size_t i = 0; i < 29; ++i) {
                hv_vcpu_get_reg(vcpu,
                    static_cast<hv_reg_t>(HV_REG_X0 + i), &ctx.x[i]);
            }
            hv_vcpu_get_reg(vcpu, HV_REG_X29, &ctx.x[29]);
            hv_vcpu_get_reg(vcpu, HV_REG_LR, &ctx.x[30]);
            hv_vcpu_get_reg(vcpu, HV_REG_PC, &ctx.pc);
            hv_vcpu_get_reg(vcpu, HV_REG_FPCR, &ctx.fpcr);
            hv_vcpu_get_reg(vcpu, HV_REG_FPSR, &ctx.fpsr);
            hv_vcpu_get_reg(vcpu, HV_REG_CPSR, &ctx.cpsr);
            hv_vcpu_get_sys_reg(vcpu, HV_SYS_REG_SP_EL0, &ctx.sp_el0);
            hv_vcpu_get_sys_reg(vcpu, HV_SYS_REG_SP_EL1, &ctx.sp_el1);
            hv_vcpu_get_sys_reg(vcpu, HV_SYS_REG_SPSR_EL1, &ctx.spsr_el1);
        };

        auto restore_context = [&](const VcpuContext& ctx) {
            for (size_t i = 0; i < 29; ++i) {
                hv_vcpu_set_reg(vcpu,
                    static_cast<hv_reg_t>(HV_REG_X0 + i), ctx.x[i]);
            }
            hv_vcpu_set_reg(vcpu, HV_REG_X29, ctx.x[29]);
            hv_vcpu_set_reg(vcpu, HV_REG_LR, ctx.x[30]);
            hv_vcpu_set_reg(vcpu, HV_REG_PC, ctx.pc);
            hv_vcpu_set_reg(vcpu, HV_REG_FPCR, ctx.fpcr);
            hv_vcpu_set_reg(vcpu, HV_REG_FPSR, ctx.fpsr);
            hv_vcpu_set_reg(vcpu, HV_REG_CPSR, ctx.cpsr);
            hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_SP_EL0, ctx.sp_el0);
            hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_SP_EL1, ctx.sp_el1);
            hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_SPSR_EL1, ctx.spsr_el1);
        };

        struct ManagedGuestThread {
            uint64_t handle = 0;
            uint64_t start_routine = 0;
            uint64_t arg = 0;
            uint64_t stack_top = 0;
            bool started = false;
            bool yielded = false;
            bool finished = false;
            VcpuContext ctx;
        };

        std::vector<ManagedGuestThread> guest_threads;

        auto run_managed_thread = [&](ManagedGuestThread& thread) -> bool {
            if (thread.finished || !thread.start_routine)
                return false;

            VcpuContext caller;
            save_context(caller);

            if (!thread.started) {
                thread.ctx = caller;
                for (size_t i = 0; i < 8; ++i)
                    thread.ctx.x[i] = 0;
                thread.ctx.x[0] = thread.arg;
                thread.ctx.x[30] = jni_onload.return_sentinel_gpa();
                thread.ctx.pc = thread.start_routine;
                if (thread.stack_top) {
                    thread.ctx.sp_el0 = thread.stack_top;
                    thread.ctx.sp_el1 = thread.stack_top;
                }
                thread.started = true;
                std::printf(
                    "[Muplar] starting managed guest pthread handle=0x%llx start=0x%llx arg=0x%llx sp=0x%llx\n",
                    (unsigned long long)thread.handle,
                    (unsigned long long)thread.start_routine,
                    (unsigned long long)thread.arg,
                    (unsigned long long)thread.ctx.sp_el1);
            } else {
                std::printf(
                    "[Muplar] resuming managed guest pthread handle=0x%llx pc=0x%llx\n",
                    (unsigned long long)thread.handle,
                    (unsigned long long)thread.ctx.pc);
            }

            thread.yielded = false;
            restore_context(thread.ctx);
            art.set_thread_yield_enabled(true);
            run_current_vcpu(vcpu, vexit, &g);
            bool yielded = art.consume_thread_yield();
            art.set_thread_yield_enabled(false);

            if (yielded) {
                save_context(thread.ctx);
                thread.ctx.pc += 4; // Resume after the trapping HVC insn.
                thread.yielded = true;
                std::printf(
                    "[Muplar] guest pthread yielded handle=0x%llx pc=0x%llx\n",
                    (unsigned long long)thread.handle,
                    (unsigned long long)thread.ctx.pc);
            } else {
                thread.finished = true;
                uint64_t retval = jni_onload.last_return_value();
                art.complete_pthread_call(thread.handle, retval);
                std::printf(
                    "[Muplar] guest pthread returned handle=0x%llx ret=0x%llx\n",
                    (unsigned long long)thread.handle,
                    (unsigned long long)retval);
            }

            restore_context(caller);
            return true;
        };

        auto run_pending_pthread_calls = [&]() -> bool {
            bool did_work = false;
            auto calls = art.take_pending_pthread_calls();
            for (const auto& call : calls) {
                did_work = true;
                if (!call.start_routine) {
                    art.complete_pthread_call(call.handle, 0);
                    continue;
                }

                ManagedGuestThread thread;
                thread.handle = call.handle;
                thread.start_routine = call.start_routine;
                thread.arg = call.arg;
                thread.stack_top = call.stack_top;
                run_managed_thread(thread);
                if (!thread.finished)
                    guest_threads.push_back(thread);
            }
            return did_work;
        };

        auto resume_yielded_threads_once = [&]() -> bool {
            bool did_work = false;
            for (auto& thread : guest_threads) {
                if (thread.finished || !thread.yielded)
                    continue;
                did_work |= run_managed_thread(thread);
            }
            return did_work;
        };

        auto run_frame_callbacks_once = [&]() -> bool {
            bool did_work = false;
            auto frame_callbacks = art.take_pending_frame_callbacks();
            for (const auto& callback : frame_callbacks) {
                if (!callback.callback) continue;
                did_work = true;
                std::printf(
                    "[Muplar] running Choreographer frame callback frame=%llu data=0x%llx callback=0x%llx\n",
                    (unsigned long long)callback.frame_time_nanos,
                    (unsigned long long)callback.data,
                    (unsigned long long)callback.callback);
                jni_onload.call_guest_function(
                    callback.callback,
                    { callback.frame_time_nanos, callback.data },
                    vcpu, vexit, run_current_vcpu);
            }
            return did_work;
        };

        auto drain_guest_events = [&](bool include_frame_callbacks = true,
                                      bool resume_guest_threads = true) -> bool {
            constexpr size_t kMaxDrainRounds = 64;
            size_t rounds = 0;
            bool did_any_work = false;
            bool resumed_threads = false;
            bool ran_new_threads = false;
            for (;;) {
                if (++rounds > kMaxDrainRounds) {
                    std::fprintf(stderr,
                        "[Muplar] guest event drain budget exhausted (%zu rounds)\n",
                        kMaxDrainRounds);
                    break;
                }

                bool did_work = false;

                bool ran_pending = run_pending_pthread_calls();
                if (ran_pending) {
                    did_work = true;
                    ran_new_threads = true;
                }

                if (resume_guest_threads && !ran_new_threads &&
                    !resumed_threads) {
                    did_work |= resume_yielded_threads_once();
                    resumed_threads = true;
                }

                auto looper_callbacks = art.take_pending_looper_callbacks();
                for (const auto& callback : looper_callbacks) {
                    if (!callback.callback) continue;
                    did_work = true;
                    std::printf(
                        "[Muplar] running ALooper callback fd=%d events=0x%x data=0x%llx callback=0x%llx\n",
                        callback.fd, callback.events,
                        (unsigned long long)callback.data,
                        (unsigned long long)callback.callback);
                    jni_onload.call_guest_function(
                        callback.callback,
                        {
                            static_cast<uint64_t>(static_cast<int64_t>(callback.fd)),
                            static_cast<uint64_t>(static_cast<int64_t>(callback.events)),
                            callback.data
                        },
                        vcpu, vexit, run_current_vcpu);
                }

                if (include_frame_callbacks)
                    did_work |= run_frame_callbacks_once();

                if (!did_work) break;
                did_any_work = true;
            }
            return did_any_work;
        };

        auto call_guest_and_drain =
            [&](uint64_t fn,
                const std::vector<uint64_t>& args,
                bool include_frame_callbacks = true,
                bool resume_guest_threads = true) -> int64_t {
                if (!fn) return 0;
                int64_t ret = jni_onload.call_guest_function(
                    fn, args, vcpu, vexit, run_current_vcpu);
                drain_guest_events(include_frame_callbacks,
                                   resume_guest_threads);
                return ret;
            };

        auto run_host_app_loop = [&]() {
            if (!cfg.host_window)
                return false;

            using clock = std::chrono::steady_clock;
            using namespace std::chrono_literals;

            const bool bounded = cfg.host_window_linger_ms >= 0;
            const auto start = clock::now();
            const auto deadline = start +
                std::chrono::milliseconds(
                    bounded ? cfg.host_window_linger_ms : 0);

            std::printf("[Muplar] entering host app loop%s\n",
                        bounded ? " (bounded)" : " (close window to exit)");

            size_t ticks = 0;
            for (;;) {
                bool did_work = art.pump_host_app_events();
                if (!art.host_window_active())
                    break;

                did_work |= drain_guest_events(false, did_work);
                did_work |= run_frame_callbacks_once();
                if (did_work)
                    drain_guest_events(false, false);

                ++ticks;
                if (bounded && clock::now() >= deadline)
                    break;
                if (!art.host_window_active())
                    break;

                std::this_thread::sleep_for(16ms);
            }

            std::printf("[Muplar] host app loop exited after %zu ticks\n", ticks);
            return true;
        };

        uint64_t jni_onload_gpa = direct_so_ready
            ? jni_onload.find_jni_onload(g.elf_load_min, cfg.elf_path)
            : 0;
        if (direct_so_ready && jni_onload_gpa) {
            int jni_ret = jni_onload.call_jni_onload(
                jni_onload_gpa, vcpu, vexit, run_current_vcpu);
            drain_guest_events();
            std::printf("[Muplar] JNI_OnLoad returned 0x%x (%s)\n",
                        jni_ret,
                        jni_ret == jni::JNI_VERSION_1_6 ? "JNI_VERSION_1_6 ✓" :
                        jni_ret < 0 ? "error" : "unknown version");
            exit_code = (jni_ret == jni::JNI_VERSION_1_6) ? 0 : 1;
        } else if (!cfg.native_activity) {
            std::fprintf(stderr, "[Muplar] no JNI_OnLoad found in %s\n",
                         cfg.elf_path.c_str());
            exit_code = 1;
        } else {
            std::printf("[Muplar] no JNI_OnLoad; continuing with NativeActivity entry\n");
        }

        if (exit_code == 0) {
            if (cfg.native_activity) {
                uint64_t on_create = jni_onload.find_symbol(
                    g.elf_load_min, cfg.elf_path, "ANativeActivity_onCreate");
                if (!on_create) {
                    exit_code = 1;
                } else {
                    uint64_t activity = prepare_native_activity(
                        &g, jni_onload, g.shim_data_base + 0x004000,
                        art.asset_manager_handle());

                    call_guest_and_drain(on_create, { activity, 0, 0 },
                                         true, false);

                    std::printf("[Muplar] ANativeActivity_onCreate returned ✓\n");

                    uint64_t callbacks = read_guest_u64_or_zero(&g, activity + 0x00);

                    uint64_t on_start = read_guest_u64_or_zero(&g, callbacks + 0x00);
                    uint64_t on_resume = read_guest_u64_or_zero(&g, callbacks + 0x08);
                    uint64_t on_pause = read_guest_u64_or_zero(&g, callbacks + 0x18);
                    uint64_t on_stop = read_guest_u64_or_zero(&g, callbacks + 0x20);
                    uint64_t on_destroy = read_guest_u64_or_zero(&g, callbacks + 0x28);
                    uint64_t on_focus = read_guest_u64_or_zero(&g, callbacks + 0x30);
                    uint64_t on_window_created = read_guest_u64_or_zero(&g, callbacks + 0x38);
                    uint64_t on_window_resized = read_guest_u64_or_zero(&g, callbacks + 0x40);
                    uint64_t on_window_redraw = read_guest_u64_or_zero(&g, callbacks + 0x48);
                    uint64_t on_window_destroyed = read_guest_u64_or_zero(&g, callbacks + 0x50);
                    uint64_t on_input_created = read_guest_u64_or_zero(&g, callbacks + 0x58);
                    uint64_t on_input_destroyed = read_guest_u64_or_zero(&g, callbacks + 0x60);

                    bool drain_frames_inline = !cfg.host_window;
                    bool resume_bootstrap_threads_inline = !cfg.host_window;

                    call_guest_and_drain(on_start, { activity },
                                         drain_frames_inline,
                                         resume_bootstrap_threads_inline);
                    call_guest_and_drain(on_resume, { activity },
                                         drain_frames_inline,
                                         resume_bootstrap_threads_inline);

                    uint64_t window = art.native_window_handle();
                    uint64_t input_queue = art.input_queue_handle();
                    call_guest_and_drain(on_focus, { activity, 1 },
                                         drain_frames_inline,
                                         resume_bootstrap_threads_inline);
                    if (on_input_created) {
                        std::printf("[Muplar] dispatching onInputQueueCreated(queue=0x%llx)\n",
                                    (unsigned long long)input_queue);
                        call_guest_and_drain(on_input_created,
                                             { activity, input_queue },
                                             drain_frames_inline,
                                             drain_frames_inline);
                    }
                    if (on_window_created) {
                        std::printf("[Muplar] dispatching onNativeWindowCreated(window=0x%llx)\n",
                                    (unsigned long long)window);
                        call_guest_and_drain(on_window_created,
                                             { activity, window },
                                             drain_frames_inline,
                                             drain_frames_inline);
                    }
                    call_guest_and_drain(on_window_resized,
                                         { activity, window },
                                         drain_frames_inline,
                                         drain_frames_inline);
                    call_guest_and_drain(on_window_redraw,
                                         { activity, window },
                                         drain_frames_inline,
                                         drain_frames_inline);

                    if (cfg.host_window)
                        host_app_loop_ran = run_host_app_loop();

                    if (on_input_destroyed) {
                        std::printf("[Muplar] dispatching onInputQueueDestroyed(queue=0x%llx)\n",
                                    (unsigned long long)input_queue);
                        call_guest_and_drain(on_input_destroyed,
                                             { activity, input_queue },
                                             drain_frames_inline,
                                             true);
                    }
                    call_guest_and_drain(on_window_destroyed,
                                         { activity, window },
                                         drain_frames_inline, true);

                    call_guest_and_drain(on_pause, { activity },
                                         drain_frames_inline, true);
                    call_guest_and_drain(on_stop, { activity },
                                         drain_frames_inline, true);
                    call_guest_and_drain(on_destroy, { activity },
                                         drain_frames_inline, true);
                }
            } else if (cfg.jni_call.enabled) {
                if (cfg.jni_call.int_args.size() > 6) {
                    std::fprintf(stderr,
                        "[Muplar] --jni-call supports up to 6 integer args for now\n");
                    exit_code = 1;
                } else {
                    std::string class_name;
                    uint64_t fn = resolve_jni_call_target(
                        jni_env, jni_onload, g.elf_load_min, cfg.elf_path,
                        cfg.jni_call, &class_name);
                    if (!fn) {
                        exit_code = 1;
                    } else {
                        int64_t native_ret = jni_onload.call_native(
                            fn, 0, cfg.jni_call.int_args, vcpu, vexit,
                            run_current_vcpu);
                        std::printf("[Muplar] %s.%s%s returned %lld\n",
                                    class_name.c_str(),
                                    cfg.jni_call.method_name.c_str(),
                                    cfg.jni_call.signature.c_str(),
                                    (long long)native_ret);
                    }
                }
            } else {
                uint64_t cls = jni_env.find_class("com/example/Muplar");
                uint64_t fn = cls ? jni_env.find_native(cls, "nativeAdd", "(II)I") : 0;
                if (fn) {
                    int native_ret = jni_onload.call_native_int2(
                        fn, 0, 40, 2, vcpu, vexit, run_current_vcpu);
                    std::printf("[Muplar] nativeAdd(40, 2) returned %d%s\n",
                                native_ret, native_ret == 42 ? " ✓" : "");
                    if (native_ret != 42) exit_code = 1;
                }
            }
        }
    } else {
        // For executables: run the normal elfuse main loop.
        std::printf("[Muplar] entering vcpu_run_loop...\n");
        exit_code = vcpu_run_loop(vcpu, vexit, &g, cfg.verbose, cfg.timeout_sec);
    }

    if (cfg.host_window && !host_app_loop_ran)
        art.run_host_window_after_guest(cfg.host_window_linger_ms);

    std::printf("[Muplar] exit code: %d\n", exit_code);

    guest_destroy(&g);
    return exit_code;
}

} // namespace muplar::runtime::elf
