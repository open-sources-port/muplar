#pragma once

#include "platform_runtime.h"

namespace muplar::runtime::linux_aarch64 {

class LinuxAarch64Runtime final : public PlatformRuntime {
public:
    int run(const PlatformLaunchConfig& config) override;
};

} // namespace muplar::runtime::linux_aarch64
