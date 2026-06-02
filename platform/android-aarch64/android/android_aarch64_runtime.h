#pragma once

#include "platform_runtime.h"

namespace muplar::runtime::android {

class AndroidAarch64Runtime final : public PlatformRuntime {
public:
    int run(const PlatformLaunchConfig& config) override;
};

} // namespace muplar::runtime::android
