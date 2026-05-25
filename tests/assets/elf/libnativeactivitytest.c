#include <android/log.h>
#include <android/native_activity.h>
#include <android/native_window.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdint.h>

typedef struct {
    int magic;
    int window_seen;
    int window_lock_ok;
    int egl_frame_ok;
    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;
} AppState;

static void on_start(ANativeActivity *activity) {
    (void)activity;
    __android_log_print(ANDROID_LOG_INFO, "libnativeactivitytest", "onStart");
}

static void on_resume(ANativeActivity *activity) {
    (void)activity;
    __android_log_print(ANDROID_LOG_INFO, "libnativeactivitytest", "onResume");
}

static void on_pause(ANativeActivity *activity) {
    (void)activity;
    __android_log_print(ANDROID_LOG_INFO, "libnativeactivitytest", "onPause");
}

static void on_stop(ANativeActivity *activity) {
    (void)activity;
    __android_log_print(ANDROID_LOG_INFO, "libnativeactivitytest", "onStop");
}

static void on_destroy(ANativeActivity *activity) {
    AppState *state = (AppState *)activity->instance;

    if (state && state->magic == 0x12345678) {
        __android_log_print(ANDROID_LOG_INFO, "libnativeactivitytest", "onDestroy state OK");
    } else {
        __android_log_print(ANDROID_LOG_ERROR, "libnativeactivitytest", "onDestroy state BAD");
    }
}

static void on_window_focus_changed(ANativeActivity *activity, int has_focus) {
    (void)activity;
    __android_log_print(ANDROID_LOG_INFO, "libnativeactivitytest",
                        "onWindowFocusChanged %d", has_focus);
}

static void render_egl_frame(AppState *state, ANativeWindow *window, int width, int height) {
    EGLint major = 0;
    EGLint minor = 0;
    EGLint num_configs = 0;
    EGLConfig config = 0;
    EGLint config_attribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };

    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY ||
        !eglInitialize(display, &major, &minor) ||
        !eglBindAPI(EGL_OPENGL_ES_API) ||
        !eglChooseConfig(display, config_attribs, &config, 1, &num_configs) ||
        num_configs < 1) {
        __android_log_print(ANDROID_LOG_ERROR, "libnativeactivitytest",
                            "egl init failed error=0x%x", eglGetError());
        return;
    }

    EGLSurface surface = eglCreateWindowSurface(display, config, window, 0);
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attribs);
    if (surface == EGL_NO_SURFACE ||
        context == EGL_NO_CONTEXT ||
        !eglMakeCurrent(display, surface, surface, context)) {
        __android_log_print(ANDROID_LOG_ERROR, "libnativeactivitytest",
                            "egl surface/context failed error=0x%x", eglGetError());
        return;
    }

    glViewport(0, 0, width, height);
    glClear(GL_COLOR_BUFFER_BIT);
    GLenum gl_error = glGetError();
    EGLBoolean swapped = eglSwapBuffers(display, surface);

    if (state) {
        state->display = display;
        state->surface = surface;
        state->context = context;
        state->egl_frame_ok = swapped && gl_error == GL_NO_ERROR;
    }

    __android_log_print(ANDROID_LOG_INFO, "libnativeactivitytest",
                        "egl frame %d.%d swap=%d glerr=0x%x",
                        major, minor, swapped ? 1 : 0, gl_error);
}

static void on_native_window_created(ANativeActivity *activity, ANativeWindow *window) {
    AppState *state = (AppState *)activity->instance;
    int width = ANativeWindow_getWidth(window);
    int height = ANativeWindow_getHeight(window);
    int format = ANativeWindow_getFormat(window);

    __android_log_print(ANDROID_LOG_INFO, "libnativeactivitytest",
                        "onNativeWindowCreated %dx%d fmt=%d",
                        width, height, format);

    ANativeWindow_acquire(window);
    ANativeWindow_setBuffersGeometry(window, 320, 240, WINDOW_FORMAT_RGBA_8888);

    ANativeWindow_Buffer buffer;
    if (ANativeWindow_lock(window, &buffer, 0) == 0 && buffer.bits) {
        uint32_t *pixels = (uint32_t *)buffer.bits;
        for (int y = 0; y < buffer.height; ++y) {
            for (int x = 0; x < buffer.width; ++x) {
                uint32_t r = (uint32_t)((x * 255) / buffer.width);
                uint32_t g = (uint32_t)((y * 255) / buffer.height);
                pixels[y * buffer.stride + x] =
                    0xff000000u | (r << 16) | (g << 8) | 0x66u;
            }
        }
        if (state) {
            state->window_seen = 1;
            state->window_lock_ok =
                buffer.width == 320 && buffer.height == 240 && buffer.stride >= 320;
        }
        __android_log_print(ANDROID_LOG_INFO, "libnativeactivitytest",
                            "window lock %dx%d stride=%d fmt=%d",
                            buffer.width, buffer.height, buffer.stride, buffer.format);
        ANativeWindow_unlockAndPost(window);
    } else if (state) {
        state->window_seen = 1;
        state->window_lock_ok = 0;
    }

    render_egl_frame(state, window, 320, 240);
    ANativeWindow_release(window);
}

static void on_native_window_resized(ANativeActivity *activity, ANativeWindow *window) {
    (void)activity;
    __android_log_print(ANDROID_LOG_INFO, "libnativeactivitytest",
                        "onNativeWindowResized %dx%d",
                        ANativeWindow_getWidth(window),
                        ANativeWindow_getHeight(window));
}

static void on_native_window_redraw_needed(ANativeActivity *activity, ANativeWindow *window) {
    (void)activity;
    (void)window;
    __android_log_print(ANDROID_LOG_INFO, "libnativeactivitytest",
                        "onNativeWindowRedrawNeeded");
}

static void on_native_window_destroyed(ANativeActivity *activity, ANativeWindow *window) {
    AppState *state = (AppState *)activity->instance;
    (void)window;
    if (state && state->display != EGL_NO_DISPLAY) {
        eglMakeCurrent(state->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (state->surface != EGL_NO_SURFACE)
            eglDestroySurface(state->display, state->surface);
        if (state->context != EGL_NO_CONTEXT)
            eglDestroyContext(state->display, state->context);
        eglTerminate(state->display);
        state->display = EGL_NO_DISPLAY;
        state->surface = EGL_NO_SURFACE;
        state->context = EGL_NO_CONTEXT;
    }
    __android_log_print(ANDROID_LOG_INFO, "libnativeactivitytest",
                        "onNativeWindowDestroyed seen=%d lock=%d egl=%d",
                        state ? state->window_seen : 0,
                        state ? state->window_lock_ok : 0,
                        state ? state->egl_frame_ok : 0);
}

void ANativeActivity_onCreate(ANativeActivity *activity, void *saved_state, size_t saved_state_size) {
    (void)saved_state;
    (void)saved_state_size;

    static AppState state;

    if (!activity || !activity->callbacks || !activity->vm || !activity->env) {
        __android_log_print(ANDROID_LOG_ERROR, "libnativeactivitytest", "invalid NativeActivity bootstrap");
        return;
    }

    state.magic = 0x12345678;
    state.window_seen = 0;
    state.window_lock_ok = 0;
    state.egl_frame_ok = 0;
    state.display = EGL_NO_DISPLAY;
    state.surface = EGL_NO_SURFACE;
    state.context = EGL_NO_CONTEXT;
    activity->instance = &state;

    activity->callbacks->onStart = on_start;
    activity->callbacks->onResume = on_resume;
    activity->callbacks->onPause = on_pause;
    activity->callbacks->onStop = on_stop;
    activity->callbacks->onDestroy = on_destroy;
    activity->callbacks->onWindowFocusChanged = on_window_focus_changed;
    activity->callbacks->onNativeWindowCreated = on_native_window_created;
    activity->callbacks->onNativeWindowResized = on_native_window_resized;
    activity->callbacks->onNativeWindowRedrawNeeded = on_native_window_redraw_needed;
    activity->callbacks->onNativeWindowDestroyed = on_native_window_destroyed;

    __android_log_print(ANDROID_LOG_INFO, "libnativeactivitytest", "ANativeActivity_onCreate ok sdk=%d data=%s", activity->sdkVersion, activity->internalDataPath ? activity->internalDataPath : "");
}
