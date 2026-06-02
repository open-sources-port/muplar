#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "apk_envelope.h"

namespace muplar::runtime::android {

struct ArtBootstrapConfig {
    std::filesystem::path apk_path;
    std::filesystem::path sysroot;
    apk::ApkClassification apk_classification;
};

struct ArtBootstrapPlan {
    std::filesystem::path apk_path;
    std::string guest_apk_path;
    std::filesystem::path sysroot;
    std::optional<std::string> package_name;
    std::optional<std::string> launch_activity;
    std::vector<std::string> dex_files;

    std::filesystem::path app_process64;
    std::string app_process64_guest_path;
    std::filesystem::path bootstrap_jar;
    std::string bootstrap_jar_guest_path;
    std::filesystem::path framework_dir;
    std::vector<std::filesystem::path> classpath;
    std::vector<std::filesystem::path> bootclasspath;
    std::vector<std::filesystem::path> required_native_libraries;
    std::vector<std::string> native_library_paths;
    std::vector<std::string> missing_required_inputs;
    std::vector<std::string> argv;
    std::vector<std::string> env;

    bool ready() const { return missing_required_inputs.empty(); }
};

ArtBootstrapPlan build_art_bootstrap_plan(const ArtBootstrapConfig& config);
std::filesystem::path stage_art_apk_for_sysroot(
    const std::filesystem::path& apk_path,
    const std::filesystem::path& sysroot);
void print_art_bootstrap_plan(const ArtBootstrapPlan& plan);
std::string art_bootstrap_missing_summary(const ArtBootstrapPlan& plan);

} // namespace muplar::runtime::android
