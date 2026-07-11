#include "android_aarch64_runtime.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include "apk_envelope.h"
#include "art_bootstrap.h"
#include "elf_loader.h"
#include "guest_runner.h"

namespace muplar::runtime::android
{
namespace
{
constexpr uintmax_t kMaxLogFileBytes = 5ULL * 1024ULL * 1024ULL;
constexpr int kMaxLogHistoryFiles = 5;

void rotate_log_file_if_needed(const std::filesystem::path &log_file)
{
    std::error_code ec;
    if (std::filesystem::file_size(log_file, ec) < kMaxLogFileBytes || ec)
        return;

    std::filesystem::remove(
        log_file.string() + "." + std::to_string(kMaxLogHistoryFiles), ec);
    for (int index = kMaxLogHistoryFiles - 1; index >= 1; --index) {
        std::filesystem::path from =
            log_file.string() + "." + std::to_string(index);
        std::filesystem::path to =
            log_file.string() + "." + std::to_string(index + 1);
        ec.clear();
        if (std::filesystem::exists(from, ec))
            std::filesystem::rename(from, to, ec);
    }

    ec.clear();
    if (std::filesystem::exists(log_file, ec))
        std::filesystem::rename(log_file, log_file.string() + ".1", ec);
}

class PrefixPidRegistration
{
public:
    explicit PrefixPidRegistration(
        const std::optional<prefix::PrefixLayout> &active_prefix)
    {
        if (std::getenv("MUPLAR_DISPATCHED_APP") != nullptr || !active_prefix ||
            active_prefix->kind == prefix::PrefixKind::Wine) {
            return;
        }

        layout_ = active_prefix;
        pid_ = getpid();

        std::filesystem::path run_dir = layout_->root / "run";
        std::error_code ec;
        std::filesystem::create_directories(run_dir, ec);
        if (ec) {
            std::cerr << "[Prefix] warning: cannot create run directory "
                      << run_dir << ": " << ec.message() << "\n";
            layout_.reset();
            return;
        }

        std::ofstream out(prefix::pid_file_path(*layout_), std::ios::trunc);
        if (!out) {
            std::cerr << "[Prefix] warning: cannot write PID file "
                      << prefix::pid_file_path(*layout_) << "\n";
            layout_.reset();
            return;
        }
        out << pid_ << "\n";
    }

    ~PrefixPidRegistration()
    {
        if (!layout_)
            return;
        if (prefix::read_prefix_pid(*layout_) != pid_)
            return;

        std::error_code ec;
        std::filesystem::remove(prefix::pid_file_path(*layout_), ec);
    }

    PrefixPidRegistration(const PrefixPidRegistration &) = delete;
    PrefixPidRegistration &operator=(const PrefixPidRegistration &) = delete;

private:
    std::optional<prefix::PrefixLayout> layout_;
    pid_t pid_ = 0;
};

std::string lower_ext(const std::string &path)
{
    std::string ext = std::filesystem::path(path).extension().string();
    for (char &c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

std::filesystem::path current_executable_path()
{
#ifdef __APPLE__
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size + 1, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
        std::error_code ec;
        std::filesystem::path canonical =
            std::filesystem::weakly_canonical(buffer.data(), ec);
        return ec ? std::filesystem::path(buffer.data()) : canonical;
    }
#endif
    return {};
}

bool unix_socket_is_live(const std::filesystem::path &path)
{
    if (path.string().size() >=
        sizeof(static_cast<sockaddr_un *>(nullptr)->sun_path))
        return false;
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return false;
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1);
    bool live = connect(fd, reinterpret_cast<sockaddr *>(&address),
                        sizeof(address)) == 0;
    close(fd);
    return live;
}

std::filesystem::path ensure_muplard(const prefix::PrefixLayout &active_prefix)
{
    std::filesystem::path run_dir = active_prefix.root / "run";
    std::filesystem::path socket_path = run_dir / "muplard.sock";
    if (unix_socket_is_live(socket_path))
        return socket_path;

    std::filesystem::path daemon =
        current_executable_path().parent_path() / "muplard";
    if (!std::filesystem::is_regular_file(daemon)) {
        std::cerr << "[muplard] warning: daemon binary not found at " << daemon
                  << "\n";
        return {};
    }
    std::error_code ec;
    std::filesystem::create_directories(run_dir, ec);
    std::filesystem::create_directories(active_prefix.logs_dir, ec);
    std::filesystem::path registry =
        active_prefix.registry_dir / "android-packages.properties";
    std::filesystem::path pid_file = run_dir / "muplard.pid";
    std::filesystem::path settings_file =
        active_prefix.registry_dir / "android-state" / "framework-settings.db";
    std::filesystem::path log_file = active_prefix.logs_dir / "muplard.log";
    rotate_log_file_if_needed(log_file);

    pid_t child = fork();
    if (child == 0) {
        setsid();
        int log_fd =
            open(log_file.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600);
        if (log_fd >= 0) {
            dup2(log_fd, STDOUT_FILENO);
            dup2(log_fd, STDERR_FILENO);
            if (log_fd > STDERR_FILENO)
                close(log_fd);
        }
        execl(daemon.c_str(), daemon.c_str(), "--socket", socket_path.c_str(),
              "--registry", registry.c_str(), "--pid-file", pid_file.c_str(),
              "--settings", settings_file.c_str(),
              static_cast<char *>(nullptr));
        _exit(127);
    }
    if (child < 0) {
        std::cerr << "[muplard] warning: fork failed\n";
        return {};
    }
    for (int attempt = 0; attempt < 40; ++attempt) {
        if (unix_socket_is_live(socket_path)) {
            std::cerr << "[muplard] service ready at " << socket_path << "\n";
            return socket_path;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    std::cerr << "[muplard] warning: daemon did not become ready\n";
    return {};
}

void copy_jni_call_config(const RuntimeJniCallConfig &from,
                          elf::JniCallConfig &to)
{
    to.enabled = from.enabled;
    to.class_name = from.class_name;
    to.method_name = from.method_name;
    to.signature = from.signature;
    to.int_args = from.int_args;
    to.receiver_explicit = from.receiver_explicit;
    to.receiver_static = from.receiver_static;
}

void ensure_linux_guest_x11_socket_dir(
    const prefix::PrefixLayout &active_prefix)
{
    if (active_prefix.kind != prefix::PrefixKind::Linux)
        return;

    std::error_code ec;
    std::filesystem::path x11_dir = active_prefix.rootfs / "tmp" / ".X11-unix";
    if (std::filesystem::is_symlink(x11_dir, ec))
        std::filesystem::remove(x11_dir, ec);
    if (!std::filesystem::exists(x11_dir, ec))
        std::filesystem::create_directories(x11_dir, ec);
}

std::vector<std::string> default_guest_environment(
    const prefix::PrefixLayout &active_prefix)
{
    switch (active_prefix.kind) {
    case prefix::PrefixKind::Android:
        return {
            "PATH=/system/bin:/system/xbin:/bin",
            "ANDROID_DATA=/data",
            "ANDROID_ROOT=/system",
            "HOME=/data/local/tmp",
            "LOGNAME=shell",
            "SHELL=/system/bin/sh",
            "TMPDIR=/data/local/tmp",
            "USER=shell",
            "TERM=xterm-256color",
        };
    default:
        return {
            "PATH=/bin:/usr/bin", "HOME=/home/muplar", "LOGNAME=muplar",
            "TMPDIR=/tmp",        "USER=muplar",       "TERM=xterm-256color",
        };
    }
}

std::filesystem::path default_host_cwd(
    const prefix::PrefixLayout &active_prefix)
{
    switch (active_prefix.kind) {
    case prefix::PrefixKind::Android:
        return active_prefix.rootfs / "data/local/tmp";
    default:
        return active_prefix.rootfs / "home/muplar";
    }
}

void inspect_elf(const std::string &elf_path)
{
    try {
        elf::ElfLoader loader;
        auto image = loader.load(elf_path);

        std::cout << "ELF entry   : 0x" << std::hex << image.entry << "\n"
                  << "Load range  : 0x" << image.load_min << " – 0x"
                  << image.load_max << "\n"
                  << "Segments    : " << std::dec << image.segments.size()
                  << "\n";
    } catch (const std::exception &e) {
        std::cerr << "ELF inspect error: " << e.what() << "\n";
    }
}

void apply_apk_launch(const PlatformLaunchConfig &launch_cfg,
                      elf::GuestRunnerConfig &guest_cfg)
{
    const auto &active_prefix = launch_cfg.active_prefix;
    if (active_prefix && active_prefix->kind != prefix::PrefixKind::Android) {
        throw std::runtime_error(
            "APK launch requires an android prefix; got kind=" +
            prefix::to_string(active_prefix->kind));
    }
    if (active_prefix && active_prefix->arch != prefix::GuestArch::Aarch64) {
        throw std::runtime_error(
            "Android launch requires an ARM64 prefix; got arch=" +
            prefix::to_string(active_prefix->arch));
    }

    apk::ApkLaunchConfig apk_cfg;
    apk_cfg.apk_path = launch_cfg.input_path;
    apk_cfg.lib_name = launch_cfg.apk_lib_name;
    apk_cfg.output_dir = launch_cfg.apk_extract_dir;
    if (active_prefix)
        apk_cfg.output_base_dir = active_prefix->apk_cache_dir;

    auto apk = apk::prepare_apk_launch(apk_cfg);
    guest_cfg.elf_path = apk.so_path.string();
    guest_cfg.native_activity = true;
    guest_cfg.force_android_so = true;
    guest_cfg.native_lib_search_dirs.push_back(
        (apk.extract_dir / "lib" / "arm64-v8a").string());
    guest_cfg.apk_assets_dir = apk.assets_dir.string();
    guest_cfg.package_code_path = apk.apk_path.string();
    if (apk.manifest_package)
        guest_cfg.package_name = *apk.manifest_package;

    std::cerr << "[APK] extracted " << apk.extracted_libs.size()
              << " arm64-v8a lib(s) to " << apk.extract_dir.string() << "\n";
    if (!apk.extracted_assets.empty()) {
        std::cerr << "[APK] extracted " << apk.extracted_assets.size()
                  << " asset(s) to " << apk.assets_dir.string() << "\n";
    }
    if (apk.manifest_lib)
        std::cerr << "[APK] manifest lib_name=" << *apk.manifest_lib << "\n";
    if (apk.manifest_package)
        std::cerr << "[APK] manifest package=" << *apk.manifest_package << "\n";
    std::cerr << "[APK] selected lib " << apk.selected_lib << ".so -> "
              << guest_cfg.elf_path << "\n";
}

int handle_guest_art_apk_launch(const PlatformLaunchConfig &launch_cfg,
                                const apk::ApkClassification &classification)
{
    if (launch_cfg.sysroot.empty()) {
        std::cerr << "APK error: Java/ART bootstrap incomplete: "
                     "--sysroot is required for production Android Java apps. "
                     "Import an Android ART root with "
                     "tools/import-android-art-sysroot.sh, then prepare "
                     "build/sysroot with tools/prepare-android-sysroot.sh "
                     "--strict-art.\n";
        return 1;
    }

    std::filesystem::path staged_apk =
        stage_art_apk_for_sysroot(launch_cfg.input_path, launch_cfg.sysroot);

    ArtBootstrapConfig art_cfg;
    art_cfg.apk_path = staged_apk;
    art_cfg.sysroot = launch_cfg.sysroot;
    art_cfg.apk_classification = classification;

    ArtBootstrapPlan plan = build_art_bootstrap_plan(art_cfg);
    print_art_bootstrap_plan(plan);

    if (!plan.ready()) {
        std::cerr << "APK error: Java/ART bootstrap incomplete: missing "
                  << art_bootstrap_missing_summary(plan) << "\n";
        return 1;
    }

    if (plan.argv.empty()) {
        std::cerr << "APK error: Java/ART bootstrap incomplete: missing "
                  << "app_process64 guest argv\n";
        return 1;
    }

    elf::GuestRunnerConfig guest_cfg;
    guest_cfg.elf_path = plan.app_process64.string();
    guest_cfg.guest_elf_path = plan.app_process64_guest_path;
    guest_cfg.argv = plan.argv;
    guest_cfg.env = plan.env;
    guest_cfg.sysroot = launch_cfg.sysroot;
    guest_cfg.verbose = launch_cfg.verbose;
    guest_cfg.timeout_sec = launch_cfg.timeout_sec;
    guest_cfg.host_window = launch_cfg.host_window;
    guest_cfg.host_window_linger_ms = launch_cfg.host_window_linger_ms;
    guest_cfg.real_art_vm = true;
    guest_cfg.package_code_path = plan.guest_apk_path;
    if (plan.package_name)
        guest_cfg.package_name = *plan.package_name;
    if (launch_cfg.active_prefix) {
        guest_cfg.service_socket = ensure_muplard(*launch_cfg.active_prefix);
        guest_cfg.host_cwd =
            default_host_cwd(*launch_cfg.active_prefix).string();
    }

    inspect_elf(guest_cfg.elf_path);
    std::cerr << "[ART] executing app_process64 bootstrap path\n";
    elf::GuestRunner runner;
    return runner.run(guest_cfg);
}

int handle_java_apk_launch(const PlatformLaunchConfig &launch_cfg,
                           const apk::ApkClassification &classification)
{
    (void) classification;
    std::cerr << "[ART] using guest ART/app_process64\n";
    return handle_guest_art_apk_launch(launch_cfg, classification);
}

}  // namespace

int AndroidAarch64Runtime::run(const PlatformLaunchConfig &config)
{
    PrefixPidRegistration pid_registration(config.active_prefix);

    elf::GuestRunnerConfig guest_cfg;
    guest_cfg.elf_path = config.input_path;
    guest_cfg.sysroot = config.sysroot;
    guest_cfg.is_android =
        !config.linux_guest &&
        (!config.active_prefix ||
         config.active_prefix->kind == prefix::PrefixKind::Android ||
         config.apk_mode || lower_ext(config.input_path) == ".apk");
    if (config.active_prefix) {
        ensure_linux_guest_x11_socket_dir(*config.active_prefix);
        guest_cfg.inherit_host_env = false;
        if (!config.guest_env.empty()) {
            guest_cfg.env = config.guest_env;
        } else {
            guest_cfg.env = default_guest_environment(*config.active_prefix);
        }
        guest_cfg.host_cwd = default_host_cwd(*config.active_prefix).string();
        if (config.active_prefix->kind == prefix::PrefixKind::Android)
            guest_cfg.service_socket =
                ensure_muplard(*config.active_prefix).string();
    }

    auto resolve_path = [&](const std::string &input_path,
                            std::string &resolved_elf_path,
                            std::string &resolved_guest_elf_path) {
        resolved_elf_path = input_path;
        resolved_guest_elf_path = "";

        if (!input_path.empty()) {
            if (input_path[0] == '/') {
                // Absolute path
                if (!guest_cfg.sysroot.empty()) {
                    std::filesystem::path host_candidate =
                        std::filesystem::path(guest_cfg.sysroot) /
                        input_path.substr(1);
                    std::error_code ec;
                    if (std::filesystem::exists(host_candidate, ec)) {
                        resolved_elf_path = host_candidate.string();
                        resolved_guest_elf_path = input_path;
                    }
                }
            } else if (input_path.find('/') == std::string::npos) {
                // Relative command name, look up in guest PATH
                std::vector<std::string> search_dirs;
                if (config.active_prefix) {
                    if (config.active_prefix->kind ==
                        prefix::PrefixKind::Android) {
                        search_dirs = {"/system/bin", "/system/xbin", "/bin"};
                    } else if (config.active_prefix->kind ==
                               prefix::PrefixKind::Linux) {
                        search_dirs = {"/bin", "/usr/bin", "/sbin",
                                       "/usr/sbin"};
                    } else if (config.active_prefix->kind ==
                               prefix::PrefixKind::Wine) {
                        search_dirs = {"/bin", "/usr/bin"};
                    }
                } else {
                    search_dirs = {"/bin", "/usr/bin", "/system/bin"};
                }

                for (const auto &dir : search_dirs) {
                    if (guest_cfg.sysroot.empty())
                        continue;
                    std::string rel_dir = (dir[0] == '/') ? dir.substr(1) : dir;
                    std::filesystem::path host_candidate =
                        std::filesystem::path(guest_cfg.sysroot) / rel_dir /
                        input_path;
                    std::error_code ec;
                    if (std::filesystem::exists(host_candidate, ec)) {
                        resolved_elf_path = host_candidate.string();
                        resolved_guest_elf_path =
                            (dir[0] == '/') ? (dir + "/" + input_path)
                                            : ("/" + dir + "/" + input_path);
                        break;
                    }
                }
            } else {
                // Relative path with directories (e.g., ./foo or bin/foo)
                std::filesystem::path base_dir =
                    std::filesystem::current_path();
                if (config.active_prefix) {
                    base_dir = default_host_cwd(*config.active_prefix);
                }
                std::filesystem::path host_candidate = base_dir / input_path;
                std::error_code ec;
                if (std::filesystem::exists(host_candidate, ec)) {
                    resolved_elf_path = host_candidate.string();
                    if (!guest_cfg.sysroot.empty() &&
                        resolved_elf_path.rfind(guest_cfg.sysroot, 0) == 0) {
                        resolved_guest_elf_path = resolved_elf_path.substr(
                            guest_cfg.sysroot.length());
                        if (resolved_guest_elf_path.empty() ||
                            resolved_guest_elf_path[0] != '/') {
                            resolved_guest_elf_path =
                                "/" + resolved_guest_elf_path;
                        }
                    }
                }
            }
        }
    };

    std::string resolved_elf_path;
    std::string resolved_guest_elf_path;
    resolve_path(config.input_path, resolved_elf_path, resolved_guest_elf_path);

    if (!resolved_guest_elf_path.empty()) {
        guest_cfg.elf_path = resolved_elf_path;
        guest_cfg.guest_elf_path = resolved_guest_elf_path;
    } else {
        guest_cfg.elf_path = resolved_elf_path;
        if (guest_cfg.guest_elf_path.empty() && !guest_cfg.sysroot.empty() &&
            guest_cfg.elf_path.rfind(guest_cfg.sysroot, 0) == 0) {
            guest_cfg.guest_elf_path =
                guest_cfg.elf_path.substr(guest_cfg.sysroot.length());
            if (guest_cfg.guest_elf_path.empty() ||
                guest_cfg.guest_elf_path[0] != '/') {
                guest_cfg.guest_elf_path = "/" + guest_cfg.guest_elf_path;
            }
        }
    }

    std::vector<std::string> current_guest_args = config.guest_args;
    int loop_count = 0;
    while (loop_count < 5) {
        std::ifstream f(guest_cfg.elf_path, std::ios::binary);
        if (!f.is_open()) {
            break;
        }
        char magic[4];
        if (f.read(magic, 4) && magic[0] == 0x7f && magic[1] == 'E' &&
            magic[2] == 'L' && magic[3] == 'F') {
            break;  // ELF file
        }
        f.seekg(0);
        char shebang_buf[256];
        f.read(shebang_buf, sizeof(shebang_buf) - 1);
        std::streamsize bytes_read = f.gcount();
        shebang_buf[bytes_read] = '\0';
        f.close();

        if (bytes_read < 2 || shebang_buf[0] != '#' || shebang_buf[1] != '!') {
            break;  // Not a shebang script
        }

        std::string line(shebang_buf + 2);
        size_t eol = line.find('\n');
        if (eol != std::string::npos) {
            line = line.substr(0, eol);
        }

        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) {
            break;
        }
        line = line.substr(start);

        size_t end = line.find_last_not_of(" \t\r");
        if (end != std::string::npos) {
            line = line.substr(0, end + 1);
        }

        if (line.empty()) {
            break;
        }

        std::string interp;
        std::string interp_arg;
        size_t space = line.find_first_of(" \t");
        if (space != std::string::npos) {
            interp = line.substr(0, space);
            std::string remaining = line.substr(space + 1);
            size_t arg_start = remaining.find_first_not_of(" \t");
            if (arg_start != std::string::npos) {
                interp_arg = remaining.substr(arg_start);
            }
        } else {
            interp = line;
        }

        std::string script_guest_path = guest_cfg.guest_elf_path.empty()
                                            ? guest_cfg.elf_path
                                            : guest_cfg.guest_elf_path;

        current_guest_args.insert(current_guest_args.begin(),
                                  script_guest_path);
        if (!interp_arg.empty()) {
            current_guest_args.insert(current_guest_args.begin(), interp_arg);
        }

        std::string resolved_interp_path;
        std::string resolved_guest_interp_path;
        resolve_path(interp, resolved_interp_path, resolved_guest_interp_path);

        if (!resolved_guest_interp_path.empty()) {
            guest_cfg.elf_path = resolved_interp_path;
            guest_cfg.guest_elf_path = resolved_guest_interp_path;
        } else {
            guest_cfg.elf_path = resolved_interp_path;
            if (!guest_cfg.sysroot.empty() &&
                guest_cfg.elf_path.rfind(guest_cfg.sysroot, 0) == 0) {
                guest_cfg.guest_elf_path =
                    guest_cfg.elf_path.substr(guest_cfg.sysroot.length());
                if (guest_cfg.guest_elf_path.empty() ||
                    guest_cfg.guest_elf_path[0] != '/') {
                    guest_cfg.guest_elf_path = "/" + guest_cfg.guest_elf_path;
                }
            } else {
                guest_cfg.guest_elf_path = "";
            }
        }

        loop_count++;
    }

    guest_cfg.verbose = config.verbose;
    guest_cfg.quiet = config.quiet;
    guest_cfg.timeout_sec = config.timeout_sec;
    guest_cfg.native_activity = config.native_activity;
    guest_cfg.strict_direct_imports = config.strict_direct_imports;
    guest_cfg.host_window = config.host_window;
    guest_cfg.host_window_linger_ms = config.host_window_linger_ms;
    copy_jni_call_config(config.jni_call, guest_cfg.jni_call);

    if (config.apk_mode || lower_ext(config.input_path) == ".apk") {
        try {
            apk::ApkClassification classification =
                apk::classify_apk(config.input_path);
            std::cerr << "[APK] runtime kind="
                      << apk::to_string(classification.runtime_kind) << "\n";
            if (!classification.dex_files.empty()) {
                std::cerr << "[APK] dex files="
                          << classification.dex_files.size() << "\n";
            }
            if (classification.runtime_kind == apk::ApkRuntimeKind::JavaOnly)
                return handle_java_apk_launch(config, classification);

            apply_apk_launch(config, guest_cfg);
        } catch (const std::exception &e) {
            std::cerr << "APK error: " << e.what() << "\n";
            return 1;
        }
    }

    guest_cfg.argv.push_back(guest_cfg.guest_elf_path.empty()
                                 ? guest_cfg.elf_path
                                 : guest_cfg.guest_elf_path);
    for (const auto &arg : current_guest_args)
        guest_cfg.argv.push_back(arg);

    if (!config.quiet)
        inspect_elf(guest_cfg.elf_path);

    elf::GuestRunner runner;
    return runner.run(guest_cfg);
}

}  // namespace muplar::runtime::android
