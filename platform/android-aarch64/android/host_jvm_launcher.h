#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace muplar::runtime::android {

// Configuration for launching a Java-only APK via the bundled JDK's
// JNI_CreateJavaVM, bypassing ART / elfuse entirely.
struct HostJvmLaunchConfig {
    // Path to muplar-art-bootstrap.jar (contains ArtApkMain).
    std::filesystem::path bootstrap_jar;

    // Path to the converted APK classes JAR (classes.dex -> plain JAR via d8).
    // Empty means no APK classes are added to the classpath.
    std::filesystem::path apk_jar;

    // Optional extra jars (future Android stubs, etc.).
    std::vector<std::filesystem::path> extra_jars;

    // Arguments forwarded to ArtApkMain.main(String[]).
    std::string apk_path;
    std::string package_name;
    std::string launch_activity;

    // Writable scratch directory for dexopt / temp files.
    std::filesystem::path scratch_dir;
};

// Launch a Java-only APK through the bundled JDK's JNI_CreateJavaVM.
// Returns 0 on success, non-zero on failure.
int host_jvm_launch(const HostJvmLaunchConfig& config);

// Convert an APK's DEX bytecode to a plain JAR loadable by URLClassLoader.
// Uses d8 (Android SDK) if available. Returns empty path on failure.
std::filesystem::path convert_apk_dex_to_jar(
    const std::filesystem::path& apk_path,
    const std::filesystem::path& out_jar,
    const std::filesystem::path& scratch_dir);

} // namespace muplar::runtime::android
