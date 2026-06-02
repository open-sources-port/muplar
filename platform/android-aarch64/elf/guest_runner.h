#pragma once

// platform/android-aarch64/elf/guest_runner.h
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

#include <cstdint>
#include <string>
#include <vector>

namespace muplar::runtime::elf {

    struct JniCallConfig {
        bool enabled = false;
        std::string class_name;
        std::string method_name;
        std::string signature;
        std::vector<int64_t> int_args;
        bool receiver_explicit = false;
        bool receiver_static = false;
    };

    struct GuestRunnerConfig {
        // Path to the AArch64 Linux ELF binary.
        std::string elf_path;

        // Guest-visible path for elf_path. When empty, elf_path is also used
        // as the guest path.
        std::string guest_elf_path;

        // argv[0] is conventionally the binary name; add real args after.
        std::vector<std::string> argv;

        // Optional KEY=value environment entries to overlay onto the host
        // environment before building the guest stack.
        std::vector<std::string> env;

        // Optional sysroot. When set, elfuse resolves absolute guest paths
        // (e.g. /lib/ld-musl-aarch64.so.1) under this sysroot first.
        // Required for dynamically linked binaries; leave empty for static.
        std::string sysroot;

        // Print every guest syscall as it executes (very verbose).
        bool verbose = false;

        // Per-vCPU-iteration timeout in seconds.  A guest that spins
        // indefinitely without a syscall will be killed after this.
        int timeout_sec = 10;

        // Optional phase-5 JNI/native call target for direct Android .so runs.
        JniCallConfig jni_call;

        // Optional phase-5 NativeActivity bootstrap entry.
        bool native_activity = false;

        // Force Android/JNI .so launch semantics for APK-selected native libs.
        // Some Android shared objects have a non-zero ELF entry but are still
        // loaded by ART as libraries, not executed as PIE binaries.
        bool force_android_so = false;

        // Optional host-visible window for NativeActivity / GLES smoke runs.
        bool host_window = false;

        // Optional host directories containing APK-local native libraries.
        // Used by the direct Android .so path to satisfy DT_NEEDED entries
        // before invoking JNI_OnLoad or ANativeActivity_onCreate.
        std::vector<std::string> native_lib_search_dirs;

        // Fail before guest execution if direct Android .so relocation leaves
        // any required strong import unresolved.  The default is exploratory:
        // install trap stubs and fail only if the guest actually calls one.
        bool strict_direct_imports = false;

        // Optional host directory containing extracted APK assets.
        // Exposed to the guest through libandroid AAssetManager/AAsset stubs.
        std::string apk_assets_dir;

        // Optional APK/package identity used to populate NativeActivity paths
        // and lightweight Java Context methods such as getPackageName().
        std::string package_name;
        std::string package_code_path;

        // Negative means wait until the window closes. Non-negative means
        // pump the host window for that many milliseconds before exiting.
        int host_window_linger_ms = -1;
    };

    class GuestRunner {
    public:
        // Run the guest binary described by cfg and return its exit code.
        // Throws std::runtime_error on fatal host-side errors.
        int run(const GuestRunnerConfig& cfg);
    };

} // namespace muplar::runtime::elf
