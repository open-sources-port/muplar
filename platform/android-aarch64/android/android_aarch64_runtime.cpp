#include "android_aarch64_runtime.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "apk_envelope.h"
#include "art_bootstrap.h"
#include "elf_loader.h"
#include "guest_runner.h"
#ifdef MUPLAR_HAS_BUNDLED_JDK
#include "host_jvm_launcher.h"
#endif

namespace muplar::runtime::android {
namespace {

std::string lower_ext(const std::string& path)
{
    std::string ext = std::filesystem::path(path).extension().string();
    for (char& c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

void copy_jni_call_config(const RuntimeJniCallConfig& from,
                          elf::JniCallConfig& to)
{
    to.enabled = from.enabled;
    to.class_name = from.class_name;
    to.method_name = from.method_name;
    to.signature = from.signature;
    to.int_args = from.int_args;
    to.receiver_explicit = from.receiver_explicit;
    to.receiver_static = from.receiver_static;
}

void inspect_elf(const std::string& elf_path)
{
    try {
        elf::ElfLoader loader;
        auto image = loader.load(elf_path);

        std::cout << "ELF entry   : 0x" << std::hex << image.entry << "\n"
                  << "Load range  : 0x" << image.load_min
                  << " – 0x" << image.load_max << "\n"
                  << "Segments    : " << std::dec << image.segments.size()
                  << "\n";
    } catch (const std::exception& e) {
        std::cerr << "ELF inspect error: " << e.what() << "\n";
    }
}

void apply_apk_launch(const PlatformLaunchConfig& launch_cfg,
                      elf::GuestRunnerConfig& guest_cfg)
{
    const auto& active_prefix = launch_cfg.active_prefix;
    if (active_prefix &&
        active_prefix->kind != prefix::PrefixKind::Android) {
        throw std::runtime_error(
            "APK launch requires an android prefix; got kind=" +
            prefix::to_string(active_prefix->kind));
    }
    if (active_prefix &&
        active_prefix->arch != prefix::GuestArch::Aarch64) {
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

int handle_java_apk_launch(const PlatformLaunchConfig& launch_cfg,
                           const apk::ApkClassification& classification)
{
#ifdef MUPLAR_HAS_BUNDLED_JDK
    // ── Host JVM path (bundled Temurin JDK) ─────────────────────────────────
    // Converts DEX -> plain JAR then runs ArtApkMain via host JNI_CreateJavaVM.
    // No elfuse / app_process64 / boot OAT files needed.
    std::cerr << "[ART] using host JVM (bundled Temurin JDK)\n";

    // Determine scratch directory for the converted JAR and dexopt files.
    std::filesystem::path scratch_dir;
    if (!launch_cfg.sysroot.empty())
        scratch_dir = std::filesystem::path(launch_cfg.sysroot) / "data" / "local" / "tmp"
                    / "muplar" / "host-jvm";
    else
        scratch_dir = std::filesystem::temp_directory_path() / "muplar" / "host-jvm";

    // Build the bootstrap jar path (reuse existing ART bootstrap jar if present).
    std::filesystem::path bootstrap_jar;
    if (!launch_cfg.sysroot.empty()) {
        bootstrap_jar = std::filesystem::path(launch_cfg.sysroot) / "data" / "local" / "tmp"
                      / "muplar" / "art" / "muplar-art-bootstrap.jar";
    }

    // Convert APK DEX -> plain JAR so URLClassLoader can load it.
    std::filesystem::path apk_jar;
    if (!classification.dex_files.empty()) {
        // Stamp the output name by APK filename so re-conversion is skipped
        // when the APK hasn't changed.
        std::string apk_stem =
            std::filesystem::path(launch_cfg.input_path).stem().string();
        std::filesystem::path out_jar = scratch_dir / (apk_stem + "-classes.jar");
        apk_jar = convert_apk_dex_to_jar(
            launch_cfg.input_path, out_jar, scratch_dir);
        if (apk_jar.empty()) {
            std::cerr << "[ART] warning: DEX->JAR conversion failed; "
                         "class loading may fail if APK has DEX\n";
        }
    }

    HostJvmLaunchConfig jvm_cfg;
    jvm_cfg.bootstrap_jar   = bootstrap_jar;
    jvm_cfg.apk_jar         = apk_jar;
    // For the host JVM path, pass the converted JAR as apk_path so that
    // ArtApkMain's URLClassLoader fallback loads the JAR (plain .class files)
    // rather than the original APK containing raw DEX bytecode.
    jvm_cfg.apk_path        = apk_jar.empty()
                                  ? launch_cfg.input_path
                                  : std::filesystem::absolute(apk_jar).string();
    jvm_cfg.package_name    =
        classification.manifest_package.value_or("");
    jvm_cfg.launch_activity =
        classification.manifest_launch_activity.value_or("");
    jvm_cfg.scratch_dir     = scratch_dir;

    return host_jvm_launch(jvm_cfg);

#else
    // ── ART guest path (requires sysroot + boot OAT files) ───────────────────
    if (launch_cfg.sysroot.empty()) {
        std::cerr << "APK error: Java/ART bootstrap incomplete: "
                     "--sysroot is required (or run tools/setup-jdk.sh for "
                     "the host JVM path)\n";
        return 1;
    }

    std::filesystem::path staged_apk = stage_art_apk_for_sysroot(
        launch_cfg.input_path, launch_cfg.sysroot);

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
    guest_cfg.package_code_path = plan.guest_apk_path;
    if (plan.package_name)
        guest_cfg.package_name = *plan.package_name;

    inspect_elf(guest_cfg.elf_path);
    std::cerr << "[ART] executing app_process64 bootstrap path\n";
    elf::GuestRunner runner;
    return runner.run(guest_cfg);
#endif
}

} // namespace

int AndroidAarch64Runtime::run(const PlatformLaunchConfig& config)
{
    elf::GuestRunnerConfig guest_cfg;
    guest_cfg.elf_path = config.input_path;
    guest_cfg.sysroot = config.sysroot;
    guest_cfg.verbose = config.verbose;
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
        } catch (const std::exception& e) {
            std::cerr << "APK error: " << e.what() << "\n";
            return 1;
        }
    }

    guest_cfg.argv.push_back(guest_cfg.elf_path);
    for (const auto& arg : config.guest_args)
        guest_cfg.argv.push_back(arg);

    inspect_elf(guest_cfg.elf_path);

    elf::GuestRunner runner;
    return runner.run(guest_cfg);
}

} // namespace muplar::runtime::android
