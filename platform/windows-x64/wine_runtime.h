// platform/windows-x64/wine_runtime.h
//
// WineRuntime: launches a Windows .exe inside a Wine/DXMT prefix.
// Supports both foreground (wait for wine to exit) and daemon
// (detach immediately) modes via PlatformLaunchConfig::daemon_mode.

#pragma once

#include <filesystem>
#include <string>

#include "platform_runtime.h"

namespace muplar::runtime::wine {

/// Launch configuration extras specific to Wine.
/// These are layered on top of PlatformLaunchConfig.
struct WineLaunchOptions {
    /// Path to the wine binary.  When empty the runtime tries
    ///   1. active_prefix.runtime_sysroot / bin / wine64  (old layout)
    ///   2. active_prefix.runtime_sysroot / bin / wine    (Wine 8+)
    ///   3. MUPLAR_WINE_PREFIX_DIR/bin/wine64 or wine     (compile-time default)
    ///   4. wine64 or wine on PATH
    std::filesystem::path wine_bin;

    /// When true, the parent process returns immediately after fork().
    /// The child writes its PID to <prefix>/run/wine.pid and continues.
    bool daemon = false;
};

class WineRuntime final : public PlatformRuntime {
public:
    explicit WineRuntime(WineLaunchOptions opts = {});
    int run(const PlatformLaunchConfig& config) override;

private:
    WineLaunchOptions opts_;
};

} // namespace muplar::runtime::wine
