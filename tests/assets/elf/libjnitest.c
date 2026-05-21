// libjnitest.c — minimal JNI .so to test JNI_OnLoad + RegisterNatives
#include <jni.h>
#include <android/log.h>

#define TAG "libjnitest"

static jint native_add(JNIEnv* env, jobject thiz, jint a, jint b) {
    __android_log_print(ANDROID_LOG_DEBUG, TAG, "native_add(%d, %d)", a, b);
    return a + b;
}

static JNINativeMethod methods[] = {
    { "nativeAdd", "(II)I", (void*)native_add },
};

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    JNIEnv* env = NULL;
    if ((*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        return -1;
    }
    jclass clazz = (*env)->FindClass(env, "com/example/Muplar");
    if (!clazz) return -1;
    (*env)->RegisterNatives(env, clazz, methods, 1);
    return JNI_VERSION_1_6;
}