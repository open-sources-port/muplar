#include "linux_aarch64_runtime.h"

#include "android_aarch64_runtime.h"
#include "prefix.h"

#include <stdexcept>

namespace muplar::runtime::linux_aarch64 {

int LinuxAarch64Runtime::run(const PlatformLaunchConfig& config)
{
    if (config.active_prefix) {
        const auto& layout = *config.active_prefix;
        if (layout.kind != prefix::PrefixKind::Linux) {
            throw std::runtime_error(
                "Linux ARM64 runtime requires a linux prefix; got kind=" +
                prefix::to_string(layout.kind));
        }
        if (layout.arch != prefix::GuestArch::Aarch64) {
            throw std::runtime_error(
                "Linux ARM64 runtime requires ARM64 arch; got arch=" +
                prefix::to_string(layout.arch));
        }
    }

    PlatformLaunchConfig linux_config = config;
    linux_config.linux_guest = true;
    if (config.active_prefix && config.guest_env.empty()) {
        linux_config.guest_env = prefix::default_linux_guest_environment(*config.active_prefix);
    }

    android::AndroidAarch64Runtime runtime;
    return runtime.run(linux_config);
}

} // namespace muplar::runtime::linux_aarch64
