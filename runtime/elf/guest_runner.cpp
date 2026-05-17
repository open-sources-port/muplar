// runtime/elf/guest_runner.cpp
//
// GuestRunner::run() — wraps elfuse bootstrap + owns the vCPU run loop.
//
// Changes from v1: intercepts HVC #6 (muplar JNI/Android calls) before
// delegating HVC #5 to elfuse's syscall_dispatch.
//
// HVC immediate assignment:
//   #5  → elfuse Linux syscall forwarding (unchanged)
//   #6  → muplar dispatch (JNI 0x1000–0x1FFF, Android 0x2000–0x23FF)
//         X8 carries the muplar call number, X0–X7 are arguments.

#include "guest_runner.h"

#include <stdexcept>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <vector>

extern "C" {
    #include "core/bootstrap.h"
    #include "core/guest.h"
    #include "debug/log.h"
    #include "debug/crashreport.h"
    #include "runtime/forkipc.h"
    #include "shim_blob.h"
    #include "syscall/proc.h"
    #include "syscall/abi.h"
    extern char** environ;
}

#include <Hypervisor/Hypervisor.h>

// JNI bridge
#include "../jni/jni_env.h"
#include "../jni/jni_bridge.h"
#include "../jni/jni_onload.h"

// Android runtime stubs
#include "../android/android_runtime.h"

namespace muplar::runtime::elf {

// ── Helpers ───────────────────────────────────────────────────────────────────

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


// ── muplar_dispatch ───────────────────────────────────────────────────────────
//
// Called from the run loop on HVC #6.
// X8 = muplar call number (0x1000–0x23FF)
// X0–X7 = arguments
// Returns value to write back into X0.

static uint64_t muplar_dispatch(hv_vcpu_t vcpu,
                                 [[maybe_unused]] jni::JniBridge* jni_bridge,
                                 jni::JniOnLoad*                  jni_onload,
                                 android::AndroidRuntime*         art,
                                 [[maybe_unused]] guest_t*        g)
{
    uint64_t x8 = 0;
    hv_vcpu_get_reg(vcpu, HV_REG_X8, &x8);
    uint32_t call_nr = static_cast<uint32_t>(x8);

    uint64_t regs[8] = {};
    hv_vcpu_get_reg(vcpu, HV_REG_X0, &regs[0]);
    hv_vcpu_get_reg(vcpu, HV_REG_X1, &regs[1]);
    hv_vcpu_get_reg(vcpu, HV_REG_X2, &regs[2]);
    hv_vcpu_get_reg(vcpu, HV_REG_X3, &regs[3]);
    hv_vcpu_get_reg(vcpu, HV_REG_X4, &regs[4]);
    hv_vcpu_get_reg(vcpu, HV_REG_X5, &regs[5]);
    hv_vcpu_get_reg(vcpu, HV_REG_X6, &regs[6]);
    hv_vcpu_get_reg(vcpu, HV_REG_X7, &regs[7]);

    uint64_t x0_out = 0;

    // JNI_OnLoad sentinel / JNI calls (0x1000–0x1FFF)
    if (call_nr >= 0x1000 && call_nr <= 0x1FFF) {
        jni_onload->try_intercept(call_nr, regs, &x0_out);
        return x0_out;
    }

    // Android runtime stubs (0x2000–0x23FF)
    if (call_nr >= 0x2000 && call_nr <= 0x23FF) {
        art->try_dispatch(call_nr, regs, &x0_out);
        return x0_out;
    }

    std::fprintf(stderr, "[muplar] unknown HVC #6 call_nr=0x%X\n", call_nr);
    return 0;
}

// ── muplar_run_loop ───────────────────────────────────────────────────────────
//
// Mirrors elfuse's vcpu_run_loop but intercepts HVC #6 before passing
// HVC #5 to syscall_dispatch.

static int muplar_run_loop(hv_vcpu_t              vcpu,
                            hv_vcpu_exit_t*        vexit,
                            guest_t*               g,
                            jni::JniBridge*        jni_bridge,
                            jni::JniOnLoad*        jni_onload,
                            android::AndroidRuntime* art,
                            bool                   verbose,
                            [[maybe_unused]] int   timeout_sec)
{
    int  exit_code = 0;
    bool running   = true;

    while (running) {
        if (proc_exit_group_requested()) {
            exit_code = proc_exit_group_requested();
            break;
        }

        if (verbose) {
            uint64_t pc = 0;
            hv_vcpu_get_reg(vcpu, HV_REG_PC, &pc);
            std::fprintf(stderr, "[muplar] vcpu_run PC=0x%llx\n",
                         (unsigned long long)pc);
        }

        hv_return_t hr = hv_vcpu_run(vcpu);
        if (hr != HV_SUCCESS) {
            std::fprintf(stderr, "[muplar] hv_vcpu_run failed: 0x%x\n", hr);
            exit_code = 1;
            break;
        }

        if (proc_exit_group_requested()) {
            exit_code = proc_exit_group_requested();
            break;
        }

        if (vexit->reason == HV_EXIT_REASON_EXCEPTION) {
            uint32_t ec  = (vexit->exception.syndrome >> 26) & 0x3F;
            uint16_t imm = vexit->exception.syndrome & 0xFFFF;

            if (ec == 0x16) {
                // HVC exit
                if (imm == 6) {
                    // ── muplar intercept ──────────────────────────────────
                    uint64_t result = muplar_dispatch(vcpu, jni_bridge,
                                                       jni_onload, art, g);
                    hv_vcpu_set_reg(vcpu, HV_REG_X0, result);

                    // Advance PC past the HVC instruction (4 bytes)
                    uint64_t pc = 0;
                    hv_vcpu_get_reg(vcpu, HV_REG_PC, &pc);
                    hv_vcpu_set_reg(vcpu, HV_REG_PC, pc + 4);

                } else {
                    // ── all other HVCs belong to elfuse ───────────────────
                    // (#5 = Linux syscall, #11 = EL0 fault/signal,
                    //  #0,2,4,7,9,10,12 = internal elfuse mechanisms)
                    int ret = syscall_dispatch(vcpu, g, &exit_code, verbose);
                    if (ret == 1)
                        running = false;
                }
            } else {
                // Non-HVC exception — hand off to elfuse crash reporter
                crash_report(vcpu, g, CRASH_BAD_EXCEPTION, nullptr);
                exit_code = 128 + ec;
                running = false;
            }
        } else if (vexit->reason == HV_EXIT_REASON_CANCELED) {
            // hv_vcpus_exit() was called (e.g. from exit_group handler)
            if (proc_exit_group_requested()) {
                exit_code = proc_exit_group_requested();
            }
            running = false;
        }
        // HV_EXIT_REASON_VTIMER_ACTIVATED: re-enter immediately
    }

    return exit_code;
}

// ── GuestRunner::run ──────────────────────────────────────────────────────────

int GuestRunner::run(const GuestRunnerConfig& cfg)
{
    log_init();
    if (cfg.verbose) log_set_level(LOG_DEBUG);

    const char*  elf_path  = cfg.elf_path.c_str();
    const char*  sysroot   = cfg.sysroot.empty() ? nullptr : cfg.sysroot.c_str();
    int          guest_argc = static_cast<int>(cfg.argv.size());
    const char** guest_argv = to_cstrings(cfg.argv);

    if (!guest_argv)
        throw std::runtime_error("GuestRunner: OOM allocating argv");

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

    // ── Set up muplar subsystems ──────────────────────────────────────────────
    //
    // Use shim_data_base (2MiB RW block) for our arenas — safe to write.
    // shim_base is RX shim code; below it is the PT pool — do NOT write there.
    // Layout within shim_data_base:
    //   +0x000000 : JNI stub area    (4 KB)
    //   +0x001000 : Android stubs    (4 KB)
    //   +0x002000 : JNI table        (2 KB)
    //   +0x003000 : JavaVM arena     (256 bytes)
    uint64_t jni_stubs_gpa     = g.shim_data_base + 0x000000;
    uint64_t android_stubs_gpa = g.shim_data_base + 0x001000;
    uint64_t jni_table_gpa     = g.shim_data_base + 0x002000;
    uint64_t java_vm_gpa       = g.shim_data_base + 0x003000;

    // 1. JNI environment
    jni::JniEnv jni_env;

    // 2. JNI bridge — installs JNINativeInterface table in guest memory
    jni::JniBridge jni_bridge(&g, &jni_env, jni_table_gpa, jni_stubs_gpa);
    jni_bridge.install();

    // 3. JNI_OnLoad bootstrap
    jni::JniOnLoad jni_onload(&g, &jni_bridge, &jni_env, java_vm_gpa);
    jni_onload.install();

    // 4. Android runtime stubs — installs HVC shims + builds symbol tables
    android::AndroidRuntime art(&g, android_stubs_gpa);
    art.install();

    // (If you have a Linker, call art.builtin_symbols() + linker.add_builtin()
    //  here before loading the target .so.)

    std::printf("[Muplar] entering muplar_run_loop...\n");
    int exit_code = muplar_run_loop(vcpu, vexit, &g,
                                     &jni_bridge, &jni_onload, &art,
                                     cfg.verbose, cfg.timeout_sec);
    std::printf("[Muplar] exit code: %d\n", exit_code);

    guest_destroy(&g);
    return exit_code;
}

} // namespace muplar::runtime::elf