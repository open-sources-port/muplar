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
// Hypervisor.framework pipeline; Linux and Wine runtimes can plug in beside it.

#include <iostream>
#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include <sys/resource.h>

#include "android_aarch64_runtime.h"
#include "platform_runtime.h"
#include "prefix.h"

static void print_usage(const char* prog)
{
    std::cerr << "Usage: " << prog
              << " [--verbose] [--sysroot PATH]\n"
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
                 " [--kind android|linux|wine]"
              << " [--arch aarch64|x86_64] [--runner elfuse]"
              << " [--sysroot PATH]\n"
              << "              (android supports aarch64 only)\n"
              << "       " << prog << " prefix list [--plain]\n"
              << "       " << prog << " prefix info NAME|PATH\n"
              << "       " << prog << " prefix clone SRC_NAME|PATH DST_NAME"
              << " [--root PATH] [--replace]\n"
              << "       " << prog << " prefix delete NAME|PATH --yes\n";
}

static std::string prefix_kind_string(
    const muplar::runtime::prefix::PrefixLayout& prefix)
{
    return muplar::runtime::prefix::to_string(prefix.kind);
}

static std::string prefix_arch_string(
    const muplar::runtime::prefix::PrefixLayout& prefix)
{
    return muplar::runtime::prefix::to_string(prefix.arch);
}

static std::string optional_path_string(const std::filesystem::path& path)
{
    return path.empty() ? "-" : path.string();
}

static void print_prefix_table(
    const std::vector<muplar::runtime::prefix::PrefixLayout>& prefixes)
{
    struct Row {
        std::string name;
        std::string kind;
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
            prefix_arch_string(prefix),
            prefix.runner,
            "stopped",
            optional_path_string(prefix.runtime_sysroot),
            prefix.root.string(),
        });
    }

    if (rows.empty()) {
        std::cout << "No prefixes found.\n";
        std::cout << "Create one with: mup prefix create default --kind android"
                  << " --arch aarch64 --sysroot build/sysroot\n";
        return;
    }

    size_t name_w = 4, kind_w = 4, arch_w = 4, runner_w = 6, state_w = 5,
           sysroot_w = 7;
    for (const auto& row : rows) {
        name_w = std::max(name_w, row.name.size());
        kind_w = std::max(kind_w, row.kind.size());
        arch_w = std::max(arch_w, row.arch.size());
        runner_w = std::max(runner_w, row.runner.size());
        state_w = std::max(state_w, row.state.size());
        sysroot_w = std::max(sysroot_w, row.sysroot.size());
    }

    std::cout << std::left
              << std::setw(static_cast<int>(name_w + 2)) << "Name"
              << std::setw(static_cast<int>(kind_w + 2)) << "Kind"
              << std::setw(static_cast<int>(arch_w + 2)) << "Arch"
              << std::setw(static_cast<int>(runner_w + 2)) << "Runner"
              << std::setw(static_cast<int>(state_w + 2)) << "State"
              << std::setw(static_cast<int>(sysroot_w + 2)) << "Sysroot"
              << "Root\n";
    std::cout << std::string(name_w, '-') << "  "
              << std::string(kind_w, '-') << "  "
              << std::string(arch_w, '-') << "  "
              << std::string(runner_w, '-') << "  "
              << std::string(state_w, '-') << "  "
              << std::string(sysroot_w, '-') << "  "
              << "----\n";

    for (const auto& row : rows) {
        std::cout << std::left
                  << std::setw(static_cast<int>(name_w + 2)) << row.name
                  << std::setw(static_cast<int>(kind_w + 2)) << row.kind
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
                  << "\t" << prefix_arch_string(prefix)
                  << "\t" << prefix.runner
                  << "\t" << prefix.root.string();
        if (!prefix.runtime_sysroot.empty())
            std::cout << "\t" << prefix.runtime_sysroot.string();
        std::cout << "\n";
    }
}

static void print_prefix_info(
    const muplar::runtime::prefix::PrefixLayout& prefix)
{
    std::cout << "Name: " << prefix.name << "\n";
    std::cout << "Kind: " << prefix_kind_string(prefix) << "\n";
    std::cout << "Arch: " << prefix_arch_string(prefix) << "\n";
    std::cout << "Runner: " << prefix.runner << "\n";
    std::cout << "State: stopped\n";
    std::cout << "Root: " << prefix.root.string() << "\n";
    std::cout << "Rootfs: " << prefix.rootfs.string() << "\n";
    std::cout << "Packages: " << prefix.packages_dir.string() << "\n";
    std::cout << "Registry: " << prefix.registry_dir.string() << "\n";
    std::cout << "Cache: " << prefix.cache_dir.string() << "\n";
    std::cout << "APK cache: " << prefix.apk_cache_dir.string() << "\n";
    std::cout << "Logs: " << prefix.logs_dir.string() << "\n";
    std::cout << "Runtime sysroot: "
              << optional_path_string(prefix.runtime_sysroot) << "\n";
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
            } catch (const std::exception& e) {
                std::cerr << e.what() << "\n";
                return 2;
            }
            i += 2;
        } else if (flag == "--runner" && i + 1 < argc) {
            runner = argv[i + 1];
            i += 2;
        } else {
            std::cerr << "Unknown prefix create flag: " << flag << "\n";
            return 2;
        }
    }

    try {
        auto prefix = root.empty()
            ? muplar::runtime::prefix::open_prefix(
                  spec, runtime_sysroot, true, kind, arch, runner)
            : muplar::runtime::prefix::open_prefix_at_root(
                  spec, root, runtime_sysroot, true, kind, arch, runner);
        std::cout << "prefix: " << prefix.root << "\n";
        std::cout << "kind: "
                  << muplar::runtime::prefix::to_string(prefix.kind) << "\n";
        std::cout << "arch: "
                  << muplar::runtime::prefix::to_string(prefix.arch) << "\n";
        std::cout << "runner: " << prefix.runner << "\n";
        std::cout << "rootfs: " << prefix.rootfs << "\n";
        if (!prefix.runtime_sysroot.empty())
            std::cout << "runtime sysroot: " << prefix.runtime_sysroot << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Prefix error: " << e.what() << "\n";
        return 1;
    }
}

int main(int argc, char** argv)
{
    // Increase soft limit for open files descriptor limit from 256 to maximum allowed
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
        rl.rlim_cur = rl.rlim_max;
        if (setrlimit(RLIMIT_NOFILE, &rl) != 0) {
            rl.rlim_cur = 10240;
            setrlimit(RLIMIT_NOFILE, &rl);
        }
    }

    std::cout << "Muplar CLI (mup)\n";

    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    if (std::string(argv[1]) == "prefix")
        return handle_prefix_command(argc, argv);

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
    launch_cfg.verbose = true;

    if (prefix_spec) {
        try {
            active_prefix = muplar::runtime::prefix::open_prefix(
                *prefix_spec, launch_cfg.sysroot, true);
            if (launch_cfg.sysroot.empty() &&
                !active_prefix->runtime_sysroot.empty()) {
                launch_cfg.sysroot =
                    active_prefix->runtime_sysroot.string();
            }
            launch_cfg.active_prefix = active_prefix;
            std::string prefix_kind =
                muplar::runtime::prefix::to_string(active_prefix->kind);
            std::string guest_arch =
                muplar::runtime::prefix::to_string(active_prefix->arch);
            std::cerr << "[Prefix] " << active_prefix->name
                      << " kind=" << prefix_kind
                      << " arch=" << guest_arch
                      << " runner=" << active_prefix->runner
                      << " root=" << active_prefix->root.string()
                      << " rootfs=" << active_prefix->rootfs.string() << "\n";
            if (!active_prefix->runtime_sysroot.empty()) {
                std::cerr << "[Prefix] runtime sysroot="
                          << active_prefix->runtime_sysroot.string() << "\n";
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
        muplar::runtime::android::AndroidAarch64Runtime runtime;
        return runtime.run(launch_cfg);
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return 1;
    }
}
