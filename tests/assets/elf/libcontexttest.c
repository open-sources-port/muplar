#include <android/log.h>
#include <android/native_activity.h>
#include <jni.h>

static int g_context_ok;

static int streq(const char* a, const char* b)
{
    if (!a || !b)
        return 0;
    while (*a && *b && *a == *b) {
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

static int contains(const char* value, const char* needle)
{
    if (!value || !needle)
        return 0;
    if (*needle == '\0')
        return 1;
    for (const char* p = value; *p; ++p) {
        const char* a = p;
        const char* b = needle;
        while (*a && *b && *a == *b) {
            ++a;
            ++b;
        }
        if (*b == '\0')
            return 1;
    }
    return 0;
}

static void copy_string(char* dst, int dst_size, const char* src)
{
    if (!dst || dst_size <= 0)
        return;
    int i = 0;
    if (src) {
        for (; i + 1 < dst_size && src[i]; ++i)
            dst[i] = src[i];
    }
    dst[i] = '\0';
}

static void on_destroy(ANativeActivity* activity)
{
    (void)activity;
    __android_log_print(ANDROID_LOG_INFO, "libcontexttest",
                        "onDestroy context=%d", g_context_ok);
}

void ANativeActivity_onCreate(ANativeActivity* activity,
                              void* saved_state,
                              size_t saved_state_size)
{
    (void)saved_state;
    (void)saved_state_size;

    const char* expected_package = "com.example.muplar.context";
    char package_name[128];
    int paths_ok = 0;
    int java_ok = 0;
    for (int i = 0; i < (int)sizeof(package_name); ++i)
        package_name[i] = '\0';

    if (!activity || !activity->callbacks || !activity->env || !activity->clazz) {
        __android_log_print(ANDROID_LOG_ERROR, "libcontexttest",
                            "invalid context bootstrap");
        return;
    }

    paths_ok =
        contains(activity->internalDataPath, expected_package) &&
        contains(activity->externalDataPath, expected_package) &&
        contains(activity->obbPath, expected_package);

    JNIEnv* env = activity->env;
    jclass cls = (*env)->GetObjectClass(env, activity->clazz);
    jmethodID get_package_name =
        (*env)->GetMethodID(env, cls, "getPackageName", "()Ljava/lang/String;");
    jstring package_string =
        (jstring)(*env)->CallObjectMethod(env, activity->clazz, get_package_name);
    const char* chars = (*env)->GetStringUTFChars(env, package_string, 0);
    if (chars) {
        copy_string(package_name, (int)sizeof(package_name), chars);
        java_ok = streq(chars, expected_package);
        (*env)->ReleaseStringUTFChars(env, package_string, chars);
    }

    g_context_ok = paths_ok && java_ok;
    activity->callbacks->onDestroy = on_destroy;

    __android_log_print(ANDROID_LOG_INFO, "libcontexttest",
                        "onCreate package=%s paths=%d java=%d ok=%d data=%s",
                        package_name,
                        paths_ok,
                        java_ok,
                        g_context_ok,
                        activity->internalDataPath ? activity->internalDataPath : "");
}
