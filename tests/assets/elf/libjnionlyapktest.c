#include <jni.h>
#include <android/log.h>
#include <sys/system_properties.h>

#define TAG "libjnionlyapktest"

static jint native_probe(JNIEnv* env, jclass clazz)
{
    (void)env;
    (void)clazz;
    return 7;
}

static void trigger_unaligned_zero_vector_pattern(void)
{
    volatile unsigned char scratch[160];
    scratch[0] = 0;
    scratch[sizeof(scratch) - 1] = 0;

    __asm__ volatile(
        "movi v0.2d, #0\n"
        "stur q0, [sp, #0x5c]\n"
        "stp q0, q0, [sp, #0x40]\n"
        "stp q0, q0, [sp, #0x20]\n"
        "str q0, [sp, #0x10]\n"
        :
        :
        : "v0", "memory");
}

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* reserved)
{
    (void)reserved;

    JNIEnv* env = 0;
    if ((*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6) != JNI_OK)
        return JNI_ERR;

    jclass path_class = (*env)->FindClass(env, "android/graphics/Path");
    if (!path_class)
        return JNI_ERR;

    jfieldID native_path =
        (*env)->GetFieldID(env, path_class, "mNativePath", "J");
    if (!native_path)
        return JNI_ERR;

    trigger_unaligned_zero_vector_pattern();

    char sdk[PROP_VALUE_MAX] = {0};
    __system_property_get("ro.build.version.sdk", sdk);

    jclass test_class = (*env)->FindClass(env, "com/example/JniOnlyApk");
    if (!test_class)
        return JNI_ERR;

    static JNINativeMethod methods[] = {
        {"nativeProbe", "()I", (void*)native_probe},
    };
    if ((*env)->RegisterNatives(env, test_class, methods, 1) != JNI_OK)
        return JNI_ERR;

    __android_log_print(ANDROID_LOG_INFO, TAG, "JNI_OnLoad-only ok sdk=%s", sdk);
    return JNI_VERSION_1_6;
}
