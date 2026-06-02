#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace muplar::runtime::prefix {

enum class PrefixKind {
    Android,
    Linux,
    Wine,
};

enum class GuestArch {
    Aarch64,
    X86_64,
};

struct PrefixLayout {
    std::string name;
    PrefixKind kind = PrefixKind::Android;
    GuestArch arch = GuestArch::Aarch64;
    std::string runner = "elfuse";
    std::filesystem::path root;
    std::filesystem::path rootfs;
    std::filesystem::path packages_dir;
    std::filesystem::path registry_dir;
    std::filesystem::path cache_dir;
    std::filesystem::path apk_cache_dir;
    std::filesystem::path logs_dir;
    std::filesystem::path runtime_sysroot;
};

std::filesystem::path muplar_home();
std::filesystem::path instance_registry_path();
std::filesystem::path resolve_prefix_root(const std::string& spec);

std::string to_string(PrefixKind kind);
std::string to_string(GuestArch arch);
PrefixKind parse_prefix_kind(const std::string& value);
GuestArch parse_guest_arch(const std::string& value);

PrefixLayout open_prefix(const std::string& spec,
                         const std::filesystem::path& runtime_sysroot = {},
                         bool create_if_missing = false,
                         PrefixKind kind = PrefixKind::Android,
                         GuestArch arch = GuestArch::Aarch64,
                         std::string runner = "elfuse");
PrefixLayout open_prefix_at_root(const std::string& name,
                                 const std::filesystem::path& root,
                                 const std::filesystem::path& runtime_sysroot = {},
                                 bool create_if_missing = false,
                                 PrefixKind kind = PrefixKind::Android,
                                 GuestArch arch = GuestArch::Aarch64,
                                 std::string runner = "elfuse");

std::vector<PrefixLayout> list_prefixes();
bool is_prefix_root(const std::filesystem::path& root);
PrefixLayout clone_prefix(const std::string& source_spec,
                          const std::string& dest_spec,
                          bool replace_existing = false);
PrefixLayout clone_prefix_to_root(const std::string& source_spec,
                                  const std::string& dest_name,
                                  const std::filesystem::path& dest_root,
                                  bool replace_existing = false);
void delete_prefix(const std::string& spec);

} // namespace muplar::runtime::prefix
