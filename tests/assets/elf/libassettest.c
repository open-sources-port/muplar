#include <android/asset_manager.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_asset_ok;

static void on_destroy(ANativeActivity* activity)
{
    (void)activity;
    __android_log_print(ANDROID_LOG_INFO, "libassettest",
                        "onDestroy asset=%d", g_asset_ok);
}

void ANativeActivity_onCreate(ANativeActivity* activity,
                              void* saved_state,
                              size_t saved_state_size)
{
    (void)saved_state;
    (void)saved_state_size;

    const char* expected = "hello asset from apk";
    char buffer[64] = {};

    if (!activity || !activity->callbacks || !activity->assetManager) {
        __android_log_print(ANDROID_LOG_ERROR, "libassettest",
                            "invalid asset bootstrap");
        return;
    }

    AAsset* asset = AAssetManager_open(activity->assetManager,
                                       "hello.txt",
                                       AASSET_MODE_BUFFER);
    if (!asset) {
        __android_log_print(ANDROID_LOG_ERROR, "libassettest",
                            "asset open failed");
        return;
    }

    off_t length = AAsset_getLength(asset);
    int read_len = AAsset_read(asset, buffer, sizeof(buffer) - 1);
    if (read_len < 0)
        read_len = 0;
    buffer[read_len] = '\0';
    off_t remaining = AAsset_getRemainingLength(asset);
    AAsset_close(asset);

    g_asset_ok =
        length == (off_t)strlen(expected) &&
        read_len == (int)strlen(expected) &&
        remaining == 0 &&
        strcmp(buffer, expected) == 0;

    activity->callbacks->onDestroy = on_destroy;

    __android_log_print(ANDROID_LOG_INFO, "libassettest",
                        "onCreate asset len=%lld read=%d remaining=%lld text=%s ok=%d",
                        (long long)length,
                        read_len,
                        (long long)remaining,
                        buffer,
                        g_asset_ok);
}
