#include "host_jvm_launcher.h"

#include <cstdlib>
#include <dlfcn.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

// Use the real jni.h shipped by the bundled Temurin JDK.
// CMakeLists.txt adds third_party/jdk-bin/<arch>/include to the include path.
#include <jni.h>

namespace muplar::runtime::android {
namespace {

// ---------------------------------------------------------------------------
// Locate d8 for DEX -> JAR conversion
// ---------------------------------------------------------------------------
// std::string find_d8()
// {
//     // 1. Explicit env var
//     if (const char* d8 = std::getenv("D8")) {
//         if (std::filesystem::is_regular_file(d8))
//             return d8;
//     }

//     // 2. PATH
//     {
//         FILE* f = popen("command -v d8 2>/dev/null", "r");
//         if (f) {
//             char buf[2048] = {};
//             if (fgets(buf, sizeof(buf), f)) {
//                 pclose(f);
//                 std::string path(buf);
//                 while (!path.empty() &&
//                        (path.back() == '\n' || path.back() == '\r'))
//                     path.pop_back();
//                 if (!path.empty() && std::filesystem::is_regular_file(path))
//                     return path;
//             } else {
//                 pclose(f);
//             }
//         }
//     }

//     // 3. Android SDK known locations
//     const char* sdk_env_vars[] = {"ANDROID_HOME", "ANDROID_SDK_ROOT", nullptr};
//     std::vector<std::string> sdk_roots;
//     for (int i = 0; sdk_env_vars[i]; ++i) {
//         if (const char* v = std::getenv(sdk_env_vars[i]))
//             sdk_roots.push_back(v);
//     }
//     if (const char* home = std::getenv("HOME"))
//         sdk_roots.push_back(std::string(home) + "/Library/Android/sdk");

//     for (const auto& sdk : sdk_roots) {
//         std::filesystem::path bt(sdk + "/build-tools");
//         if (!std::filesystem::is_directory(bt))
//             continue;
//         std::string latest;
//         std::error_code ec;
//         for (auto& entry : std::filesystem::directory_iterator(bt, ec)) {
//             if (!entry.is_directory()) continue;
//             std::string name = entry.path().filename().string();
//             if (name > latest) latest = name;
//         }
//         if (!latest.empty()) {
//             auto candidate = bt / latest / "d8";
//             if (std::filesystem::is_regular_file(candidate))
//                 return candidate.string();
//         }
//     }

//     return {};
// }

// ---------------------------------------------------------------------------
// libjvm path — resolved at compile time by CMakeLists.txt
// ---------------------------------------------------------------------------

std::filesystem::path bundled_libjvm_path()
{
#ifdef MUPLAR_JDK_LIBJVM_PATH
    return std::filesystem::path(MUPLAR_JDK_LIBJVM_PATH);
#else
    return {};
#endif
}

// ---------------------------------------------------------------------------
// JNI helper — build a jstring String[] from a vector<string>
// ---------------------------------------------------------------------------

jobjectArray make_string_array(JNIEnv* env,
                               const std::vector<std::string>& strings)
{
    jclass string_class = env->FindClass("java/lang/String");
    if (!string_class) return nullptr;

    jobjectArray arr = env->NewObjectArray(
        static_cast<jsize>(strings.size()), string_class, nullptr);
    if (!arr) return nullptr;

    for (size_t i = 0; i < strings.size(); ++i) {
        jstring s = env->NewStringUTF(strings[i].c_str());
        if (!s) return nullptr;
        env->SetObjectArrayElement(arr, static_cast<jsize>(i), s);
        env->DeleteLocalRef(s);
    }
    return arr;
}

} // namespace

// ---------------------------------------------------------------------------
// Public: convert_apk_dex_to_jar
// ---------------------------------------------------------------------------

std::filesystem::path convert_apk_dex_to_jar(
    const std::filesystem::path& apk_path,
    const std::filesystem::path& out_jar,
    const std::filesystem::path& scratch_dir)
{
    std::error_code ec;
    std::filesystem::create_directories(scratch_dir, ec);
    std::filesystem::create_directories(out_jar.parent_path(), ec);

    // Strategy 0: Look for a companion *-classes.jar placed next to the APK
    // by the APK build script (create-tiny-java-activity-apk.sh and similar).
    // This is a plain JAR of .class files — perfect for URLClassLoader.
    {
        std::filesystem::path stem = apk_path.stem(); // e.g. "tinyjavaactivity"
        std::filesystem::path companion =
            apk_path.parent_path() / (stem.string() + "-classes.jar");
        if (std::filesystem::is_regular_file(companion, ec)) {
            // Copy to out_jar location for consistency
            std::filesystem::copy_file(companion, out_jar,
                std::filesystem::copy_options::overwrite_existing, ec);
            if (!ec) {
                std::cerr << "[HostJVM] companion classes JAR: "
                          << companion.string() << "\n";
                return out_jar;
            }
        }
    }

    // Strategy 1: Use dex-tools (dex2jar) if installed.

    // This converts DEX bytecode to plain .class files in a JAR.
    {
        FILE* f = popen("command -v d2j-dex2jar 2>/dev/null", "r");
        std::string d2j;
        if (f) {
            char buf[2048] = {};
            if (fgets(buf, sizeof(buf), f)) {
                d2j = std::string(buf);
                while (!d2j.empty() &&
                       (d2j.back() == '\n' || d2j.back() == '\r'))
                    d2j.pop_back();
            }
            pclose(f);
        }
        if (!d2j.empty() && std::filesystem::is_regular_file(d2j)) {
            std::string cmd = "\"" + d2j + "\""
                            + " -f"
                            + " -o \"" + out_jar.string() + "\""
                            + " \"" + apk_path.string() + "\""
                            + " 2>&1";
            std::cerr << "[HostJVM] converting DEX -> class JAR via dex2jar\n"
                      << "[HostJVM]   " << cmd << "\n";
            int rc = std::system(cmd.c_str());
            if (rc == 0 && std::filesystem::is_regular_file(out_jar, ec)) {
                std::cerr << "[HostJVM] DEX -> JAR: " << out_jar.string() << "\n";
                return out_jar;
            }
            std::cerr << "[HostJVM] dex2jar failed (exit " << rc << "), trying fallback\n";
        }
    }

    // Strategy 2: Extract .class files from the APK's companion build directory.
    // The tinyjavaactivity APK is built by create-tiny-java-activity-apk.sh which
    // leaves compiled .class files in build/apk/<name>-<pid>/classes/.
    // Find the most recently modified companion classes dir next to the APK.
    {
        std::filesystem::path apk_dir = apk_path.parent_path();
        // APK build dirs land in build/apk/ two levels up from data/local/tmp
        std::filesystem::path apk_root = apk_dir.parent_path().parent_path()
                                       / "apk";

        std::filesystem::path best_classes_dir;
        std::filesystem::file_time_type best_time{};

        if (std::filesystem::is_directory(apk_root, ec)) {
            for (auto& entry : std::filesystem::directory_iterator(apk_root, ec)) {
                if (!entry.is_directory()) continue;
                auto classes_dir = entry.path() / "classes";
                if (!std::filesystem::is_directory(classes_dir, ec)) continue;
                auto mtime = entry.last_write_time(ec);
                if (mtime > best_time) {
                    best_time = mtime;
                    best_classes_dir = classes_dir;
                }
            }
        }

        if (!best_classes_dir.empty()) {
            // Pack the .class files into a plain JAR
            std::string jar_cmd = "cd \"" + best_classes_dir.string() + "\""
                                + " && jar cf \"" + out_jar.string() + "\" .";
            std::cerr << "[HostJVM] packaging .class files -> JAR\n"
                      << "[HostJVM]   " << jar_cmd << "\n";
            int rc = std::system(jar_cmd.c_str());
            if (rc == 0 && std::filesystem::is_regular_file(out_jar, ec)) {
                std::cerr << "[HostJVM] DEX -> JAR (via .class files): "
                          << out_jar.string() << "\n";
                return out_jar;
            }
        }
    }

    // Strategy 3: Use d8 to get a classes.dex, then use dex2jar via jar tool.
    // (Informational fallback — URLClassLoader needs plain .class files.)
    std::cerr << "[HostJVM] WARNING: no DEX->class converter found.\n"
              << "[HostJVM]   Install dex-tools (brew install dex-tools) for full support.\n"
              << "[HostJVM]   Continuing without converted JAR; class loading will fail "
                 "if APK has DEX-only classes.\n";
    return {};
}


// ---------------------------------------------------------------------------
// Public: host_jvm_launch
// ---------------------------------------------------------------------------

int host_jvm_launch(const HostJvmLaunchConfig& config)
{
    // ── 1. Locate bundled libjvm ────────────────────────────────────────────
    std::filesystem::path libjvm = bundled_libjvm_path();
    if (libjvm.empty() || !std::filesystem::is_regular_file(libjvm)) {
        std::cerr << "[HostJVM] bundled libjvm not found.\n"
                  << "[HostJVM] Run: tools/setup-jdk.sh\n";
        return 1;
    }
    std::cerr << "[HostJVM] loading " << libjvm.string() << "\n";

    void* handle = dlopen(libjvm.string().c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        std::cerr << "[HostJVM] dlopen failed: " << dlerror() << "\n";
        return 1;
    }

    using JNI_CreateJavaVM_t = jint(*)(JavaVM**, void**, void*);
    auto create_jvm = reinterpret_cast<JNI_CreateJavaVM_t>(
        dlsym(handle, "JNI_CreateJavaVM"));
    if (!create_jvm) {
        std::cerr << "[HostJVM] JNI_CreateJavaVM symbol not found\n";
        dlclose(handle);
        return 1;
    }

    // ── 2.    // classpath = bootstrap_jar : apk_jar : extra_jars...
    // All paths must be absolute — JNI_CreateJavaVM requires it.
    std::vector<std::filesystem::path> cp_entries;
    auto add_to_cp = [&](const std::filesystem::path& p) {
        if (p.empty()) return;
        std::error_code ec;
        auto abs = std::filesystem::absolute(p, ec);
        if (!ec && std::filesystem::exists(abs))
            cp_entries.push_back(abs);
        else if (std::filesystem::exists(p))
            cp_entries.push_back(std::filesystem::absolute(p));
    };
    add_to_cp(config.bootstrap_jar);
    add_to_cp(config.apk_jar);
    for (const auto& e : config.extra_jars) add_to_cp(e);

    std::string classpath;
    for (size_t i = 0; i < cp_entries.size(); ++i) {
        if (i) classpath += ':';
        classpath += cp_entries[i].string();
    }

    std::cerr << "[HostJVM] classpath=" << classpath << "\n";

    // ── 3. JVM options ──────────────────────────────────────────────────────
    // java.home must point to the JDK root (parent of lib/) so that libjvm
    // can find lib/modules (JDK9+ boot image) and other support files.
    // We set both the JVM option and the JAVA_HOME env var.
    std::filesystem::path jdk_home = libjvm.parent_path().parent_path(); // lib/server -> lib -> jdk_home
    std::string opt_home = "-Djava.home=" + jdk_home.string();
    ::setenv("JAVA_HOME", jdk_home.string().c_str(), /*overwrite=*/0);
    std::cerr << "[HostJVM] java.home=" << jdk_home.string() << "\n";

    std::string opt_cp  = "-Djava.class.path=" + classpath;
    std::string opt_tmp;
    if (!config.scratch_dir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(config.scratch_dir, ec);
        opt_tmp = "-Djava.io.tmpdir=" + config.scratch_dir.string();
    }

    std::vector<JavaVMOption> jvm_options;
    auto add_opt = [&](std::string& s) {
        JavaVMOption o{};
        o.optionString = s.data();
        o.extraInfo    = nullptr;
        jvm_options.push_back(o);
    };
    add_opt(opt_home);
    add_opt(opt_cp);
    if (!opt_tmp.empty()) add_opt(opt_tmp);

    JavaVMInitArgs vm_args{};
    vm_args.version            = JNI_VERSION_1_8;
    vm_args.nOptions           = static_cast<jint>(jvm_options.size());
    vm_args.options            = jvm_options.data();
    vm_args.ignoreUnrecognized = JNI_TRUE;


    // ── 4. Create JVM ───────────────────────────────────────────────────────
    JavaVM* jvm = nullptr;
    JNIEnv* env = nullptr;
    jint rc = create_jvm(&jvm, reinterpret_cast<void**>(&env), &vm_args);
    if (rc != JNI_OK) {
        std::cerr << "[HostJVM] JNI_CreateJavaVM failed (rc=" << rc << ")\n";
        dlclose(handle);
        return 1;
    }
    std::cerr << "[HostJVM] JNI_CreateJavaVM OK\n";

    int exit_code = 0;

    // ── 5. Find ArtApkMain ──────────────────────────────────────────────────
    jclass main_class = env->FindClass("com/muplar/runtime/ArtApkMain");
    if (!main_class || env->ExceptionCheck()) {
        std::cerr << "[HostJVM] ArtApkMain class not found in classpath\n";
        env->ExceptionDescribe();
        env->ExceptionClear();
        exit_code = 1;
        goto destroy;
    }

    {
        // ── 6. Find main(String[]) ─────────────────────────────────────────
        jmethodID main_mid = env->GetStaticMethodID(
            main_class, "main", "([Ljava/lang/String;)V");
        if (!main_mid || env->ExceptionCheck()) {
            std::cerr << "[HostJVM] ArtApkMain.main([Ljava/lang/String;)V not found\n";
            env->ExceptionDescribe();
            env->ExceptionClear();
            exit_code = 1;
            goto destroy;
        }

        // ── 7. Build String[] { apkPath, packageName, launchActivity } ─────
        std::vector<std::string> args = {
            config.apk_path,
            config.package_name,
            config.launch_activity,
        };
        jobjectArray args_arr = make_string_array(env, args);
        if (!args_arr || env->ExceptionCheck()) {
            std::cerr << "[HostJVM] failed to create String[] args\n";
            env->ExceptionDescribe();
            env->ExceptionClear();
            exit_code = 1;
            goto destroy;
        }

        // ── 8. Invoke ArtApkMain.main ──────────────────────────────────────
        env->CallStaticVoidMethod(main_class, main_mid, args_arr);

        if (env->ExceptionCheck()) {
            std::cerr << "[HostJVM] ArtApkMain.main threw an exception:\n";
            env->ExceptionDescribe();
            env->ExceptionClear();
            exit_code = 1;
        } else {
            std::cerr << "[HostJVM] ArtApkMain.main returned successfully\n";
        }
    }

destroy:
    jvm->DestroyJavaVM();
    dlclose(handle);
    return exit_code;
}

} // namespace muplar::runtime::android
