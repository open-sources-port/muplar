// platform/android-aarch64/elf/guest_runner.cpp
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
static constexpr uint64_t MUPLAR_RUNTIME_ARENA_GPA  = 0x1E0000000ULL;
static constexpr uint64_t MUPLAR_RUNTIME_ARENA_SIZE = 0x00400000ULL;

static uint64_t read_guest_u64_or_zero(guest_t *g, uint64_t gpa)
{
    uint64_t value = 0;
    if (guest_read(g, gpa, &value, sizeof(value)) != 0)
        return 0;
    return value;
}

static void map_muplar_runtime_arena(guest_t* g)
{
    pthread_mutex_lock(&mmap_lock);
    int rc = guest_extend_page_tables(
        g,
        MUPLAR_RUNTIME_ARENA_GPA,
        MUPLAR_RUNTIME_ARENA_GPA + MUPLAR_RUNTIME_ARENA_SIZE,
        MEM_PERM_R | MEM_PERM_W | MEM_PERM_X);
    pthread_mutex_unlock(&mmap_lock);
    if (rc < 0)
        throw std::runtime_error("GuestRunner: failed to map Muplar runtime arena");

    std::vector<uint8_t> zeroes(MUPLAR_RUNTIME_ARENA_SIZE, 0);
    if (guest_write(g, MUPLAR_RUNTIME_ARENA_GPA,
                    zeroes.data(), zeroes.size()) != 0) {
        throw std::runtime_error("GuestRunner: failed to clear Muplar runtime arena");
    }
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

static const char** to_null_terminated_cstrings(const std::vector<std::string>& v)
{
    auto** arr =
        static_cast<const char**>(std::calloc(v.size() + 1, sizeof(const char*)));
    if (!arr) return nullptr;
    for (size_t i = 0; i < v.size(); ++i) {
        arr[i] = strdup(v[i].c_str());
        if (!arr[i]) {
            for (size_t j = 0; j < i; ++j) free(const_cast<char*>(arr[j]));
            free(arr);
            return nullptr;
        }
    }
    arr[v.size()] = nullptr;
    return arr;
}

static void free_cstrings(const char** arr, int n)
{
    if (!arr) return;
    for (int i = 0; i < n; ++i) free(const_cast<char*>(arr[i]));
    free(arr);
}

static std::string env_key(const std::string& env)
{
    size_t eq = env.find('=');
    return eq == std::string::npos ? env : env.substr(0, eq);
}

static std::vector<std::string> merge_environment(
    const std::vector<std::string>& overrides)
{
    std::vector<std::string> merged;
    std::vector<std::string> override_keys;
    override_keys.reserve(overrides.size());
    for (const std::string& entry : overrides)
        override_keys.push_back(env_key(entry));

    for (size_t i = 0; environ && environ[i]; ++i) {
        std::string entry = environ[i];
        std::string key = env_key(entry);
        if (std::find(override_keys.begin(), override_keys.end(), key) ==
            override_keys.end()) {
            merged.push_back(std::move(entry));
        }
    }

    merged.insert(merged.end(), overrides.begin(), overrides.end());
    return merged;
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

static bool parse_jni_parameter_types(const std::string& signature,
                                      std::vector<std::string>* types_out)
{
    std::string args;
    if (!jni_argument_signature(signature, &args))
        return false;

    std::vector<std::string> types;
    for (size_t i = 0; i < args.size();) {
        size_t start = i;
        while (i < args.size() && args[i] == '[')
            ++i;

        if (i >= args.size())
            return false;

        if (args[i] == 'L') {
            size_t end = args.find(';', i);
            if (end == std::string::npos)
                return false;
            i = end + 1;
        } else {
            char c = args[i++];
            if (std::string("ZBCSIJFD").find(c) == std::string::npos)
                return false;
        }
        types.push_back(args.substr(start, i - start));
    }

    if (types_out)
        *types_out = std::move(types);
    return true;
}

static bool jni_type_is_object_like(const std::string& type)
{
    return !type.empty() && (type[0] == 'L' || type[0] == '[');
}

static std::string jni_class_from_type(const std::string& type)
{
    if (type.size() >= 2 && type.front() == 'L' && type.back() == ';')
        return type.substr(1, type.size() - 2);
    return type;
}

static bool jni_type_is_direct_buffer(const std::string& type)
{
    return type == "Ljava/nio/Buffer;" ||
           type == "Ljava/nio/ByteBuffer;" ||
           type == "Ljava/nio/DirectByteBuffer;";
}

static uint64_t alloc_guest_scratch(guest_t* g,
                                    uint64_t* bump,
                                    size_t size,
                                    uint8_t fill)
{
    uint64_t ptr = (*bump + 15) & ~15ULL;
    *bump = ptr + ((static_cast<uint64_t>(size) + 15) & ~15ULL);

    std::vector<uint8_t> bytes(size, fill);
    if (!bytes.empty())
        guest_write(g, ptr, bytes.data(), bytes.size());
    return ptr;
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
                                        uint64_t asset_manager_gpa,
                                        uint64_t activity_object,
                                        const std::string& package_name)
{
    constexpr uint64_t kActivitySize = 0x80;
    constexpr uint64_t kCallbacksSize = 16 * 8;

    uint64_t activity_gpa = scratch_gpa;
    uint64_t callbacks_gpa = scratch_gpa + 0x100;
    uint64_t internal_path_gpa = scratch_gpa + 0x200;
    uint64_t external_path_gpa = scratch_gpa + 0x300;
    uint64_t obb_path_gpa = scratch_gpa + 0x400;

    std::vector<uint8_t> zeroes(0x500, 0);
    guest_write(g, scratch_gpa, zeroes.data(), zeroes.size());

    std::string package = package_name.empty() ? "muplar" : package_name;
    std::string internal_path = "/data/data/" + package;
    std::string external_path = "/sdcard/Android/data/" + package + "/files";
    std::string obb_path = "/sdcard/Android/obb/" + package;

    write_guest_string(g, internal_path_gpa, internal_path.c_str());
    write_guest_string(g, external_path_gpa, external_path.c_str());
    write_guest_string(g, obb_path_gpa, obb_path.c_str());

    // NDK ANativeActivity layout on arm64:
    // callbacks, vm, env, clazz, internalDataPath, externalDataPath,
    // sdkVersion, padding, instance, assetManager, obbPath.
    write_guest_u64(g, activity_gpa + 0x00, callbacks_gpa);
    write_guest_u64(g, activity_gpa + 0x08, jni_onload.java_vm_ptr_gpa());
    write_guest_u64(g, activity_gpa + 0x10, jni_onload.jni_env_ptr_gpa());
    write_guest_u64(g, activity_gpa + 0x18, activity_object);
    write_guest_u64(g, activity_gpa + 0x20, internal_path_gpa);
    write_guest_u64(g, activity_gpa + 0x28, external_path_gpa);
    write_guest_i32(g, activity_gpa + 0x30, 35);
    write_guest_u64(g, activity_gpa + 0x38, 0);
    write_guest_u64(g, activity_gpa + 0x40, asset_manager_gpa);
    write_guest_u64(g, activity_gpa + 0x48, obb_path_gpa);

    (void)kActivitySize;
    (void)kCallbacksSize;
    std::fprintf(stderr,
        "[Muplar] prepared ANativeActivity at GPA 0x%llx callbacks=0x%llx package=%s\n",
        (unsigned long long)activity_gpa,
        (unsigned long long)callbacks_gpa,
        package.c_str());
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
    uint64_t gnu_hash = 0;    // DT_GNU_HASH vaddr — used to derive symbol count
    uint64_t rela = 0;
    uint64_t rela_size = 0;
    uint64_t jmprel = 0;
    uint64_t jmprel_size = 0;
    uint64_t relr = 0;
    uint64_t relr_size = 0;
    uint64_t init = 0;
    uint64_t init_array = 0;
    uint64_t init_array_size = 0;
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
            case DT_GNU_HASH: obj.dyn.gnu_hash = val; break;
            case DT_RELA:     obj.dyn.rela = val; break;
            case DT_RELASZ:   obj.dyn.rela_size = val; break;
            case DT_JMPREL:   obj.dyn.jmprel = val; break;
            case DT_PLTRELSZ: obj.dyn.jmprel_size = val; break;
            case DT_INIT:     obj.dyn.init = val; break;
            case DT_INIT_ARRAY: obj.dyn.init_array = val; break;
            case DT_INIT_ARRAYSZ: obj.dyn.init_array_size = val; break;
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

// Derive the number of exported symbols from GNU hash table.
// GNU hash header: [nbuckets(4)][symndx(4)][maskwords(4)][shift2(4)]
//   followed by maskwords 8-byte bloom filters, then nbuckets 4-byte bucket
//   entries.  The first bucket entry gives us the first symbol index.
//   The last symbol is found by walking chains, but that's expensive.
//   Instead we use: sym_count = number of chain entries, which equals
//   the total symbols in the hash table = symndx + len(chains).
//   We can bound chains: read from the first bucket-referenced sym to the
//   end of the chain section.  Since we don't know its size, we instead
//   use a safe large upper bound and stop at the null sentinel in DYNSYM.
static uint32_t gnu_hash_sym_count(FILE* f,
                                   const std::vector<Elf64_Phdr>& phdrs,
                                   uint64_t gnu_hash_vaddr)
{
    if (!gnu_hash_vaddr) return 0;
    uint64_t off = 0;
    if (!vaddr_to_file_offset(phdrs, gnu_hash_vaddr, 16, &off)) return 0;

    uint32_t nbuckets = 0, symndx = 0, maskwords = 0;
    if (!read_at(f, off + 0,  &nbuckets,  4)) return 0;
    if (!read_at(f, off + 4,  &symndx,    4)) return 0;
    if (!read_at(f, off + 8,  &maskwords, 4)) return 0;

    // Buckets start at: off + 16 + maskwords * 8
    uint64_t bucket_off = off + 16 + (uint64_t)maskwords * 8;

    // Find the highest bucket value (= highest sym index in use)
    uint32_t max_sym = symndx;
    for (uint32_t b = 0; b < nbuckets; ++b) {
        uint32_t bucket_val = 0;
        if (!read_at(f, bucket_off + b * 4, &bucket_val, 4)) break;
        if (bucket_val > max_sym) max_sym = bucket_val;
    }

    // Chains start after buckets: off + 16 + maskwords*8 + nbuckets*4
    // Walk chains from max_sym until we hit a chain entry with bit0 set (end)
    uint64_t chain_base_off = bucket_off + (uint64_t)nbuckets * 4;
    uint32_t sym = max_sym;
    for (uint32_t iter = 0; iter < 65536; ++iter) {
        uint32_t chain_val = 0;
        uint64_t chain_off = chain_base_off + (uint64_t)(sym - symndx) * 4;
        if (!read_at(f, chain_off, &chain_val, 4)) break;
        if (chain_val & 1) { sym++; break; }  // end of chain
        sym++;
    }
    return sym;  // last valid index + 1 is our upper bound
}

static uint64_t lookup_direct_symbol(const std::vector<DirectSoObject>& objects,
                                     const std::string& name)
{
    for (const auto& obj : objects) {
        if (!obj.dyn.symtab || !obj.dyn.strtab || !obj.dyn.strsz)
            continue;

        FILE* f = std::fopen(obj.path.c_str(), "rb");
        if (!f) continue;

        // Determine how many symbols to scan.
        // GNU hash gives us a tight upper bound; fall back to a large safe limit.
        uint32_t sym_count = gnu_hash_sym_count(f, obj.phdrs, obj.dyn.gnu_hash);
        if (sym_count == 0 || sym_count > 131072)
            sym_count = 131072;  // hard upper bound

        // Get the file offset of the DYNSYM table directly from the first entry.
        uint64_t symtab_file_off = 0;
        if (!vaddr_to_file_offset(obj.phdrs, obj.dyn.symtab, sizeof(Elf64_Sym),
                                  &symtab_file_off)) {
            std::fclose(f);
            continue;
        }

        for (uint32_t i = 1; i < sym_count; ++i) {
            Elf64_Sym sym{};
            // Read directly by file offset — avoids repeated vaddr_to_file_offset
            // calls which break on large tables near segment boundaries.
            uint64_t entry_off = symtab_file_off + (uint64_t)i * sizeof(Elf64_Sym);
            if (!read_at(f, entry_off, &sym, sizeof(sym))) break;

            // Null sentinel: both name and value are 0 (only at index 0 normally,
            // but treat as end-of-table if we see it again)
            if (sym.st_name == 0 && sym.st_value == 0 && i > 1) break;
            if (sym.st_name >= obj.dyn.strsz)  continue;
            if (sym.st_shndx == SHN_UNDEF)      continue;
            if (sym.st_value == 0)               continue;

            // Read symbol name directly from file (strtab is contiguous)
            uint64_t str_file_off = 0;
            if (!vaddr_to_file_offset(obj.phdrs,
                                      obj.dyn.strtab + sym.st_name, 1,
                                      &str_file_off)) continue;

            // Read just enough bytes to compare
            char buf[256] = {};
            size_t to_read = std::min<size_t>(name.size() + 1, sizeof(buf) - 1);
            if (!read_at(f, str_file_off, buf, to_read)) continue;
            if (std::strncmp(buf, name.c_str(), name.size()) == 0
                && buf[name.size()] == '\0') {
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
                    "[Muplar] required direct .so dependency not found locally: %s\n",
                    needed.c_str());
                return false;
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

static bool apply_direct_so_rela(FILE* f,
                                 const DirectSoObject& obj,
                                 const std::vector<DirectSoObject>& objects,
                                 guest_t* g,
                                 uint64_t rela_vaddr,
                                 uint64_t rela_size,
                                 android::AndroidRuntime& art,
                                 size_t* applied,
                                 size_t* unresolved)
{
    if (!rela_vaddr || !rela_size) return true;
    uint64_t rela_off = 0;
    if (!vaddr_to_file_offset(obj.phdrs, rela_vaddr, rela_size, &rela_off)) {
        std::fprintf(stderr,
            "[Muplar] unable to read relocation table for %s at vaddr 0x%llx\n",
            obj.soname.c_str(),
            (unsigned long long)rela_vaddr);
        return false;
    }

    size_t count = rela_size / sizeof(Elf64_Rela);
    for (size_t i = 0; i < count; ++i) {
        Elf64_Rela rela{};
        if (!read_at(f, rela_off + i * sizeof(rela), &rela, sizeof(rela))) {
            std::fprintf(stderr,
                "[Muplar] unable to read relocation %zu for %s\n",
                i,
                obj.soname.c_str());
            return false;
        }

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
                uint64_t resolved = resolve_android_symbol(art, name);
                if (!resolved)
                    resolved = lookup_direct_symbol(objects, name);
                if (!resolved) {
                    if (ELF64_ST_BIND(sym.st_info) == STB_WEAK) {
                        value = static_cast<uint64_t>(rela.r_addend);
                        break;
                    }
                    uint64_t trap = art.unsupported_import_stub(obj.soname, name);
                    constexpr uint32_t kUnresolvedImportLogLimit = 64;
                    if (*unresolved < kUnresolvedImportLogLimit) {
                        std::fprintf(stderr,
                            "[Muplar] unresolved direct .so import: %s needs %s "
                            "(reloc=%u slot=0x%llx%s)\n",
                            obj.soname.c_str(),
                            name.empty() ? "<unnamed>" : name.c_str(),
                            type,
                            (unsigned long long)slot_gpa,
                            trap ? " trap=installed" : "");
                    } else if (*unresolved == kUnresolvedImportLogLimit) {
                        std::fprintf(stderr,
                            "[Muplar] unresolved direct .so import: %s has more "
                            "unresolved imports; suppressing further detail\n",
                            obj.soname.c_str());
                    }
                    ++(*unresolved);
                    if (trap) {
                        value = trap + static_cast<uint64_t>(rela.r_addend);
                    } else {
                        should_write = false;
                    }
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

    return true;
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
                                        android::AndroidRuntime& art,
                                        bool strict_direct_imports)
{
    FILE* f = std::fopen(obj.path.c_str(), "rb");
    if (!f) return false;

    size_t relr_applied = apply_direct_so_relr(f, obj.phdrs, g, obj.load_base,
                                               obj.dyn.relr, obj.dyn.relr_size);
    size_t rela_applied = 0;
    size_t unresolved = 0;
    bool rela_ok =
        apply_direct_so_rela(f, obj, objects, g, obj.dyn.rela, obj.dyn.rela_size,
                             art, &rela_applied, &unresolved) &&
        apply_direct_so_rela(f, obj, objects, g, obj.dyn.jmprel,
                             obj.dyn.jmprel_size, art, &rela_applied, &unresolved);

    std::fprintf(stderr,
        "[Muplar] direct .so relocations applied for %s: RELR=%zu RELA/PLT=%zu unresolved=%zu\n",
        obj.soname.c_str(), relr_applied, rela_applied, unresolved);
    if (unresolved) {
        std::fprintf(stderr,
            "[Muplar] WARNING: %s has %zu unresolved direct import(s); "
            "continuing until a referenced symbol is actually used\n",
            obj.soname.c_str(),
            unresolved);
        if (strict_direct_imports) {
            std::fprintf(stderr,
                "[Muplar] strict direct import mode: failing %s because "
                "%zu required import(s) are unresolved\n",
                obj.soname.c_str(),
                unresolved);
            std::fclose(f);
            return false;
        }
    }
    std::fclose(f);
    return rela_ok;
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

static bool patch_lse_atomics_flag(guest_t* g, const std::string& path, uint64_t load_base)
{
    FILE* ef = std::fopen(path.c_str(), "rb");
    if (!ef) return false;

    Elf64_Ehdr ehdr{};
    if (std::fread(&ehdr, sizeof(ehdr), 1, ef) != 1) {
        std::fclose(ef);
        return false;
    }

    std::vector<Elf64_Shdr> shdrs(ehdr.e_shnum);
    std::fseek(ef, static_cast<long>(ehdr.e_shoff), SEEK_SET);
    if (std::fread(shdrs.data(), sizeof(Elf64_Shdr), ehdr.e_shnum, ef) != ehdr.e_shnum) {
        std::fclose(ef);
        return false;
    }

    for (const auto& shdr : shdrs) {
        if (shdr.sh_type != SHT_SYMTAB && shdr.sh_type != SHT_DYNSYM) continue;
        if (shdr.sh_link >= ehdr.e_shnum) continue;

        const Elf64_Shdr& strhdr = shdrs[shdr.sh_link];

        std::vector<Elf64_Sym> syms(shdr.sh_size / sizeof(Elf64_Sym));
        std::fseek(ef, static_cast<long>(shdr.sh_offset), SEEK_SET);
        std::fread(syms.data(), sizeof(Elf64_Sym), syms.size(), ef);

        std::vector<char> strtab(strhdr.sh_size);
        std::fseek(ef, static_cast<long>(strhdr.sh_offset), SEEK_SET);
        std::fread(strtab.data(), 1, strhdr.sh_size, ef);

        for (const auto& sym : syms) {
            if (!sym.st_name || sym.st_name >= strhdr.sh_size) continue;

            if (std::strcmp(&strtab[sym.st_name], "__aarch64_have_lse_atomics") == 0) {
                uint64_t gpa = load_base + sym.st_value;
                uint8_t zero = 0;
                guest_write(g, gpa, &zero, 1);
                std::printf("[Muplar] patched __aarch64_have_lse_atomics=0 at GPA 0x%llx for %s\n", (unsigned long long)gpa, path.c_str());
                std::fclose(ef);
                return true;
            }
        }
    }

    std::fclose(ef);
    return false;
}

static size_t patch_unaligned_zero_vector_stack_stores(guest_t* g,
                                                       const DirectSoObject& obj)
{
    FILE* f = std::fopen(obj.path.c_str(), "rb");
    if (!f) return 0;

    static constexpr uint32_t kSturQ0Sp5c = 0x3C85C3E0u;
    static constexpr uint32_t kStpQ0Sp40  = 0xAD0203E0u;
    static constexpr uint32_t kStpQ0Sp20  = 0xAD0103E0u;
    static constexpr uint32_t kStrQ0Sp10  = 0x3D8007E0u;
    static constexpr uint32_t kStpXzrSp60 = 0xA9067FFFu;

    auto read_u32 = [](const std::vector<uint8_t>& buf, size_t off) -> uint32_t {
        uint32_t v = 0;
        std::memcpy(&v, buf.data() + off, sizeof(v));
        return v;
    };

    size_t patched = 0;
    for (const Elf64_Phdr& ph : obj.phdrs) {
        if (ph.p_type != PT_LOAD || !(ph.p_flags & PF_X) || ph.p_filesz < 16)
            continue;

        std::vector<uint8_t> text(static_cast<size_t>(ph.p_filesz));
        if (!read_at(f, ph.p_offset, text.data(), text.size()))
            continue;

        for (size_t off = 0; off + 16 <= text.size(); off += 4) {
            if (read_u32(text, off) != kSturQ0Sp5c ||
                read_u32(text, off + 4) != kStpQ0Sp40 ||
                read_u32(text, off + 8) != kStpQ0Sp20 ||
                read_u32(text, off + 12) != kStrQ0Sp10) {
                continue;
            }

            uint64_t gpa = obj.load_base + ph.p_vaddr + off;
            if (gpa + sizeof(kStpXzrSp60) > g->guest_size)
                continue;
            auto* host = static_cast<uint8_t*>(g->host_base) + gpa;
            std::memcpy(host, &kStpXzrSp60, sizeof(kStpXzrSp60));
            sys_icache_invalidate(host, sizeof(kStpXzrSp60));
            ++patched;
        }
    }

    std::fclose(f);
    if (patched) {
        std::printf(
            "[Muplar] patched %zu unaligned zero-vector stack store(s) in %s\n",
            patched,
            obj.soname.empty() ? obj.path.c_str() : obj.soname.c_str());
    }
    return patched;
}

int GuestRunner::run(const GuestRunnerConfig& cfg)
{
    log_init();
    if (cfg.verbose) log_set_level(LOG_DEBUG);

    const char*  elf_path   = cfg.elf_path.c_str();
    std::string  guest_elf_path_storage =
        cfg.guest_elf_path.empty() ? cfg.elf_path : cfg.guest_elf_path;
    const char*  guest_elf_path = guest_elf_path_storage.c_str();
    const char*  sysroot    = cfg.sysroot.empty() ? nullptr : cfg.sysroot.c_str();
    int          guest_argc = static_cast<int>(cfg.argv.size());
    const char** guest_argv = to_cstrings(cfg.argv);
    if (!guest_argv) throw std::runtime_error("GuestRunner: OOM allocating argv");
    std::vector<std::string> guest_env_storage;
    const char** guest_envp = nullptr;
    if (!cfg.env.empty()) {
        guest_env_storage = merge_environment(cfg.env);
        guest_envp = to_null_terminated_cstrings(guest_env_storage);
        if (!guest_envp) {
            free_cstrings(guest_argv, guest_argc);
            throw std::runtime_error("GuestRunner: OOM allocating envp");
        }
    }

    guest_t           g;
    bool              guest_initialized = false;
    guest_bootstrap_t boot;

    std::printf("[Muplar] guest_bootstrap_prepare...\n");
    int rc = guest_bootstrap_prepare(&g, elf_path, false, guest_elf_path, sysroot,
                                      guest_argc, guest_argv,
                                      guest_envp ? const_cast<char**>(guest_envp)
                                                 : environ,
                                      shim_bin, shim_bin_len,
                                      cfg.verbose, &guest_initialized, &boot);
    free_cstrings(guest_argv, guest_argc);
    free_cstrings(guest_envp, static_cast<int>(guest_env_storage.size()));

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
    // Do not place Muplar user-mode tables/stubs in elfuse shim_data. Recent
    // elfuse maps shim_data as EL1-only, so guest EL0 cannot read JNI tables or
    // execute HVC stubs there. Keep Muplar runtime state in a normal
    // guest-visible arena instead.
    map_muplar_runtime_arena(&g);
    uint64_t muplar_arena_gpa  = MUPLAR_RUNTIME_ARENA_GPA;
    uint64_t jni_stubs_gpa     = muplar_arena_gpa + 0x000000;
    uint64_t android_stubs_gpa = muplar_arena_gpa + 0x020000;
    uint64_t jni_table_gpa     = muplar_arena_gpa + 0x030000;
    uint64_t java_vm_gpa       = muplar_arena_gpa + 0x031000;
    uint64_t activity_gpa      = muplar_arena_gpa + 0x032000;
    uint64_t synthetic_arg_gpa = muplar_arena_gpa + 0x033000;

    jni::JniEnv    jni_env;
    std::string package_name = cfg.package_name.empty()
        ? "muplar"
        : cfg.package_name;
    jni_env.set_app_context(package_name, cfg.package_code_path);
    uint64_t activity_object =
        jni_env.register_object("android/app/NativeActivity");

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
                        lse_flag_gpa = g.elf_load_min + sym.st_value;
                        break;
                    }
                }
                if (lse_flag_gpa) break;
            }
            std::fclose(ef);
        }

        if (lse_flag_gpa) {
            uint8_t zero = 0;
            guest_write(&g, lse_flag_gpa, &zero, 1);
            std::printf("[Muplar] patched __aarch64_have_lse_atomics=0 "
                        "at GPA 0x%llx\n", (unsigned long long)lse_flag_gpa);
        } else {
            std::fprintf(stderr,
                "[Muplar] WARNING: __aarch64_have_lse_atomics not found\n");
        }
    }

    // ── Detect if the input is a shared library (ET_DYN with no _start) ──────
    bool is_shared_lib = cfg.force_android_so;
    {
        FILE* ef = std::fopen(cfg.elf_path.c_str(), "rb");
        if (ef) {
            Elf64_Ehdr ehdr{};
            std::fread(&ehdr, sizeof(ehdr), 1, ef);
            // ET_DYN with entry=0 means .so (no _start); ET_DYN with entry≠0
            // is a PIE executable.  libjnitest.so has e_entry=0.
            // APK-selected libs are Android shared objects even when the ELF
            // header carries a non-zero entry.
            if (ehdr.e_type == ET_DYN &&
                (cfg.force_android_so || ehdr.e_entry == 0))
                is_shared_lib = true;
            std::fclose(ef);
        }
    }

    // ── Run via elfuse's own vcpu_run_loop ────────────────────────────────────
    int exit_code = 0;
    bool host_app_loop_ran = false;
    bool direct_so_ready = true;
    std::vector<DirectSoObject> direct_objects;

    if (is_shared_lib) {
        // For .so files: run Android/JNI entrypoints directly.
        std::printf("[Muplar] detected shared library — running Android .so path\n");

        DirectSoObject main_so;
        if (parse_direct_so_metadata(cfg.elf_path, &main_so)) {
            main_so.load_base = g.elf_load_min - main_so.load_min;
            main_so.mapped_by_bootstrap = true;

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
                // Apply relocations in reverse dependency order: deps first, then
                // dependents.  This ensures that when libfoo.so's PLT entries are
                // patched, the symbols they reference inside libc++_shared (or any
                // other dep) already have their own GOT/PLT slots resolved, so
                // lookup_direct_symbol() returns the correct runtime address.
                //
                // Example: direct_objects = [libaidlndktest.so, libc++_shared.so]
                // Without this fix: libaidlndktest's PLT slot for a libc++ symbol
                //   resolves to libc++_shared's UNRELOCATED st_value → crash.
                // With this fix: libc++_shared's internal GOT is patched first,
                //   then libaidlndktest's PLT entries get the correct addresses.
                for (int ri = static_cast<int>(direct_objects.size()) - 1; ri >= 0; --ri) {
                    if (!apply_direct_so_relocations(&g, direct_objects[ri],
                                                     direct_objects, art,
                                                     cfg.strict_direct_imports)) {
                        exit_code = 1;
                        direct_so_ready = false;
                    }
                }

                // Re-patch after direct relocations.  The earlier patch is kept
                // in place, but this is the one that matters for directly loaded
                // APK .so files and their dependencies.
                for (const DirectSoObject& obj : direct_objects) {
                    patch_unaligned_zero_vector_stack_stores(&g, obj);
                    patch_lse_atomics_flag(&g, obj.path, obj.load_base);
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

        art.set_guest_function_invoker(
            [&](uint64_t fn, const std::vector<uint64_t>& args) -> int64_t {
                return jni_onload.call_guest_function(
                    fn, args, vcpu, vexit, run_current_vcpu);
            });

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

        auto run_direct_so_initializers = [&]() {
            if (!direct_so_ready)
                return;

            for (int ri = static_cast<int>(direct_objects.size()) - 1;
                 ri >= 0; --ri) {
                const DirectSoObject& obj = direct_objects[ri];
                std::string label = obj.soname.empty()
                    ? path_filename(obj.path)
                    : obj.soname;
                if (ri != 0 && android_builtin_soname(art, label)) {
                    std::printf("[Muplar] skipping DT_INIT_ARRAY for builtin dependency %s\n",
                                label.c_str());
                    continue;
                }

                if (obj.dyn.init) {
                    uint64_t init_fn = obj.load_base + obj.dyn.init;
                    std::printf("[Muplar] running DT_INIT for %s at GPA 0x%llx\n",
                                label.c_str(),
                                (unsigned long long)init_fn);
                    call_guest_and_drain(init_fn, {}, false, false);
                }

                if (!obj.dyn.init_array || !obj.dyn.init_array_size)
                    continue;

                uint64_t array_gpa = obj.load_base + obj.dyn.init_array;
                size_t count =
                    static_cast<size_t>(obj.dyn.init_array_size / sizeof(uint64_t));
                std::printf("[Muplar] running DT_INIT_ARRAY for %s (%zu entries)\n",
                            label.c_str(), count);
                for (size_t i = 0; i < count; ++i) {
                    uint64_t init_fn = 0;
                    if (!read_u64_guest(&g, array_gpa + i * sizeof(uint64_t),
                                        &init_fn)) {
                        continue;
                    }
                    if (init_fn <= 1)
                        continue;
                    call_guest_and_drain(init_fn, {}, false, false);
                }
            }
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

        run_direct_so_initializers();

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
        } else if (!cfg.native_activity && !cfg.jni_call.enabled) {
            std::fprintf(stderr, "[Muplar] no JNI_OnLoad found in %s\n",
                         cfg.elf_path.c_str());
            exit_code = 1;
        } else if (cfg.jni_call.enabled) {
            std::printf("[Muplar] no JNI_OnLoad; continuing with explicit JNI call\n");
        } else {
            std::printf("[Muplar] no JNI_OnLoad; continuing with NativeActivity entry\n");
        }

        if (exit_code == 0) {
            if (cfg.jni_call.enabled) {
                std::vector<std::string> param_types;
                if (!parse_jni_parameter_types(cfg.jni_call.signature,
                                               &param_types)) {
                    std::fprintf(stderr,
                        "[Muplar] invalid JNI signature: %s\n",
                        cfg.jni_call.signature.c_str());
                    exit_code = 1;
                } else if (param_types.size() > 6 ||
                           cfg.jni_call.int_args.size() > 6) {
                    std::fprintf(stderr,
                        "[Muplar] --jni-call supports up to 6 Java args for now\n");
                    exit_code = 1;
                } else if (cfg.jni_call.int_args.size() > param_types.size()) {
                    std::fprintf(stderr,
                        "[Muplar] --jni-call got more --jni-int values than signature args\n");
                    exit_code = 1;
                } else {
                    std::string class_name;
                    uint64_t fn = resolve_jni_call_target(
                        jni_env, jni_onload, g.elf_load_min, cfg.elf_path,
                        cfg.jni_call, &class_name);
                    if (!fn) {
                        exit_code = 1;
                    } else {
                        uint64_t receiver = 0;
                        if (cfg.jni_call.receiver_explicit) {
                            if (cfg.jni_call.receiver_static) {
                                jni_env.register_class(class_name);
                                receiver = jni_env.find_class(class_name);
                            } else {
                                receiver = jni_env.register_object(class_name);
                            }
                        }

                        uint64_t synthetic_arg_bump = synthetic_arg_gpa;
                        std::vector<int64_t> native_args;
                        native_args.reserve(param_types.size());

                        for (size_t i = 0; i < param_types.size(); ++i) {
                            if (i < cfg.jni_call.int_args.size()) {
                                native_args.push_back(cfg.jni_call.int_args[i]);
                                continue;
                            }

                            const std::string& type = param_types[i];
                            uint64_t value = 0;
                            if (type == "Ljava/lang/String;") {
                                value = jni_env.make_string("muplar");
                            } else if (type == "[B") {
                                value = jni_env.make_byte_array(4);
                            } else if (type == "[I") {
                                value = jni_env.make_int_array(4);
                            } else if (type == "[J") {
                                value = jni_env.make_long_array(4);
                            } else if (type == "[F") {
                                value = jni_env.make_float_array(4);
                            } else if (!type.empty() && type[0] == '[') {
                                std::string element_desc = type.substr(1);
                                uint64_t initial = 0;
                                if (element_desc == "Ljava/lang/String;")
                                    initial = jni_env.make_string("muplar");
                                else if (element_desc == "[B")
                                    initial = jni_env.make_byte_array(4);
                                else if (element_desc == "[I")
                                    initial = jni_env.make_int_array(4);
                                else if (element_desc == "[J")
                                    initial = jni_env.make_long_array(4);
                                else if (element_desc == "[F")
                                    initial = jni_env.make_float_array(4);
                                value = jni_env.make_object_array(
                                    jni_class_from_type(element_desc),
                                    4,
                                    initial);
                            } else if (jni_type_is_direct_buffer(type)) {
                                constexpr size_t BUFFER_BYTES = 256;
                                uint64_t data = alloc_guest_scratch(
                                    &g, &synthetic_arg_bump, BUFFER_BYTES, 0);
                                value = jni_env.make_direct_buffer(
                                    data, BUFFER_BYTES);
                            } else if (jni_type_is_object_like(type)) {
                                value = jni_env.register_object(
                                    jni_class_from_type(type));
                            }
                            native_args.push_back(static_cast<int64_t>(value));
                        }

                        int64_t native_ret = jni_onload.call_native(
                            fn, receiver, native_args, vcpu, vexit,
                            run_current_vcpu);
                        std::printf("[Muplar] %s.%s%s returned %lld\n",
                                    class_name.c_str(),
                                    cfg.jni_call.method_name.c_str(),
                                    cfg.jni_call.signature.c_str(),
                                    (long long)native_ret);
                    }
                }
            } else if (cfg.native_activity) {
                uint64_t on_create = jni_onload.find_symbol(
                    g.elf_load_min, cfg.elf_path, "ANativeActivity_onCreate",
                    true);
                if (!on_create) {
                    if (jni_onload_gpa) {
                        std::printf("[Muplar] no NativeActivity entry; JNI_OnLoad-only APK path complete\n");
                    } else {
                        std::fprintf(stderr,
                            "[Muplar] no NativeActivity entry found in %s\n",
                            cfg.elf_path.c_str());
                        exit_code = 1;
                    }
                } else {
                    uint64_t activity = prepare_native_activity(
                        &g, jni_onload, activity_gpa,
                        art.asset_manager_handle(), activity_object,
                        package_name);

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
