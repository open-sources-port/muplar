#include <android/input.h>
#include <android/log.h>
#include <android/looper.h>
#include <android/native_activity.h>
#include <android/native_window.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define TAG "libnativeappgluecmdtest"
#define LOOPER_ID_MAIN 1001

enum {
    APP_CMD_INPUT_CHANGED = 0,
    APP_CMD_INIT_WINDOW = 1,
    APP_CMD_TERM_WINDOW = 2,
    APP_CMD_WINDOW_RESIZED = 3,
    APP_CMD_WINDOW_REDRAW_NEEDED = 4,
    APP_CMD_GAINED_FOCUS = 6,
    APP_CMD_LOST_FOCUS = 7,
    APP_CMD_START = 10,
    APP_CMD_RESUME = 11,
    APP_CMD_PAUSE = 13,
    APP_CMD_STOP = 14,
    APP_CMD_DESTROY = 15,
};

typedef struct AppCmdState {
    ANativeActivity *activity;
    AInputQueue *input_queue;
    ANativeWindow *window;
    pthread_t thread;
    int msgread;
    int msgwrite;
    int thread_started;
    int looper_ready;
    int add_fd_result;
    int poll_timeouts;
    int poll_result;
    int poll_fd;
    int poll_events;
    int poll_data_ok;
    int read_errors;
    int commands_seen;
    int input_changed;
    int init_window;
    int term_window;
    int window_resized;
    int window_redraw;
    int gained_focus;
    int lost_focus;
    int start;
    int resume;
    int pause;
    int stop;
    int destroy;
} AppCmdState;

static void record_cmd(AppCmdState *state, unsigned char cmd)
{
    state->commands_seen++;
    switch (cmd) {
    case APP_CMD_INPUT_CHANGED:
        state->input_changed++;
        break;
    case APP_CMD_INIT_WINDOW:
        state->init_window++;
        break;
    case APP_CMD_TERM_WINDOW:
        state->term_window++;
        break;
    case APP_CMD_WINDOW_RESIZED:
        state->window_resized++;
        break;
    case APP_CMD_WINDOW_REDRAW_NEEDED:
        state->window_redraw++;
        break;
    case APP_CMD_GAINED_FOCUS:
        state->gained_focus++;
        break;
    case APP_CMD_LOST_FOCUS:
        state->lost_focus++;
        break;
    case APP_CMD_START:
        state->start++;
        break;
    case APP_CMD_RESUME:
        state->resume++;
        break;
    case APP_CMD_PAUSE:
        state->pause++;
        break;
    case APP_CMD_STOP:
        state->stop++;
        break;
    case APP_CMD_DESTROY:
        state->destroy++;
        break;
    default:
        break;
    }
}

static void write_cmd(AppCmdState *state, unsigned char cmd)
{
    if (!state || state->msgwrite < 0)
        return;

    ssize_t n = write(state->msgwrite, &cmd, 1);
    __android_log_print(ANDROID_LOG_INFO, TAG,
                        "write cmd=%u n=%ld",
                        (unsigned)cmd, (long)n);
}

static void *app_thread_main(void *opaque)
{
    AppCmdState *state = (AppCmdState *)opaque;
    state->thread_started = 1;

    ALooper *looper = ALooper_prepare(0);
    state->looper_ready = looper != 0;
    state->add_fd_result = ALooper_addFd(
        looper, state->msgread, LOOPER_ID_MAIN,
        ALOOPER_EVENT_INPUT, 0, state);

    while (!state->destroy && state->commands_seen < 32) {
        int out_fd = -1;
        int out_events = 0;
        void *out_data = 0;
        int ident = ALooper_pollOnce(-1, &out_fd, &out_events, &out_data);

        state->poll_result = ident;
        state->poll_fd = out_fd;
        state->poll_events = out_events;
        state->poll_data_ok = out_data == state;

        if (ident == ALOOPER_POLL_TIMEOUT) {
            state->poll_timeouts++;
            continue;
        }
        if (ident != LOOPER_ID_MAIN || out_fd != state->msgread ||
            out_data != state) {
            state->read_errors++;
            continue;
        }

        for (;;) {
            unsigned char cmd = 0;
            ssize_t n = read(state->msgread, &cmd, 1);
            if (n <= 0)
                break;

            record_cmd(state, cmd);
            __android_log_print(ANDROID_LOG_INFO, TAG,
                                "app thread cmd=%u total=%d fd=%d events=0x%x",
                                (unsigned)cmd, state->commands_seen,
                                state->poll_fd, state->poll_events);
            if (cmd == APP_CMD_DESTROY)
                break;
        }
    }

    if (state->msgread >= 0) {
        close(state->msgread);
        state->msgread = -1;
    }

    __android_log_print(ANDROID_LOG_INFO, TAG,
                        "thread exit cmds=%d start=%d resume=%d focus=%d input=%d",
                        state->commands_seen,
                        state->start,
                        state->resume,
                        state->gained_focus,
                        state->input_changed);
    __android_log_print(ANDROID_LOG_INFO, TAG,
                        "thread window init=%d resize=%d redraw=%d term=%d",
                        state->init_window,
                        state->window_resized,
                        state->window_redraw,
                        state->term_window);
    __android_log_print(ANDROID_LOG_INFO, TAG,
                        "thread end pause=%d stop=%d destroy=%d timeouts=%d errors=%d",
                        state->pause,
                        state->stop,
                        state->destroy,
                        state->poll_timeouts,
                        state->read_errors);
    return (void *)0x5a19;
}

static void on_start(ANativeActivity *activity)
{
    write_cmd((AppCmdState *)activity->instance, APP_CMD_START);
}

static void on_resume(ANativeActivity *activity)
{
    write_cmd((AppCmdState *)activity->instance, APP_CMD_RESUME);
}

static void on_pause(ANativeActivity *activity)
{
    write_cmd((AppCmdState *)activity->instance, APP_CMD_PAUSE);
}

static void on_stop(ANativeActivity *activity)
{
    write_cmd((AppCmdState *)activity->instance, APP_CMD_STOP);
}

static void on_destroy(ANativeActivity *activity)
{
    AppCmdState *state = (AppCmdState *)activity->instance;
    write_cmd(state, APP_CMD_DESTROY);
    if (state && state->msgwrite >= 0) {
        close(state->msgwrite);
        state->msgwrite = -1;
    }
}

static void on_window_focus_changed(ANativeActivity *activity, int has_focus)
{
    write_cmd((AppCmdState *)activity->instance,
              has_focus ? APP_CMD_GAINED_FOCUS : APP_CMD_LOST_FOCUS);
}

static void on_input_queue_created(ANativeActivity *activity,
                                   AInputQueue *queue)
{
    AppCmdState *state = (AppCmdState *)activity->instance;
    if (state)
        state->input_queue = queue;
    write_cmd(state, APP_CMD_INPUT_CHANGED);
}

static void on_input_queue_destroyed(ANativeActivity *activity,
                                     AInputQueue *queue)
{
    (void)queue;
    AppCmdState *state = (AppCmdState *)activity->instance;
    if (state)
        state->input_queue = 0;
    write_cmd(state, APP_CMD_INPUT_CHANGED);
}

static void on_native_window_created(ANativeActivity *activity,
                                     ANativeWindow *window)
{
    AppCmdState *state = (AppCmdState *)activity->instance;
    if (state)
        state->window = window;
    write_cmd(state, APP_CMD_INIT_WINDOW);
}

static void on_native_window_resized(ANativeActivity *activity,
                                     ANativeWindow *window)
{
    (void)window;
    write_cmd((AppCmdState *)activity->instance, APP_CMD_WINDOW_RESIZED);
}

static void on_native_window_redraw_needed(ANativeActivity *activity,
                                           ANativeWindow *window)
{
    (void)window;
    write_cmd((AppCmdState *)activity->instance, APP_CMD_WINDOW_REDRAW_NEEDED);
}

static void on_native_window_destroyed(ANativeActivity *activity,
                                       ANativeWindow *window)
{
    (void)window;
    AppCmdState *state = (AppCmdState *)activity->instance;
    if (state)
        state->window = 0;
    write_cmd(state, APP_CMD_TERM_WINDOW);
}

void ANativeActivity_onCreate(ANativeActivity *activity,
                              void *saved_state,
                              size_t saved_state_size)
{
    (void)saved_state;
    (void)saved_state_size;

    static AppCmdState state;
    memset(&state, 0, sizeof(state));
    state.activity = activity;
    state.msgread = -1;
    state.msgwrite = -1;

    int fds[2] = { -1, -1 };
    int pipe_rc = pipe(fds);
    state.msgread = fds[0];
    state.msgwrite = fds[1];

    activity->instance = &state;
    activity->callbacks->onStart = on_start;
    activity->callbacks->onResume = on_resume;
    activity->callbacks->onPause = on_pause;
    activity->callbacks->onStop = on_stop;
    activity->callbacks->onDestroy = on_destroy;
    activity->callbacks->onWindowFocusChanged = on_window_focus_changed;
    activity->callbacks->onInputQueueCreated = on_input_queue_created;
    activity->callbacks->onInputQueueDestroyed = on_input_queue_destroyed;
    activity->callbacks->onNativeWindowCreated = on_native_window_created;
    activity->callbacks->onNativeWindowResized = on_native_window_resized;
    activity->callbacks->onNativeWindowRedrawNeeded = on_native_window_redraw_needed;
    activity->callbacks->onNativeWindowDestroyed = on_native_window_destroyed;

    int thread_rc = pipe_rc == 0
        ? pthread_create(&state.thread, 0, app_thread_main, &state)
        : -1;

    __android_log_print(ANDROID_LOG_INFO, TAG,
                        "onCreate pipe=%d read=%d write=%d pthread=%d",
                        pipe_rc, state.msgread, state.msgwrite, thread_rc);
}
