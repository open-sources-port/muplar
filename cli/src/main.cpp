// cli/src/main.cpp
//
// Muplar CLI (mup)
//
// Before this change, main() called ExecutionContext::execute() which
// cast the AArch64 entrypoint to a C function pointer and jumped to it
// directly.  That crashed on the first Linux syscall.
//
// Now main() parses CLI flags into a platform launch config and delegates to a
// runtime implementation. Android ARM64 currently wraps the elfuse
// Hypervisor.framework pipeline; Linux and Windows-compatible runtimes plug in beside it.

#include <iostream>
#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include <sys/resource.h>
#include <sys/wait.h>
#include <signal.h>
#include <cerrno>
#include <cstring>
#include <unistd.h>

#include "android_aarch64_runtime.h"
#include "linux_aarch64_runtime.h"
#include "linux_x86_64_runtime.h"
#include "platform_runtime.h"
#include "prefix.h"

extern "C" {
#include "runtime/forkipc.h"
}

#ifdef MUPLAR_HAS_WINE
#include "wine_runtime.h"
#endif

static constexpr const char* kRosettadTranslatorPath =
    "/Library/Apple/usr/libexec/oah/RosettaLinux/rosettad";

static void print_usage(const char* prog)
{
    std::cerr << "Usage: " << prog
              << " [--verbose] [--quiet] [--sysroot PATH]\n"
              << "              [--prefix NAME|PATH]\n"
              << "              [--apk] [--apk-lib NAME] [--apk-extract-dir PATH]\n"
              << "              [--native-activity]\n"
              << "              [--strict-direct-imports]\n"
              << "              [--host-window] [--host-window-ms VALUE]\n"
              << "              [--jni-call CLASS METHOD SIGNATURE]"
              << " [--jni-static|--jni-instance] [--jni-int VALUE ...]\n"
              << "              (omitted JNI args are synthesized from SIGNATURE)\n"
              << "              <elf-file|apk-file> [args...]\n";
    std::cerr << "       " << prog
              << " prefix create NAME|PATH [--root PATH]"
                 " [--kind android|linux|windows]"
              << " [--arch aarch64|x86_64] [--runner elfuse]"
              << " [--distro alpine|debian|ubuntu|fedora|arch|opensuse|generic]"
              << " [--sysroot PATH]\n"
              << "              (android supports aarch64 only; windows supports x86_64 only; linux supports both)\n"
              << "       " << prog << " prefix list [--plain]\n"
              << "       " << prog << " prefix info NAME|PATH\n"
              << "       " << prog << " prefix clone SRC_NAME|PATH DST_NAME"
              << " [--root PATH] [--replace]\n"
              << "       " << prog << " prefix delete NAME|PATH --yes\n";
    std::cerr << "       " << prog
              << " instance start NAME [--daemon] [--windows-runtime-bin PATH] <program> [args...]\n"
              << "       " << prog << " instance stop  NAME [--force]\n"
              << "       " << prog << " instance status NAME\n"
              << "       " << prog << " instance list\n";
}

static int handle_rosettad_translate_command(int argc, char** argv)
{
    if (argc < 5) {
        std::cerr << "Usage: " << argv[0]
                  << " rosettad translate <input> <output>\n";
        return 1;
    }

    muplar::runtime::PlatformLaunchConfig launch_cfg;
    launch_cfg.input_path = kRosettadTranslatorPath;
    launch_cfg.quiet = true;
    launch_cfg.linux_guest = true;
    launch_cfg.timeout_sec = 60;
    launch_cfg.guest_args.push_back("translate");
    launch_cfg.guest_args.push_back(argv[3]);
    launch_cfg.guest_args.push_back(argv[4]);

    try {
        muplar::runtime::android::AndroidAarch64Runtime runtime;
        return runtime.run(launch_cfg);
    } catch (const std::exception& e) {
        std::cerr << "rosettad translate failed: " << e.what() << "\n";
        return 1;
    }
}

static std::string prefix_kind_display_string(
    muplar::runtime::prefix::PrefixKind kind)
{
    if (kind == muplar::runtime::prefix::PrefixKind::Wine)
        return "windows";
    return muplar::runtime::prefix::to_string(kind);
}

static std::string prefix_kind_string(
    const muplar::runtime::prefix::PrefixLayout& prefix)
{
    return prefix_kind_display_string(prefix.kind);
}

static std::string prefix_arch_string(
    const muplar::runtime::prefix::PrefixLayout& prefix)
{
    return muplar::runtime::prefix::to_string(prefix.arch);
}

static std::string prefix_state_string(
    const muplar::runtime::prefix::PrefixLayout& layout)
{
    auto state = muplar::runtime::prefix::query_prefix_state(layout);
    return (state == muplar::runtime::prefix::PrefixState::Running)
               ? "running"
               : "stopped";
}

static std::string optional_path_string(const std::filesystem::path& path)
{
    return path.empty() ? "-" : path.string();
}

static std::string runtime_sysroot_display_string(
    const muplar::runtime::prefix::PrefixLayout& prefix)
{
    if (prefix.runtime_sysroot.empty())
        return "-";
    if (prefix.kind == muplar::runtime::prefix::PrefixKind::Wine)
        return "configured";
    return prefix.runtime_sysroot.string();
}

static void print_prefix_table(
    const std::vector<muplar::runtime::prefix::PrefixLayout>& prefixes)
{
    struct Row {
        std::string name;
        std::string kind;
        std::string distro;
        std::string arch;
        std::string runner;
        std::string state;
        std::string sysroot;
        std::string root;
    };

    std::vector<Row> rows;
    rows.reserve(prefixes.size());
    for (const auto& prefix : prefixes) {
        rows.push_back({
            prefix.name,
            prefix_kind_string(prefix),
            prefix.distro.empty() ? "-" : prefix.distro,
            prefix_arch_string(prefix),
            prefix.runner,
            prefix_state_string(prefix),
            runtime_sysroot_display_string(prefix),
            prefix.root.string(),
        });
    }

    if (rows.empty()) {
        std::cout << "No prefixes found.\n";
        std::cout << "Create one with: mup prefix create default --kind android"
                  << " --arch aarch64 --sysroot build/sysroot\n";
        return;
    }

    size_t name_w = 4, kind_w = 4, distro_w = 6, arch_w = 4, runner_w = 6, state_w = 5,
           sysroot_w = 7;
    for (const auto& row : rows) {
        name_w = std::max(name_w, row.name.size());
        kind_w = std::max(kind_w, row.kind.size());
        distro_w = std::max(distro_w, row.distro.size());
        arch_w = std::max(arch_w, row.arch.size());
        runner_w = std::max(runner_w, row.runner.size());
        state_w = std::max(state_w, row.state.size());
        sysroot_w = std::max(sysroot_w, row.sysroot.size());
    }

    std::cout << std::left
              << std::setw(static_cast<int>(name_w + 2)) << "Name"
              << std::setw(static_cast<int>(kind_w + 2)) << "Kind"
              << std::setw(static_cast<int>(distro_w + 2)) << "Distro"
              << std::setw(static_cast<int>(arch_w + 2)) << "Arch"
              << std::setw(static_cast<int>(runner_w + 2)) << "Runner"
              << std::setw(static_cast<int>(state_w + 2)) << "State"
              << std::setw(static_cast<int>(sysroot_w + 2)) << "Sysroot"
              << "Root\n";
    std::cout << std::string(name_w, '-') << "  "
              << std::string(kind_w, '-') << "  "
              << std::string(distro_w, '-') << "  "
              << std::string(arch_w, '-') << "  "
              << std::string(runner_w, '-') << "  "
              << std::string(state_w, '-') << "  "
              << std::string(sysroot_w, '-') << "  "
              << "----\n";

    for (const auto& row : rows) {
        std::cout << std::left
                  << std::setw(static_cast<int>(name_w + 2)) << row.name
                  << std::setw(static_cast<int>(kind_w + 2)) << row.kind
                  << std::setw(static_cast<int>(distro_w + 2)) << row.distro
                  << std::setw(static_cast<int>(arch_w + 2)) << row.arch
                  << std::setw(static_cast<int>(runner_w + 2)) << row.runner
                  << std::setw(static_cast<int>(state_w + 2)) << row.state
                  << std::setw(static_cast<int>(sysroot_w + 2)) << row.sysroot
                  << row.root << "\n";
    }
}

static void print_prefix_plain(
    const std::vector<muplar::runtime::prefix::PrefixLayout>& prefixes)
{
    for (const auto& prefix : prefixes) {
        std::cout << prefix.name
                  << "\t" << prefix_kind_string(prefix)
                  << "\t" << (prefix.distro.empty() ? "-" : prefix.distro)
                  << "\t" << prefix_arch_string(prefix)
                  << "\t" << prefix.runner
                  << "\t" << prefix.root.string();
        if (!prefix.runtime_sysroot.empty())
            std::cout << "\t" << runtime_sysroot_display_string(prefix);
        std::cout << "\n";
    }
}

static void print_prefix_info(
    const muplar::runtime::prefix::PrefixLayout& prefix)
{
    std::cout << "Name: " << prefix.name << "\n";
    std::cout << "Kind: " << prefix_kind_string(prefix) << "\n";
    if (!prefix.distro.empty()) {
        std::cout << "Distro: " << prefix.distro << "\n";
    }
    std::cout << "Arch: " << prefix_arch_string(prefix) << "\n";
    std::cout << "Runner: " << prefix.runner << "\n";
    std::cout << "State: " << prefix_state_string(prefix) << "\n";
    std::cout << "Root: " << prefix.root.string() << "\n";
    std::cout << "Rootfs: " << prefix.rootfs.string() << "\n";
    std::cout << "Packages: " << prefix.packages_dir.string() << "\n";
    std::cout << "Registry: " << prefix.registry_dir.string() << "\n";
    std::cout << "Cache: " << prefix.cache_dir.string() << "\n";
    std::cout << "APK cache: " << prefix.apk_cache_dir.string() << "\n";
    std::cout << "Logs: " << prefix.logs_dir.string() << "\n";
    std::cout << "Runtime sysroot: "
              << runtime_sysroot_display_string(prefix) << "\n";
}

static int handle_prefix_command(int argc, char** argv)
{
    if (argc < 3) {
        print_usage(argv[0]);
        return 2;
    }

    std::string command = argv[2];
    if (command == "list" || command == "ls" || command == "manager") {
        bool plain = false;
        for (int i = 3; i < argc; ++i) {
            std::string flag = argv[i];
            if (flag == "--plain" || flag == "--tsv") {
                plain = true;
            } else {
                std::cerr << "Unknown prefix list flag: " << flag << "\n";
                return 2;
            }
        }

        auto prefixes = muplar::runtime::prefix::list_prefixes();
        if (plain)
            print_prefix_plain(prefixes);
        else
            print_prefix_table(prefixes);
        return 0;
    }

    if (command == "info" || command == "show") {
        if (argc != 4) {
            std::cerr << "prefix info requires NAME|PATH\n";
            return 2;
        }
        try {
            auto prefix = muplar::runtime::prefix::open_prefix(
                argv[3], {}, false);
            print_prefix_info(prefix);
            return 0;
        } catch (const std::exception& e) {
            std::cerr << "Prefix error: " << e.what() << "\n";
            return 1;
        }
    }

    if (command == "clone" || command == "copy") {
        if (argc < 5) {
            std::cerr << "prefix clone requires SRC_NAME|PATH DST_NAME\n";
            return 2;
        }
        bool replace = false;
        std::filesystem::path root;
        for (int i = 5; i < argc; ++i) {
            std::string flag = argv[i];
            if (flag == "--replace") {
                replace = true;
            } else if (flag == "--root" && i + 1 < argc) {
                root = argv[i + 1];
                ++i;
            } else {
                std::cerr << "Unknown prefix clone flag: " << flag << "\n";
                return 2;
            }
        }

        try {
            auto prefix = root.empty()
                ? muplar::runtime::prefix::clone_prefix(
                      argv[3], argv[4], replace)
                : muplar::runtime::prefix::clone_prefix_to_root(
                      argv[3], argv[4], root, replace);
            std::cout << "cloned: " << argv[3] << " -> "
                      << prefix.root.string() << "\n";
            print_prefix_info(prefix);
            return 0;
        } catch (const std::exception& e) {
            std::cerr << "Prefix error: " << e.what() << "\n";
            return 1;
        }
    }

    if (command == "delete" || command == "remove" || command == "rm") {
        if (argc < 4) {
            std::cerr << "prefix delete requires NAME|PATH --yes\n";
            return 2;
        }

        bool confirmed = false;
        for (int i = 4; i < argc; ++i) {
            std::string flag = argv[i];
            if (flag == "--yes" || flag == "-y") {
                confirmed = true;
            } else {
                std::cerr << "Unknown prefix delete flag: " << flag << "\n";
                return 2;
            }
        }
        if (!confirmed) {
            std::cerr << "Refusing to delete prefix without --yes\n";
            return 2;
        }

        try {
            auto prefix = muplar::runtime::prefix::open_prefix(
                argv[3], {}, false);
            muplar::runtime::prefix::delete_prefix(argv[3]);
            std::cout << "deleted: " << prefix.root.string() << "\n";
            return 0;
        } catch (const std::exception& e) {
            std::cerr << "Prefix error: " << e.what() << "\n";
            return 1;
        }
    }

    if (command != "create") {
        std::cerr << "Unknown prefix command: " << command << "\n";
        print_usage(argv[0]);
        return 2;
    }
    if (argc < 4) {
        std::cerr << "prefix create requires NAME|PATH\n";
        return 2;
    }

    std::string spec = argv[3];
    std::filesystem::path root;
    std::filesystem::path runtime_sysroot;
    auto kind = muplar::runtime::prefix::PrefixKind::Android;
    auto arch = muplar::runtime::prefix::GuestArch::Aarch64;
    std::string runner = "elfuse";
    std::string distro = "";
    bool arch_set = false;
    for (int i = 4; i < argc;) {
        std::string flag = argv[i];
        if (flag == "--root" && i + 1 < argc) {
            root = argv[i + 1];
            i += 2;
        } else if ((flag == "--sysroot" || flag == "--runtime") && i + 1 < argc) {
            runtime_sysroot = argv[i + 1];
            i += 2;
        } else if (flag == "--kind" && i + 1 < argc) {
            try {
                kind = muplar::runtime::prefix::parse_prefix_kind(argv[i + 1]);
            } catch (const std::exception& e) {
                std::cerr << e.what() << "\n";
                return 2;
            }
            i += 2;
        } else if (flag == "--arch" && i + 1 < argc) {
            try {
                arch = muplar::runtime::prefix::parse_guest_arch(argv[i + 1]);
                arch_set = true;
            } catch (const std::exception& e) {
                std::cerr << e.what() << "\n";
                return 2;
            }
            i += 2;
        } else if (flag == "--runner" && i + 1 < argc) {
            runner = argv[i + 1];
            i += 2;
        } else if (flag == "--distro" && i + 1 < argc) {
            distro = argv[i + 1];
            i += 2;
        } else {
            std::cerr << "Unknown prefix create flag: " << flag << "\n";
            return 2;
        }
    }

    if (!arch_set && kind == muplar::runtime::prefix::PrefixKind::Wine) {
        arch = muplar::runtime::prefix::GuestArch::X86_64;
    }

    try {
        auto prefix = root.empty()
            ? muplar::runtime::prefix::open_prefix(
                  spec, runtime_sysroot, true, kind, arch, runner, distro)
            : muplar::runtime::prefix::open_prefix_at_root(
                  spec, root, runtime_sysroot, true, kind, arch, runner, distro);
        std::cout << "prefix: " << prefix.root << "\n";
        std::cout << "kind: "
                  << prefix_kind_string(prefix) << "\n";
        if (!prefix.distro.empty())
            std::cout << "distro: " << prefix.distro << "\n";
        std::cout << "arch: "
                  << muplar::runtime::prefix::to_string(prefix.arch) << "\n";
        std::cout << "runner: " << prefix.runner << "\n";
        std::cout << "rootfs: " << prefix.rootfs << "\n";
        if (!prefix.runtime_sysroot.empty())
            std::cout << "runtime sysroot: "
                      << runtime_sysroot_display_string(prefix) << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Prefix error: " << e.what() << "\n";
        return 1;
    }
}

static int run_platform_runtime(
    const muplar::runtime::PlatformLaunchConfig& cfg)
{
    namespace prefix = muplar::runtime::prefix;

    if (!cfg.active_prefix) {
        muplar::runtime::android::AndroidAarch64Runtime runtime;
        return runtime.run(cfg);
    }

    const auto& layout = *cfg.active_prefix;
    switch (layout.kind) {
    case prefix::PrefixKind::Android: {
        if (layout.arch != prefix::GuestArch::Aarch64) {
            std::cerr << "Android runtime supports ARM64 only; got arch="
                      << prefix::to_string(layout.arch) << "\n";
            return 1;
        }
        muplar::runtime::android::AndroidAarch64Runtime runtime;
        return runtime.run(cfg);
    }
    case prefix::PrefixKind::Linux:
        if (layout.arch == prefix::GuestArch::Aarch64) {
            muplar::runtime::linux_aarch64::LinuxAarch64Runtime runtime;
            return runtime.run(cfg);
        }
        if (layout.arch == prefix::GuestArch::X86_64) {
            muplar::runtime::linux_x86_64::LinuxX86_64Runtime runtime;
            return runtime.run(cfg);
        }
        std::cerr << "Unsupported linux arch: "
                  << prefix::to_string(layout.arch) << "\n";
        return 1;
    case prefix::PrefixKind::Wine:
#ifdef MUPLAR_HAS_WINE
    {
        muplar::runtime::wine::WineRuntime runtime({});
        return runtime.run(cfg);
    }
#else
        std::cerr << "Muplar Windows Compatibility support was not built into this binary.\n"
                  << "Rebuild with Windows compatibility enabled.\n";
        return 1;
#endif
    }

    std::cerr << "Unsupported prefix kind: "
              << prefix::to_string(layout.kind) << "\n";
    return 1;
}

// ==========================================================================
// instance sub-command: start / stop / status / list
// ==========================================================================

static int handle_instance_command(int argc, char** argv)
{
    namespace prefix = muplar::runtime::prefix;

    if (argc < 3) {
        print_usage(argv[0]);
        return 2;
    }
    std::string sub = argv[2];

    // ── instance list ──────────────────────────────────────────────────────
    if (sub == "list" || sub == "ls") {
        auto prefixes = prefix::list_prefixes();
        if (prefixes.empty()) {
            std::cout << "No instances found.\n";
            return 0;
        }
        // Column widths
        size_t name_w = 4, kind_w = 4, arch_w = 4, state_w = 7;
        for (const auto& p : prefixes) {
            name_w  = std::max(name_w,  p.name.size());
            kind_w  = std::max(kind_w,  prefix::to_string(p.kind).size());
            arch_w  = std::max(arch_w,  prefix::to_string(p.arch).size());
        }
        std::cout << std::left
                  << std::setw(static_cast<int>(name_w + 2))  << "Name"
                  << std::setw(static_cast<int>(kind_w + 2))  << "Kind"
                  << std::setw(static_cast<int>(arch_w + 2))  << "Arch"
                  << std::setw(static_cast<int>(state_w + 2)) << "State"
                  << "Root\n";
        std::cout << std::string(name_w,  '-') << "  "
                  << std::string(kind_w,  '-') << "  "
                  << std::string(arch_w,  '-') << "  "
                  << std::string(state_w, '-') << "  "
                  << "----\n";
        for (const auto& p : prefixes) {
            std::cout << std::left
                      << std::setw(static_cast<int>(name_w + 2))  << p.name
                      << std::setw(static_cast<int>(kind_w + 2))  << prefix::to_string(p.kind)
                      << std::setw(static_cast<int>(arch_w + 2))  << prefix::to_string(p.arch)
                      << std::setw(static_cast<int>(state_w + 2)) << prefix_state_string(p)
                      << p.root.string() << "\n";
        }
        return 0;
    }

    // ── instance status ────────────────────────────────────────────────────
    if (sub == "status") {
        if (argc < 4) {
            std::cerr << "instance status requires NAME|PATH\n";
            return 2;
        }
        try {
            auto layout = prefix::open_prefix(argv[3], {}, false);
            auto state  = prefix::query_prefix_state(layout);
            pid_t pid   = prefix::read_prefix_pid(layout);
            std::cout << "Name:  " << layout.name << "\n";
            std::cout << "Kind:  " << prefix_kind_display_string(layout.kind) << "\n";
            std::cout << "Arch:  " << prefix::to_string(layout.arch) << "\n";
            std::cout << "State: "
                      << (state == prefix::PrefixState::Running ? "running" : "stopped")
                      << "\n";
            if (state == prefix::PrefixState::Running && pid > 0)
                std::cout << "PID:   " << pid << "\n";
            return 0;
        } catch (const std::exception& e) {
            std::cerr << "instance status error: " << e.what() << "\n";
            return 1;
        }
    }

    // ── instance stop ──────────────────────────────────────────────────────
    if (sub == "stop") {
        if (argc < 4) {
            std::cerr << "instance stop requires NAME|PATH\n";
            return 2;
        }
        bool force = false;
        for (int i = 4; i < argc; ++i) {
            std::string flag = argv[i];
            if (flag == "--force" || flag == "-f") {
                force = true;
            } else {
                std::cerr << "Unknown instance stop flag: " << flag << "\n";
                return 2;
            }
        }
        try {
            auto layout = prefix::open_prefix(argv[3], {}, false);
            auto state  = prefix::query_prefix_state(layout);
            if (state != prefix::PrefixState::Running) {
                std::cerr << "Instance '" << layout.name << "' is not running.\n";
                return 1;
            }
            pid_t pid = prefix::read_prefix_pid(layout);
            if (pid <= 0) {
                std::cerr << "Cannot read PID for instance '" << layout.name << "'.\n";
                return 1;
            }
            int sig = force ? SIGKILL : SIGTERM;
            std::cerr << "[instance] sending "
                      << (force ? "SIGKILL" : "SIGTERM")
                      << " to pid=" << pid << "\n";
            if (kill(pid, sig) != 0) {
                std::cerr << "kill() failed: " << std::strerror(errno) << "\n";
                return 1;
            }
            // Wait up to 5 seconds for graceful exit (SIGTERM).
            if (!force) {
                for (int i = 0; i < 50; ++i) {
                    usleep(100000); // 100 ms
                    if (prefix::query_prefix_state(layout) == prefix::PrefixState::Stopped)
                        break;
                }
                // If still running, escalate.
                if (prefix::query_prefix_state(layout) == prefix::PrefixState::Running) {
                    std::cerr << "[instance] still running; sending SIGKILL\n";
                    kill(pid, SIGKILL);
                    usleep(500000); // 0.5 s
                }
            }
            // Remove PID file.
            std::error_code ec;
            std::filesystem::remove(prefix::pid_file_path(layout), ec);
            std::cout << "stopped: " << layout.name << " (pid=" << pid << ")\n";
            return 0;
        } catch (const std::exception& e) {
            std::cerr << "instance stop error: " << e.what() << "\n";
            return 1;
        }
    }

    // ── instance start ─────────────────────────────────────────────────────
    if (sub == "start") {
        if (argc < 5) {
            std::cerr << "Usage: mup instance start NAME [--daemon] "
                         "[--windows-runtime-bin PATH] <program> [args...]\n";
            return 2;
        }

        std::string prefix_name = argv[3];
        bool daemon_mode        = false;
        std::filesystem::path wine_bin;
        std::string program_path;
        std::vector<std::string> program_args;

        int i = 4;
        // Parse optional flags before the program path.
        while (i < argc && argv[i][0] == '-') {
            std::string flag = argv[i];
            if (flag == "--daemon" || flag == "-d") {
                daemon_mode = true;
                ++i;
            } else if ((flag == "--windows-runtime-bin" || flag == "--wine-bin") && i + 1 < argc) {
                wine_bin = argv[i + 1];
                i += 2;
            } else if (flag == "--") {
                ++i;
                break;
            } else {
                std::cerr << "Unknown instance start flag: " << flag << "\n";
                return 2;
            }
        }
        if (i >= argc) {
            std::cerr << "instance start: missing <program> argument\n";
            return 2;
        }
        program_path = argv[i++];
        for (; i < argc; ++i)
            program_args.push_back(argv[i]);

        try {
            auto layout = prefix::open_prefix(prefix_name, {}, false);

            if (prefix::query_prefix_state(layout) == prefix::PrefixState::Running) {
                pid_t existing = prefix::read_prefix_pid(layout);
                std::cerr << "instance start: prefix '" << layout.name
                          << "' is already running (pid=" << existing << ").\n"
                          << "Use 'mup instance stop " << layout.name << "' first.\n";
                return 1;
            }

            muplar::runtime::PlatformLaunchConfig cfg;
            cfg.input_path   = program_path;
            cfg.guest_args   = program_args;
            cfg.verbose      = true;
            cfg.timeout_sec  = 0;
            cfg.active_prefix = layout;
            if (!layout.runtime_sysroot.empty())
                cfg.sysroot = layout.runtime_sysroot.string();
            else if (layout.kind == prefix::PrefixKind::Android ||
                     layout.kind == prefix::PrefixKind::Linux)
                cfg.sysroot = layout.rootfs.string();

#ifdef MUPLAR_HAS_WINE
            muplar::runtime::wine::WineLaunchOptions opts;
            opts.wine_bin = wine_bin;
            opts.daemon   = daemon_mode;

            if (layout.kind == prefix::PrefixKind::Wine) {
                muplar::runtime::wine::WineRuntime runtime(opts);
                return runtime.run(cfg);
            }
#else
            if (layout.kind == prefix::PrefixKind::Wine) {
                std::cerr << "instance start: Muplar Windows Compatibility support was not built into this binary.\n"
                          << "Rebuild with Windows compatibility enabled.\n";
                return 1;
            }
#endif

            if (!wine_bin.empty()) {
                std::cerr << "instance start: --windows-runtime-bin is only valid for Windows instances.\n";
                return 2;
            }
            if (daemon_mode) {
                std::cerr << "instance start: --daemon is only valid for Windows instances for now.\n";
                return 2;
            }

            return run_platform_runtime(cfg);
        } catch (const std::exception& e) {
            std::cerr << "instance start error: " << e.what() << "\n";
            return 1;
        }
    }

    std::cerr << "Unknown instance command: " << sub << "\n";
    print_usage(argv[0]);
    return 2;
}

int main(int argc, char** argv)
{
    // Check if we are running as a fork-child helper process
    int fork_child_fd = -1;
    int vfork_notify_fd = -1;
    bool verbose = false;
    int timeout_sec = 0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--fork-child" && i + 1 < argc) {
            try {
                fork_child_fd = std::stoi(argv[i + 1]);
            } catch (...) {}
        } else if (arg == "--vfork-notify-fd" && i + 1 < argc) {
            try {
                vfork_notify_fd = std::stoi(argv[i + 1]);
            } catch (...) {}
        } else if (arg == "--verbose" || arg == "-v") {
            verbose = true;
        }
    }

    if (fork_child_fd >= 0) {
        return fork_child_main(fork_child_fd, vfork_notify_fd, verbose, timeout_sec);
    }

    // Increase soft limit for open files descriptor limit from 256 to maximum allowed
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
        rl.rlim_cur = rl.rlim_max;
        if (setrlimit(RLIMIT_NOFILE, &rl) != 0) {
            rl.rlim_cur = 10240;
            setrlimit(RLIMIT_NOFILE, &rl);
        }
    }

    if (argc < 2) {
        std::cout << "Muplar CLI (mup)\n";
        print_usage(argv[0]);
        return 1;
    }

    if (argc >= 3 && std::string(argv[1]) == "rosettad" &&
        std::string(argv[2]) == "translate") {
        return handle_rosettad_translate_command(argc, argv);
    }

    bool quiet_requested = false;
    for (int i = 1; i < argc && argv[i][0] == '-'; ++i) {
        std::string flag = argv[i];
        if (flag == "--quiet" || flag == "-q") {
            quiet_requested = true;
            break;
        }
        if (flag == "--")
            break;
        if ((flag == "--apk-lib" || flag == "--apk-extract-dir" ||
             flag == "--sysroot" || flag == "--prefix" ||
             flag == "--host-window-ms" || flag == "--jni-int" ||
             flag == "--jni-arg") &&
            i + 1 < argc) {
            ++i;
        } else if (flag == "--jni-call" && i + 3 < argc) {
            i += 3;
        }
    }

    if (!quiet_requested)
        std::cout << "Muplar CLI (mup)\n";

    if (std::string(argv[1]) == "prefix")
        return handle_prefix_command(argc, argv);

    if (std::string(argv[1]) == "instance" || std::string(argv[1]) == "inst")
        return handle_instance_command(argc, argv);

    // -----------------------------------------------------------------------
    // Parse muplar-level flags (before the ELF path)
    // -----------------------------------------------------------------------
    muplar::runtime::PlatformLaunchConfig launch_cfg;
    int arg_start = 1;
    std::optional<std::string> prefix_spec;
    std::optional<muplar::runtime::prefix::PrefixLayout> active_prefix;

    while (arg_start < argc && argv[arg_start][0] == '-') {
        std::string flag = argv[arg_start];

        if (flag == "--verbose" || flag == "-v") {
            launch_cfg.verbose = true;
            ++arg_start;
        } else if (flag == "--quiet" || flag == "-q") {
            launch_cfg.quiet = true;
            ++arg_start;
        } else if (flag == "--apk") {
            launch_cfg.apk_mode = true;
            ++arg_start;
        } else if ((flag == "--apk-lib") && arg_start + 1 < argc) {
            launch_cfg.apk_mode = true;
            launch_cfg.apk_lib_name = argv[arg_start + 1];
            arg_start += 2;
        } else if ((flag == "--apk-extract-dir") && arg_start + 1 < argc) {
            launch_cfg.apk_mode = true;
            launch_cfg.apk_extract_dir = argv[arg_start + 1];
            arg_start += 2;
        } else if ((flag == "--sysroot") && arg_start + 1 < argc) {
            launch_cfg.sysroot = argv[arg_start + 1];
            arg_start  += 2;
        } else if ((flag == "--prefix") && arg_start + 1 < argc) {
            prefix_spec = argv[arg_start + 1];
            arg_start += 2;
        } else if (flag == "--native-activity") {
            launch_cfg.native_activity = true;
            ++arg_start;
        } else if (flag == "--strict-direct-imports") {
            launch_cfg.strict_direct_imports = true;
            ++arg_start;
        } else if (flag == "--host-window") {
            launch_cfg.host_window = true;
            ++arg_start;
        } else if ((flag == "--host-window-ms") && arg_start + 1 < argc) {
            launch_cfg.host_window = true;
            try {
                launch_cfg.host_window_linger_ms =
                    std::stoi(argv[arg_start + 1], nullptr, 0);
            } catch (const std::exception&) {
                std::cerr << "Invalid --host-window-ms value: "
                          << argv[arg_start + 1] << "\n";
                return 1;
            }
            arg_start += 2;
        } else if ((flag == "--jni-call") && arg_start + 3 < argc) {
            launch_cfg.jni_call.enabled = true;
            launch_cfg.jni_call.class_name = argv[arg_start + 1];
            launch_cfg.jni_call.method_name = argv[arg_start + 2];
            launch_cfg.jni_call.signature = argv[arg_start + 3];
            arg_start += 4;
        } else if (flag == "--jni-static") {
            launch_cfg.jni_call.receiver_explicit = true;
            launch_cfg.jni_call.receiver_static = true;
            ++arg_start;
        } else if (flag == "--jni-instance") {
            launch_cfg.jni_call.receiver_explicit = true;
            launch_cfg.jni_call.receiver_static = false;
            ++arg_start;
        } else if ((flag == "--jni-int" || flag == "--jni-arg") &&
                   arg_start + 1 < argc) {
            try {
                launch_cfg.jni_call.int_args.push_back(
                    std::stoll(argv[arg_start + 1], nullptr, 0));
            } catch (const std::exception&) {
                std::cerr << "Invalid --jni-int value: "
                          << argv[arg_start + 1] << "\n";
                return 1;
            }
            arg_start += 2;
        } else if (flag == "--") {
            ++arg_start;
            break;
        } else {
            std::cerr << "Unknown flag: " << flag << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    if (arg_start >= argc) {
        print_usage(argv[0]);
        return 1;
    }

    launch_cfg.input_path = argv[arg_start];

    if (prefix_spec) {
        try {
            active_prefix = muplar::runtime::prefix::open_prefix(
                *prefix_spec, launch_cfg.sysroot, true);
            if (launch_cfg.sysroot.empty()) {
                if (!active_prefix->runtime_sysroot.empty() && std::filesystem::exists(active_prefix->runtime_sysroot)) {
                    launch_cfg.sysroot =
                        active_prefix->runtime_sysroot.string();
                } else if (active_prefix->kind == muplar::runtime::prefix::PrefixKind::Android ||
                           active_prefix->kind == muplar::runtime::prefix::PrefixKind::Linux) {
                    launch_cfg.sysroot = active_prefix->rootfs.string();
                }
            }
            launch_cfg.active_prefix = active_prefix;
            std::string prefix_kind =
                prefix_kind_display_string(active_prefix->kind);
            std::string guest_arch =
                muplar::runtime::prefix::to_string(active_prefix->arch);
            if (!launch_cfg.quiet) {
                std::cerr << "[Prefix] " << active_prefix->name
                          << " kind=" << prefix_kind
                          << " arch=" << guest_arch
                          << " runner=" << active_prefix->runner
                          << " root=" << active_prefix->root.string()
                          << " rootfs=" << active_prefix->rootfs.string() << "\n";
                if (!active_prefix->runtime_sysroot.empty()) {
                    if (active_prefix->kind == muplar::runtime::prefix::PrefixKind::Wine) {
                        std::cerr << "[Prefix] Windows compatibility runtime=configured\n";
                    } else {
                        std::cerr << "[Prefix] runtime sysroot="
                                  << active_prefix->runtime_sysroot.string() << "\n";
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Prefix error: " << e.what() << "\n";
            return 1;
        }
    }

    for (int i = arg_start + 1; i < argc; ++i) {
        launch_cfg.guest_args.push_back(argv[i]);
    }

    try {
        return run_platform_runtime(launch_cfg);
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return 1;
    }
}
