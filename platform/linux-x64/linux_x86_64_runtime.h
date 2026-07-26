#pragma once

#include "platform_runtime.h"

namespace muplar::runtime::linux_x86_64
{

class LinuxX86_64Runtime final : public PlatformRuntime
{
public:
    int run(const PlatformLaunchConfig &config) override;
};

}  // namespace muplar::runtime::linux_x86_64
