#pragma once

#include "platform_runtime.h"

namespace muplar::runtime::android
{

class AndroidAarch64Runtime final : public PlatformRuntime
{
public:
    int run(const PlatformLaunchConfig &config) override;
};

std::filesystem::path ensure_muplard(const prefix::PrefixLayout &active_prefix);
void stop_muplard(const prefix::PrefixLayout &active_prefix);

}  // namespace muplar::runtime::android
