#include "linux_aarch64_runtime.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <unistd.h>

#include "elf_loader.h"
#include "guest_runner.h"
#include "prefix.h"

namespace muplar::runtime::linux_aarch64
{
namespace
{

class PrefixPidRegistration
{
public:
    explicit PrefixPidRegistration(
        const std::optional<prefix::PrefixLayout> &active_prefix)
    {
        if (!active_prefix)
            return;

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

void ensure_linux_guest_x11_socket_dir(
    const prefix::PrefixLayout &active_prefix)
{
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
    return {
        "PATH=/bin:/usr/bin:/sbin:/usr/sbin",
        "HOME=/home/muplar",
        "LOGNAME=muplar",
        "TMPDIR=/tmp",
        "USER=muplar",
        "TERM=xterm-256color",
    };
}

std::filesystem::path default_host_cwd(
    const prefix::PrefixLayout &active_prefix)
{
    return active_prefix.rootfs / "home" / "muplar";
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

}  // namespace

int LinuxAarch64Runtime::run(const PlatformLaunchConfig &config)
{
    if (config.active_prefix) {
        const auto &layout = *config.active_prefix;
        if (layout.kind != prefix::PrefixKind::Linux) {
            throw std::runtime_error(
                "Linux ARM64 runtime requires a linux prefix; got kind=" +
                prefix::to_string(layout.kind));
        }
        if (layout.arch != prefix::GuestArch::Aarch64) {
            throw std::runtime_error(
                "Linux ARM64 runtime requires ARM64 arch; got arch=" +
                prefix::to_string(layout.arch));
        }
    }

    PrefixPidRegistration pid_registration(config.active_prefix);

    elf::GuestRunnerConfig guest_cfg;
    guest_cfg.elf_path = config.input_path;
    guest_cfg.sysroot = config.sysroot;
    guest_cfg.is_android = false;  // Pure Linux execution

    if (config.active_prefix) {
        ensure_linux_guest_x11_socket_dir(*config.active_prefix);
        guest_cfg.inherit_host_env = false;
        if (!config.guest_env.empty()) {
            guest_cfg.env = config.guest_env;
        } else {
            guest_cfg.env =
                prefix::default_linux_guest_environment(*config.active_prefix);
        }
        guest_cfg.host_cwd = default_host_cwd(*config.active_prefix).string();
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
                std::vector<std::string> search_dirs = {"/bin", "/usr/bin",
                                                        "/sbin", "/usr/sbin"};

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
    guest_cfg.native_activity = false;
    guest_cfg.strict_direct_imports = config.strict_direct_imports;
    guest_cfg.host_window = config.host_window;
    guest_cfg.host_window_linger_ms = config.host_window_linger_ms;

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

}  // namespace muplar::runtime::linux_aarch64
