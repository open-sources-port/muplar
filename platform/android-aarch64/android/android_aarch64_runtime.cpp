#include "android_aarch64_runtime.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#include "apk_envelope.h"
#include "elf_loader.h"
#include "guest_runner.h"

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
