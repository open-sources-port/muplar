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
// Hypervisor.framework pipeline; Linux and Windows-compatible runtimes plug in
// beside it.

#include <iostream>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <signal.h>
#include <cerrno>
#include <cstring>
#include <unistd.h>
#ifdef __APPLE__
#include <libproc.h>
#include <sys/event.h>
#endif

#include "android_aarch64_runtime.h"
#include "linux_aarch64_runtime.h"
#include "linux_x86_64_runtime.h"
#include "platform_runtime.h"
#include "guest_runner.h"
#include "prefix.h"

extern "C" {
#include "debug/log.h"
#include "runtime/forkipc.h"
void proc_set_fakeroot_enabled(bool enabled);
}

#ifdef MUPLAR_HAS_WINE
#include "wine_runtime.h"
#endif

static constexpr const char *kRosettadTranslatorPath =
    "/Library/Apple/usr/libexec/oah/RosettaLinux/rosettad";

static volatile sig_atomic_t g_session_client_cancelled = 0;

static void session_client_signal_handler(int)
{
    g_session_client_cancelled = 1;
}

static std::string shell_single_quote(const std::string &value)
{
    std::string result = "'";
    for (char ch : value) {
        if (ch == '\n' || ch == '\r')
            throw std::runtime_error(
                "session arguments cannot contain newlines");
        if (ch == '\'')
            result += "'\\''";
        else
            result += ch;
    }
    result += "'";
    return result;
}

static bool write_session_command(const std::filesystem::path &fifo,
                                  const std::string &command)
{
    int fd = open(fifo.c_str(), O_WRONLY | O_NONBLOCK);
    if (fd < 0)
        return false;
    std::string line = command + "\n";
    ssize_t written = write(fd, line.data(), line.size());
    close(fd);
    return written == static_cast<ssize_t>(line.size());
}

static std::filesystem::path current_executable_path()
{
#ifdef __APPLE__
    char buffer[PROC_PIDPATHINFO_MAXSIZE];
    int len = proc_pidpath(getpid(), buffer, sizeof(buffer));
    if (len > 0)
        return std::filesystem::path(std::string(buffer, len));
#endif
    return {};
}

static std::filesystem::path workspace_root_from_executable()
{
    std::filesystem::path current = current_executable_path();
    if (current.empty())
        current = std::filesystem::current_path();
    if (std::filesystem::is_regular_file(current))
        current = current.parent_path();
    for (int i = 0; i < 8 && !current.empty(); ++i) {
        if (std::filesystem::is_regular_file(current / "CMakeLists.txt"))
            return current;
        current = current.parent_path();
    }
    return std::filesystem::current_path();
}

static std::filesystem::path find_bundled_tool(const std::string &name)
{
    std::filesystem::path exe = current_executable_path();
    if (!exe.empty()) {
        std::filesystem::path macos = exe.parent_path();
        std::filesystem::path bundled =
            macos.parent_path() / "Resources" / "tools" / name;
        if (std::filesystem::is_regular_file(bundled))
            return bundled;
    }
    std::filesystem::path workspace_tool =
        workspace_root_from_executable() / "tools" / name;
    if (std::filesystem::is_regular_file(workspace_tool))
        return workspace_tool;
    return {};
}

static int run_script(const std::filesystem::path &script,
                      const std::vector<std::string> &args)
{
    if (script.empty())
        return 127;
    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "fork failed: " << std::strerror(errno) << "\n";
        return 127;
    }
    if (pid == 0) {
        std::vector<std::string> storage;
        storage.push_back("/bin/zsh");
        storage.push_back(script.string());
        storage.insert(storage.end(), args.begin(), args.end());
        std::vector<char *> argv;
        for (std::string &item : storage)
            argv.push_back(item.data());
        argv.push_back(nullptr);
        execv("/bin/zsh", argv.data());
        std::fprintf(stderr, "exec failed for %s: %s\n",
                     script.string().c_str(), std::strerror(errno));
        _exit(127);
    }

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR)
            continue;
        std::cerr << "waitpid failed: " << std::strerror(errno) << "\n";
        return 127;
    }
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (WIFSIGNALED(status))
        return 128 + WTERMSIG(status);
    return 127;
}

// Blocks preemption/doorbell signals and starts elfuse's dedicated consumer
// thread for them, right before actually launching a guest -- not at process
// bring-up. run_script()'s fork()+execv() helper above also runs on
// non-guest subcommands (e.g. `prefix create --kind android`'s sysroot setup
// script); forking after this thread exists risks the classic post-fork
// malloc deadlock, since the fork child allocates (std::vector, std::string)
// before exec while the vanished thread could have been holding an
// allocator lock. Idempotent -- elfuse guards it with a plain bool -- so
// it's safe to call from every guest-launch site rather than once at the
// top of main().
static void ensure_preempt_or_disable_timeout(
    muplar::runtime::PlatformLaunchConfig &cfg)
{
    bool preempt_ok = true;
#ifndef _WIN32
    preempt_ok = muplar::runtime::elf::ensure_preempt_initialized();
#endif
    if (!preempt_ok && cfg.timeout_sec > 0) {
        std::cerr << "[Muplar] preemption thread failed to start; "
                     "disabling --timeout-sec (was "
                  << cfg.timeout_sec
                  << ") since it can no longer be enforced\n";
        cfg.timeout_sec = 0;
    }
}

static std::filesystem::path default_android_art_sysroot()
{
    const char *override_path = std::getenv("MUPLAR_ANDROID_SYSROOT");
    if (override_path && override_path[0])
        return std::filesystem::path(override_path);
    const char *home = std::getenv("HOME");
    if (!home || !home[0])
        return {};
    return std::filesystem::path(home) / ".muplar" / "sysroots" /
           "android-arm64" / "api-35" / "sysroot";
}

static bool ensure_android_art_sysroot(std::filesystem::path &runtime_sysroot)
{
    if (runtime_sysroot.empty())
        runtime_sysroot = default_android_art_sysroot();
    if (runtime_sysroot.empty()) {
        std::cerr
            << "Unable to choose Android ART sysroot path: HOME is unset\n";
        return false;
    }

    std::filesystem::path script =
        find_bundled_tool("ensure-android-art-sysroot.sh");
    if (script.empty()) {
        if (std::filesystem::is_directory(runtime_sysroot))
            return true;
        std::cerr << "Android ART sysroot helper is unavailable.\n";
        return false;
    }

    std::cout << "[android-sysroot] ensuring production ART sysroot...\n";
    int rc = run_script(script, {"--sysroot", runtime_sysroot.string()});
    if (rc != 0) {
        std::cerr << "Android ART sysroot setup failed with exit code " << rc
                  << "\n";
        return false;
    }
    return true;
}

static void stream_session_log(const std::filesystem::path &log_path,
                               std::uintmax_t &offset)
{
    std::ifstream input(log_path, std::ios::binary);
    if (!input)
        return;
    input.seekg(static_cast<std::streamoff>(offset));
    char buffer[8192];
    while (input) {
        input.read(buffer, sizeof(buffer));
        std::streamsize count = input.gcount();
        if (count > 0) {
            std::cout.write(buffer, count);
            std::cout.flush();
            offset += static_cast<std::uintmax_t>(count);
        }
    }
}

static void terminate_host_process_tree(pid_t pid)
{
#ifdef __APPLE__
    pid_t children[1024];
    int count = proc_listchildpids(pid, children, sizeof(children));
    if (count > 0) {
        for (int i = 0; i < count; ++i) {
            if (children[i] > 0)
                terminate_host_process_tree(children[i]);
        }
    }
#endif
    if (pid > 1)
        kill(pid, SIGTERM);
}

static int handle_linux_session_exec(int argc, char **argv)
{
    if (argc < 5 || std::string(argv[3]) != "--") {
        std::cerr << "Usage: " << argv[0]
                  << " linux-session-exec PREFIX -- PROGRAM [ARGS...]\n";
        return 2;
    }

    namespace fs = std::filesystem;
    namespace prefix = muplar::runtime::prefix;
    prefix::PrefixLayout layout;
    try {
        layout = prefix::open_prefix(argv[2], {}, true);
    } catch (const std::exception &e) {
        std::cerr << "Session prefix error: " << e.what() << "\n";
        return 1;
    }
    if (layout.kind != prefix::PrefixKind::Linux) {
        std::cerr << "linux-session-exec requires a Linux prefix\n";
        return 2;
    }

    fs::path host_session_dir = layout.rootfs / "tmp" / "muplar-session";
    fs::path host_requests_dir = host_session_dir / "requests";
    fs::path host_pid_map_dir = host_session_dir / "host-pids";
    fs::path fifo = host_session_dir / "commands";
    for (int i = 0; i < 200; ++i) {
        if (fs::exists(host_session_dir / "ready") && fs::exists(fifo))
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (!fs::exists(host_session_dir / "ready") || !fs::exists(fifo)) {
        std::cerr << "Linux session launcher did not become ready\n";
        return 1;
    }

    std::ostringstream id_builder;
    id_builder << getpid() << "-"
               << std::chrono::steady_clock::now().time_since_epoch().count();
    std::string id = id_builder.str();
    fs::path request = host_requests_dir / (id + ".request");
    fs::path request_tmp = host_requests_dir / (id + ".request.tmp");
    fs::path status = host_requests_dir / (id + ".status");
    fs::path log = host_requests_dir / (id + ".log");
    fs::path pid = host_requests_dir / (id + ".pid");
    std::string guest_request =
        "/tmp/muplar-session/requests/" + id + ".request";

    try {
        std::ofstream output(request_tmp, std::ios::trunc);
        if (!output)
            throw std::runtime_error("cannot create session request");
        output << "set --";
        for (int i = 4; i < argc; ++i)
            output << " " << shell_single_quote(argv[i]);
        output << "\n";
        output.close();
        fs::rename(request_tmp, request);
    } catch (const std::exception &e) {
        std::cerr << "Session request error: " << e.what() << "\n";
        std::error_code ec;
        fs::remove(request_tmp, ec);
        return 1;
    }

    bool submitted = false;
    for (int i = 0; i < 100 && !submitted; ++i) {
        submitted = write_session_command(fifo, guest_request);
        if (!submitted)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (!submitted) {
        std::cerr << "Linux session launcher is not accepting requests\n";
        return 1;
    }

    struct sigaction action = {};
    action.sa_handler = session_client_signal_handler;
    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, nullptr);
    sigaction(SIGINT, &action, nullptr);

    std::uintmax_t log_offset = 0;
    while (!fs::exists(status)) {
        stream_session_log(log, log_offset);
        if (g_session_client_cancelled && fs::exists(pid)) {
            long long guest_pid = 0;
            std::ifstream pid_input(pid);
            pid_input >> guest_pid;
            fs::path host_pid_path =
                host_pid_map_dir / (std::to_string(guest_pid) + ".hostpid");
            long long host_pid = 0;
            std::ifstream host_pid_input(host_pid_path);
            host_pid_input >> host_pid;
            if (host_pid > 1)
                terminate_host_process_tree(static_cast<pid_t>(host_pid));
            for (int i = 0; i < 20 && !fs::exists(status); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            stream_session_log(log, log_offset);
            std::error_code cancel_ec;
            fs::remove(request, cancel_ec);
            fs::remove(status, cancel_ec);
            fs::remove(log, cancel_ec);
            fs::remove(pid, cancel_ec);
            fs::remove(host_pid_path, cancel_ec);
            return 143;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    stream_session_log(log, log_offset);

    int exit_code = 1;
    std::ifstream status_input(status);
    status_input >> exit_code;
    std::error_code ec;
    long long guest_pid = 0;
    std::ifstream pid_input(pid);
    pid_input >> guest_pid;
    if (guest_pid > 0) {
        fs::remove(host_pid_map_dir / (std::to_string(guest_pid) + ".hostpid"),
                   ec);
    }
    fs::remove(request, ec);
    fs::remove(status, ec);
    fs::remove(log, ec);
    fs::remove(pid, ec);
    return exit_code;
}

static void print_usage(const char *prog)
{
    std::cerr
        << "Usage: " << prog << " [--verbose] [--quiet] [--sysroot PATH]\n"
        << "              [--prefix NAME|PATH]\n"
        << "              [--apk] [--apk-lib NAME] [--apk-extract-dir PATH]\n"
        << "              [--native-activity]\n"
        << "              [--strict-direct-imports] [--fakeroot]\n"
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
              << "              (android supports aarch64 only; windows "
                 "supports x86_64 only; linux supports both)\n"
              << "       " << prog << " prefix list [--plain]\n"
              << "       " << prog << " prefix info NAME|PATH\n"
              << "       " << prog << " prefix clone SRC_NAME|PATH DST_NAME"
              << " [--root PATH] [--replace]\n"
              << "       " << prog << " prefix delete NAME|PATH --yes\n";
    std::cerr << "       " << prog
              << " instance start NAME [--daemon] [--windows-runtime-bin PATH] "
                 "<program> [args...]\n"
              << "       " << prog << " instance stop  NAME [--force]\n"
              << "       " << prog << " instance status NAME\n"
              << "       " << prog << " instance list\n";
    std::cerr << "       " << prog
              << " linux-session-exec PREFIX -- PROGRAM [ARGS...]\n";
}

static int handle_rosettad_translate_command(int argc, char **argv)
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
        ensure_preempt_or_disable_timeout(launch_cfg);
        muplar::runtime::android::AndroidAarch64Runtime runtime;
        return runtime.run(launch_cfg);
    } catch (const std::exception &e) {
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
    const muplar::runtime::prefix::PrefixLayout &prefix)
{
    return prefix_kind_display_string(prefix.kind);
}

static std::string prefix_arch_string(
    const muplar::runtime::prefix::PrefixLayout &prefix)
{
    return muplar::runtime::prefix::to_string(prefix.arch);
}

static std::string prefix_state_string(
    const muplar::runtime::prefix::PrefixLayout &layout)
{
    auto state = muplar::runtime::prefix::query_prefix_state(layout);
    return (state == muplar::runtime::prefix::PrefixState::Running) ? "running"
                                                                    : "stopped";
}

static std::string optional_path_string(const std::filesystem::path &path)
{
    return path.empty() ? "-" : path.string();
}

static std::string runtime_sysroot_display_string(
    const muplar::runtime::prefix::PrefixLayout &prefix)
{
    if (prefix.runtime_sysroot.empty())
        return "-";
    if (prefix.kind == muplar::runtime::prefix::PrefixKind::Wine)
        return "configured";
    return prefix.runtime_sysroot.string();
}

static void print_prefix_table(
    const std::vector<muplar::runtime::prefix::PrefixLayout> &prefixes)
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
    for (const auto &prefix : prefixes) {
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

    size_t name_w = 4, kind_w = 4, distro_w = 6, arch_w = 4, runner_w = 6,
           state_w = 5, sysroot_w = 7;
    for (const auto &row : rows) {
        name_w = std::max(name_w, row.name.size());
        kind_w = std::max(kind_w, row.kind.size());
        distro_w = std::max(distro_w, row.distro.size());
        arch_w = std::max(arch_w, row.arch.size());
        runner_w = std::max(runner_w, row.runner.size());
        state_w = std::max(state_w, row.state.size());
        sysroot_w = std::max(sysroot_w, row.sysroot.size());
    }

    std::cout << std::left << std::setw(static_cast<int>(name_w + 2)) << "Name"
              << std::setw(static_cast<int>(kind_w + 2)) << "Kind"
              << std::setw(static_cast<int>(distro_w + 2)) << "Distro"
              << std::setw(static_cast<int>(arch_w + 2)) << "Arch"
              << std::setw(static_cast<int>(runner_w + 2)) << "Runner"
              << std::setw(static_cast<int>(state_w + 2)) << "State"
              << std::setw(static_cast<int>(sysroot_w + 2)) << "Sysroot"
              << "Root\n";
    std::cout << std::string(name_w, '-') << "  " << std::string(kind_w, '-')
              << "  " << std::string(distro_w, '-') << "  "
              << std::string(arch_w, '-') << "  " << std::string(runner_w, '-')
              << "  " << std::string(state_w, '-') << "  "
              << std::string(sysroot_w, '-') << "  ----\n";

    for (const auto &row : rows) {
        std::cout << std::left << std::setw(static_cast<int>(name_w + 2))
                  << row.name << std::setw(static_cast<int>(kind_w + 2))
                  << row.kind << std::setw(static_cast<int>(distro_w + 2))
                  << row.distro << std::setw(static_cast<int>(arch_w + 2))
                  << row.arch << std::setw(static_cast<int>(runner_w + 2))
                  << row.runner << std::setw(static_cast<int>(state_w + 2))
                  << row.state << std::setw(static_cast<int>(sysroot_w + 2))
                  << row.sysroot << row.root << "\n";
    }
}

static void print_prefix_plain(
    const std::vector<muplar::runtime::prefix::PrefixLayout> &prefixes)
{
    for (const auto &prefix : prefixes) {
        std::cout << prefix.name << "\t" << prefix_kind_string(prefix) << "\t"
                  << (prefix.distro.empty() ? "-" : prefix.distro) << "\t"
                  << prefix_arch_string(prefix) << "\t" << prefix.runner << "\t"
                  << prefix.root.string();
        if (!prefix.runtime_sysroot.empty())
            std::cout << "\t" << runtime_sysroot_display_string(prefix);
        std::cout << "\n";
    }
}

static void print_prefix_info(
    const muplar::runtime::prefix::PrefixLayout &prefix)
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
    std::cout << "Runtime sysroot: " << runtime_sysroot_display_string(prefix)
              << "\n";
}

static int handle_prefix_command(int argc, char **argv)
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
            auto prefix =
                muplar::runtime::prefix::open_prefix(argv[3], {}, false);
            print_prefix_info(prefix);
            return 0;
        } catch (const std::exception &e) {
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
            std::cout << "cloned: " << argv[3] << " -> " << prefix.root.string()
                      << "\n";
            print_prefix_info(prefix);
            return 0;
        } catch (const std::exception &e) {
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
            auto root = muplar::runtime::prefix::resolve_prefix_root(argv[3]);
            muplar::runtime::prefix::delete_prefix(argv[3]);
            std::cout << "deleted: " << root.string() << "\n";
            return 0;
        } catch (const std::exception &e) {
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
        } else if ((flag == "--sysroot" || flag == "--runtime") &&
                   i + 1 < argc) {
            runtime_sysroot = argv[i + 1];
            i += 2;
        } else if (flag == "--kind" && i + 1 < argc) {
            try {
                kind = muplar::runtime::prefix::parse_prefix_kind(argv[i + 1]);
            } catch (const std::exception &e) {
                std::cerr << e.what() << "\n";
                return 2;
            }
            i += 2;
        } else if (flag == "--arch" && i + 1 < argc) {
            try {
                arch = muplar::runtime::prefix::parse_guest_arch(argv[i + 1]);
                arch_set = true;
            } catch (const std::exception &e) {
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
    if (kind == muplar::runtime::prefix::PrefixKind::Android &&
        runtime_sysroot.empty()) {
        if (!ensure_android_art_sysroot(runtime_sysroot))
            return 1;
    }

    try {
        auto prefix =
            root.empty()
                ? muplar::runtime::prefix::open_prefix(
                      spec, runtime_sysroot, true, kind, arch, runner, distro)
                : muplar::runtime::prefix::open_prefix_at_root(
                      spec, root, runtime_sysroot, true, kind, arch, runner,
                      distro);
        std::cout << "prefix: " << prefix.root << "\n";
        std::cout << "kind: " << prefix_kind_string(prefix) << "\n";
        if (!prefix.distro.empty())
            std::cout << "distro: " << prefix.distro << "\n";
        std::cout << "arch: " << muplar::runtime::prefix::to_string(prefix.arch)
                  << "\n";
        std::cout << "runner: " << prefix.runner << "\n";
        std::cout << "rootfs: " << prefix.rootfs << "\n";
        if (!prefix.runtime_sysroot.empty())
            std::cout << "runtime sysroot: "
                      << runtime_sysroot_display_string(prefix) << "\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Prefix error: " << e.what() << "\n";
        return 1;
    }
}

// Blocks preemption/doorbell signals and starts elfuse's dedicated consumer
// thread for them, right before actually launching a guest -- not at process
// bring-up. run_script()'s fork()+execv() helper also runs on non-guest
// subcommands (e.g. `prefix create --kind android`'s sysroot setup script);
// forking after this thread exists risks the classic post-fork malloc
// deadlock, since the fork child allocates (std::vector, std::string) before
// exec while the vanished thread could have been holding an allocator lock.
// Idempotent -- elfuse guards it with a plain bool -- so it's safe to call
// from every guest-launch site below rather than once at the top of main().
static int run_platform_runtime(
    const muplar::runtime::PlatformLaunchConfig &cfg_in)
{
    namespace prefix = muplar::runtime::prefix;

    muplar::runtime::PlatformLaunchConfig cfg = cfg_in;
    ensure_preempt_or_disable_timeout(cfg);

    proc_set_fakeroot_enabled(cfg.fakeroot);

    if (!cfg.active_prefix) {
        muplar::runtime::android::AndroidAarch64Runtime runtime;
        return runtime.run(cfg);
    }

    const auto &layout = *cfg.active_prefix;
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
        std::cerr << "Muplar Windows Compatibility support was not built into "
                     "this binary.\n"
                  << "Rebuild with Windows compatibility enabled.\n";
        return 1;
#endif
    }

    std::cerr << "Unsupported prefix kind: " << prefix::to_string(layout.kind)
              << "\n";
    return 1;
}

// ==========================================================================
// instance sub-command: start / stop / status / list
// ==========================================================================

static int handle_instance_command(int argc, char **argv)
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
        for (const auto &p : prefixes) {
            name_w = std::max(name_w, p.name.size());
            kind_w = std::max(kind_w, prefix::to_string(p.kind).size());
            arch_w = std::max(arch_w, prefix::to_string(p.arch).size());
        }
        std::cout << std::left << std::setw(static_cast<int>(name_w + 2))
                  << "Name" << std::setw(static_cast<int>(kind_w + 2)) << "Kind"
                  << std::setw(static_cast<int>(arch_w + 2)) << "Arch"
                  << std::setw(static_cast<int>(state_w + 2)) << "State"
                  << "Root\n";
        std::cout << std::string(name_w, '-') << "  "
                  << std::string(kind_w, '-') << "  "
                  << std::string(arch_w, '-') << "  "
                  << std::string(state_w, '-') << "  ----\n";
        for (const auto &p : prefixes) {
            std::cout << std::left << std::setw(static_cast<int>(name_w + 2))
                      << p.name << std::setw(static_cast<int>(kind_w + 2))
                      << prefix::to_string(p.kind)
                      << std::setw(static_cast<int>(arch_w + 2))
                      << prefix::to_string(p.arch)
                      << std::setw(static_cast<int>(state_w + 2))
                      << prefix_state_string(p) << p.root.string() << "\n";
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
            auto state = prefix::query_prefix_state(layout);
            pid_t pid = prefix::read_prefix_pid(layout);
            std::cout << "Name:  " << layout.name << "\n";
            std::cout << "Kind:  " << prefix_kind_display_string(layout.kind)
                      << "\n";
            std::cout << "Arch:  " << prefix::to_string(layout.arch) << "\n";
            std::cout << "State: "
                      << (state == prefix::PrefixState::Running ? "running"
                                                                : "stopped")
                      << "\n";
            if (state == prefix::PrefixState::Running && pid > 0)
                std::cout << "PID:   " << pid << "\n";
            return 0;
        } catch (const std::exception &e) {
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
            auto state = prefix::query_prefix_state(layout);
            if (state != prefix::PrefixState::Running) {
                std::cerr << "Instance '" << layout.name
                          << "' is not running.\n";
                return 1;
            }
            pid_t pid = prefix::read_prefix_pid(layout);
            if (pid <= 0) {
                std::cerr << "Cannot read PID for instance '" << layout.name
                          << "'.\n";
                return 1;
            }
            int sig = force ? SIGKILL : SIGTERM;
            std::cerr << "[instance] sending "
                      << (force ? "SIGKILL" : "SIGTERM") << " to pid=" << pid
                      << "\n";
            if (kill(pid, sig) != 0) {
                std::cerr << "kill() failed: " << std::strerror(errno) << "\n";
                return 1;
            }
            // Wait up to 5 seconds for graceful exit (SIGTERM).
            if (!force) {
                for (int i = 0; i < 50; ++i) {
                    usleep(100000);  // 100 ms
                    if (prefix::query_prefix_state(layout) ==
                        prefix::PrefixState::Stopped)
                        break;
                }
                // If still running, escalate.
                if (prefix::query_prefix_state(layout) ==
                    prefix::PrefixState::Running) {
                    std::cerr << "[instance] still running; sending SIGKILL\n";
                    kill(pid, SIGKILL);
                    usleep(500000);  // 0.5 s
                }
            }
            // Remove PID file.
            std::error_code ec;
            std::filesystem::remove(prefix::pid_file_path(layout), ec);
            std::cout << "stopped: " << layout.name << " (pid=" << pid << ")\n";
            return 0;
        } catch (const std::exception &e) {
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
        bool daemon_mode = false;
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
            } else if ((flag == "--windows-runtime-bin" ||
                        flag == "--wine-bin") &&
                       i + 1 < argc) {
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

            if (prefix::query_prefix_state(layout) ==
                prefix::PrefixState::Running) {
                pid_t existing = prefix::read_prefix_pid(layout);
                std::cerr << "instance start: prefix '" << layout.name
                          << "' is already running (pid=" << existing << ").\n"
                          << "Use 'mup instance stop " << layout.name
                          << "' first.\n";
                return 1;
            }

            muplar::runtime::PlatformLaunchConfig cfg;
            cfg.input_path = program_path;
            cfg.guest_args = program_args;
            if (program_path == "sudo" || program_path == "/usr/bin/sudo" ||
                program_path == "/usr/local/bin/sudo" ||
                program_path == "/bin/sudo") {
                cfg.fakeroot = true;
            }
            cfg.verbose = true;
            cfg.timeout_sec = 0;
            cfg.active_prefix = layout;
            if (!layout.runtime_sysroot.empty())
                cfg.sysroot = layout.runtime_sysroot.string();
            else if (layout.kind == prefix::PrefixKind::Android ||
                     layout.kind == prefix::PrefixKind::Linux)
                cfg.sysroot = layout.rootfs.string();

#ifdef MUPLAR_HAS_WINE
            muplar::runtime::wine::WineLaunchOptions opts;
            opts.wine_bin = wine_bin;
            opts.daemon = daemon_mode;

            if (layout.kind == prefix::PrefixKind::Wine) {
                ensure_preempt_or_disable_timeout(cfg);
                muplar::runtime::wine::WineRuntime runtime(opts);
                return runtime.run(cfg);
            }
#else
            if (layout.kind == prefix::PrefixKind::Wine) {
                std::cerr << "instance start: Muplar Windows Compatibility "
                             "support was not built into this binary.\n"
                          << "Rebuild with Windows compatibility enabled.\n";
                return 1;
            }
#endif

            if (!wine_bin.empty()) {
                std::cerr << "instance start: --windows-runtime-bin is only "
                             "valid for Windows instances.\n";
                return 2;
            }
            if (daemon_mode) {
                std::cerr << "instance start: --daemon is only valid for "
                             "Windows instances for now.\n";
                return 2;
            }

            return run_platform_runtime(cfg);
        } catch (const std::exception &e) {
            std::cerr << "instance start error: " << e.what() << "\n";
            return 1;
        }
    }

    std::cerr << "Unknown instance command: " << sub << "\n";
    print_usage(argv[0]);
    return 2;
}

// A guest fork() is emulated by re-executing this binary as --fork-child, wired
// back to the process that forked it over an IPC socket. That socket is the
// child's only reason to exist, but nothing ties its lifetime to the parent's:
// when the parent goes away without an orderly teardown -- a crash, or a KILL
// aimed at the session -- the child is reparented to launchd and keeps running
// its vCPU loop at full tilt with no peer left to serve. Observed in practice
// as several orphans pinning a core each, indefinitely.
//
// Watch the parent and follow it down. Darwin has no PR_SET_PDEATHSIG, so use
// kqueue's process filter, which delivers NOTE_EXIT without polling.
static void exit_when_parent_dies()
{
#ifdef __APPLE__
    const pid_t parent = getppid();
    // Already orphaned before we got here: launchd (1) is never a real parent
    // for a fork-child, so there is nobody left to serve.
    if (parent <= 1)
        _exit(0);

    std::thread([parent]() {
        const int kq = kqueue();
        if (kq < 0)
            return;

        struct kevent change;
        EV_SET(&change, parent, EVFILT_PROC, EV_ADD | EV_ENABLE, NOTE_EXIT, 0,
               nullptr);
        if (kevent(kq, &change, 1, nullptr, 0, nullptr) < 0) {
            const int err = errno;
            close(kq);
            // ESRCH means the parent exited between getppid() and the
            // registration above, so the death we are waiting for already
            // happened.
            if (err == ESRCH)
                _exit(0);
            return;
        }

        struct kevent event;
        if (kevent(kq, nullptr, 0, &event, 1, nullptr) > 0)
            _exit(0);
        close(kq);
    }).detach();
#endif
}

int main(int argc, char **argv)
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
            } catch (...) {
            }
        } else if (arg == "--vfork-notify-fd" && i + 1 < argc) {
            try {
                vfork_notify_fd = std::stoi(argv[i + 1]);
            } catch (...) {
            }
        } else if (arg == "--verbose" || arg == "-v") {
            verbose = true;
        }
    }

    if (fork_child_fd >= 0) {
        exit_when_parent_dies();
        return fork_child_main(fork_child_fd, vfork_notify_fd, verbose,
                               timeout_sec);
    }

    if (argc >= 2 && std::string(argv[1]) == "linux-session-exec")
        return handle_linux_session_exec(argc, argv);

    const char *app_process_group = std::getenv("MUPLAR_APP_PROCESS_GROUP");
    if (app_process_group && app_process_group[0] == '1')
        setpgid(0, 0);

    // Increase soft limit for open files descriptor limit from 256 to maximum
    // allowed
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
             flag == "--host-window-ms" || flag == "--android-runtime" ||
             flag == "--jni-int" || flag == "--jni-arg") &&
            i + 1 < argc) {
            ++i;
        } else if (flag == "--jni-call" && i + 3 < argc) {
            i += 3;
        }
    }

    // Every mup subcommand (not just guest launches) can now log through
    // elfuse's leveled logger, so the level needs to be set here rather
    // than only inside GuestRunner::run() -- otherwise commands like
    // `prefix create` would run at elfuse's own library default (WARN),
    // silently hiding their own log_info progress output.
    log_init();
    if (verbose)
        log_set_level(LOG_DEBUG);
    else if (quiet_requested)
        log_set_level(LOG_WARN);
    else
        log_set_level(LOG_INFO);

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
        } else if ((flag == "--timeout-sec") && arg_start + 1 < argc) {
            launch_cfg.timeout_sec = std::atoi(argv[arg_start + 1]);
            arg_start += 2;
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
        } else if ((flag == "--android-runtime") && arg_start + 1 < argc) {
            std::cerr << "--android-runtime was removed. Android Java APKs now "
                         "run through guest ART/app_process64 only.\n";
            return 1;
        } else if ((flag == "--sysroot") && arg_start + 1 < argc) {
            launch_cfg.sysroot = argv[arg_start + 1];
            arg_start += 2;
        } else if ((flag == "--prefix") && arg_start + 1 < argc) {
            prefix_spec = argv[arg_start + 1];
            arg_start += 2;
        } else if (flag == "--native-activity") {
            launch_cfg.native_activity = true;
            ++arg_start;
        } else if (flag == "--strict-direct-imports") {
            launch_cfg.strict_direct_imports = true;
            ++arg_start;
        } else if (flag == "--fakeroot") {
            launch_cfg.fakeroot = true;
            ++arg_start;
        } else if (flag == "--host-window") {
            launch_cfg.host_window = true;
            ++arg_start;
        } else if ((flag == "--host-window-ms") && arg_start + 1 < argc) {
            launch_cfg.host_window = true;
            try {
                launch_cfg.host_window_linger_ms =
                    std::stoi(argv[arg_start + 1], nullptr, 0);
            } catch (const std::exception &) {
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
            } catch (const std::exception &) {
                std::cerr << "Invalid --jni-int value: " << argv[arg_start + 1]
                          << "\n";
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

    if (launch_cfg.sysroot.empty()) {
        if (std::filesystem::exists("build/sysroot/system/bin/app_process64")) {
            launch_cfg.sysroot = "build/sysroot";
        }
    }

    if (launch_cfg.input_path == "sudo" ||
        launch_cfg.input_path == "/usr/bin/sudo" ||
        launch_cfg.input_path == "/usr/local/bin/sudo" ||
        launch_cfg.input_path == "/bin/sudo") {
        launch_cfg.fakeroot = true;
    }

    if (prefix_spec) {
        try {
            active_prefix = muplar::runtime::prefix::open_prefix(
                *prefix_spec, launch_cfg.sysroot, true);
            if (launch_cfg.sysroot.empty()) {
                if (!active_prefix->runtime_sysroot.empty() &&
                    std::filesystem::exists(active_prefix->runtime_sysroot)) {
                    launch_cfg.sysroot =
                        active_prefix->runtime_sysroot.string();
                } else if (active_prefix->kind ==
                               muplar::runtime::prefix::PrefixKind::Android ||
                           active_prefix->kind ==
                               muplar::runtime::prefix::PrefixKind::Linux) {
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
                          << " kind=" << prefix_kind << " arch=" << guest_arch
                          << " runner=" << active_prefix->runner
                          << " root=" << active_prefix->root.string()
                          << " rootfs=" << active_prefix->rootfs.string()
                          << "\n";
                if (!active_prefix->runtime_sysroot.empty()) {
                    if (active_prefix->kind ==
                        muplar::runtime::prefix::PrefixKind::Wine) {
                        std::cerr << "[Prefix] Windows compatibility "
                                     "runtime=configured\n";
                    } else {
                        std::cerr << "[Prefix] runtime sysroot="
                                  << active_prefix->runtime_sysroot.string()
                                  << "\n";
                    }
                }
            }
        } catch (const std::exception &e) {
            std::cerr << "Prefix error: " << e.what() << "\n";
            return 1;
        }
    }

    for (int i = arg_start + 1; i < argc; ++i) {
        launch_cfg.guest_args.push_back(argv[i]);
    }

    if (std::getenv("MUP_VERBOSE")) {
        launch_cfg.verbose = true;
    }

    try {
        return run_platform_runtime(launch_cfg);
    } catch (const std::exception &e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return 1;
    }
}
