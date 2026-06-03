// platform/windows-x64/wine_runtime.cpp
//
// WineRuntime implementation.
//
// Execution path:
//   1. Validate prefix kind=wine, arch=x86_64
//   2. Locate wine64 binary (runtime_sysroot -> compile-time default -> PATH)
//   3. Prepare environment: WINEPREFIX, WINEDLLPATH, DYLD/LD_LIBRARY_PATH
//   4. fork() + execve() wine64 <exe> [args...]
//   5. Write child PID to <prefix>/run/wine.pid
//   6. Daemon mode: parent returns 0 immediately
//      Foreground mode: parent waitpid() and forwards exit code

#include "wine_runtime.h"

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>

#include "prefix.h"

#ifdef __APPLE__
#include <crt_externs.h>
#else
extern char **environ;
#endif

static char **GetProcessEnvironment()
{
#ifdef __APPLE__
    return *_NSGetEnviron();
#else
    return environ;
#endif
}

// Compile-time Wine prefix path injected by CMake (-DMUPLAR_WINE_PREFIX=...).
// Falls back to empty string when Wine is not built.
#ifndef MUPLAR_WINE_PREFIX
#define MUPLAR_WINE_PREFIX ""
#endif


namespace muplar::runtime::wine {
namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Search for an executable on PATH.  Returns empty path on failure.
std::filesystem::path find_on_path(const std::string& name)
{
    const char* path_env = std::getenv("PATH");
    if (!path_env)
        return {};
    std::istringstream ss(path_env);
    std::string dir;
    while (std::getline(ss, dir, ':')) {
        std::filesystem::path candidate = std::filesystem::path(dir) / name;
        if (std::filesystem::is_regular_file(candidate)) {
            std::error_code ec;
            auto status = std::filesystem::status(candidate, ec);
            if (!ec && (status.permissions() & std::filesystem::perms::owner_exec) !=
                           std::filesystem::perms::none)
                return candidate;
        }
    }
    return {};
}

/// Return the path to the wine64 binary, trying several locations in order.
std::filesystem::path resolve_wine64(
    const std::filesystem::path& hint,
    const prefix::PrefixLayout& layout)
{
    // 1. Explicit override from the caller (e.g. --wine-bin flag).
    if (!hint.empty()) {
        if (!std::filesystem::is_regular_file(hint))
            throw std::runtime_error("wine64 not found at --wine-bin path: " +
                                     hint.string());
        return hint;
    }

    // 2. runtime_sysroot stored in the prefix (set at prefix create time).
    if (!layout.runtime_sysroot.empty()) {
        auto candidate = layout.runtime_sysroot / "bin" / "wine";
        if (std::filesystem::is_regular_file(candidate))
            return candidate;
    }

    // 3. Compile-time default (build/wine-prefix).
    {
        std::string ct = MUPLAR_WINE_PREFIX;
        if (!ct.empty()) {
            auto candidate = std::filesystem::path(ct) / "bin" / "wine";
            if (std::filesystem::is_regular_file(candidate))
                return candidate;
        }
    }

    // 4. System PATH.
    auto from_path = find_on_path("wine");
    if (!from_path.empty())
        return from_path;

    throw std::runtime_error(
        "wine binary not found. "
        "Build Wine first (cmake --build build --target wine_lib) "
        "or pass --wine-bin <path>.");
}

/// Write a PID to <prefix>/run/wine.pid, creating the directory as needed.
void write_pid_file(const prefix::PrefixLayout& layout, pid_t pid)
{
    std::filesystem::path run_dir = layout.root / "run";
    std::error_code ec;
    std::filesystem::create_directories(run_dir, ec);
    if (ec)
        throw std::runtime_error("cannot create run directory: " + ec.message());

    std::ofstream out(prefix::pid_file_path(layout), std::ios::trunc);
    if (!out)
        throw std::runtime_error("cannot write PID file: " +
                                 prefix::pid_file_path(layout).string());
    out << pid << "\n";
}

/// Build the argv array for execve.
std::vector<std::string> build_argv(
    const std::filesystem::path& wine64,
    const std::string& exe_path,
    const std::vector<std::string>& extra_args)
{
    std::vector<std::string> argv;
    argv.push_back(wine64.string());
    argv.push_back(exe_path);
    for (const auto& a : extra_args)
        argv.push_back(a);
    return argv;
}

/// Collect the current environment into a vector of strings.
std::vector<std::string> collect_env()
{
    std::vector<std::string> env;
    for (char** e = GetProcessEnvironment(); *e; ++e)
        env.push_back(*e);
    return env;
}

/// Update (or insert) a NAME=VALUE entry in an environment vector.
void set_env_var(std::vector<std::string>& env,
                 const std::string& name,
                 const std::string& value)
{
    const std::string prefix = name + "=";
    for (auto& entry : env) {
        if (entry.rfind(prefix, 0) == 0) {
            entry = name + "=" + value;
            return;
        }
    }
    env.push_back(name + "=" + value);
}

/// Prepend `value` to a colon-separated env var (like PATH prepending).
void prepend_env_var(std::vector<std::string>& env,
                     const std::string& name,
                     const std::string& value)
{
    const std::string prefix_str = name + "=";
    for (auto& entry : env) {
        if (entry.rfind(prefix_str, 0) == 0) {
            std::string existing = entry.substr(prefix_str.size());
            entry = name + "=" + value +
                    (existing.empty() ? "" : (":" + existing));
            return;
        }
    }
    env.push_back(name + "=" + value);
}

/// Build the child environment for wine64.
std::vector<std::string> build_env(
    const prefix::PrefixLayout& layout,
    const std::filesystem::path& wine64)
{
    std::vector<std::string> env = collect_env();

    // WINEPREFIX: the per-prefix C: drive / registry equivalent.
    set_env_var(env, "WINEPREFIX", layout.rootfs.string());

    // WINEDLLPATH: extra DLL search path so DXMT overrides are found.
    // We point at the Wine install's x86_64-windows directory which
    // contains the DXMT-injected d3d11.dll / dxgi.dll / winemetal.dll.
    std::filesystem::path wine_lib_dir = wine64.parent_path().parent_path()
                                         / "lib" / "wine" / "x86_64-windows";
    if (std::filesystem::is_directory(wine_lib_dir))
        prepend_env_var(env, "WINEDLLPATH", wine_lib_dir.string());

    // Library path so libwine.dylib (if any) and dependencies are found.
    std::filesystem::path wine_lib = wine64.parent_path().parent_path() / "lib";
    if (std::filesystem::is_directory(wine_lib)) {
#ifdef __APPLE__
        // Use DYLD_FALLBACK_LIBRARY_PATH so we don't break system/freetype dynamic loading.
        prepend_env_var(env, "DYLD_FALLBACK_LIBRARY_PATH", wine_lib.string());
        prepend_env_var(env, "DYLD_FALLBACK_LIBRARY_PATH", "/usr/local/lib");
        prepend_env_var(env, "DYLD_FALLBACK_LIBRARY_PATH", "/opt/homebrew/lib");
#else
        prepend_env_var(env, "LD_LIBRARY_PATH", wine_lib.string());
#endif
    }

    // Disable Wine's debug spam by default; callers can override via env.
    // Don't overwrite WINEDEBUG if the user already set it.
    bool has_winedebug = false;
    for (const auto& e : env) {
        if (e.rfind("WINEDEBUG=", 0) == 0) {
            has_winedebug = true;
            break;
        }
    }
    if (!has_winedebug)
        env.push_back("WINEDEBUG=-all");

    return env;
}

} // namespace

// ---------------------------------------------------------------------------
// WineRuntime
// ---------------------------------------------------------------------------

WineRuntime::WineRuntime(WineLaunchOptions opts)
    : opts_(std::move(opts))
{
}

int WineRuntime::run(const PlatformLaunchConfig& config)
{
    // ------------------------------------------------------------------
    // 1. Validate the active prefix
    // ------------------------------------------------------------------
    if (!config.active_prefix)
        throw std::runtime_error(
            "Wine runtime requires a prefix (--prefix NAME)");

    const prefix::PrefixLayout& layout = *config.active_prefix;

    if (layout.kind != prefix::PrefixKind::Wine)
        throw std::runtime_error(
            "Wine runtime requires a wine prefix; got kind=" +
            prefix::to_string(layout.kind));

    if (layout.arch != prefix::GuestArch::X86_64)
        throw std::runtime_error(
            "Wine runtime requires x86_64 arch; got arch=" +
            prefix::to_string(layout.arch));

    // ------------------------------------------------------------------
    // 2. Locate wine64
    // ------------------------------------------------------------------
    std::filesystem::path wine64 = resolve_wine64(opts_.wine_bin, layout);
    if (config.verbose)
        std::cerr << "[Wine] wine64: " << wine64.string() << "\n";

    // ------------------------------------------------------------------
    // 3. Resolve the .exe path
    // ------------------------------------------------------------------
    if (config.input_path.empty())
        throw std::runtime_error("No Windows executable specified");

    // ------------------------------------------------------------------
    // 4. Build argv and environment
    // ------------------------------------------------------------------
    std::vector<std::string> argv =
        build_argv(wine64, config.input_path, config.guest_args);
    std::vector<std::string> env = build_env(layout, wine64);

    if (config.verbose) {
        std::cerr << "[Wine] WINEPREFIX: " << layout.rootfs.string() << "\n";
        std::cerr << "[Wine] exec:";
        for (const auto& a : argv)
            std::cerr << " " << a;
        std::cerr << "\n";
    }

    // Build C-style argv/envp arrays.
    std::vector<char*> c_argv, c_envp;
    for (auto& s : argv)
        c_argv.push_back(const_cast<char*>(s.c_str()));
    c_argv.push_back(nullptr);
    for (auto& s : env)
        c_envp.push_back(const_cast<char*>(s.c_str()));
    c_envp.push_back(nullptr);

    // ------------------------------------------------------------------
    // 5. Fork + exec
    // ------------------------------------------------------------------
    pid_t pid = fork();
    if (pid < 0)
        throw std::runtime_error(std::string("fork() failed: ") +
                                 std::strerror(errno));

    if (pid == 0) {
        // Child: exec wine64.
        execve(wine64.c_str(), c_argv.data(), c_envp.data());
        // execve only returns on error.
        std::cerr << "[Wine] execve failed: " << std::strerror(errno) << "\n";
        _exit(127);
    }

    // Parent: write PID file then decide whether to wait.
    try {
        write_pid_file(layout, pid);
    } catch (const std::exception& e) {
        std::cerr << "[Wine] warning: " << e.what() << "\n";
    }

    std::cerr << "[Wine] started: pid=" << pid
              << " prefix=" << layout.name << "\n";

    if (opts_.daemon) {
        // Daemon mode: return immediately, Wine runs in background.
        std::cerr << "[Wine] running in background (daemon mode)\n";
        return 0;
    }

    // Foreground mode: wait for wine64 to exit and forward its exit code.
    int status = 0;
    while (true) {
        pid_t ret = waitpid(pid, &status, 0);
        if (ret == pid)
            break;
        if (ret < 0 && errno != EINTR) {
            std::cerr << "[Wine] waitpid error: " << std::strerror(errno) << "\n";
            break;
        }
    }

    // Clean up PID file on normal exit.
    std::error_code ec;
    std::filesystem::remove(prefix::pid_file_path(layout), ec);

    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) {
        std::cerr << "[Wine] killed by signal " << WTERMSIG(status) << "\n";
        return 128 + WTERMSIG(status);
    }
    return 1;
}

} // namespace muplar::runtime::wine
