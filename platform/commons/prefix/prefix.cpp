#include "prefix.h"
#include "distro_profile.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>

namespace muplar::runtime::prefix {
namespace {

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

static std::filesystem::path get_executable_path()
{
#if defined(__APPLE__)
    char path[1024];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) == 0) {
        std::error_code ec;
        return std::filesystem::canonical(path, ec).parent_path();
    }
#endif
    return std::filesystem::current_path();
}

static void ensure_wine_tmp_dir_private()
{
    std::filesystem::path sock_dir =
        std::filesystem::path("/tmp") /
        (".wine-" + std::to_string(static_cast<unsigned long>(getuid())));
    std::error_code ec;
    std::filesystem::create_directories(sock_dir, ec);
    if (ec) {
        std::fprintf(stderr, "[Muplar Windows Compatibility] failed to create %s: %s\n",
                     sock_dir.c_str(), ec.message().c_str());
        return;
    }
    if (chmod(sock_dir.c_str(), 0700) != 0) {
        std::fprintf(stderr, "[Muplar Windows Compatibility] chmod 0700 %s failed: %s\n",
                     sock_dir.c_str(), std::strerror(errno));
    }
}

static bool is_case_insensitive_directory(const std::filesystem::path& dir)
{
    std::error_code ec;
    auto probe_dir = dir / (".muplar-case-probe-" + std::to_string(static_cast<unsigned long>(getpid())));
    std::filesystem::create_directories(probe_dir, ec);
    if (ec)
        return false;

    auto lower = probe_dir / "a";
    auto upper = probe_dir / "A";
    {
        std::ofstream lower_out(lower);
        std::ofstream upper_out(upper);
    }

    bool insensitive = false;
    if (std::filesystem::exists(lower, ec) && std::filesystem::exists(upper, ec)) {
        ec.clear();
        insensitive = std::filesystem::equivalent(lower, upper, ec) && !ec;
    }

    std::filesystem::remove_all(probe_dir, ec);
    return insensitive;
}

static std::filesystem::path find_guest_sh()
{
    auto exec_dir = get_executable_path();

    // Check if guest_sh is in the same directory (CLI)
    auto candidate = exec_dir / "guest_sh";
    std::error_code ec;
    if (std::filesystem::exists(candidate, ec)) {
        return candidate;
    }

    // Check if it's next to the app bundle (MacOS app -> Contents/MacOS -> Contents -> App -> parent)
    auto parent_dir = exec_dir.parent_path().parent_path().parent_path();
    candidate = parent_dir / "guest_sh";
    if (std::filesystem::exists(candidate, ec)) {
        return candidate;
    }

    // Check if it's inside the bundle frameworks (MacOS app bundle -> Contents/Frameworks/guest_sh)
    candidate = exec_dir.parent_path().parent_path() / "Frameworks" / "guest_sh";
    if (std::filesystem::exists(candidate, ec)) {
        return candidate;
    }

    return {};
}


static bool auto_download_bootstrap(const std::string& distro, GuestArch arch, const std::filesystem::path& dest)
{
    std::string normalized_distro = distro;
    std::transform(normalized_distro.begin(), normalized_distro.end(), normalized_distro.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    if (normalized_distro.empty()) normalized_distro = "ubuntu";

    std::string arch_str = (arch == GuestArch::Aarch64) ? "aarch64" : "x86_64";
    std::string ubuntu_arch = (arch == GuestArch::Aarch64) ? "arm64" : "amd64";

    std::error_code ec;
    std::filesystem::create_directories(dest.parent_path(), ec);

    std::fprintf(stderr, "[linux] Auto-downloading '%s' bootstrap for %s...\n", normalized_distro.c_str(), arch_str.c_str());

    std::string url;
    if (normalized_distro == "ubuntu") {
        std::string py_cmd = "python3 -c 'import urllib.request, re, sys; "
                             "arch = \"" + ubuntu_arch + "\"; dest = \"" + dest.string() + "\"; "
                             "try:\n"
                             "    html = urllib.request.urlopen(\"https://cdimage.ubuntu.com/ubuntu-base/releases/26.04/release/\").read().decode(\"utf-8\")\n"
                             "    match = re.findall(rf\"ubuntu-base-26\\.04(?:\\.(\\d+))?-base-{arch}\\.tar\\.gz\", html)\n"
                             "    if match:\n"
                             "        version = max(match)\n"
                             "        filename = f\"ubuntu-base-26.04.{version}-base-{arch}.tar.gz\" if version else f\"ubuntu-base-26.04-base-{arch}.tar.gz\"\n"
                             "        url = f\"https://cdimage.ubuntu.com/ubuntu-base/releases/26.04/release/{filename}\"\n"
                             "        print(f\"Downloading {url}...\")\n"
                             "        urllib.request.urlretrieve(url, dest)\n"
                             "        sys.exit(0)\n"
                             "except Exception as e:\n"
                             "    print(f\"Python download failed: {e}\")\n"
                             "sys.exit(1)'";
        int rc = std::system(py_cmd.c_str());
        if (rc == 0) return true;
        
        url = "https://cdimage.ubuntu.com/ubuntu-base/releases/26.04/release/ubuntu-base-26.04-base-" + ubuntu_arch + ".tar.gz";
    } else if (normalized_distro == "alpine") {
        std::string py_cmd = "python3 -c 'import urllib.request, re, sys; "
                             "arch = \"" + arch_str + "\"; dest = \"" + dest.string() + "\"; "
                             "try:\n"
                             "    url_idx = f\"https://dl-cdn.alpinelinux.org/alpine/latest-stable/releases/{arch}/latest-releases.yaml\"\n"
                             "    text = urllib.request.urlopen(url_idx).read().decode(\"utf-8\")\n"
                             "    filename, branch = None, None\n"
                             "    for line in text.splitlines():\n"
                             "        if \"file: alpine-minirootfs-\" in line:\n"
                             "            filename = line.split(\":\")[1].strip().strip(\"\\\"\")\n"
                             "        if \"branch:\" in line:\n"
                             "            branch = line.split(\":\")[1].strip().strip(\"\\\"\")\n"
                             "        if filename and branch:\n"
                             "            break\n"
                             "    if filename and branch:\n"
                             "        url = f\"https://dl-cdn.alpinelinux.org/alpine/{branch}/releases/{arch}/{filename}\"\n"
                             "        print(f\"Downloading {url}...\")\n"
                             "        urllib.request.urlretrieve(url, dest)\n"
                             "        sys.exit(0)\n"
                             "except Exception as e:\n"
                             "    print(f\"Python download failed: {e}\")\n"
                             "sys.exit(1)'";
        int rc = std::system(py_cmd.c_str());
        if (rc == 0) return true;

        url = "https://dl-cdn.alpinelinux.org/alpine/v3.20/releases/" + arch_str + "/alpine-minirootfs-3.20.3-" + arch_str + ".tar.gz";
    } else if (normalized_distro == "debian") {
        std::string debian_branch = (arch == GuestArch::Aarch64) ? "dist-arm64v8" : "dist-amd64";
        url = "https://github.com/debuerreotype/docker-debian-artifacts/raw/" + debian_branch + "/bookworm/oci/blobs/rootfs.tar.gz";
    } else if (normalized_distro == "fedora") {
        url = "https://dl.fedoraproject.org/pub/fedora/linux/releases/40/Container/" + arch_str + "/images/Fedora-Container-Base-Generic-40-1.14." + arch_str + ".tar.xz";
    } else if (normalized_distro == "arch") {
        url = (arch == GuestArch::Aarch64)
            ? "http://archlinuxarm.org/os/ArchLinuxARM-aarch64-latest.tar.gz"
            : "https://geo.mirror.pkgbuild.com/iso/latest/archlinux-bootstrap-x86_64.tar.zst";
    } else if (normalized_distro == "opensuse") {
        url = (arch == GuestArch::Aarch64)
            ? "https://download.opensuse.org/ports/aarch64/tumbleweed/appliances/opensuse-tumbleweed-image.aarch64-lxc.tar.xz"
            : "https://download.opensuse.org/tumbleweed/appliances/opensuse-tumbleweed-image.x86_64-lxc.tar.xz";
    }

    if (!url.empty()) {
        std::string cmd = "curl -L -f -s -S -o \"" + dest.string() + "\" \"" + url + "\"";
        std::fprintf(stderr, "[linux] Downloading %s...\n", url.c_str());
        int rc = std::system(cmd.c_str());
        return rc == 0;
    }

    return false;
}

static std::filesystem::path find_linux_distro_bootstrap(const std::string& distro, GuestArch arch)
{
    auto exec_dir = get_executable_path();
    std::string normalized_distro = distro;
    std::transform(normalized_distro.begin(), normalized_distro.end(), normalized_distro.begin(),
                   [](unsigned char c){ return std::tolower(c); });

    if (normalized_distro.empty()) {
        normalized_distro = "ubuntu";
    }

    std::string arch_str = (arch == GuestArch::Aarch64) ? "aarch64" : "x86_64";
    std::string filename = "linux-bootstrap-" + normalized_distro + "-" + arch_str + ".tar.gz";

    std::error_code ec;
    
    // 1. Check same directory
    auto candidate = exec_dir / filename;
    if (std::filesystem::exists(candidate, ec))
        return candidate;

    // 2. Check next to app bundle
    auto parent_dir = exec_dir.parent_path().parent_path().parent_path();
    candidate = parent_dir / filename;
    if (std::filesystem::exists(candidate, ec))
        return candidate;

    // 3. Check inside bundle frameworks
    candidate = exec_dir.parent_path().parent_path() / "Frameworks" / filename;
    if (std::filesystem::exists(candidate, ec))
        return candidate;

    // 4. Check source tree
    candidate = exec_dir.parent_path().parent_path() / "third_party" / "linux-bootstrap" / filename;
    if (std::filesystem::exists(candidate, ec))
        return candidate;

    // 5. Check global cache
    candidate = muplar_home() / "cache" / "linux-bootstrap" / filename;
    if (std::filesystem::exists(candidate, ec)) {
        ec.clear();
        auto size = std::filesystem::file_size(candidate, ec);
        if (!ec && size > 0)
            return candidate;
    }

    // 6. Compatibility fallback for old naming scheme (linux-bootstrap-<arch>.tar.gz)
    if (normalized_distro == "ubuntu") {
        std::string old_filename = "linux-bootstrap-" + arch_str + ".tar.gz";
        candidate = exec_dir / old_filename;
        if (std::filesystem::exists(candidate, ec)) return candidate;
        candidate = parent_dir / old_filename;
        if (std::filesystem::exists(candidate, ec)) return candidate;
        candidate = exec_dir.parent_path().parent_path() / "Frameworks" / old_filename;
        if (std::filesystem::exists(candidate, ec)) return candidate;
        candidate = exec_dir.parent_path().parent_path() / "third_party" / "linux-bootstrap" / old_filename;
        if (std::filesystem::exists(candidate, ec)) return candidate;
        candidate = muplar_home() / "cache" / "linux-bootstrap" / old_filename;
        if (std::filesystem::exists(candidate, ec)) {
            ec.clear();
            auto size = std::filesystem::file_size(candidate, ec);
            if (!ec && size > 0) return candidate;
        }
    }

    // Auto-download to cache if not found
    candidate = muplar_home() / "cache" / "linux-bootstrap" / filename;
    if (auto_download_bootstrap(normalized_distro, arch, candidate)) {
        return candidate;
    }

    return {};
}

static void print_distro_download_suggestion(const std::string& distro, GuestArch arch, const std::filesystem::path& dest)
{
    std::string normalized_distro = distro;
    std::transform(normalized_distro.begin(), normalized_distro.end(), normalized_distro.begin(),
                   [](unsigned char c){ return std::tolower(c); });

    std::string arch_str = (arch == GuestArch::Aarch64) ? "aarch64" : "x86_64";
    std::string ubuntu_arch = (arch == GuestArch::Aarch64) ? "arm64" : "amd64";

    std::string url = "";
    if (normalized_distro == "ubuntu") {
        url = "https://cdimage.ubuntu.com/ubuntu-base/releases/26.04/release/ubuntu-base-26.04-base-" + ubuntu_arch + ".tar.gz";
    } else if (normalized_distro == "alpine") {
        url = "https://dl-cdn.alpinelinux.org/alpine/latest-stable/releases/" + arch_str + "/ (Download alpine-minirootfs)";
    } else if (normalized_distro == "debian") {
        url = "https://github.com/debuerreotype/docker-debian-artifacts/raw/dist-" + ubuntu_arch + "/bookworm/rootfs.tar.xz";
    } else if (normalized_distro == "fedora") {
        url = "https://dl.fedoraproject.org/pub/fedora/linux/releases/40/Container/" + arch_str + "/images/";
    } else if (normalized_distro == "arch") {
        url = (arch == GuestArch::Aarch64)
            ? "http://archlinuxarm.org/os/ArchLinuxARM-aarch64-latest.tar.gz"
            : "https://geo.mirror.pkgbuild.com/iso/latest/archlinux-bootstrap-x86_64.tar.zst";
    } else if (normalized_distro == "opensuse") {
        url = (arch == GuestArch::Aarch64)
            ? "https://download.opensuse.org/ports/aarch64/tumbleweed/appliances/opensuse-tumbleweed-image.aarch64-lxc.tar.xz"
            : "https://download.opensuse.org/tumbleweed/appliances/opensuse-tumbleweed-image.x86_64-lxc.tar.xz";
    } else {
        url = "https://cdimage.ubuntu.com/ubuntu-base/releases/";
    }

    std::fprintf(stderr,
        "[linux] ERROR: Distro bootstrap rootfs not found for '%s' (%s).\n"
        "[linux] Please download a minimal rootfs tarball manually from:\n"
        "[linux]   %s\n"
        "[linux] and save it to:\n"
        "[linux]   %s\n",
        distro.c_str(), arch_str.c_str(), url.c_str(), dest.c_str());
}

static bool extract_bootstrap_rootfs(const std::filesystem::path& tarball,
                                     const std::filesystem::path& rootfs_dir,
                                     const std::string& distro)
{
    std::error_code ec;
    std::filesystem::create_directories(rootfs_dir, ec);
    bool skip_case_variant_terminfo =
        distro == "arch" && is_case_insensitive_directory(rootfs_dir);

    std::fprintf(stderr, "[linux] Extracting bootstrap rootfs: %s\n",
                 tarball.filename().c_str());

    pid_t pid = fork();
    if (pid < 0)
        return false;

    if (pid == 0) {
        std::string tarball_str = tarball.string();
        std::string rootfs_str = rootfs_dir.string();
        std::vector<std::string> args = {"tar", "-x"};
        if (skip_case_variant_terminfo) {
            args.push_back("--exclude");
            args.push_back("./usr/share/terminfo/*");
            args.push_back("--exclude");
            args.push_back("usr/share/terminfo/*");
        }
        args.push_back("-f");
        args.push_back(tarball_str);
        args.push_back("-C");
        args.push_back(rootfs_str);

        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (auto& arg : args)
            argv.push_back(const_cast<char*>(arg.c_str()));
        argv.push_back(nullptr);

        execvp("tar", argv.data());
        _exit(127);
    }

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR)
            continue;
        std::fprintf(stderr, "[linux] waitpid failed for tar: %s\n",
                     std::strerror(errno));
        return false;
    }

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        auto home_dir = rootfs_dir / "home";
        std::error_code chmod_ec;
        if (std::filesystem::is_directory(home_dir, chmod_ec) &&
            chmod(home_dir.c_str(), 0755) != 0) {
            std::fprintf(stderr, "[linux] chmod 0755 %s failed: %s\n",
                         home_dir.c_str(), std::strerror(errno));
        }
        std::fprintf(stderr, "[linux] Bootstrap rootfs extracted successfully\n");
        return true;
    }

    std::fprintf(stderr, "[linux] tar extraction failed (exit %d)\n",
                 WIFEXITED(status) ? WEXITSTATUS(status) : -1);
    return false;
}

static std::filesystem::path find_guest_stub(const std::string& filename, GuestArch arch)
{
    auto exec_dir = get_executable_path();
    std::string arch_str = (arch == GuestArch::Aarch64) ? "aarch64" : "x86_64";

    // Check if the binary is in the same directory (CLI/build bin) under arch folder
    auto candidate = exec_dir / arch_str / filename;
    std::error_code ec;
    if (std::filesystem::exists(candidate, ec)) {
        return candidate;
    }

    // Check if it's next to the app bundle under arch folder
    auto parent_dir = exec_dir.parent_path().parent_path().parent_path();
    candidate = parent_dir / arch_str / filename;
    if (std::filesystem::exists(candidate, ec)) {
        return candidate;
    }

    // Check if it's inside the bundle frameworks under arch folder
    candidate = exec_dir.parent_path().parent_path() / "Frameworks" / arch_str / filename;
    if (std::filesystem::exists(candidate, ec)) {
        return candidate;
    }

    // Fallback to legacy path for compatibility
    candidate = exec_dir.parent_path().parent_path() / "Frameworks" / filename;
    if (std::filesystem::exists(candidate, ec)) {
        return candidate;
    }

    return {};
}

static void copy_file_if_changed(const std::filesystem::path& source,
                                 const std::filesystem::path& destination,
                                 std::error_code& ec)
{
    ec.clear();
    std::filesystem::create_directories(destination.parent_path(), ec);
    ec.clear();

    if (std::filesystem::is_regular_file(destination, ec)) {
        ec.clear();
        auto source_size = std::filesystem::file_size(source, ec);
        if (!ec) {
            auto destination_size = std::filesystem::file_size(destination, ec);
            if (!ec && source_size == destination_size) {
                auto source_time = std::filesystem::last_write_time(source, ec);
                if (!ec) {
                    auto destination_time =
                        std::filesystem::last_write_time(destination, ec);
                    if (!ec && destination_time >= source_time)
                        return;
                }
            }
        }
    }

    ec.clear();
    std::filesystem::copy_file(source, destination,
                               std::filesystem::copy_options::overwrite_existing,
                               ec);
}

static void write_text_file_if_missing(const std::filesystem::path& path,
                                       const std::string& content)
{
    std::error_code ec;
    if (std::filesystem::exists(path, ec))
        return;

    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (out)
        out << content;
}

static void write_managed_text_file(const std::filesystem::path& path,
                                    const std::string& content)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (out)
        out << content;
}

static void write_text_file_if_missing_or_empty(const std::filesystem::path& path,
                                                const std::string& content)
{
    std::error_code ec;
    bool should_write = !std::filesystem::exists(path, ec);
    ec.clear();

    if (!should_write && std::filesystem::is_symlink(path, ec)) {
        should_write = true;
        std::filesystem::remove(path, ec);
        ec.clear();
    }

    if (!should_write && std::filesystem::is_regular_file(path, ec)) {
        auto size = std::filesystem::file_size(path, ec);
        should_write = !ec && size == 0;
    }

    if (!should_write)
        return;

    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (out)
        out << content;
}

static void ensure_deb_noninteractive_compat(
    const std::filesystem::path& rootfs,
    const std::string& distro)
{
    std::string normalized = distro;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (normalized != "ubuntu" && normalized != "debian")
        return;

    write_text_file_if_missing(
        rootfs / "etc" / "profile.d" / "muplar-debconf.sh",
        "# Muplar shells currently have no controlling Linux tty.\n"
        "export DEBIAN_FRONTEND=${DEBIAN_FRONTEND:-noninteractive}\n"
        "export DEBCONF_NONINTERACTIVE_SEEN=true\n");

    const auto prepare_script =
        rootfs / "usr" / "local" / "sbin" / "muplar-prepare-dpkg";
    {
        std::filesystem::create_directories(prepare_script.parent_path());
        std::ofstream prepare_out(prepare_script, std::ios::trunc);
        prepare_out <<
        "#!/bin/sh\n"
        "set -e\n"
        "ca_file=/var/lib/dpkg/info/ca-certificates.postinst\n"
        "ca_marker='# Muplar noninteractive ca-certificates compatibility v2'\n"
        "if [ -f \"$ca_file\" ] && ! grep -Fq \"$ca_marker\" \"$ca_file\"; then\n"
        "  ca_tmp=\"$ca_file.muplar.$$\"\n"
        "  awk '\n"
        "  { print }\n"
        "  !done && $0 == \"set -e\" {\n"
        "    print \"# Muplar noninteractive ca-certificates compatibility v2\"\n"
        "    print \"case \\\"$1\\\" in configure|triggered) [ \\\"${DEBIAN_FRONTEND:-}\\\" = noninteractive ] && exit 0 ;; esac\"\n"
        "    done=1\n"
        "  }\n"
        "  END { if (!done) exit 2 }\n"
        "  ' \"$ca_file\" >\"$ca_tmp\"\n"
        "  cat \"$ca_tmp\" >\"$ca_file\"\n"
        "  rm -f \"$ca_tmp\"\n"
        "fi\n"
        "man_file=/var/lib/dpkg/info/man-db.postinst\n"
        "if [ -f \"$man_file\" ]; then\n"
        "  sed 's|setpriv --reuid man --regid man --init-groups -- /usr/bin/mandb \"$@\"|/usr/bin/mandb \"$@\"|' \"$man_file\" >\"$man_file.muplar.$$\"\n"
        "  cat \"$man_file.muplar.$$\" >\"$man_file\"\n"
        "  rm -f \"$man_file.muplar.$$\"\n"
        "fi\n"
        "file=/var/lib/dpkg/info/tzdata.postinst\n"
        "marker='# Muplar noninteractive tzdata compatibility'\n"
        "[ -f \"$file\" ] || exit 0\n"
        "grep -Fq \"$marker\" \"$file\" && exit 0\n"
        "tmp=\"$file.muplar.$$\"\n"
        "awk '\n"
        "{ print }\n"
        "!done && $0 == \"set -e\" {\n"
        "  print \"\"\n"
        "  print \"# Muplar noninteractive tzdata compatibility\"\n"
        "  print \"if [ \\\"${DEBIAN_FRONTEND:-}\\\" = noninteractive ]; then\"\n"
        "  print \"  if [ ! -e \\\"${DPKG_ROOT:-}/etc/localtime\\\" ]; then\"\n"
        "  print \"    ln -snf /usr/share/zoneinfo/Etc/UTC \\\"${DPKG_ROOT:-}/etc/localtime\\\"\"\n"
        "  print \"  fi\"\n"
        "  print \"  exit 0\"\n"
        "  print \"fi\"\n"
        "  done=1\n"
        "}\n"
        "END { if (!done) exit 2 }\n"
        "' \"$file\" >\"$tmp\"\n"
        "cat \"$tmp\" >\"$file\"\n"
        "rm -f \"$tmp\"\n";
    }
    std::error_code permissions_ec;
    std::filesystem::permissions(
        prepare_script,
        std::filesystem::perms::owner_all |
            std::filesystem::perms::group_read |
            std::filesystem::perms::group_exec |
            std::filesystem::perms::others_read |
            std::filesystem::perms::others_exec,
        std::filesystem::perm_options::replace, permissions_ec);

    write_text_file_if_missing(
        rootfs / "etc" / "apt" / "apt.conf.d" /
            "00muplar-noninteractive",
        "DPkg::Pre-Invoke {\"/usr/local/sbin/muplar-prepare-dpkg\";};\n");

    const auto ca_postinst = rootfs / "var" / "lib" / "dpkg" / "info" /
                             "ca-certificates.postinst";
    std::ifstream ca_in(ca_postinst);
    if (ca_in) {
        std::string ca_content((std::istreambuf_iterator<char>(ca_in)),
                               std::istreambuf_iterator<char>());
        const std::string ca_marker =
            "# Muplar noninteractive ca-certificates compatibility v2";
        const std::string ca_anchor = "set -e\n";
        if (ca_content.find(ca_marker) == std::string::npos) {
            const auto ca_pos = ca_content.find(ca_anchor);
            if (ca_pos != std::string::npos) {
                ca_content.insert(
                    ca_pos + ca_anchor.size(),
                    "# Muplar noninteractive ca-certificates compatibility v2\n"
                    "case \"$1\" in\n"
                    "    configure|triggered)\n"
                    "        [ \"${DEBIAN_FRONTEND:-}\" = noninteractive ] && exit 0\n"
                    "        ;;\n"
                    "esac\n");
                std::ofstream ca_out(ca_postinst, std::ios::trunc);
                if (ca_out)
                    ca_out << ca_content;
            }
        }
    }

    const auto man_postinst = rootfs / "var" / "lib" / "dpkg" / "info" /
                              "man-db.postinst";
    std::ifstream man_in(man_postinst);
    if (man_in) {
        std::string man_content((std::istreambuf_iterator<char>(man_in)),
                                std::istreambuf_iterator<char>());
        const std::string setpriv =
            "setpriv --reuid man --regid man --init-groups -- /usr/bin/mandb \"$@\"";
        const auto man_pos = man_content.find(setpriv);
        if (man_pos != std::string::npos) {
            man_content.replace(man_pos, setpriv.size(),
                                "/usr/bin/mandb \"$@\"");
            std::ofstream man_out(man_postinst, std::ios::trunc);
            if (man_out)
                man_out << man_content;
        }
    }

    const auto postinst = rootfs / "var" / "lib" / "dpkg" / "info" /
                          "tzdata.postinst";
    std::ifstream in(postinst);
    if (!in)
        return;

    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    const std::string marker = "# Muplar noninteractive tzdata compatibility";
    if (content.find(marker) != std::string::npos)
        return;

    const std::string anchor = "set -e\n";
    const auto pos = content.find(anchor);
    if (pos == std::string::npos)
        return;

    const std::string guard =
        "\n# Muplar noninteractive tzdata compatibility\n"
        "if [ \"${DEBIAN_FRONTEND:-}\" = noninteractive ]; then\n"
        "\tif [ ! -e \"${DPKG_ROOT:-}/etc/localtime\" ]; then\n"
        "\t\tln -snf /usr/share/zoneinfo/Etc/UTC "
        "\"${DPKG_ROOT:-}/etc/localtime\"\n"
        "\tfi\n"
        "\texit 0\n"
        "fi\n";
    content.insert(pos + anchor.size(), guard);

    std::ofstream out(postinst, std::ios::trunc);
    if (out)
        out << content;
}

static void ensure_linux_machine_id(const std::filesystem::path& rootfs)
{
    const auto path = rootfs / "etc" / "machine-id";
    std::error_code ec;
    if (std::filesystem::is_regular_file(path, ec) &&
        std::filesystem::file_size(path, ec) >= 32)
        return;

    unsigned char bytes[16];
    arc4random_buf(bytes, sizeof(bytes));
    static constexpr char hex[] = "0123456789abcdef";
    char id[34];
    for (size_t i = 0; i < sizeof(bytes); ++i) {
        id[i * 2] = hex[bytes[i] >> 4];
        id[i * 2 + 1] = hex[bytes[i] & 0x0f];
    }
    id[32] = '\n';
    id[33] = '\0';

    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (out)
        out << id;
}

static void rename_linux_account_entry(const std::filesystem::path& path,
                                       const std::string& id,
                                       bool passwd_file)
{
    std::ifstream in(path);
    if (!in)
        return;

    std::vector<std::string> lines;
    std::string line;
    bool changed = false;
    while (std::getline(in, line)) {
        std::vector<std::string> fields;
        std::stringstream stream(line);
        std::string field;
        while (std::getline(stream, field, ':'))
            fields.push_back(field);
        if ((passwd_file && fields.size() >= 7 && fields[2] == id) ||
            (!passwd_file && fields.size() >= 3 && fields[2] == id)) {
            fields[0] = "muplar";
            if (passwd_file) {
                fields[4] = "Muplar user";
                fields[5] = "/home/muplar";
            }
            line.clear();
            for (size_t i = 0; i < fields.size(); ++i) {
                if (i != 0)
                    line += ':';
                line += fields[i];
            }
            changed = true;
        }
        lines.push_back(line);
    }
    if (!changed)
        return;

    std::ofstream out(path, std::ios::trunc);
    for (const auto& output_line : lines)
        out << output_line << '\n';
}

static void rename_linux_account_by_name(const std::filesystem::path& path,
                                         const std::string& old_name)
{
    std::ifstream in(path);
    if (!in)
        return;

    std::vector<std::string> lines;
    std::string line;
    bool changed = false;
    while (std::getline(in, line)) {
        if (line.rfind(old_name + ":", 0) == 0) {
            line.replace(0, old_name.size(), "muplar");
            if (path.filename() == "shadow") {
                const auto password_end = line.find(':', 7);
                if (password_end != std::string::npos)
                    line.replace(7, password_end - 7, "");
            }
            changed = true;
        }
        lines.push_back(line);
    }
    if (!changed)
        return;

    std::ofstream out(path, std::ios::trunc);
    for (const auto& output_line : lines)
        out << output_line << '\n';
}

static void ensure_linux_unprivileged_user(const std::filesystem::path& rootfs)
{
    std::string old_name = "user";
    {
        std::ifstream passwd(rootfs / "etc" / "passwd");
        std::string line;
        while (std::getline(passwd, line)) {
            std::stringstream stream(line);
            std::string name, password, uid;
            std::getline(stream, name, ':');
            std::getline(stream, password, ':');
            std::getline(stream, uid, ':');
            if (uid == "1000") {
                old_name = name;
                break;
            }
        }
    }

    rename_linux_account_entry(rootfs / "etc" / "passwd", "1000", true);
    rename_linux_account_entry(rootfs / "etc" / "group", "1000", false);
    rename_linux_account_by_name(rootfs / "etc" / "shadow", old_name);
    rename_linux_account_by_name(rootfs / "etc" / "gshadow", old_name);

    auto has_id = [](const std::filesystem::path& path, const char* id) {
        std::ifstream in(path);
        std::string line;
        const std::string marker = std::string(":") + id + ":";
        while (std::getline(in, line)) {
            if (line.find(marker) != std::string::npos)
                return true;
        }
        return false;
    };
    if (!has_id(rootfs / "etc" / "passwd", "1000")) {
        std::ofstream passwd(rootfs / "etc" / "passwd", std::ios::app);
        passwd << "muplar:x:1000:1000:Muplar user:/home/muplar:/bin/sh\n";
    }
    if (!has_id(rootfs / "etc" / "group", "1000")) {
        std::ofstream group(rootfs / "etc" / "group", std::ios::app);
        group << "muplar:x:1000:\n";
    }
    {
        std::ifstream shadow_in(rootfs / "etc" / "shadow");
        std::string shadow_content((std::istreambuf_iterator<char>(shadow_in)),
                                   std::istreambuf_iterator<char>());
        if (!shadow_content.empty() &&
            shadow_content.find("muplar:") == std::string::npos) {
            std::ofstream shadow(rootfs / "etc" / "shadow", std::ios::app);
            shadow << "muplar::20000:0:99999:7:::\n";
        }
    }

    {
        const auto hosts_path = rootfs / "etc" / "hosts";
        std::ifstream hosts_in(hosts_path);
        std::string hosts_content((std::istreambuf_iterator<char>(hosts_in)),
                                  std::istreambuf_iterator<char>());
        if (hosts_content.find("elfuse") == std::string::npos) {
            std::ofstream hosts(hosts_path, std::ios::app);
            hosts << "127.0.0.1 elfuse\n";
        }
    }

    const auto sudoers = rootfs / "etc" / "sudoers.d" / "90-muplar";
    write_text_file_if_missing(sudoers,
                               "muplar ALL=(ALL:ALL) NOPASSWD: ALL\n");
    std::error_code ec;
    std::filesystem::permissions(
        sudoers,
        std::filesystem::perms::owner_read |
            std::filesystem::perms::group_read,
        std::filesystem::perm_options::replace, ec);
    ec.clear();
    std::filesystem::permissions(
        rootfs / "home" / "muplar",
        std::filesystem::perms::owner_all |
            std::filesystem::perms::group_all |
            std::filesystem::perms::others_all,
        std::filesystem::perm_options::replace, ec);

    const auto sudo_compat = rootfs / "usr" / "local" / "bin" / "sudo";
    write_text_file_if_missing(
        sudo_compat,
        "#!/bin/sh\n"
        "while [ $# -gt 0 ]; do\n"
        "  case \"$1\" in\n"
        "    -n|-E|-H|-S) shift ;;\n"
        "    -u) [ \"${2:-root}\" = root ] || { echo 'sudo: only root is supported' >&2; exit 1; }; shift 2 ;;\n"
        "    --) shift; break ;;\n"
        "    -*) echo \"sudo: unsupported option: $1\" >&2; exit 1 ;;\n"
        "    *) break ;;\n"
        "  esac\n"
        "done\n"
        "[ $# -gt 0 ] || exec /bin/sh\n"
        "exec \"$@\"\n");
    ec.clear();
    std::filesystem::permissions(
        sudo_compat,
        std::filesystem::perms::owner_all |
            std::filesystem::perms::set_uid |
            std::filesystem::perms::group_read |
            std::filesystem::perms::group_exec |
            std::filesystem::perms::others_read |
            std::filesystem::perms::others_exec,
        std::filesystem::perm_options::replace, ec);

    // GTK 4 delegates SVG decoding to Glycin, which normally invokes its
    // loader through bubblewrap. Elfuse does not provide Linux namespaces, so
    // retain the loader process boundary while omitting the unsupported
    // bubblewrap setup.
    const auto bwrap_compat = rootfs / "usr" / "local" / "bin" / "bwrap";
    write_text_file_if_missing(
        bwrap_compat,
        "#!/bin/sh\n"
        "while [ $# -gt 0 ]; do\n"
        "  case \"$1\" in\n"
        "    /usr/libexec/glycin-loaders/*) exec \"$@\" ;;\n"
        "  esac\n"
        "  shift\n"
        "done\n"
        "echo 'bwrap: no supported guest command found' >&2\n"
        "exit 127\n");
    ec.clear();
    std::filesystem::permissions(
        bwrap_compat,
        std::filesystem::perms::owner_all |
            std::filesystem::perms::group_read |
            std::filesystem::perms::group_exec |
            std::filesystem::perms::others_read |
            std::filesystem::perms::others_exec,
        std::filesystem::perm_options::replace, ec);

    // Desktop portal activation pulls in mount namespaces and FUSE. Terminal
    // applications only need a private session bus and their own service.
    const auto dbus_config = rootfs / "etc" / "dbus-1" /
                             "muplar-session.conf";
    std::filesystem::create_directories(dbus_config.parent_path(), ec);
    std::ofstream dbus_out(dbus_config, std::ios::trunc);
    if (dbus_out)
        dbus_out <<
        "<!DOCTYPE busconfig PUBLIC \"-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN\"\n"
        " \"http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd\">\n"
        "<busconfig>\n"
        "  <type>session</type>\n"
        "  <keep_umask/>\n"
        "  <listen>unix:tmpdir=/tmp</listen>\n"
        "  <auth>EXTERNAL</auth>\n"
        "  <servicedir>/usr/local/share/dbus-1/muplar-services</servicedir>\n"
        "  <policy context=\"default\">\n"
        "    <allow send_destination=\"*\" eavesdrop=\"true\"/>\n"
        "    <allow eavesdrop=\"true\"/>\n"
        "    <allow own=\"*\"/>\n"
        "  </policy>\n"
        "</busconfig>\n";
    write_text_file_if_missing(
        rootfs / "usr" / "local" / "share" / "dbus-1" /
            "muplar-services" / "org.gnome.Terminal.service",
        "[D-BUS Service]\n"
        "Name=org.gnome.Terminal\n"
        "Exec=/usr/libexec/gnome-terminal-server\n");

    const auto session_launcher =
        rootfs / "usr" / "local" / "libexec" / "muplar-session-launcher";
    write_managed_text_file(
        session_launcher,
        "#!/bin/sh\n"
        "export GTK_USE_PORTAL=0\n"
        "export GDK_DEBUG=no-portals\n"
        "export NO_AT_BRIDGE=1\n"
        "export GTK_A11Y=none\n"
        "session_dir=/tmp/muplar-session\n"
        "requests_dir=$session_dir/requests\n"
        "host_pids_dir=$session_dir/host-pids\n"
        "fifo=$session_dir/commands\n"
        "rm -rf \"$session_dir\"\n"
        "mkdir -p \"$requests_dir\" \"$host_pids_dir\" || exit 1\n"
        "chmod 700 \"$session_dir\" \"$requests_dir\" \"$host_pids_dir\" "
            "2>/dev/null || true\n"
        "mkfifo \"$fifo\" || exit 1\n"
        "dbus_pid=\n"
        "if command -v dbus-daemon >/dev/null 2>&1 && "
            "[ -f /etc/dbus-1/muplar-session.conf ]; then\n"
        "  address_file=$session_dir/dbus-address\n"
        "  rm -f \"$address_file\"\n"
        "  dbus-daemon --nofork --config-file=/etc/dbus-1/muplar-session.conf "
            "--print-address=3 3>\"$address_file\" >\"$session_dir/dbus.log\" 2>&1 &\n"
        "  dbus_pid=$!\n"
        "  i=0\n"
        "  while [ ! -s \"$address_file\" ] && "
            "kill -0 \"$dbus_pid\" 2>/dev/null && [ \"$i\" -lt 100 ]; do\n"
        "    sleep .02\n"
        "    i=$((i + 1))\n"
        "  done\n"
        "  if [ -s \"$address_file\" ]; then\n"
        "    DBUS_SESSION_BUS_ADDRESS=$(cat \"$address_file\")\n"
        "    export DBUS_SESSION_BUS_ADDRESS\n"
        "  fi\n"
        "fi\n"
        "cleanup() {\n"
        "  rm -f \"$session_dir/ready\" \"$fifo\"\n"
        "  [ -n \"$dbus_pid\" ] && kill \"$dbus_pid\" 2>/dev/null || true\n"
        "}\n"
        "trap cleanup EXIT INT TERM HUP\n"
        "run_request() {\n"
        "  request=$1\n"
        "  status_file=${request%.request}.status\n"
        "  log_file=${request%.request}.log\n"
        "  . \"$request\"\n"
        "  \"$@\" >>\"$log_file\" 2>&1 &\n"
        "  child=$!\n"
        "  printf '%s\\n' \"$child\" >\"${request%.request}.pid\"\n"
        "  trap 'kill -TERM \"$child\" 2>/dev/null; wait \"$child\" 2>/dev/null' "
            "INT TERM HUP\n"
        "  wait \"$child\"\n"
        "  code=$?\n"
        "  printf '%s\\n' \"$code\" >\"$status_file.tmp\"\n"
        "  mv \"$status_file.tmp\" \"$status_file\"\n"
        "}\n"
        ": >\"$session_dir/ready\"\n"
        "while :; do\n"
        "  while IFS= read -r request; do\n"
        "    case \"$request\" in\n"
        "      $requests_dir/*.request)\n"
        "        if [ -f \"$request\" ]; then\n"
        "          run_request \"$request\" &\n"
        "        fi\n"
        "        ;;\n"
        "    esac\n"
        "  done <\"$fifo\"\n"
        "done\n");
    ec.clear();
    std::filesystem::permissions(
        session_launcher,
        std::filesystem::perms::owner_all |
            std::filesystem::perms::group_read |
            std::filesystem::perms::group_exec |
            std::filesystem::perms::others_read |
            std::filesystem::perms::others_exec,
        std::filesystem::perm_options::replace, ec);
}

static std::string join_strings(const std::vector<std::string>& values,
                                const char* separator)
{
    std::string out;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i != 0)
            out += separator;
        out += values[i];
    }
    return out;
}


static void ensure_relative_symlink(const std::filesystem::path& link_path,
                                    const char* target)
{
    std::error_code ec;
    std::filesystem::create_directories(link_path.parent_path(), ec);

    if (std::filesystem::is_symlink(link_path, ec)) {
        std::filesystem::path existing = std::filesystem::read_symlink(link_path, ec);
        if (!ec && existing == target)
            return;
        std::filesystem::remove(link_path, ec);
    } else if (std::filesystem::exists(link_path, ec)) {
        return;
    }

    std::filesystem::create_symlink(target, link_path, ec);
}

static void expose_host_socket_path(const std::filesystem::path& rootfs,
                                    const std::filesystem::path& host_socket,
                                    bool allow_missing = false)
{
    if (!host_socket.is_absolute())
        return;

    std::error_code ec;
    if (!allow_missing && !std::filesystem::exists(host_socket, ec))
        return;

    std::filesystem::path link_path = rootfs / host_socket.relative_path();
    std::filesystem::path target = std::filesystem::relative(
        host_socket, link_path.parent_path(), ec);
    if (ec || target.empty())
        target = host_socket;
    ensure_relative_symlink(link_path, target.string().c_str());
}

static std::filesystem::path default_wawona_runtime_dir()
{
    return std::filesystem::path("/tmp") /
           ("wawona-" + std::to_string(static_cast<unsigned long>(getuid())));
}

static void expose_host_display_sockets(const std::filesystem::path& rootfs)
{
    const char* wayland_display = std::getenv("WAYLAND_DISPLAY");
    if (!wayland_display || !wayland_display[0]) {
        expose_host_socket_path(rootfs,
                                default_wawona_runtime_dir() / "wayland-0",
                                true);
        return;
    }

    std::filesystem::path wayland_path = wayland_display;
    if (wayland_path.is_absolute()) {
        expose_host_socket_path(rootfs, wayland_path);
        return;
    }

    const char* runtime_dir = std::getenv("XDG_RUNTIME_DIR");
    if (runtime_dir && runtime_dir[0])
        expose_host_socket_path(rootfs,
                                std::filesystem::path(runtime_dir) / wayland_path);
    else
        expose_host_socket_path(rootfs,
                                default_wawona_runtime_dir() / wayland_path,
                                true);
}

static void ensure_guest_x11_socket_dir(const std::filesystem::path& rootfs)
{
    std::error_code ec;
    std::filesystem::path x11_dir = rootfs / "tmp" / ".X11-unix";
    if (std::filesystem::is_symlink(x11_dir, ec))
        std::filesystem::remove(x11_dir, ec);
    if (!std::filesystem::exists(x11_dir, ec))
        std::filesystem::create_directories(x11_dir, ec);
}


struct InstanceRegistryEntry {
    std::string name;
    std::filesystem::path root;
};

bool looks_like_path(const std::string& spec)
{
    return spec.find('/') != std::string::npos ||
           spec.rfind("./", 0) == 0 ||
           spec.rfind("../", 0) == 0 ||
           spec.rfind("~/", 0) == 0;
}

std::filesystem::path home_dir()
{
    if (const char* home = std::getenv("HOME"); home && home[0])
        return home;
    throw std::runtime_error("HOME is not set; cannot resolve Muplar prefix");
}

std::filesystem::path expand_user_path(const std::string& spec)
{
    if (spec == "~")
        return home_dir();
    if (spec.rfind("~/", 0) == 0)
        return home_dir() / spec.substr(2);
    return spec;
}

std::filesystem::path absolute_normal_path(const std::filesystem::path& path)
{
    return std::filesystem::absolute(path).lexically_normal();
}

std::string quote_toml(const std::string& value)
{
    std::string out = "\"";
    for (char c : value) {
        if (c == '\\' || c == '"')
            out.push_back('\\');
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

std::optional<std::string> read_toml_string(const std::filesystem::path& path,
                                            const std::string& key)
{
    std::ifstream in(path);
    if (!in)
        return std::nullopt;

    std::string line;
    const std::string prefix = key + " = ";
    while (std::getline(in, line)) {
        if (line.rfind(prefix, 0) != 0)
            continue;
        std::string value = line.substr(prefix.size());
        if (value.size() < 2 || value.front() != '"' || value.back() != '"')
            return std::nullopt;
        value = value.substr(1, value.size() - 2);
        std::string decoded;
        decoded.reserve(value.size());
        bool escaped = false;
        for (char c : value) {
            if (escaped) {
                decoded.push_back(c);
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else {
                decoded.push_back(c);
            }
        }
        return decoded;
    }
    return std::nullopt;
}

std::string quote_json_string(const std::string& value)
{
    std::string out = "\"";
    for (unsigned char c : value) {
        switch (c) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c < 0x20) {
                const char* hex = "0123456789abcdef";
                out += "\\u00";
                out.push_back(hex[(c >> 4) & 0xF]);
                out.push_back(hex[c & 0xF]);
            } else {
                out.push_back(static_cast<char>(c));
            }
            break;
        }
    }
    out.push_back('"');
    return out;
}

class JsonReader {
public:
    explicit JsonReader(std::string text)
        : text_(std::move(text))
    {
    }

    std::vector<InstanceRegistryEntry> parse_registry()
    {
        std::vector<InstanceRegistryEntry> entries;
        skip_ws();
        expect('{');
        while (true) {
            skip_ws();
            if (consume('}'))
                break;

            std::string key = parse_string();
            skip_ws();
            expect(':');
            skip_ws();
            if (key == "instances") {
                entries = parse_instances();
            } else {
                skip_value();
            }

            skip_ws();
            if (consume(','))
                continue;
            expect('}');
            break;
        }
        skip_ws();
        if (pos_ != text_.size())
            throw std::runtime_error("unexpected trailing JSON data");
        return entries;
    }

private:
    void skip_ws()
    {
        while (pos_ < text_.size() &&
               std::isspace(static_cast<unsigned char>(text_[pos_]))) {
            ++pos_;
        }
    }

    bool consume(char expected)
    {
        skip_ws();
        if (pos_ >= text_.size() || text_[pos_] != expected)
            return false;
        ++pos_;
        return true;
    }

    void expect(char expected)
    {
        if (!consume(expected)) {
            throw std::runtime_error(
                std::string("expected JSON token '") + expected + "'");
        }
    }

    std::string parse_string()
    {
        skip_ws();
        expect('"');
        std::string out;
        while (pos_ < text_.size()) {
            char c = text_[pos_++];
            if (c == '"')
                return out;
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (pos_ >= text_.size())
                throw std::runtime_error("unterminated JSON escape");
            char esc = text_[pos_++];
            switch (esc) {
            case '"':
            case '\\':
            case '/':
                out.push_back(esc);
                break;
            case 'b':
                out.push_back('\b');
                break;
            case 'f':
                out.push_back('\f');
                break;
            case 'n':
                out.push_back('\n');
                break;
            case 'r':
                out.push_back('\r');
                break;
            case 't':
                out.push_back('\t');
                break;
            case 'u':
                // Registry paths are written as UTF-8 without \\u escapes.
                // Accept simple ASCII escapes so hand-edited files do not fail
                // on punctuation, but reject non-ASCII code points for now.
                if (pos_ + 4 > text_.size())
                    throw std::runtime_error("truncated JSON unicode escape");
                {
                    unsigned value = 0;
                    for (int i = 0; i < 4; ++i) {
                        char h = text_[pos_++];
                        value <<= 4;
                        if (h >= '0' && h <= '9')
                            value |= static_cast<unsigned>(h - '0');
                        else if (h >= 'a' && h <= 'f')
                            value |= static_cast<unsigned>(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F')
                            value |= static_cast<unsigned>(h - 'A' + 10);
                        else
                            throw std::runtime_error("invalid JSON unicode escape");
                    }
                    if (value > 0x7F)
                        throw std::runtime_error("non-ASCII JSON unicode escape");
                    out.push_back(static_cast<char>(value));
                }
                break;
            default:
                throw std::runtime_error("invalid JSON escape");
            }
        }
        throw std::runtime_error("unterminated JSON string");
    }

    void skip_number()
    {
        skip_ws();
        if (pos_ < text_.size() && text_[pos_] == '-')
            ++pos_;
        while (pos_ < text_.size() &&
               std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
            ++pos_;
        }
        if (pos_ < text_.size() && text_[pos_] == '.') {
            ++pos_;
            while (pos_ < text_.size() &&
                   std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
                ++pos_;
            }
        }
    }

    void skip_literal(const char* literal)
    {
        while (*literal) {
            if (pos_ >= text_.size() || text_[pos_++] != *literal++)
                throw std::runtime_error("invalid JSON literal");
        }
    }

    void skip_array()
    {
        expect('[');
        while (true) {
            skip_ws();
            if (consume(']'))
                break;
            skip_value();
            skip_ws();
            if (consume(','))
                continue;
            expect(']');
            break;
        }
    }

    void skip_object()
    {
        expect('{');
        while (true) {
            skip_ws();
            if (consume('}'))
                break;
            (void)parse_string();
            skip_ws();
            expect(':');
            skip_value();
            skip_ws();
            if (consume(','))
                continue;
            expect('}');
            break;
        }
    }

    void skip_value()
    {
        skip_ws();
        if (pos_ >= text_.size())
            throw std::runtime_error("unexpected end of JSON");
        char c = text_[pos_];
        if (c == '"') {
            (void)parse_string();
        } else if (c == '{') {
            skip_object();
        } else if (c == '[') {
            skip_array();
        } else if (c == 't') {
            skip_literal("true");
        } else if (c == 'f') {
            skip_literal("false");
        } else if (c == 'n') {
            skip_literal("null");
        } else if (c == '-' ||
                   std::isdigit(static_cast<unsigned char>(c))) {
            skip_number();
        } else {
            throw std::runtime_error("invalid JSON value");
        }
    }

    std::vector<InstanceRegistryEntry> parse_instances()
    {
        std::vector<InstanceRegistryEntry> entries;
        expect('[');
        while (true) {
            skip_ws();
            if (consume(']'))
                break;
            entries.push_back(parse_instance());
            skip_ws();
            if (consume(','))
                continue;
            expect(']');
            break;
        }
        return entries;
    }

    InstanceRegistryEntry parse_instance()
    {
        InstanceRegistryEntry entry;
        expect('{');
        while (true) {
            skip_ws();
            if (consume('}'))
                break;
            std::string key = parse_string();
            skip_ws();
            expect(':');
            if (key == "name") {
                entry.name = parse_string();
            } else if (key == "root") {
                entry.root = expand_user_path(parse_string());
            } else {
                skip_value();
            }
            skip_ws();
            if (consume(','))
                continue;
            expect('}');
            break;
        }
        return entry;
    }

    std::string text_;
    size_t pos_ = 0;
};

#if !defined(__APPLE__)
extern char **environ;
#else
#include <crt_externs.h>
static char **GetProcessEnvironment() {
    return *_NSGetEnviron();
}
#define environ GetProcessEnvironment()
#endif

static std::filesystem::path find_on_path(const std::string& name)
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

static std::filesystem::path resolve_wineboot(const PrefixLayout& layout)
{
    auto exec_dir = get_executable_path();
    std::error_code ec;

    // 1. Check embedded in app bundle Contents/Frameworks/wine/bin/wineboot
    auto bundle_wineboot = exec_dir.parent_path() / "Frameworks" / "wine" / "bin" / "wineboot";
    if (std::filesystem::exists(bundle_wineboot, ec))
        return bundle_wineboot;

    // 2. Check runtime_sysroot / bin / wineboot
    if (!layout.runtime_sysroot.empty()) {
        auto candidate = layout.runtime_sysroot / "bin" / "wineboot";
        if (std::filesystem::exists(candidate, ec))
            return candidate;
    }

    // 3. Check dev build relative
    auto build_dir = exec_dir.parent_path().parent_path().parent_path().parent_path();
    auto dev_wineboot = build_dir / "wine-prefix" / "bin" / "wineboot";
    if (std::filesystem::exists(dev_wineboot, ec))
        return dev_wineboot;

    // 4. System PATH
    auto from_path = find_on_path("wineboot");
    if (!from_path.empty())
        return from_path;

    return {};
}

static std::filesystem::path resolve_wine_from_bin_dir(
    const std::filesystem::path& bin_dir)
{
    std::error_code ec;
    for (const char* name : {"wine64", "wine"}) {
        auto candidate = bin_dir / name;
        if (std::filesystem::exists(candidate, ec))
            return candidate;
    }
    return {};
}

static std::filesystem::path resolve_windows_font_registry_file()
{
    std::error_code ec;

    if (const char* env = std::getenv("MUPLAR_WINDOWS_FONTS_REG");
        env && env[0]) {
        std::filesystem::path candidate = expand_user_path(env);
        if (std::filesystem::is_regular_file(candidate, ec))
            return candidate;
        ec.clear();
    }

    auto exec_dir = get_executable_path();
    for (const auto& candidate : {
             exec_dir.parent_path() / "Resources" / "muplar-fonts.reg",
             exec_dir.parent_path() / "Frameworks" / "muplar-fonts.reg",
             exec_dir.parent_path().parent_path() / "platform" / "windows-x64" /
                 "muplar-fonts.reg",
             std::filesystem::current_path() / "platform" / "windows-x64" /
                 "muplar-fonts.reg",
             muplar_home() / "muplar-fonts.reg",
         }) {
        if (std::filesystem::is_regular_file(candidate, ec))
            return candidate;
        ec.clear();
    }

    return {};
}

static void set_windows_compatibility_env(
    const PrefixLayout& layout,
    const std::filesystem::path& runtime_root)
{
    setenv("WINEPREFIX", layout.rootfs.string().c_str(), 1);
    setenv("WINEDEBUG", "-all", 1);

    std::filesystem::path runtime_lib_dir = runtime_root / "lib" / "wine";
    if (std::filesystem::is_directory(runtime_lib_dir))
        setenv("WINEDLLPATH", runtime_lib_dir.string().c_str(), 1);

    std::filesystem::path runtime_lib = runtime_root / "lib";
    if (std::filesystem::is_directory(runtime_lib)) {
        setenv("DYLD_FALLBACK_LIBRARY_PATH",
               (runtime_lib.string() + ":/usr/local/lib:/opt/homebrew/lib").c_str(),
               1);
    }
}

static int wait_for_child(pid_t pid, const char* label)
{
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR)
            continue;
        std::fprintf(stderr, "[Muplar Windows Compatibility] waitpid failed for %s: %s\n",
                     label, std::strerror(errno));
        return 1;
    }

    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) {
        std::fprintf(stderr, "[Muplar Windows Compatibility] %s killed by signal %d\n",
                     label, WTERMSIG(status));
        return 128 + WTERMSIG(status);
    }
    return 1;
}

static std::filesystem::path ensure_wine_mono_msi_cached()
{
    static constexpr const char* kWineMonoVersion = "11.1.0";
    std::filesystem::path cache_dir =
        muplar_home() / "cache" / "wine-mono" / kWineMonoVersion;
    std::filesystem::path msi =
        cache_dir / ("wine-mono-" + std::string(kWineMonoVersion) + "-x86.msi");

    std::error_code ec;
    if (std::filesystem::exists(msi, ec)) {
        ec.clear();
        auto size = std::filesystem::file_size(msi, ec);
        if (!ec && size > 0)
            return msi;
    }

    std::filesystem::create_directories(cache_dir, ec);
    if (ec) {
        std::fprintf(stderr, "[Muplar Windows Compatibility] cannot create managed runtime cache dir %s: %s\n",
                     cache_dir.c_str(), ec.message().c_str());
        return {};
    }

    std::filesystem::path curl = find_on_path("curl");
    if (curl.empty())
        curl = "/usr/bin/curl";
    if (!std::filesystem::exists(curl, ec)) {
        std::fprintf(stderr, "[Muplar Windows Compatibility] curl not found, skipping managed runtime download\n");
        return {};
    }

    std::string url =
        "https://dl.winehq.org/wine/wine-mono/" +
        std::string(kWineMonoVersion) + "/wine-mono-" +
        std::string(kWineMonoVersion) + "-x86.msi";
    std::filesystem::path tmp = msi;
    tmp += ".tmp";

    pid_t pid = fork();
    if (pid < 0) {
        std::fprintf(stderr, "[Muplar Windows Compatibility] fork() failed for managed runtime download: %s\n",
                     std::strerror(errno));
        return {};
    }
    if (pid == 0) {
        std::string curl_str = curl.string();
        std::string tmp_str = tmp.string();
        char* argv[] = {
            const_cast<char*>(curl_str.c_str()),
            const_cast<char*>("-fL"),
            const_cast<char*>("--retry"),
            const_cast<char*>("3"),
            const_cast<char*>("-o"),
            const_cast<char*>(tmp_str.c_str()),
            const_cast<char*>(url.c_str()),
            nullptr
        };
        execve(curl_str.c_str(), argv, environ);
        std::fprintf(stderr, "[Muplar Windows Compatibility] execve curl failed: %s\n", std::strerror(errno));
        _exit(127);
    }

    int rc = wait_for_child(pid, "managed runtime download");
    if (rc != 0) {
        std::filesystem::remove(tmp, ec);
        std::fprintf(stderr, "[Muplar Windows Compatibility] managed runtime download failed with code %d\n", rc);
        return {};
    }

    std::filesystem::rename(tmp, msi, ec);
    if (ec) {
        std::fprintf(stderr, "[Muplar Windows Compatibility] cannot move managed runtime package into cache: %s\n",
                     ec.message().c_str());
        return {};
    }
    return msi;
}

static void install_wine_mono(const PrefixLayout& layout,
                              const std::filesystem::path& wine_bin_dir)
{
    std::filesystem::path msi = ensure_wine_mono_msi_cached();
    if (msi.empty())
        return;

    std::filesystem::path wine = resolve_wine_from_bin_dir(wine_bin_dir);
    if (wine.empty()) {
        wine = find_on_path("wine64");
        if (wine.empty())
            wine = find_on_path("wine");
    }
    if (wine.empty()) {
        std::fprintf(stderr, "[Muplar Windows Compatibility] runtime binary not found, skipping managed runtime install\n");
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        std::fprintf(stderr, "[Muplar Windows Compatibility] fork() failed for managed runtime install: %s\n",
                     std::strerror(errno));
        return;
    }

    if (pid == 0) {
        std::filesystem::path wine_root = wine.parent_path().parent_path();
        set_windows_compatibility_env(layout, wine_root);

        std::string wine_str = wine.string();
        std::string msi_str = msi.string();
        char* argv[] = {
            const_cast<char*>(wine_str.c_str()),
            const_cast<char*>("msiexec"),
            const_cast<char*>("/i"),
            const_cast<char*>(msi_str.c_str()),
            const_cast<char*>("/quiet"),
            nullptr
        };
        execve(wine_str.c_str(), argv, environ);
        std::fprintf(stderr, "[Muplar Windows Compatibility] execve msiexec failed: %s\n",
                     std::strerror(errno));
        _exit(127);
    }

    int rc = wait_for_child(pid, "managed runtime install");
    if (rc != 0) {
        std::fprintf(stderr, "[Muplar Windows Compatibility] managed runtime install exited with code %d\n", rc);
    }
}

static void import_windows_font_registry(
    const PrefixLayout& layout,
    const std::filesystem::path& wine_bin_dir)
{
    std::filesystem::path reg_file = resolve_windows_font_registry_file();
    if (reg_file.empty())
        return;

    std::filesystem::path runtime = resolve_wine_from_bin_dir(wine_bin_dir);
    if (runtime.empty()) {
        runtime = find_on_path("wine64");
        if (runtime.empty())
            runtime = find_on_path("wine");
    }
    if (runtime.empty()) {
        std::fprintf(stderr, "[Muplar Windows Compatibility] runtime binary not found, skipping font setup\n");
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        std::fprintf(stderr, "[Muplar Windows Compatibility] fork() failed for font setup: %s\n",
                     std::strerror(errno));
        return;
    }

    if (pid == 0) {
        set_windows_compatibility_env(layout, runtime.parent_path().parent_path());

        std::string runtime_str = runtime.string();
        std::string reg_file_str = reg_file.string();
        char* argv[] = {
            const_cast<char*>(runtime_str.c_str()),
            const_cast<char*>("regedit"),
            const_cast<char*>("/S"),
            const_cast<char*>(reg_file_str.c_str()),
            nullptr
        };
        execve(runtime_str.c_str(), argv, environ);
        std::fprintf(stderr, "[Muplar Windows Compatibility] execve font setup failed: %s\n",
                     std::strerror(errno));
        _exit(127);
    }

    int rc = wait_for_child(pid, "font setup");
    if (rc != 0) {
        std::fprintf(stderr, "[Muplar Windows Compatibility] font setup exited with code %d\n", rc);
    }
}

static void run_wineboot_init(const PrefixLayout& layout)
{
    std::filesystem::path wineboot = resolve_wineboot(layout);
    if (wineboot.empty()) {
        std::fprintf(stderr, "[Muplar Windows Compatibility] setup binary not found, skipping auto-init\n");
        return;
    }

    ensure_wine_tmp_dir_private();

    pid_t pid = fork();
    if (pid < 0) {
        std::fprintf(stderr, "[Muplar Windows Compatibility] fork() failed for setup init: %s\n", std::strerror(errno));
        return;
    }

    if (pid == 0) {
        set_windows_compatibility_env(layout, wineboot.parent_path().parent_path());

        std::string wineboot_str = wineboot.string();
        char* argv[] = {
            const_cast<char*>(wineboot_str.c_str()),
            const_cast<char*>("--init"),
            nullptr
        };

        execve(wineboot_str.c_str(), argv, environ);
        std::fprintf(stderr, "[Muplar Windows Compatibility] execve setup failed: %s\n", std::strerror(errno));
        _exit(127);
    }

    int rc = wait_for_child(pid, "Windows compatibility setup");
    if (rc != 0) {
        std::fprintf(stderr, "[Muplar Windows Compatibility] setup exited with code %d\n", rc);
        return;
    }

    import_windows_font_registry(layout, wineboot.parent_path());
    install_wine_mono(layout, wineboot.parent_path());
}

void ensure_layout_dirs(const PrefixLayout& layout)
{
    std::filesystem::create_directories(layout.rootfs / "tmp");
    std::filesystem::create_directories(layout.rootfs / "var" / "tmp");
    std::filesystem::create_directories(layout.packages_dir);
    std::filesystem::create_directories(layout.registry_dir);
    std::filesystem::create_directories(layout.apk_cache_dir);
    std::filesystem::create_directories(layout.logs_dir);

    switch (layout.kind) {
    case PrefixKind::Android:
        std::filesystem::create_directories(layout.rootfs / "data" / "app");
        std::filesystem::create_directories(layout.rootfs / "data" / "data");
        std::filesystem::create_directories(
            layout.rootfs / "data" / "local" / "tmp");
        std::filesystem::create_directories(layout.rootfs / "data" / "misc");
        std::filesystem::create_directories(layout.rootfs / "data" / "system");
        std::filesystem::create_directories(layout.rootfs / "system" / "bin");
        std::filesystem::create_directories(layout.rootfs / "system" / "xbin");
        std::filesystem::create_directories(layout.rootfs / "cache");
        std::filesystem::create_directories(layout.rootfs / "sdcard");
        std::filesystem::create_directories(layout.rootfs / "storage");
        std::filesystem::create_directories(layout.rootfs / "mnt");
        {
            std::error_code ec;
            auto sh_path = find_guest_sh();
            if (!sh_path.empty()) {
                copy_file_if_changed(sh_path, layout.rootfs / "system" / "bin" / "sh", ec);
                copy_file_if_changed(sh_path, layout.rootfs / "system" / "bin" / "bash", ec);
            }
        }
        break;
    case PrefixKind::Linux:
        std::filesystem::create_directories(layout.rootfs / "etc");
        std::filesystem::create_directories(layout.rootfs / "home" / "muplar");
        std::filesystem::create_directories(layout.rootfs / "var");
        std::filesystem::create_directories(layout.rootfs / "tmp");
        std::filesystem::create_directories(layout.rootfs / "var" / "tmp");
        {
            std::error_code ec;
            auto tmpPerms = std::filesystem::perms::owner_all |
                            std::filesystem::perms::group_all |
                            std::filesystem::perms::others_all |
                            std::filesystem::perms::sticky_bit;
            std::filesystem::permissions(layout.rootfs / "tmp", tmpPerms,
                                         std::filesystem::perm_options::replace,
                                         ec);
            ec.clear();
            std::filesystem::permissions(layout.rootfs / "var" / "tmp", tmpPerms,
                                         std::filesystem::perm_options::replace,
                                         ec);
        }
        {
            auto profile =
                linux_common::distro_profile(layout.distro.empty() ? "ubuntu"
                                                                   : layout.distro);
            write_text_file_if_missing(layout.rootfs / "etc" / "os-release",
                                       linux_common::os_release_content(profile));
            write_text_file_if_missing(layout.rootfs / "etc" / "hostname",
                                       layout.name + "\n");
            write_text_file_if_missing(layout.rootfs / "etc" / "hosts",
                                       "127.0.0.1 localhost elfuse\n"
                                       "::1 localhost elfuse\n");
            write_text_file_if_missing_or_empty(
                layout.rootfs / "etc" / "resolv.conf",
                "nameserver 1.1.1.1\n"
                "nameserver 8.8.8.8\n");
            write_text_file_if_missing(
                layout.rootfs / "etc" / "profile",
                "export HOME=${HOME:-/home/muplar}\n"
                "export USER=${USER:-muplar}\n"
                "export LOGNAME=${LOGNAME:-muplar}\n"
                "export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin\n");
            write_text_file_if_missing(
                layout.rootfs / "etc" / "muplar-default-packages",
                "# Distro default packages Muplar expects when this rootfs is provisioned.\n"
                "terminal=" + join_strings(profile.terminal_packages, ",") + "\n");
            if (is_case_insensitive_directory(layout.rootfs)) {
                std::filesystem::create_directories(layout.rootfs / "etc" / "dpkg" / "dpkg.cfg.d");
                write_text_file_if_missing(
                    layout.rootfs / "etc" / "dpkg" / "dpkg.cfg.d" / "muplar-casefold-terminfo",
                    "# Muplar rootfs lives on a case-insensitive host filesystem.\n"
                    "# Uppercase terminfo aliases collide with lowercase entries.\n"
                    "path-exclude=/usr/share/terminfo/[A-Z]/*\n"
                    "path-exclude=/usr/share/terminfo/*/*[A-Z]*\n"
                    "path-exclude=/lib/terminfo/[A-Z]/*\n"
                    "path-exclude=/lib/terminfo/*/*[A-Z]*\n");
            }
        }

        // Sync host home directories
        {
            const char* host_home = std::getenv("HOME");
            if (host_home && host_home[0]) {
                std::filesystem::path home_path = host_home;
                std::error_code ec;
                std::filesystem::path host_desktop = home_path / "Desktop";
                std::filesystem::path host_downloads = home_path / "Downloads";
                std::filesystem::path host_documents = home_path / "Documents";
                std::filesystem::path guest_home = layout.rootfs / "home" / "muplar";

                if (std::filesystem::exists(host_desktop, ec)) {
                    ensure_relative_symlink(guest_home / "Desktop", host_desktop.string().c_str());
                }
                if (std::filesystem::exists(host_downloads, ec)) {
                    ensure_relative_symlink(guest_home / "Downloads", host_downloads.string().c_str());
                    ensure_relative_symlink(guest_home / "Download", host_downloads.string().c_str());
                }
                if (std::filesystem::exists(host_documents, ec)) {
                    ensure_relative_symlink(guest_home / "Documents", host_documents.string().c_str());
                }
            }
        }
        // Inject foot terminal config so it doesn't attempt a 512MB memfd
        // mmap inside elfuse. On Linux, foot uses a large scrollable SHM pool
        // (max-shm-pool-size-mb=512) when fallocate(PUNCH_HOLE) succeeds.
        // Inside elfuse on macOS, fallocate returns 0 (success) but the
        // subsequent mmap(512MB) fails with EFAULT → SIGABRT → exit 134.
        // Capping the pool to 4MB keeps foot within limits elfuse can handle.
        {
            std::filesystem::path foot_config_dir =
                layout.rootfs / "home" / "muplar" / ".config" / "foot";
            std::error_code ec;
            std::filesystem::create_directories(foot_config_dir, ec);
            write_text_file_if_missing(
                foot_config_dir / "foot.ini",
                "[main]\n"
                "font=DejaVu Sans Mono:size=11\n"
                "shell=/bin/bash\n"
                "\n"
                "[tweak]\n"
                "max-shm-pool-size-mb=16\n"
                "font-monospace-warn=no\n");
        }

        std::filesystem::create_directories(layout.rootfs / "bin");
        std::filesystem::create_directories(layout.rootfs / "usr" / "bin");
        std::filesystem::create_directories(layout.rootfs / "sbin");
        std::filesystem::create_directories(layout.rootfs / "usr" / "sbin");
        ensure_guest_x11_socket_dir(layout.rootfs);
        expose_host_display_sockets(layout.rootfs);
        {
            std::error_code ec;
            auto real_sh = layout.rootfs / "bin" / "sh";
            bool rootfs_has_userland = false;
            if (std::filesystem::exists(real_sh, ec)) {
                auto resolved = std::filesystem::canonical(real_sh, ec);
                rootfs_has_userland =
                    !ec && std::filesystem::is_regular_file(resolved, ec);
            }

            if (!rootfs_has_userland) {
                const char* skip_bootstrap = std::getenv("MUPLAR_SKIP_LINUX_BOOTSTRAP");
                bool bootstrap_disabled =
                    skip_bootstrap && skip_bootstrap[0] && std::strcmp(skip_bootstrap, "0") != 0;
                if (!bootstrap_disabled) {
                    std::string normalized_distro = layout.distro;
                    std::transform(normalized_distro.begin(), normalized_distro.end(), normalized_distro.begin(),
                                   [](unsigned char c){ return std::tolower(c); });
                    if (normalized_distro.empty()) normalized_distro = "ubuntu";
                    auto bootstrap = find_linux_distro_bootstrap(layout.distro, layout.arch);
                    if (!bootstrap.empty()) {
                        extract_bootstrap_rootfs(bootstrap, layout.rootfs, normalized_distro);
                    } else {
                        std::string arch_str = (layout.arch == GuestArch::Aarch64) ? "aarch64" : "x86_64";
                        std::string filename = "linux-bootstrap-" + normalized_distro + "-" + arch_str + ".tar.gz";
                        std::filesystem::path dest = muplar_home() / "cache" / "linux-bootstrap" / filename;

                        print_distro_download_suggestion(layout.distro, layout.arch, dest);

                        auto sh_path = find_guest_sh();
                        if (!sh_path.empty()) {
                            copy_file_if_changed(sh_path, layout.rootfs / "bin" / "sh", ec);
                            copy_file_if_changed(sh_path, layout.rootfs / "bin" / "bash", ec);
                        }
                    }
                }
            }

            // Expose libEGL.so and libGLESv2.so stubs to /vendor/lib
            // to prevent guest package managers from replacing them
            {
                std::filesystem::create_directories(layout.rootfs / "vendor" / "lib");
                auto egl_path = find_guest_stub("libEGL.so", layout.arch);
                if (!egl_path.empty()) {
                    auto dest = layout.rootfs / "vendor" / "lib" / "libEGL.so";
                    copy_file_if_changed(egl_path, dest, ec);
                    ensure_relative_symlink(layout.rootfs / "vendor" / "lib" / "libEGL.so.1", "libEGL.so");
                }
                auto gles_path = find_guest_stub("libGLESv2.so", layout.arch);
                if (!gles_path.empty()) {
                    auto dest = layout.rootfs / "vendor" / "lib" / "libGLESv2.so";
                    copy_file_if_changed(gles_path, dest, ec);
                    ensure_relative_symlink(layout.rootfs / "vendor" / "lib" / "libGLESv2.so.2", "libGLESv2.so");
                }
            }

            ensure_linux_machine_id(layout.rootfs);
            ensure_deb_noninteractive_compat(layout.rootfs, layout.distro);
            ensure_linux_unprivileged_user(layout.rootfs);
        }
        break;
    case PrefixKind::Wine:
        std::filesystem::create_directories(layout.rootfs / "drive_c");
        std::filesystem::create_directories(layout.rootfs / "home" / "muplar");
        std::filesystem::create_directories(layout.rootfs / "var");
        break;
    }
}

void assign_layout_paths(PrefixLayout& layout)
{
    layout.rootfs = layout.root / "rootfs";
    layout.packages_dir = layout.root / "packages";
    layout.registry_dir = layout.root / "registry";
    layout.cache_dir = layout.root / "cache";
    layout.apk_cache_dir = layout.cache_dir / "apk";
    layout.logs_dir = layout.root / "logs";
}

void write_prefix_toml(const PrefixLayout& layout, bool overwrite = false)
{
    std::filesystem::path path = layout.root / "prefix.toml";
    if (!overwrite && std::filesystem::exists(path))
        return;
    if (!is_supported_runtime_tuple(layout.kind, layout.arch)) {
        throw std::runtime_error(
            "unsupported runtime tuple: kind=" + to_string(layout.kind) +
            " arch=" + to_string(layout.arch));
    }

    std::ofstream out(path, std::ios::trunc);
    if (!out)
        throw std::runtime_error("unable to write prefix metadata: " +
                                 path.string());

    out << "version = 1\n";
    out << "name = " << quote_toml(layout.name) << "\n";
    out << "kind = " << quote_toml(to_string(layout.kind)) << "\n";
    out << "arch = " << quote_toml(to_string(layout.arch)) << "\n";
    out << "runner = " << quote_toml(layout.runner) << "\n";
    out << "logging = " << (layout.logging ? "\"true\"" : "\"false\"") << "\n";
    if (!layout.distro.empty()) {
        out << "distro = " << quote_toml(layout.distro) << "\n";
    }
    if (!layout.runtime_sysroot.empty()) {
        out << "runtime_sysroot = "
            << quote_toml(std::filesystem::absolute(layout.runtime_sysroot).string())
            << "\n";
    }
}

std::string prefix_name_from_root(const std::filesystem::path& root)
{
    std::string name = root.filename().string();
    return name.empty() ? "default" : name;
}

bool path_is_same_or_inside(const std::filesystem::path& root,
                            const std::filesystem::path& path)
{
    auto root_norm = root.lexically_normal();
    auto path_norm = path.lexically_normal();
    auto root_it = root_norm.begin();
    auto path_it = path_norm.begin();
    for (; root_it != root_norm.end(); ++root_it, ++path_it) {
        if (path_it == path_norm.end() || *root_it != *path_it)
            return false;
    }
    return true;
}

bool same_path(const std::filesystem::path& a, const std::filesystem::path& b)
{
    return absolute_normal_path(a) == absolute_normal_path(b);
}

void write_instance_registry(const std::vector<InstanceRegistryEntry>& entries)
{
    std::filesystem::path path = instance_registry_path();
    std::filesystem::create_directories(path.parent_path());

    std::ofstream out(path, std::ios::trunc);
    if (!out)
        throw std::runtime_error("unable to write instance registry: " +
                                 path.string());

    out << "{\n";
    out << "  \"version\": 1,\n";
    out << "  \"instances\": [\n";
    for (size_t i = 0; i < entries.size(); ++i) {
        out << "    {\n";
        out << "      \"name\": " << quote_json_string(entries[i].name) << ",\n";
        out << "      \"root\": "
            << quote_json_string(absolute_normal_path(entries[i].root).string())
            << "\n";
        out << "    }";
        if (i + 1 < entries.size())
            out << ",";
        out << "\n";
    }
    out << "  ]\n";
    out << "}\n";
}

std::vector<InstanceRegistryEntry> migrate_legacy_prefixes()
{
    std::vector<InstanceRegistryEntry> entries;
    std::filesystem::path base = muplar_home() / "prefixes";
    if (!std::filesystem::exists(base))
        return entries;

    for (const auto& entry : std::filesystem::directory_iterator(base)) {
        if (!entry.is_directory())
            continue;
        std::filesystem::path root = entry.path();
        if (!is_prefix_root(root))
            continue;

        std::string name = prefix_name_from_root(root);
        if (auto stored = read_toml_string(root / "prefix.toml", "name")) {
            if (!stored->empty())
                name = *stored;
        }
        entries.push_back({name, absolute_normal_path(root)});
    }
    return entries;
}

std::vector<InstanceRegistryEntry> read_instance_registry()
{
    std::filesystem::path path = instance_registry_path();
    if (!std::filesystem::exists(path)) {
        std::vector<InstanceRegistryEntry> migrated = migrate_legacy_prefixes();
        write_instance_registry(migrated);
        return migrated;
    }

    std::ifstream in(path);
    if (!in)
        throw std::runtime_error("unable to read instance registry: " +
                                 path.string());
    std::stringstream buffer;
    buffer << in.rdbuf();
    return JsonReader(buffer.str()).parse_registry();
}

std::optional<InstanceRegistryEntry> find_registered_instance(
    const std::string& name)
{
    for (const auto& entry : read_instance_registry()) {
        if (entry.name == name)
            return entry;
    }
    return std::nullopt;
}

void register_instance(const PrefixLayout& layout)
{
    if (layout.name.empty())
        return;

    std::vector<InstanceRegistryEntry> entries = read_instance_registry();
    std::vector<InstanceRegistryEntry> filtered;
    std::filesystem::path root = absolute_normal_path(layout.root);
    for (const auto& entry : entries) {
        if (entry.name == layout.name || same_path(entry.root, root))
            continue;
        filtered.push_back(entry);
    }
    filtered.push_back({layout.name, root});
    write_instance_registry(filtered);
}

bool is_managed_prefix_store_child(const std::filesystem::path& root)
{
    std::filesystem::path managed_base =
        absolute_normal_path(muplar_home() / "prefixes");
    std::filesystem::path normalized_root = absolute_normal_path(root);
    return normalized_root.parent_path() == managed_base &&
           !normalized_root.filename().empty();
}

bool is_registered_instance_root_or_name(const std::filesystem::path& root,
                                         const std::string& name)
{
    for (const auto& entry : read_instance_registry()) {
        if ((!name.empty() && entry.name == name) ||
            same_path(entry.root, root)) {
            return true;
        }
    }
    return false;
}

void unregister_instance_root_or_name(const std::filesystem::path& root,
                                      const std::string& name)
{
    std::vector<InstanceRegistryEntry> entries = read_instance_registry();
    std::vector<InstanceRegistryEntry> filtered;
    for (const auto& entry : entries) {
        if ((!name.empty() && entry.name == name) ||
            same_path(entry.root, root)) {
            continue;
        }
        filtered.push_back(entry);
    }
    write_instance_registry(filtered);
}

PrefixLayout load_prefix_at_root(const std::filesystem::path& root,
                                 const std::string& instance_name,
                                 const std::filesystem::path& runtime_sysroot,
                                 bool create_if_missing,
                                 PrefixKind kind,
                                 GuestArch arch,
                                 std::string runner,
                                 std::string distro)
{
    PrefixLayout layout;
    layout.root = absolute_normal_path(root);
    layout.name = instance_name.empty() ? prefix_name_from_root(layout.root)
                                        : instance_name;
    if (layout.name.empty())
        layout.name = "default";

    layout.kind = kind;
    layout.arch = arch;
    layout.runner = runner.empty() ? "elfuse" : std::move(runner);
    layout.distro = std::move(distro);
    assign_layout_paths(layout);
    layout.runtime_sysroot = runtime_sysroot;

    std::filesystem::path metadata = layout.root / "prefix.toml";
    bool metadata_exists = std::filesystem::is_regular_file(metadata);
    if (!metadata_exists && std::filesystem::exists(layout.root) &&
        !create_if_missing) {
        throw std::runtime_error("not a Muplar prefix: " +
                                 layout.root.string());
    }

    if (metadata_exists) {
        if (instance_name.empty()) {
            if (auto stored = read_toml_string(metadata, "name")) {
                if (!stored->empty())
                    layout.name = *stored;
            }
        }
        if (layout.runtime_sysroot.empty()) {
            if (auto stored = read_toml_string(metadata, "runtime_sysroot"))
                layout.runtime_sysroot = *stored;
        }
        if (auto stored = read_toml_string(metadata, "kind"))
            layout.kind = parse_prefix_kind(*stored);
        if (auto stored = read_toml_string(metadata, "arch"))
            layout.arch = parse_guest_arch(*stored);
        if (auto stored = read_toml_string(metadata, "runner"))
            layout.runner = *stored;
        if (auto stored = read_toml_string(metadata, "distro"))
            layout.distro = *stored;
        if (auto stored = read_toml_string(metadata, "logging"))
            layout.logging = (*stored != "false");
    }

    if (layout.kind == PrefixKind::Linux && layout.distro.empty()) {
        layout.distro = "ubuntu";
    }

    if (create_if_missing &&
        !is_supported_runtime_tuple(layout.kind, layout.arch)) {
        throw std::runtime_error(
            "unsupported runtime tuple: kind=" + to_string(layout.kind) +
            " arch=" + to_string(layout.arch));
    }

    if (!std::filesystem::exists(layout.root)) {
        if (!create_if_missing) {
            throw std::runtime_error(
                "prefix does not exist: " + layout.root.string());
        }
        ensure_layout_dirs(layout);
        write_prefix_toml(layout);
        register_instance(layout);
        if (layout.kind == PrefixKind::Wine) {
            run_wineboot_init(layout);
        }
    } else {
        if (!std::filesystem::is_directory(layout.root)) {
            throw std::runtime_error("prefix path is not a directory: " +
                                     layout.root.string());
        }
        if (create_if_missing) {
            ensure_layout_dirs(layout);
            write_prefix_toml(layout);
            register_instance(layout);
        }
    }

    return layout;
}

PrefixLayout clone_prefix_into(const std::string& source_spec,
                               const std::string& dest_name,
                               const std::filesystem::path& dest_root,
                               bool replace_existing)
{
    if (dest_name.empty())
        throw std::runtime_error("destination prefix name is required");
    if (dest_root.empty())
        throw std::runtime_error("destination prefix root is required");

    PrefixLayout source = open_prefix(source_spec, {}, false);
    if (!is_prefix_root(source.root)) {
        throw std::runtime_error("source is not a Muplar prefix: " +
                                 source.root.string());
    }

    PrefixLayout dest = source;
    dest.root = absolute_normal_path(expand_user_path(dest_root.string()));
    dest.name = dest_name;
    assign_layout_paths(dest);

    std::filesystem::path source_abs = std::filesystem::weakly_canonical(
        source.root);
    std::filesystem::path dest_abs = std::filesystem::absolute(
        dest.root).lexically_normal();
    if (source_abs == dest_abs ||
        path_is_same_or_inside(source_abs, dest_abs) ||
        path_is_same_or_inside(dest_abs, source_abs)) {
        throw std::runtime_error("cannot clone overlapping prefix paths");
    }

    std::error_code ec;
    if (std::filesystem::exists(dest.root, ec)) {
        if (!replace_existing) {
            throw std::runtime_error(
                "destination prefix already exists: " + dest.root.string() +
                " (pass --replace to overwrite)");
        }
        std::filesystem::remove_all(dest.root, ec);
        if (ec)
            throw std::runtime_error("unable to remove destination prefix: " +
                                     ec.message());
    }

    std::filesystem::create_directories(dest.root.parent_path());
    std::filesystem::copy(
        source.root,
        dest.root,
        std::filesystem::copy_options::recursive |
            std::filesystem::copy_options::copy_symlinks);
    write_prefix_toml(dest, true);
    ensure_layout_dirs(dest);
    register_instance(dest);
    return dest;
}

} // namespace

std::filesystem::path muplar_home()
{
    if (const char* env = std::getenv("MUPLAR_HOME"); env && env[0])
        return expand_user_path(env);
    return home_dir() / ".muplar";
}

std::filesystem::path instance_registry_path()
{
    return muplar_home() / "instances.json";
}

std::filesystem::path resolve_prefix_root(const std::string& spec)
{
    if (spec.empty())
        return muplar_home() / "prefixes" / "default";
    if (looks_like_path(spec))
        return absolute_normal_path(expand_user_path(spec));
    if (auto registered_instance = find_registered_instance(spec))
        return absolute_normal_path(registered_instance->root);
    return muplar_home() / "prefixes" / spec;
}

std::string to_string(PrefixKind kind)
{
    switch (kind) {
    case PrefixKind::Android:
        return "android";
    case PrefixKind::Linux:
        return "linux";
    case PrefixKind::Wine:
        return "wine";
    }
    return "android";
}

std::string to_string(GuestArch arch)
{
    switch (arch) {
    case GuestArch::Aarch64:
        return "aarch64";
    case GuestArch::X86_64:
        return "x86_64";
    }
    return "aarch64";
}

PrefixKind parse_prefix_kind(const std::string& value)
{
    if (value.empty() || value == "android")
        return PrefixKind::Android;
    if (value == "linux")
        return PrefixKind::Linux;
    if (value == "wine" || value == "windows")
        return PrefixKind::Wine;
    throw std::runtime_error("unsupported prefix kind: " + value);
}

GuestArch parse_guest_arch(const std::string& value)
{
    if (value.empty() || value == "aarch64" || value == "arm64")
        return GuestArch::Aarch64;
    if (value == "x86_64" || value == "x64")
        return GuestArch::X86_64;
    throw std::runtime_error("unsupported prefix arch: " + value);
}

bool is_supported_runtime_tuple(PrefixKind kind, GuestArch arch)
{
    switch (kind) {
    case PrefixKind::Android:
        return arch == GuestArch::Aarch64;
    case PrefixKind::Wine:
        return arch == GuestArch::X86_64;
    case PrefixKind::Linux:
        return arch == GuestArch::Aarch64 || arch == GuestArch::X86_64;
    }
    return false;
}

PrefixLayout open_prefix(const std::string& spec,
                         const std::filesystem::path& runtime_sysroot,
                         bool create_if_missing,
                         PrefixKind kind,
                         GuestArch arch,
                         std::string runner,
                         std::string distro)
{
    std::filesystem::path root = resolve_prefix_root(spec);
    std::string name = looks_like_path(spec) ? std::string() : spec;
    return load_prefix_at_root(root, name, runtime_sysroot, create_if_missing,
                               kind, arch, std::move(runner), std::move(distro));
}

PrefixLayout open_prefix_at_root(const std::string& name,
                                 const std::filesystem::path& root,
                                 const std::filesystem::path& runtime_sysroot,
                                 bool create_if_missing,
                                 PrefixKind kind,
                                 GuestArch arch,
                                 std::string runner,
                                 std::string distro)
{
    if (name.empty())
        throw std::runtime_error("prefix instance name is required");
    return load_prefix_at_root(expand_user_path(root.string()), name,
                               runtime_sysroot, create_if_missing, kind, arch,
                               std::move(runner), std::move(distro));
}

std::vector<PrefixLayout> list_prefixes()
{
    std::vector<PrefixLayout> out;
    std::vector<InstanceRegistryEntry> entries = read_instance_registry();

    for (const auto& entry : entries) {
        try {
            PrefixLayout layout = load_prefix_at_root(
                entry.root, entry.name, {}, false, PrefixKind::Android,
                GuestArch::Aarch64, "elfuse", "");
            out.push_back(layout);
        } catch (const std::exception&) {
            // Keep the registry entry. The root may live on removable storage
            // or another user-selected location that is temporarily absent.
        }
    }

    return out;
}

bool is_prefix_root(const std::filesystem::path& root)
{
    std::error_code ec;
    return std::filesystem::is_directory(root, ec) &&
           std::filesystem::is_regular_file(root / "prefix.toml", ec);
}

PrefixLayout clone_prefix(const std::string& source_spec,
                          const std::string& dest_spec,
                          bool replace_existing)
{
    std::filesystem::path dest_root = resolve_prefix_root(dest_spec);
    std::string dest_name = looks_like_path(dest_spec)
        ? prefix_name_from_root(dest_root)
        : dest_spec;
    if (dest_name.empty())
        dest_name = "default";
    return clone_prefix_into(source_spec, dest_name, dest_root,
                             replace_existing);
}

PrefixLayout clone_prefix_to_root(const std::string& source_spec,
                                  const std::string& dest_name,
                                  const std::filesystem::path& dest_root,
                                  bool replace_existing)
{
    return clone_prefix_into(source_spec, dest_name, dest_root,
                             replace_existing);
}

void delete_prefix(const std::string& spec)
{
    std::filesystem::path root = resolve_prefix_root(spec);
    std::string name = looks_like_path(spec) ? prefix_name_from_root(root) : spec;
    bool registered = is_registered_instance_root_or_name(root, name);
    if (!is_prefix_root(root) && !registered &&
        !is_managed_prefix_store_child(root)) {
        throw std::runtime_error("not a Muplar prefix: " +
                                 root.string());
    }

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    if (ec) {
        throw std::runtime_error("unable to delete prefix " +
                                 root.string() + ": " +
                                 ec.message());
    }
    unregister_instance_root_or_name(root, name);
}

// ---------------------------------------------------------------------------
// Instance lifecycle helpers (PID file based)
// ---------------------------------------------------------------------------

std::filesystem::path pid_file_path(const PrefixLayout& layout)
{
    return layout.root / "run" / "wine.pid";
}

pid_t read_prefix_pid(const PrefixLayout& layout)
{
    std::filesystem::path pid_path = pid_file_path(layout);
    std::ifstream in(pid_path);
    if (!in)
        return 0;
    pid_t pid = 0;
    in >> pid;
    return (in && pid > 0) ? pid : 0;
}

PrefixState query_prefix_state(const PrefixLayout& layout)
{
    pid_t pid = read_prefix_pid(layout);
    if (pid <= 0)
        return PrefixState::Stopped;

    // kill(pid, 0) succeeds if the process exists (even if we can't signal it).
    if (kill(pid, 0) == 0)
        return PrefixState::Running;

    // Stale PID file — process is gone, clean it up.
    std::error_code ec;
    std::filesystem::remove(pid_file_path(layout), ec);
    return PrefixState::Stopped;
}

static void set_guest_env(std::vector<std::string>& env,
                          const std::string& key,
                          const std::string& value)
{
    std::string prefix = key + "=";
    for (std::string& entry : env) {
        if (entry.rfind(prefix, 0) == 0) {
            entry = prefix + value;
            return;
        }
    }
    env.push_back(prefix + value);
}

static bool guest_env_has_key(const std::vector<std::string>& env,
                              const std::string& key)
{
    const std::string prefix = key + "=";
    for (const std::string& entry : env) {
        if (entry.rfind(prefix, 0) == 0)
            return true;
    }
    return false;
}

static void set_guest_env_if_missing(std::vector<std::string>& env,
                                     const std::string& key,
                                     const std::string& value)
{
    if (!value.empty() && !guest_env_has_key(env, key))
        env.push_back(key + "=" + value);
}

static void pass_host_env_if_set(std::vector<std::string>& env, const char* name)
{
    const char* value = std::getenv(name);
    if (value && value[0])
        set_guest_env(env, name, value);
}

static void pass_linux_display_environment(std::vector<std::string>& env)
{
    static constexpr const char* kDisplayEnv[] = {
        "WAYLAND_DISPLAY",
        "XDG_RUNTIME_DIR",
        "DBUS_SESSION_BUS_ADDRESS",
        "XDG_SESSION_TYPE",
        "GDK_BACKEND",
        "QT_QPA_PLATFORM",
        "SDL_VIDEODRIVER",
        "CLUTTER_BACKEND",
        "EGL_PLATFORM",
        "LIBGL_ALWAYS_INDIRECT",
    };

    for (const char* name : kDisplayEnv)
        pass_host_env_if_set(env, name);

    set_guest_env_if_missing(env, "XDG_RUNTIME_DIR",
                             default_wawona_runtime_dir().string());
    set_guest_env_if_missing(env, "WAYLAND_DISPLAY", "wayland-0");
    set_guest_env_if_missing(env, "XDG_SESSION_TYPE", "wayland");
    set_guest_env_if_missing(env, "GDK_BACKEND", "wayland,x11");
    set_guest_env_if_missing(env, "QT_QPA_PLATFORM", "wayland;xcb");
    set_guest_env_if_missing(env, "SDL_VIDEODRIVER", "wayland");
    set_guest_env_if_missing(env, "CLUTTER_BACKEND", "wayland");
    set_guest_env_if_missing(env, "EGL_PLATFORM", "wayland");
}

std::vector<std::string> default_linux_guest_environment(const PrefixLayout& /*layout*/)
{
    std::string home_dir = "/home/muplar";
    std::vector<std::string> env = {
        "PATH=/bin:/usr/bin:/sbin:/usr/sbin",
        "LD_LIBRARY_PATH=/vendor/lib",
        "HOME=" + home_dir,
        "LOGNAME=muplar",
        "PWD=" + home_dir,
        "SHELL=/bin/sh",
        "TMPDIR=/tmp",
        "USER=muplar",
        "TERM=xterm-256color",
        "LANG=C.UTF-8",
        "LC_ALL=C.UTF-8",
    };
    pass_linux_display_environment(env);
    return env;
}

std::filesystem::path default_linux_host_cwd(const PrefixLayout& layout)
{
    return layout.rootfs / "home" / "muplar";
}

} // namespace muplar::runtime::prefix
