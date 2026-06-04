#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "prefix.h"

namespace muplar::runtime {

struct RuntimeJniCallConfig {
    bool enabled = false;
    std::string class_name;
    std::string method_name;
    std::string signature;
    std::vector<int64_t> int_args;
    bool receiver_explicit = false;
    bool receiver_static = false;
};

struct PlatformLaunchConfig {
    std::string input_path;
    std::vector<std::string> guest_args;
    std::string sysroot;
    bool verbose = false;
    bool quiet = false;
    int timeout_sec = 10;

    std::optional<prefix::PrefixLayout> active_prefix;

    bool apk_mode = false;
    std::optional<std::string> apk_lib_name;
    std::filesystem::path apk_extract_dir;

    RuntimeJniCallConfig jni_call;
    bool native_activity = false;
    bool strict_direct_imports = false;
    bool host_window = false;
    int host_window_linger_ms = -1;
};

class PlatformRuntime {
public:
    virtual ~PlatformRuntime() = default;
    virtual int run(const PlatformLaunchConfig& config) = 0;
};

} // namespace muplar::runtime
