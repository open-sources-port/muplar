#include "linux_x86_64_runtime.h"

#include "android_aarch64_runtime.h"
#include "prefix.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace muplar::runtime::linux_x86_64
{
namespace
{

constexpr const char *kRosettadPath =
    "/Library/Apple/usr/libexec/oah/RosettaLinux/rosettad";

}  // namespace

int LinuxX86_64Runtime::run(const PlatformLaunchConfig &config)
{
    if (!std::filesystem::exists(kRosettadPath)) {
        std::cerr << "Linux x64 runtime requires Apple Rosetta support, but "
                  << kRosettadPath << " was not found.\n"
                  << "Install Rosetta with: softwareupdate --install-rosetta\n";
        return 1;
    }

    if (config.active_prefix) {
        const auto &layout = *config.active_prefix;
        if (layout.kind != prefix::PrefixKind::Linux) {
            throw std::runtime_error(
                "Linux x64 runtime requires a linux prefix; got kind=" +
                prefix::to_string(layout.kind));
        }
        if (layout.arch != prefix::GuestArch::X86_64) {
            throw std::runtime_error(
                "Linux x64 runtime requires x86_64 arch; got arch=" +
                prefix::to_string(layout.arch));
        }
    }

    PlatformLaunchConfig linux_config = config;
    linux_config.linux_guest = true;
    if (config.active_prefix && config.guest_env.empty()) {
        linux_config.guest_env =
            prefix::default_linux_guest_environment(*config.active_prefix);
    }

    android::AndroidAarch64Runtime runtime;
    return runtime.run(linux_config);
}

}  // namespace muplar::runtime::linux_x86_64
