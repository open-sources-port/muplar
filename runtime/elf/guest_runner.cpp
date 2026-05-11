// runtime/elf/guest_runner.cpp
//
// Implements GuestRunner::run() by delegating to the elfuse
// guest_bootstrap_prepare / guest_bootstrap_create_vcpu / vcpu_run_loop
// pipeline that already lives in third_party/elfuse.
//
// The flow mirrors third_party/elfuse/src/main.c but is wrapped in a
// C++ class so the rest of muplar can call it without touching C strings
// directly.
//
// Build requirements (add to runtime/elf/CMakeLists.txt):
//   target_link_libraries(muplar_runtime_elf
//       PRIVATE
//           muplar_elfuse          # the elfuse static lib target
//           "-framework Hypervisor"
//   )

#include "guest_runner.h"

#include <stdexcept>
#include <cstring>
#include <cstdlib>
#include <vector>

// elfuse C headers — must be in an extern "C" block because elfuse is
// plain C and does not have __cplusplus guards.
extern "C" {
    #include "core/bootstrap.h"  // guest_bootstrap_prepare / create_vcpu
    #include "core/guest.h"      // guest_t, guest_destroy, vcpu_run_loop
    #include "debug/log.h"       // log_init, log_set_level, LOG_DEBUG
    #include "runtime/forkipc.h" // fork_child_main (not used here, but
                                 //   forkipc.h is included by bootstrap.h
                                 //   on some builds)

    // shim_blob.h is generated at build time by elfuse's Makefile
    // (xxd -i shim.bin > shim_blob.h).  It defines:
    //   extern const unsigned char shim_bin[];
    //   extern const unsigned int  shim_bin_len;
    #include "shim_blob.h"

    // Syscall
    #include "syscall/proc.h"

    extern char** environ;
}

#include <Hypervisor/Hypervisor.h>

namespace muplar::runtime::elf {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Convert a vector<string> to a heap-allocated NULL-terminated const char**,
// as elfuse's C API expects.  Caller owns the memory; call free_cstrings().
static const char** to_cstrings(const std::vector<std::string>& v)
{
    auto** arr = static_cast<const char**>(
        std::calloc(v.size(), sizeof(const char*))
    );
    if (!arr) return nullptr;
    for (size_t i = 0; i < v.size(); ++i) {
        arr[i] = strdup(v[i].c_str());
        if (!arr[i]) {
            // Roll back on OOM
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

// ---------------------------------------------------------------------------
// GuestRunner::run
// ---------------------------------------------------------------------------

int GuestRunner::run(const GuestRunnerConfig& cfg)
{
    // -----------------------------------------------------------------------
    // 0. Initialise elfuse logging
    // -----------------------------------------------------------------------
    log_init();
    if (cfg.verbose) {
        log_set_level(LOG_DEBUG);
    }

    // -----------------------------------------------------------------------
    // 1. Prepare argv / sysroot as C strings
    //
    //    elfuse copies these strings internally (bootstrap.c strdup's them),
    //    so we only need them alive until guest_bootstrap_prepare returns.
    // -----------------------------------------------------------------------
    const char* elf_path = cfg.elf_path.c_str();
    const char* sysroot  = cfg.sysroot.empty() ? nullptr
                                                : cfg.sysroot.c_str();

    int guest_argc = static_cast<int>(cfg.argv.size());
    const char** guest_argv = to_cstrings(cfg.argv);

    if (!guest_argv) {
        throw std::runtime_error("GuestRunner: out of memory allocating argv");
    }

    // -----------------------------------------------------------------------
    // 2. Bootstrap the guest
    //
    //    guest_bootstrap_prepare:
    //      • Calls elf_load() to parse the ELF header and segments.
    //      • Calls guest_init() to allocate the host-side hypervisor memory
    //        slab (mmap'd MAP_ANON, demand-paged by macOS).
    //      • Calls elf_map_segments() to copy PT_LOAD segments into the slab.
    //      • If the binary has a PT_INTERP (dynamic linker), loads that too.
    //      • Calls build_linux_stack() to construct the initial stack with
    //        argc/argv/envp/auxv — this is the part your old ExecutionContext
    //        was missing entirely.
    //      • Embeds the shim (EL1 exception-vector code) in guest memory.
    //      • Sets up identity-mapped page tables (GVA == GPA).
    //      • Returns the entry_point and stack_pointer to use when creating
    //        the vCPU.
    // -----------------------------------------------------------------------
    guest_t      g;
    bool         guest_initialized = false;
    guest_bootstrap_t boot;

    std::printf("[Muplar] calling guest_bootstrap_prepare...\n");
    int rc = guest_bootstrap_prepare( &g, elf_path, sysroot, guest_argc, guest_argv, environ, shim_bin, shim_bin_len, cfg.verbose, &guest_initialized, &boot );
    std::printf("[Muplar] guest_bootstrap_prepare returned: %d\n", rc);

    free_cstrings(guest_argv, guest_argc);

    if (rc < 0) {
        if (guest_initialized) guest_destroy(&g);
        throw std::runtime_error(
            "GuestRunner: guest_bootstrap_prepare failed for: " + cfg.elf_path
        );
    }

    // -----------------------------------------------------------------------
    // 3. Create the vCPU
    //
    //    guest_bootstrap_create_vcpu:
    //      • Calls hv_vm_create() if not already done.
    //      • Calls hv_vcpu_create() to allocate the hardware vCPU.
    //      • Writes all AArch64 system registers (SCTLR_EL1, TCR_EL1,
    //        MAIR_EL1, TTBR0_EL1, VBAR_EL1 …) so the guest starts in a
    //        valid EL1 state with MMU on and page tables live.
    //      • Sets PC = boot.entry_point, SP = boot.stack_pointer, CPSR to
    //        EL1h mode.
    //      • Sets X0 = 0 (as Linux kernel does for the initial process).
    // -----------------------------------------------------------------------
    hv_vcpu_t       vcpu;
    hv_vcpu_exit_t* vexit;

    std::printf("[Muplar] calling guest_bootstrap_create_vcpu...\n");
    rc = guest_bootstrap_create_vcpu(&g, &boot, cfg.verbose, &vcpu, &vexit);
    std::printf("[Muplar] guest_bootstrap_create_vcpu returned: %d\n", rc);

    if (rc < 0) {
        guest_destroy(&g);
        throw std::runtime_error(
            "GuestRunner: guest_bootstrap_create_vcpu failed"
        );
    }

    // -----------------------------------------------------------------------
    // 4. Run the guest
    //
    //    vcpu_run_loop is elfuse's main dispatch loop:
    //      • Calls hv_vcpu_run() in a loop.
    //      • On HV_EXIT_REASON_EXCEPTION with syndrome ESR_EC_HVC:
    //          - Reads the Linux syscall number from X8.
    //          - Dispatches to the appropriate handler in syscall/*.c
    //            (read, write, open, mmap, futex, clone, exit …).
    //          - Translates arguments and return values between Linux and
    //            macOS ABIs (e.g. mmap flags, errno values, struct layouts).
    //          - Writes the result back into X0.
    //      • On HV_EXIT_REASON_EXCEPTION with SIGILL / SIGSEGV:
    //          - Logs a crash report and returns a non-zero exit code.
    //      • Returns when the guest calls exit() / exit_group().
    // -----------------------------------------------------------------------
    std::printf("[Muplar] calling vcpu_run_loop...\n");
    int exit_code = vcpu_run_loop(vcpu, vexit, &g, cfg.verbose, cfg.timeout_sec);
    std::printf("[Muplar] vcpu_run_loop returned: %d\n", rc);

    // -----------------------------------------------------------------------
    // 5. Tear down
    // -----------------------------------------------------------------------
    guest_destroy(&g);

    return exit_code;
}

} // namespace muplar::runtime::elf
