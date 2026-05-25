#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace muplar::runtime::apk {

struct ApkLaunchConfig {
    std::filesystem::path apk_path;
    std::optional<std::string> lib_name;
    std::filesystem::path output_dir;
};

struct ApkLaunchResult {
    std::filesystem::path apk_path;
    std::filesystem::path extract_dir;
    std::filesystem::path so_path;
    std::filesystem::path assets_dir;
    std::string selected_lib;
    std::optional<std::string> manifest_lib;
    std::vector<std::string> extracted_libs;
    std::vector<std::string> extracted_assets;
};

ApkLaunchResult prepare_apk_launch(const ApkLaunchConfig& config);

} // namespace muplar::runtime::apk
