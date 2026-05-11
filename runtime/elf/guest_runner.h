#pragma once

// runtime/elf/guest_runner.h
//
// Replaces the raw function-pointer ExecutionContext with a proper
// elfuse-backed Hypervisor.framework vCPU run.
//
// Why: ExecutionContext::execute() cast the AArch64 entrypoint to a C
// function pointer and called it directly.  That crashes immediately on
// the first Linux syscall because macOS and Linux have incompatible
// syscall numbers and ABI.  elfuse solves this by running the guest
// inside a lightweight Apple Hypervisor.framework VM and intercepting
// every SVC #0 (Linux syscall) via HVC #5, translating it to macOS.
//
// Usage:
//   GuestRunner runner;
//   GuestRunnerConfig cfg;
//   cfg.elf_path  = "/path/to/arm64-linux-binary";
//   cfg.argv      = { cfg.elf_path };   // argv[0] = binary name
//   // cfg.sysroot = "/opt/android-sysroot";  // optional
//   int exit_code = runner.run(cfg);

#include <string>
#include <vector>

namespace muplar::runtime::elf {

    struct GuestRunnerConfig {
        // Path to the AArch64 Linux ELF binary.
        std::string elf_path;

        // argv[0] is conventionally the binary name; add real args after.
        std::vector<std::string> argv;

        // Optional sysroot.  When set, elfuse resolves absolute guest
        // paths (e.g. /lib/ld-musl-aarch64.so.1) under this prefix first.
        // Required for dynamically linked binaries; leave empty for static.
        std::string sysroot;

        // Print every guest syscall as it executes (very verbose).
        bool verbose = false;

        // Per-vCPU-iteration timeout in seconds.  A guest that spins
        // indefinitely without a syscall will be killed after this.
        int timeout_sec = 10;
    };

    class GuestRunner {
    public:
        // Run the guest binary described by cfg and return its exit code.
        // Throws std::runtime_error on fatal host-side errors.
        int run(const GuestRunnerConfig& cfg);
    };

} // namespace muplar::runtime::elf
