#include <android/native_activity.h>
#include <stdint.h>

extern void muplar_log(const char *tag, const char *msg);

typedef struct {
    int magic;
} AppState;

static void on_start(ANativeActivity *activity) {
    muplar_log("native-activity", "onStart");
}

static void on_resume(ANativeActivity *activity) {
    muplar_log("native-activity", "onResume");
}

static void on_pause(ANativeActivity *activity) {
    muplar_log("native-activity", "onPause");
}

static void on_stop(ANativeActivity *activity) {
    muplar_log("native-activity", "onStop");
}

static void on_destroy(ANativeActivity *activity) {
    AppState *state = (AppState *) activity->instance;

    if (state && state->magic == 0x12345678) {
        muplar_log("native-activity", "onDestroy state OK");
    } else {
        muplar_log("native-activity", "onDestroy state BAD");
    }
}

void ANativeActivity_onCreate(ANativeActivity *activity,
                              void *savedState,
                              size_t savedStateSize) {
    muplar_log("native-activity", "onCreate");

    static AppState state;

    state.magic = 0x12345678;

    activity->instance = &state;

    activity->callbacks->onStart = on_start;
    activity->callbacks->onResume = on_resume;
    activity->callbacks->onPause = on_pause;
    activity->callbacks->onStop = on_stop;
    activity->callbacks->onDestroy = on_destroy;
}
