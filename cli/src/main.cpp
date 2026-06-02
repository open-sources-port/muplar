// cli/src/main.cpp
//
// Muplar CLI (mup)
//
// Before this change, main() called ExecutionContext::execute() which
// cast the AArch64 entrypoint to a C function pointer and jumped to it
// directly.  That crashed on the first Linux syscall.
//
// Now main() uses GuestRunner, which wraps the elfuse Hypervisor.framework
// pipeline: ELF load → guest memory init → stack build → vCPU create →
// syscall-translated run loop.

#include <iostream>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "apk_envelope.h"
#include "prefix.h"
#include "guest_runner.h"   // NEW: replaces ExecutionContext

// Keep ElfLoader available for inspection/debug builds
#include "elf_loader.h"

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
              << "       " << prog << " prefix list [--plain]\n"
              << "       " << prog << " prefix info NAME|PATH\n"
              << "       " << prog << " prefix clone SRC_NAME|PATH DST_NAME"
              << " [--root PATH] [--replace]\n"
              << "       " << prog << " prefix delete NAME|PATH --yes\n";
}

static std::string lower_ext(const std::string& path)
{
    std::string ext = std::filesystem::path(path).extension().string();
    for (char& c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
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
    muplar::runtime::elf::GuestRunnerConfig cfg;
    int arg_start = 1;
    bool apk_mode = false;
    std::optional<std::string> apk_lib_name;
    std::optional<std::string> prefix_spec;
    std::optional<muplar::runtime::prefix::PrefixLayout> active_prefix;
    std::filesystem::path apk_extract_dir;

    while (arg_start < argc && argv[arg_start][0] == '-') {
        std::string flag = argv[arg_start];

        if (flag == "--verbose" || flag == "-v") {
            cfg.verbose = true;
            ++arg_start;
        } else if (flag == "--apk") {
            apk_mode = true;
            ++arg_start;
        } else if ((flag == "--apk-lib") && arg_start + 1 < argc) {
            apk_mode = true;
            apk_lib_name = argv[arg_start + 1];
            arg_start += 2;
        } else if ((flag == "--apk-extract-dir") && arg_start + 1 < argc) {
            apk_mode = true;
            apk_extract_dir = argv[arg_start + 1];
            arg_start += 2;
        } else if ((flag == "--sysroot") && arg_start + 1 < argc) {
            cfg.sysroot = argv[arg_start + 1];
            arg_start  += 2;
        } else if ((flag == "--prefix") && arg_start + 1 < argc) {
            prefix_spec = argv[arg_start + 1];
            arg_start += 2;
        } else if (flag == "--native-activity") {
            cfg.native_activity = true;
            ++arg_start;
        } else if (flag == "--strict-direct-imports") {
            cfg.strict_direct_imports = true;
            ++arg_start;
        } else if (flag == "--host-window") {
            cfg.host_window = true;
            ++arg_start;
        } else if ((flag == "--host-window-ms") && arg_start + 1 < argc) {
            cfg.host_window = true;
            try {
                cfg.host_window_linger_ms = std::stoi(argv[arg_start + 1], nullptr, 0);
            } catch (const std::exception&) {
                std::cerr << "Invalid --host-window-ms value: "
                          << argv[arg_start + 1] << "\n";
                return 1;
            }
            arg_start += 2;
        } else if ((flag == "--jni-call") && arg_start + 3 < argc) {
            cfg.jni_call.enabled = true;
            cfg.jni_call.class_name  = argv[arg_start + 1];
            cfg.jni_call.method_name = argv[arg_start + 2];
            cfg.jni_call.signature   = argv[arg_start + 3];
            arg_start += 4;
        } else if (flag == "--jni-static") {
            cfg.jni_call.receiver_explicit = true;
            cfg.jni_call.receiver_static = true;
            ++arg_start;
        } else if (flag == "--jni-instance") {
            cfg.jni_call.receiver_explicit = true;
            cfg.jni_call.receiver_static = false;
            ++arg_start;
        } else if ((flag == "--jni-int" || flag == "--jni-arg") &&
                   arg_start + 1 < argc) {
            try {
                cfg.jni_call.int_args.push_back(
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

    // -----------------------------------------------------------------------
    // Build the guest config
    // -----------------------------------------------------------------------
    std::string input_path = argv[arg_start];
    cfg.elf_path = input_path;
    cfg.verbose = true;

    if (prefix_spec) {
        try {
            active_prefix = muplar::runtime::prefix::open_prefix(
                *prefix_spec, cfg.sysroot, true);
            if (cfg.sysroot.empty() && !active_prefix->runtime_sysroot.empty())
                cfg.sysroot = active_prefix->runtime_sysroot.string();
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

    if (apk_mode || lower_ext(input_path) == ".apk") {
        try {
            if (active_prefix &&
                active_prefix->kind !=
                    muplar::runtime::prefix::PrefixKind::Android) {
                std::cerr << "APK launch requires an android prefix; got kind="
                          << muplar::runtime::prefix::to_string(
                                 active_prefix->kind)
                          << "\n";
                return 1;
            }
            muplar::runtime::apk::ApkLaunchConfig apk_cfg;
            apk_cfg.apk_path = input_path;
            apk_cfg.lib_name = apk_lib_name;
            apk_cfg.output_dir = apk_extract_dir;
            if (active_prefix)
                apk_cfg.output_base_dir = active_prefix->apk_cache_dir;

            auto apk = muplar::runtime::apk::prepare_apk_launch(apk_cfg);
            cfg.elf_path = apk.so_path.string();
            cfg.native_activity = true;
            cfg.force_android_so = true;
            cfg.native_lib_search_dirs.push_back(
                (apk.extract_dir / "lib" / "arm64-v8a").string());
            cfg.apk_assets_dir = apk.assets_dir.string();
            cfg.package_code_path = apk.apk_path.string();
            if (apk.manifest_package)
                cfg.package_name = *apk.manifest_package;

            std::cerr << "[APK] extracted " << apk.extracted_libs.size()
                      << " arm64-v8a lib(s) to "
                      << apk.extract_dir.string() << "\n";
            if (!apk.extracted_assets.empty()) {
                std::cerr << "[APK] extracted " << apk.extracted_assets.size()
                          << " asset(s) to "
                          << apk.assets_dir.string() << "\n";
            }
            if (apk.manifest_lib) {
                std::cerr << "[APK] manifest lib_name="
                          << *apk.manifest_lib << "\n";
            }
            if (apk.manifest_package) {
                std::cerr << "[APK] manifest package="
                          << *apk.manifest_package << "\n";
            }
            std::cerr << "[APK] selected lib " << apk.selected_lib
                      << ".so -> " << cfg.elf_path << "\n";
        } catch (const std::exception& e) {
            std::cerr << "APK error: " << e.what() << "\n";
            return 1;
        }
    }

    // argv[0] inside the guest = the executable path; followed by user args.
    cfg.argv.push_back(cfg.elf_path);
    for (int i = arg_start + 1; i < argc; ++i) {
        cfg.argv.push_back(argv[i]);
    }

    // -----------------------------------------------------------------------
    // Optional: use ElfLoader to print info before running (debug aid)
    // -----------------------------------------------------------------------
    try {
        muplar::runtime::elf::ElfLoader loader;
        auto image = loader.load(cfg.elf_path);

        std::cout << "ELF entry   : 0x" << std::hex << image.entry << "\n"
                  << "Load range  : 0x" << image.load_min
                  << " – 0x"            << image.load_max << "\n"
                  << "Segments    : " << std::dec << image.segments.size() << "\n";
    } catch (const std::exception& e) {
        std::cerr << "ELF inspect error: " << e.what() << "\n";
        // Non-fatal — GuestRunner will re-validate internally via elfuse
    }

    // -----------------------------------------------------------------------
    // Run through elfuse Hypervisor.framework pipeline
    // -----------------------------------------------------------------------
    try {
        muplar::runtime::elf::GuestRunner runner;
        return runner.run(cfg);

    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return 1;
    }
}
