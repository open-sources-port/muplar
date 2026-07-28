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

namespace muplar::runtime::elf
{

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

    // Optional KEY=value environment entries passed to the guest stack.
    std::vector<std::string> env;

    // Preserve host environment variables unless env overrides them.
    // Prefix-backed launches should set this to false so guest shells do
    // not inherit macOS PATH/HOME/etc.
    bool inherit_host_env = true;

    // Optional host cwd to enter before booting the guest. When this path
    // lives under sysroot, elfuse reports the stripped guest path from
    // getcwd() and /proc/self/cwd.
    std::string host_cwd;

    // Optional sysroot. When set, elfuse resolves absolute guest paths
    // (e.g. /lib/ld-musl-aarch64.so.1) under this sysroot first.
    // Required for dynamically linked binaries; leave empty for static.
    std::string sysroot;

    // Print every guest syscall as it executes (very verbose).
    bool verbose = false;

    // Suppress Muplar loader diagnostics for interactive guest shells.
    bool quiet = false;

    // Per-vCPU-iteration timeout in seconds.  Zero disables the guard for
    // long-lived shells and GUI applications.
    int timeout_sec = 0;

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

    // Host-side per-prefix framework service endpoint. AndroidRuntime uses
    // it to route NDK Binder calls to services owned by muplard clients.
    std::string service_socket;

    // Flag to indicate if JNI/ART/ANGLE (Android runtime environment) should be
    // initialized. For pure Linux ARM64 binaries (e.g. bash), this can be set
    // to false.
    bool is_android = true;

    // Real ART Java entrypoints own their JavaVM/JNIEnv. Keep Muplar's
    // Android host stubs available, but do not install the synthetic JNI
    // table used by direct NativeActivity/shared-library tests.
    bool real_art_vm = false;

    // Negative means wait until the window closes. Non-negative means
    // pump the host window for that many milliseconds before exiting.
    int host_window_linger_ms = -1;
};

class GuestRunner
{
public:
    // Run the guest binary described by cfg and return its exit code.
    // Throws std::runtime_error on fatal host-side errors.
    int run(const GuestRunnerConfig &cfg);
};

// Starts elfuse's preemption thread (the SIGALRM/SIGUSR2 consumer that
// backs GuestRunnerConfig::timeout_sec and cross-thread vCPU kicks).
// elfuse's own standalone CLI (third_party/elfuse/src/main.c) calls this
// during its own startup; embedders like mup's main() must call it too,
// once, before any guest/vCPU threads exist, or those signals just sit
// blocked-and-pending forever with nothing to consume them (timeout_sec
// silently becomes a no-op, and per elfuse's own comments a missed call
// site can also surface as spurious HV_EXIT_REASON_UNKNOWN). Safe to call
// more than once; only the first call does anything. Returns false on
// failure (see stderr for the reason).
bool ensure_preempt_initialized();

}  // namespace muplar::runtime::elf
