#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include <cstdint>

namespace muplar::runtime::prefix
{

enum class PrefixKind {
    Android,
    Linux,
    Wine,
};

enum class GuestArch {
    Aarch64,
    X86_64,
};

/// Runtime liveness state derived from the PID file.
enum class PrefixState {
    Stopped,  ///< No PID file or process is no longer alive.
    Running,  ///< PID file exists and the process is alive.
};

struct PrefixLayout {
    std::string name;
    PrefixKind kind = PrefixKind::Android;
    GuestArch arch = GuestArch::Aarch64;
    std::string runner = "elfuse";
    std::string distro;
    std::filesystem::path root;
    std::filesystem::path rootfs;
    std::filesystem::path packages_dir;
    std::filesystem::path registry_dir;
    std::filesystem::path cache_dir;
    std::filesystem::path apk_cache_dir;
    std::filesystem::path logs_dir;
    std::filesystem::path runtime_sysroot;
    bool logging = true;
};

inline constexpr const char *kMainLogFilename = "muplar.log";

/// Path to the primary prefix log file (e.g. <prefix>/logs/muplar.log).
std::filesystem::path main_log_path(const PrefixLayout &layout);

std::filesystem::path muplar_home();
std::filesystem::path instance_registry_path();
std::filesystem::path resolve_prefix_root(const std::string &spec);

/// Path to the PID file stored inside the prefix run directory.
std::filesystem::path pid_file_path(const PrefixLayout &layout);

/// Read the PID file and check whether the process is still alive.
/// Removes a stale PID file if the process has exited.
PrefixState query_prefix_state(const PrefixLayout &layout);

/// Read the raw PID from the prefix PID file (0 if not running).
pid_t read_prefix_pid(const PrefixLayout &layout);

std::string to_string(PrefixKind kind);
std::string to_string(GuestArch arch);
PrefixKind parse_prefix_kind(const std::string &value);
GuestArch parse_guest_arch(const std::string &value);
bool is_supported_runtime_tuple(PrefixKind kind, GuestArch arch);

PrefixLayout open_prefix(const std::string &spec,
                         const std::filesystem::path &runtime_sysroot = {},
                         bool create_if_missing = false,
                         PrefixKind kind = PrefixKind::Android,
                         GuestArch arch = GuestArch::Aarch64,
                         std::string runner = "elfuse",
                         std::string distro = "");
PrefixLayout open_prefix_at_root(
    const std::string &name,
    const std::filesystem::path &root,
    const std::filesystem::path &runtime_sysroot = {},
    bool create_if_missing = false,
    PrefixKind kind = PrefixKind::Android,
    GuestArch arch = GuestArch::Aarch64,
    std::string runner = "elfuse",
    std::string distro = "");

std::vector<PrefixLayout> list_prefixes();
bool is_prefix_root(const std::filesystem::path &root);
PrefixLayout clone_prefix(const std::string &source_spec,
                          const std::string &dest_spec,
                          bool replace_existing = false);
PrefixLayout clone_prefix_to_root(const std::string &source_spec,
                                  const std::string &dest_name,
                                  const std::filesystem::path &dest_root,
                                  bool replace_existing = false);
void delete_prefix(const std::string &spec);
std::vector<std::string> default_linux_guest_environment(
    const PrefixLayout &layout);
std::filesystem::path default_linux_host_cwd(const PrefixLayout &layout);

// Publish the running compositor's Wayland socket inside a prefix, as a hard
// link at the path the guest is told to use. The guest cannot reach the host
// socket by name -- guest /tmp resolves inside the prefix, and elfuse
// translates pathname AF_UNIX addresses the same way -- and a symlink cannot
// bridge that because both escaping and absolute targets resolve back inside
// the sysroot.
//
// Call once the compositor is listening and before starting a client: the
// socket's inode changes on every rebind, so an older link is replaced rather
// than reused. A no-op when no socket is listening, and a logged warning if the
// prefix lives on another volume, where an inode cannot be shared.
void publish_display_socket(const std::filesystem::path &rootfs);

}  // namespace muplar::runtime::prefix
