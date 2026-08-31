#include "art_bootstrap.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace muplar::runtime::android
{
namespace
{

std::filesystem::path sysroot_join(const std::filesystem::path &sysroot,
                                   const char *guest_path)
{
    std::string path = guest_path;
    while (!path.empty() && path.front() == '/')
        path.erase(path.begin());
    return sysroot / path;
}

bool exists_regular(const std::filesystem::path &path)
{
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

bool exists_dir(const std::filesystem::path &path)
{
    std::error_code ec;
    return std::filesystem::is_directory(path, ec);
}

bool is_inside_path(const std::filesystem::path &path,
                    const std::filesystem::path &root)
{
    std::filesystem::path normalized_path =
        std::filesystem::absolute(path).lexically_normal();
    std::filesystem::path normalized_root =
        std::filesystem::absolute(root).lexically_normal();

    auto path_it = normalized_path.begin();
    auto root_it = normalized_root.begin();
    for (; root_it != normalized_root.end(); ++root_it, ++path_it) {
        if (path_it == normalized_path.end() || *path_it != *root_it)
            return false;
    }
    return true;
}

std::string guest_path_for(const std::filesystem::path &host_path,
                           const std::filesystem::path &sysroot)
{
    std::filesystem::path normalized_host =
        std::filesystem::absolute(host_path).lexically_normal();
    std::filesystem::path normalized_sysroot =
        std::filesystem::absolute(sysroot).lexically_normal();

    std::string host = normalized_host.string();
    std::string root = normalized_sysroot.string();
    if (!root.empty() && host.rfind(root, 0) == 0) {
        std::string guest = host.substr(root.size());
        return guest.empty() ? "/" : guest;
    }
    return host;
}

std::string join_strings(const std::vector<std::string> &values,
                         const std::string &separator)
{
    std::string out;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i)
            out += separator;
        out += values[i];
    }
    return out;
}

struct GuestPathCandidates {
    const char *label;
    bool required;
    std::vector<const char *> guest_paths;
};

std::vector<GuestPathCandidates> app_process_candidates()
{
    return {
        {
            "ART Java entrypoint",
            true,
            {
                "/apex/com.android.art/bin/dalvikvm64",
                "/system/bin/app_process64",
            },
        },
    };
}

std::vector<GuestPathCandidates> bootclasspath_candidates()
{
    return {
        {
            "core-oj.jar",
            true,
            {
                "/apex/com.android.art/javalib/core-oj.jar",
                "/system/framework/core-oj.jar",
            },
        },
        {
            "core-libart.jar",
            true,
            {
                "/apex/com.android.art/javalib/core-libart.jar",
                "/system/framework/core-libart.jar",
            },
        },
        {
            "conscrypt.jar",
            false,
            {
                "/apex/com.android.conscrypt/javalib/conscrypt.jar",
                "/system/framework/conscrypt.jar",
            },
        },
        {
            "core-icu4j.jar",
            false,
            {
                "/apex/com.android.i18n/javalib/core-icu4j.jar",
            },
        },
        {
            "okhttp.jar",
            false,
            {
                "/apex/com.android.art/javalib/okhttp.jar",
                "/system/framework/okhttp.jar",
            },
        },
        {
            "bouncycastle.jar",
            false,
            {
                "/apex/com.android.art/javalib/bouncycastle.jar",
                "/system/framework/bouncycastle.jar",
            },
        },
        {
            "apache-xml.jar",
            false,
            {
                "/apex/com.android.art/javalib/apache-xml.jar",
                "/system/framework/apache-xml.jar",
            },
        },
        {
            "framework.jar",
            true,
            {
                "/system/framework/framework.jar",
            },
        },
        {
            "framework-graphics.jar",
            false,
            {
                "/system/framework/framework-graphics.jar",
            },
        },
        {
            "framework-location.jar",
            false,
            {
                "/system/framework/framework-location.jar",
            },
        },
        {
            "framework-nfc.jar",
            false,
            {
                "/system/framework/framework-nfc.jar",
            },
        },
        {
            "framework-ondeviceintelligence-platform.jar",
            false,
            {
                "/system/framework/framework-ondeviceintelligence-platform.jar",
            },
        },
        {
            "framework-platformcrashrecovery.jar",
            false,
            {
                "/system/framework/framework-platformcrashrecovery.jar",
            },
        },
        {
            "services.jar",
            false,
            {
                "/system/framework/services.jar",
            },
        },
        {
            "ext.jar",
            false,
            {
                "/system/framework/ext.jar",
            },
        },
    };
}

std::vector<GuestPathCandidates> classpath_candidates()
{
    return {
        {
            "muplar-art-bootstrap.jar",
            true,
            {
                "/data/local/tmp/muplar/art/muplar-art-bootstrap.jar",
            },
        },
    };
}

std::vector<GuestPathCandidates> art_shim_candidates()
{
    return {
        {
            "libmuplar_android_art_shim.so",
            false,
            {
                "/data/local/tmp/muplar/art/libmuplar_android_art_shim.so",
            },
        },
    };
}

std::vector<GuestPathCandidates> native_runtime_candidates()
{
    return {
        {
            "libandroid_runtime.so",
            true,
            {
                "/system/lib64/libandroid_runtime.so",
            },
        },
        {
            "libart.so",
            true,
            {
                "/apex/com.android.art/lib64/libart.so",
                "/system/lib64/libart.so",
            },
        },
    };
}

std::string candidate_summary(const GuestPathCandidates &candidates)
{
    std::vector<std::string> paths;
    for (const char *guest_path : candidates.guest_paths) {
        std::string path = guest_path;
        while (!path.empty() && path.front() == '/')
            path.erase(path.begin());
        paths.push_back(path);
    }
    return join_strings(paths, " or ");
}

std::string sanitize_path_component(std::string value)
{
    for (char &c : value) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (!std::isalnum(uc) && c != '-' && c != '_' && c != '.')
            c = '_';
    }
    if (value.empty())
        value = "apk";
    return value;
}

std::string file_stamp(const std::filesystem::path &path)
{
    std::string seed = std::filesystem::absolute(path).string();
    try {
        seed += ":" + std::to_string(std::filesystem::file_size(path));
        seed += ":" + std::to_string(static_cast<long long>(
                          std::filesystem::last_write_time(path)
                              .time_since_epoch()
                              .count()));
    } catch (const std::exception &) {
        seed += ":unknown";
    }

    std::ostringstream out;
    out << std::hex << std::hash<std::string>{}(seed);
    return out.str();
}

std::filesystem::path first_existing_regular(
    const std::filesystem::path &sysroot,
    const GuestPathCandidates &candidates)
{
    for (const char *guest_path : candidates.guest_paths) {
        std::filesystem::path host_path = sysroot_join(sysroot, guest_path);
        if (exists_regular(host_path))
            return host_path;
    }
    return {};
}

std::filesystem::path stage_optional_art_file(
    const std::filesystem::path &sysroot,
    const char *guest_path,
    const std::vector<std::filesystem::path> &source_candidates)
{
    std::filesystem::path destination = sysroot_join(sysroot, guest_path);

    for (const auto &source : source_candidates) {
        if (!exists_regular(source))
            continue;

        std::error_code ec;
        std::filesystem::create_directories(destination.parent_path(), ec);
        if (ec)
            continue;
        std::filesystem::copy_file(
            source, destination,
            std::filesystem::copy_options::overwrite_existing, ec);
        if (!ec) {
            std::cerr << "[ART] staged runtime file: " << destination.string()
                      << "\n";
            return destination;
        }
    }

    if (exists_regular(destination))
        return destination;

    return {};
}

}  // namespace

std::filesystem::path stage_art_apk_for_sysroot(
    const std::filesystem::path &apk_path,
    const std::filesystem::path &sysroot)
{
    if (sysroot.empty())
        return std::filesystem::absolute(apk_path).lexically_normal();

    std::filesystem::path absolute_apk =
        std::filesystem::absolute(apk_path).lexically_normal();
    std::filesystem::path absolute_sysroot =
        std::filesystem::absolute(sysroot).lexically_normal();
    if (is_inside_path(absolute_apk, absolute_sysroot))
        return absolute_apk;

    std::filesystem::path staging_dir =
        absolute_sysroot / "data" / "local" / "tmp" / "muplar" / "apks";
    std::string filename =
        sanitize_path_component(absolute_apk.stem().string()) + "-" +
        file_stamp(absolute_apk) + absolute_apk.extension().string();
    std::filesystem::path staged_apk = staging_dir / filename;

    std::error_code ec;
    std::filesystem::create_directories(staging_dir, ec);
    if (ec) {
        throw std::runtime_error(
            "unable to create ART APK staging directory: " +
            staging_dir.string() + ": " + ec.message());
    }
    std::filesystem::copy_file(
        absolute_apk, staged_apk,
        std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        throw std::runtime_error(
            "unable to stage APK for ART bootstrap: " + absolute_apk.string() +
            " -> " + staged_apk.string() + ": " + ec.message());
    }

    std::cerr << "[ART] staged APK for guest path: " << staged_apk.string()
              << "\n";
    return staged_apk;
}

ArtBootstrapPlan build_art_bootstrap_plan(const ArtBootstrapConfig &config)
{
    ArtBootstrapPlan plan;
    plan.apk_path =
        std::filesystem::absolute(config.apk_path).lexically_normal();
    if (!config.sysroot.empty())
        plan.guest_apk_path = guest_path_for(plan.apk_path, config.sysroot);
    plan.sysroot =
        config.sysroot.empty()
            ? std::filesystem::path()
            : std::filesystem::absolute(config.sysroot).lexically_normal();
    plan.package_name = config.apk_classification.manifest_package;
    plan.launch_activity = config.apk_classification.manifest_launch_activity;
    plan.dex_files = config.apk_classification.dex_files;

    if (plan.sysroot.empty()) {
        plan.missing_required_inputs.push_back(
            "--sysroot is required for Java/ART bootstrap");
        return plan;
    }

    plan.framework_dir = sysroot_join(plan.sysroot, "/system/framework");

    for (const auto &candidates : app_process_candidates()) {
        plan.app_process64 = first_existing_regular(plan.sysroot, candidates);
        if (!plan.app_process64.empty()) {
            plan.app_process64_guest_path =
                guest_path_for(plan.app_process64, plan.sysroot);
            break;
        }
        plan.missing_required_inputs.push_back(candidate_summary(candidates));
    }
    if (!exists_dir(plan.framework_dir)) {
        plan.missing_required_inputs.push_back("system/framework/");
    }

    std::vector<std::string> guest_classpath;
    for (const auto &candidates : classpath_candidates()) {
        if (std::string(candidates.label) == "muplar-art-bootstrap.jar") {
            std::filesystem::path source_root =
                std::filesystem::path(MUPLAR_SOURCE_DIR);
            std::filesystem::path staged = stage_optional_art_file(
                plan.sysroot, candidates.guest_paths.front(),
                {
                    source_root / "build" / "sysroot" / "data" / "local" /
                        "tmp" / "muplar" / "art" / "muplar-art-bootstrap.jar",
                    source_root / "build" / "builtin-android" /
                        "muplar-art-bootstrap.jar",
                });
            if (!staged.empty()) {
                plan.bootstrap_jar = staged;
                plan.bootstrap_jar_guest_path =
                    guest_path_for(staged, plan.sysroot);
                plan.classpath.push_back(staged);
                guest_classpath.push_back(plan.bootstrap_jar_guest_path);
                continue;
            }
        }

        std::filesystem::path host_path =
            first_existing_regular(plan.sysroot, candidates);
        if (!host_path.empty()) {
            plan.bootstrap_jar = host_path;
            plan.bootstrap_jar_guest_path =
                guest_path_for(host_path, plan.sysroot);
            plan.classpath.push_back(host_path);
            guest_classpath.push_back(plan.bootstrap_jar_guest_path);
            continue;
        }
        plan.missing_required_inputs.push_back(candidate_summary(candidates));
    }

    for (const auto &candidates : art_shim_candidates()) {
        if (std::string(candidates.label) == "libmuplar_android_art_shim.so") {
            std::filesystem::path source_root =
                std::filesystem::path(MUPLAR_SOURCE_DIR);
            std::filesystem::path staged = stage_optional_art_file(
                plan.sysroot, candidates.guest_paths.front(),
                {
                    source_root / "build" / "sysroot" / "data" / "local" /
                        "tmp" / "muplar" / "art" /
                        "libmuplar_android_art_shim.so",
                    source_root / "build" / "builtin-android" /
                        "libmuplar_android_art_shim.so",
                });
            if (!staged.empty()) {
                plan.art_shim = staged;
                plan.art_shim_guest_path = guest_path_for(staged, plan.sysroot);
                break;
            }
        }

        std::filesystem::path host_path =
            first_existing_regular(plan.sysroot, candidates);
        if (!host_path.empty()) {
            plan.art_shim = host_path;
            plan.art_shim_guest_path = guest_path_for(host_path, plan.sysroot);
            break;
        }
    }

    std::vector<std::string> guest_bootclasspath;
    std::vector<std::string> guest_dex2oat_bootclasspath;
    bool framework_bootclasspath_seen = false;
    for (const auto &candidates : bootclasspath_candidates()) {
        std::filesystem::path host_path =
            first_existing_regular(plan.sysroot, candidates);
        if (!host_path.empty()) {
            if (std::string(candidates.label) == "framework.jar" &&
                !plan.bootstrap_jar.empty() &&
                !plan.bootstrap_jar_guest_path.empty()) {
                plan.bootclasspath.push_back(plan.bootstrap_jar);
                guest_bootclasspath.push_back(plan.bootstrap_jar_guest_path);
            }
            plan.bootclasspath.push_back(host_path);
            std::string guest_path = guest_path_for(host_path, plan.sysroot);
            if (std::string(candidates.label) == "framework.jar")
                framework_bootclasspath_seen = true;
            if (!framework_bootclasspath_seen)
                guest_dex2oat_bootclasspath.push_back(guest_path);
            guest_bootclasspath.push_back(std::move(guest_path));
            continue;
        }
        if (candidates.required)
            plan.missing_required_inputs.push_back(
                candidate_summary(candidates));
    }

    std::set<std::string> seen_bootclasspath(guest_bootclasspath.begin(),
                                             guest_bootclasspath.end());
    std::filesystem::path system_framework =
        sysroot_join(plan.sysroot, "/system/framework");
    std::error_code ec;
    std::vector<std::filesystem::path> extra_framework_jars;
    if (std::filesystem::is_directory(system_framework, ec)) {
        for (const auto &entry :
             std::filesystem::directory_iterator(system_framework, ec)) {
            if (ec)
                break;
            const std::filesystem::path path = entry.path();
            if (!exists_regular(path) || path.extension() != ".jar")
                continue;
            std::string filename = path.filename().string();
            if (filename != "framework.jar" &&
                filename.rfind("framework-", 0) != 0)
                continue;
            if (path.filename() == "muplar-art-bootstrap.jar")
                continue;
            extra_framework_jars.push_back(path);
        }
    }
    std::sort(extra_framework_jars.begin(), extra_framework_jars.end());
    for (const auto &host_path : extra_framework_jars) {
        std::string guest_path = guest_path_for(host_path, plan.sysroot);
        if (seen_bootclasspath.count(guest_path))
            continue;
        seen_bootclasspath.insert(guest_path);
        plan.bootclasspath.push_back(host_path);
        guest_bootclasspath.push_back(std::move(guest_path));
    }

    std::filesystem::path generated_bootclasspath =
        sysroot_join(plan.sysroot, "/data/local/tmp/muplar/art/bootclasspath");
    std::vector<std::filesystem::path> generated_boot_jars;
    if (std::filesystem::is_directory(generated_bootclasspath, ec)) {
        for (const auto &entry :
             std::filesystem::directory_iterator(generated_bootclasspath, ec)) {
            if (ec)
                break;
            const std::filesystem::path path = entry.path();
            if (exists_regular(path) && path.extension() == ".jar")
                generated_boot_jars.push_back(path);
        }
    }
    std::sort(generated_boot_jars.begin(), generated_boot_jars.end());
    for (const auto &host_path : generated_boot_jars) {
        std::string guest_path = guest_path_for(host_path, plan.sysroot);
        if (seen_bootclasspath.count(guest_path))
            continue;
        seen_bootclasspath.insert(guest_path);
        plan.bootclasspath.push_back(host_path);
        guest_bootclasspath.push_back(std::move(guest_path));
    }

    for (const auto &candidates : native_runtime_candidates()) {
        std::filesystem::path host_path =
            first_existing_regular(plan.sysroot, candidates);
        if (!host_path.empty()) {
            plan.required_native_libraries.push_back(host_path);
            continue;
        }
        plan.missing_required_inputs.push_back(candidate_summary(candidates));
    }

    std::vector<const char *> library_dir_candidates = {
        "/apex/com.android.art/lib64",
        "/apex/com.android.runtime/lib64",
        "/apex/com.android.i18n/lib64",
        "/apex/com.android.conscrypt/lib64",
        "/system/lib64",
    };
    std::vector<std::string> guest_library_paths;
    for (const char *guest_dir : library_dir_candidates) {
        std::filesystem::path host_dir = sysroot_join(plan.sysroot, guest_dir);
        if (!exists_dir(host_dir))
            continue;
        std::string guest_dir_string = guest_dir;
        plan.native_library_paths.push_back(guest_dir_string);
        guest_library_paths.push_back(guest_dir_string);
    }

    if (!guest_classpath.empty()) {
        plan.env.push_back("CLASSPATH=" + join_strings(guest_classpath, ":"));
    }
    if (!guest_bootclasspath.empty()) {
        std::string bootclasspath = join_strings(guest_bootclasspath, ":");
        plan.env.push_back("BOOTCLASSPATH=" + bootclasspath);
        if (!guest_dex2oat_bootclasspath.empty()) {
            plan.env.push_back("DEX2OATBOOTCLASSPATH=" +
                               join_strings(guest_dex2oat_bootclasspath, ":"));
        }
    }
    if (!guest_library_paths.empty()) {
        plan.env.push_back("LD_LIBRARY_PATH=" +
                           join_strings(guest_library_paths, ":"));
    }
    if (!plan.art_shim_guest_path.empty())
        plan.env.push_back("LD_PRELOAD=" + plan.art_shim_guest_path);
    plan.env.push_back("ANDROID_ROOT=/system");
    plan.env.push_back("ANDROID_DATA=/data");
    if (exists_dir(sysroot_join(plan.sysroot, "/apex/com.android.runtime")))
        plan.env.push_back("ANDROID_RUNTIME_ROOT=/apex/com.android.runtime");
    if (exists_dir(sysroot_join(plan.sysroot, "/apex/com.android.art")))
        plan.env.push_back("ANDROID_ART_ROOT=/apex/com.android.art");
    if (exists_dir(sysroot_join(plan.sysroot, "/apex/com.android.i18n")))
        plan.env.push_back("ANDROID_I18N_ROOT=/apex/com.android.i18n");
    if (exists_dir(sysroot_join(plan.sysroot, "/apex/com.android.tzdata")))
        plan.env.push_back("ANDROID_TZDATA_ROOT=/apex/com.android.tzdata");
    if (const char *frame_path =
            std::getenv("MUPLAR_ANDROID_SOFTWARE_FRAME_PATH");
        frame_path && *frame_path) {
        plan.env.push_back(std::string("MUPLAR_ANDROID_SOFTWARE_FRAME_PATH=") +
                           frame_path);
    }
    if (const char *frame_socket =
            std::getenv("MUPLAR_HOST_WINDOW_FRAME_SOCKET");
        frame_socket && *frame_socket) {
        plan.env.push_back(std::string("MUPLAR_HOST_WINDOW_FRAME_SOCKET=") +
                           frame_socket);
    }
    plan.env.push_back("ANDROID_PRINTF_LOG=stdio");

    std::vector<std::string> service_property_args;
    if (const char *service_socket = std::getenv("MUPLAR_SERVICE_SOCKET");
        service_socket && *service_socket) {
        service_property_args.push_back(
            std::string("-Dmuplar.service.socket=") + service_socket);
    }

    bool use_dalvikvm =
        plan.app_process64_guest_path.find("dalvikvm") != std::string::npos;
    if (!plan.app_process64_guest_path.empty())
        plan.argv.push_back(plan.app_process64_guest_path);
    if (use_dalvikvm) {
        if (!guest_bootclasspath.empty())
            plan.argv.push_back("-Xbootclasspath:" +
                                join_strings(guest_bootclasspath, ":"));
        plan.argv.push_back("-Xusejit:true");
        for (const std::string &property_arg : service_property_args)
            plan.argv.push_back(property_arg);
        if (!guest_classpath.empty()) {
            plan.argv.push_back("-classpath");
            plan.argv.push_back(join_strings(guest_classpath, ":"));
        }
    } else {
        if (!guest_classpath.empty()) {
            plan.argv.push_back("-Djava.class.path=" +
                                join_strings(guest_classpath, ":"));
        }
        for (const std::string &property_arg : service_property_args)
            plan.argv.push_back(property_arg);
        plan.argv.push_back("/system/bin");
        plan.argv.push_back("--application");
        plan.argv.push_back("--nice-name=muplar-art");
    }
    plan.argv.push_back("com.muplar.runtime.ArtApkMain");
    plan.argv.push_back(plan.guest_apk_path.empty() ? plan.apk_path.string()
                                                    : plan.guest_apk_path);
    if (plan.package_name)
        plan.argv.push_back(*plan.package_name);
    if (plan.launch_activity)
        plan.argv.push_back(*plan.launch_activity);

    return plan;
}

void print_art_bootstrap_plan(const ArtBootstrapPlan &plan)
{
    std::cerr << "[ART] Java APK bootstrap plan\n";
    std::cerr << "[ART] apk=" << plan.apk_path.string() << "\n";
    if (!plan.guest_apk_path.empty())
        std::cerr << "[ART] guest apk=" << plan.guest_apk_path << "\n";
    if (plan.package_name)
        std::cerr << "[ART] package=" << *plan.package_name << "\n";
    if (plan.launch_activity)
        std::cerr << "[ART] launch activity=" << *plan.launch_activity << "\n";
    std::cerr << "[ART] dex files=" << plan.dex_files.size() << "\n";
    for (const std::string &dex : plan.dex_files)
        std::cerr << "[ART]   dex: " << dex << "\n";
    std::cerr << "[ART] app_process64=" << plan.app_process64.string() << "\n";
    if (!plan.app_process64_guest_path.empty()) {
        std::cerr << "[ART] guest app_process64="
                  << plan.app_process64_guest_path << "\n";
    }
    std::cerr << "[ART] bootstrap jar=" << plan.bootstrap_jar.string() << "\n";
    if (!plan.bootstrap_jar_guest_path.empty()) {
        std::cerr << "[ART] guest bootstrap jar="
                  << plan.bootstrap_jar_guest_path << "\n";
    }
    if (!plan.art_shim.empty()) {
        std::cerr << "[ART] ART preload shim=" << plan.art_shim.string()
                  << "\n";
        std::cerr << "[ART] guest ART preload shim=" << plan.art_shim_guest_path
                  << "\n";
    }
    std::cerr << "[ART] framework dir=" << plan.framework_dir.string() << "\n";
    if (!plan.classpath.empty()) {
        std::cerr << "[ART] classpath entries=" << plan.classpath.size()
                  << "\n";
        for (const auto &entry : plan.classpath)
            std::cerr << "[ART]   " << entry.string() << "\n";
    }
    if (!plan.bootclasspath.empty()) {
        std::cerr << "[ART] bootclasspath entries=" << plan.bootclasspath.size()
                  << "\n";
        for (const auto &entry : plan.bootclasspath)
            std::cerr << "[ART]   " << entry.string() << "\n";
    }
    if (!plan.required_native_libraries.empty()) {
        std::cerr << "[ART] required native libraries="
                  << plan.required_native_libraries.size() << "\n";
        for (const auto &entry : plan.required_native_libraries)
            std::cerr << "[ART]   " << entry.string() << "\n";
    }
    if (!plan.native_library_paths.empty()) {
        std::cerr << "[ART] guest library paths="
                  << plan.native_library_paths.size() << "\n";
        for (const auto &entry : plan.native_library_paths)
            std::cerr << "[ART]   " << entry << "\n";
    }
    if (!plan.argv.empty()) {
        std::cerr << "[ART] argv=" << plan.argv.size() << "\n";
        for (size_t i = 0; i < plan.argv.size(); ++i)
            std::cerr << "[ART]   argv[" << i << "]=" << plan.argv[i] << "\n";
    }
    if (!plan.env.empty()) {
        std::cerr << "[ART] env=" << plan.env.size() << "\n";
        for (const auto &entry : plan.env)
            std::cerr << "[ART]   " << entry << "\n";
    }
    if (!plan.ready()) {
        std::cerr << "[ART] missing Java/ART bootstrap inputs:\n";
        for (const std::string &missing : plan.missing_required_inputs)
            std::cerr << "[ART]   " << missing << "\n";
    }
}

std::string art_bootstrap_missing_summary(const ArtBootstrapPlan &plan)
{
    return join_strings(plan.missing_required_inputs, ", ");
}

}  // namespace muplar::runtime::android
