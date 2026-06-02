#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace muplar::runtime::apk {

enum class ApkRuntimeKind {
    Empty,
    NativeOnly,
    JavaOnly,
    Mixed,
};

struct ApkLaunchConfig {
    std::filesystem::path apk_path;
    std::optional<std::string> lib_name;
    std::filesystem::path output_base_dir;
    std::filesystem::path output_dir;
};

struct ApkLaunchResult {
    std::filesystem::path apk_path;
    std::filesystem::path extract_dir;
    std::filesystem::path so_path;
    std::filesystem::path assets_dir;
    std::string selected_lib;
    std::optional<std::string> manifest_lib;
    std::optional<std::string> manifest_package;
    std::optional<std::string> manifest_launch_activity;
    std::vector<std::string> extracted_libs;
    std::vector<std::string> extracted_assets;
};

struct ApkClassification {
    std::filesystem::path apk_path;
    ApkRuntimeKind runtime_kind = ApkRuntimeKind::Empty;
    bool has_manifest = false;
    std::optional<std::string> manifest_lib;
    std::optional<std::string> manifest_package;
    std::optional<std::string> manifest_launch_activity;
    std::vector<std::string> arm64_libs;
    std::vector<std::string> dex_files;
    std::vector<std::string> asset_entries;
};

std::string to_string(ApkRuntimeKind kind);
ApkClassification classify_apk(const std::filesystem::path& apk_path);
ApkLaunchResult prepare_apk_launch(const ApkLaunchConfig& config);

} // namespace muplar::runtime::apk
