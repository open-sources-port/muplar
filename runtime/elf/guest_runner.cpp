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

#include <stdexcept>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <vector>

#ifdef PF_R
#  undef PF_R
#  undef PF_W
#  undef PF_X
#endif
#include <elf.h>

extern "C" {
    #include "core/bootstrap.h"
    #include "core/guest.h"
    #include "debug/log.h"
    #include "runtime/forkipc.h"
    #include "shim_blob.h"
    #include "syscall/proc.h"
    extern char** environ;
}

#include <Hypervisor/Hypervisor.h>

#include "../jni/jni_env.h"
#include "../jni/jni_bridge.h"
#include "../jni/jni_onload.h"
#include "../android/android_runtime.h"

namespace muplar::runtime::elf {

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
    if (call_nr >= 0x2000 && call_nr <= 0x23FF) {
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
    int rc = guest_bootstrap_prepare(&g, elf_path, sysroot,
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

    android::AndroidRuntime art(&g, android_stubs_gpa);
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

    // ── Run via elfuse's own vcpu_run_loop ────────────────────────────────────
    std::printf("[Muplar] entering vcpu_run_loop...\n");
    int exit_code = vcpu_run_loop(vcpu, vexit, &g, cfg.verbose, cfg.timeout_sec);
    std::printf("[Muplar] exit code: %d\n", exit_code);

    guest_destroy(&g);
    return exit_code;
}

} // namespace muplar::runtime::elf