#include <android/log.h>
#include <android/choreographer.h>
#include <android/input.h>
#include <android/looper.h>
#include <android/native_activity.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>

#define TAG "libnativegluethreadtest"

typedef struct GlueState {
    ANativeActivity *activity;
    AInputQueue *input_queue;
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int thread_started;
    int looper_ready;
    int looper_add_result;
    int poll_result;
    int poll_fd;
    int poll_events;
    int poll_data_ok;
    int looper_callback_called;
    int looper_callback_fd;
    int looper_callback_events;
    int looper_callback_data_ok;
    int input_created;
    int input_destroyed;
    int input_attached;
    int input_poll_count;
    int input_event_count;
    int input_last_ident;
    int input_last_fd;
    int input_last_events;
    int input_last_data_ok;
    int input_last_type;
    int input_last_action;
    int input_last_source;
    int input_has_after;
    int choreographer_ready;
    int frame_count;
    int frame_data_ok;
    int64_t first_frame_time;
    int64_t last_frame_time;
    uintptr_t thread_ret;
} GlueState;

static int looper_callback(int fd, int events, void *data)
{
    GlueState *state = (GlueState *)data;
    if (state) {
        state->looper_callback_called = 1;
        state->looper_callback_fd = fd;
        state->looper_callback_events = events;
        state->looper_callback_data_ok = data == state;
    }

    __android_log_print(ANDROID_LOG_INFO, TAG,
                        "looper callback fd=%d events=0x%x data_ok=%d",
                        fd, events, state ? state->looper_callback_data_ok : 0);
    return 1;
}

static void *glue_thread_main(void *opaque)
{
    GlueState *state = (GlueState *)opaque;
    int out_fd = -1;
    int out_events = 0;
    void *out_data = 0;

    state->thread_started = 1;

    ALooper *looper = ALooper_prepare(0);
    state->looper_ready = looper != 0;
    state->looper_add_result = ALooper_addFd(
        looper, 77, 123, ALOOPER_EVENT_INPUT, looper_callback, state);
    state->poll_result = ALooper_pollOnce(0, &out_fd, &out_events, &out_data);
    state->poll_fd = out_fd;
    state->poll_events = out_events;
    state->poll_data_ok = out_data == state;

    pthread_mutex_lock(&state->mutex);
    pthread_cond_signal(&state->cond);
    pthread_mutex_unlock(&state->mutex);

    __android_log_print(ANDROID_LOG_INFO, TAG,
                        "thread started looper=%p add=%d poll=%d out_fd=%d out_events=0x%x",
                        looper, state->looper_add_result,
                        state->poll_result, state->poll_fd,
                        state->poll_events);
    __android_log_print(ANDROID_LOG_INFO, TAG,
                        "thread poll data_ok=%d callback=%d",
                        state->poll_data_ok, state->looper_callback_called);
    return (void *)0x5a17;
}

static void on_start(ANativeActivity *activity)
{
    GlueState *state = (GlueState *)activity->instance;
    __android_log_print(ANDROID_LOG_INFO, TAG,
                        "onStart thread_started=%d looper=%d callback=%d poll=%d",
                        state ? state->thread_started : -1,
                        state ? state->looper_ready : -1,
                        state ? state->looper_callback_called : -1,
                        state ? state->poll_result : -1);
}

static void frame_callback(int64_t frameTimeNanos, void *data)
{
    GlueState *state = (GlueState *)data;
    if (!state)
        return;

    state->frame_count++;
    if (!state->first_frame_time)
        state->first_frame_time = frameTimeNanos;
    state->last_frame_time = frameTimeNanos;
    state->frame_data_ok = data == state;

    __android_log_print(ANDROID_LOG_INFO, TAG,
                        "frame callback count=%d time=%lld data_ok=%d",
                        state->frame_count,
                        (long long)frameTimeNanos,
                        state->frame_data_ok);

    if (state->frame_count < 2) {
        AChoreographer_postFrameCallback64(
            AChoreographer_getInstance(), frame_callback, state);
    }
}

static void on_resume(ANativeActivity *activity)
{
    GlueState *state = (GlueState *)activity->instance;
    if (!state)
        return;

    AChoreographer *choreographer = AChoreographer_getInstance();
    state->choreographer_ready = choreographer != 0;
    AChoreographer_postFrameCallback64(choreographer, frame_callback, state);

    __android_log_print(ANDROID_LOG_INFO, TAG,
                        "onResume choreographer=%d",
                        state->choreographer_ready);
}

static void poll_input_once(GlueState *state, AInputQueue *queue)
{
    int out_fd = -1;
    int out_events = 0;
    void *out_data = 0;
    AInputEvent *event = 0;

    int ident = ALooper_pollOnce(0, &out_fd, &out_events, &out_data);
    state->input_poll_count++;
    state->input_last_ident = ident;
    state->input_last_fd = out_fd;
    state->input_last_events = out_events;
    state->input_last_data_ok = out_data == state;

    int has_before = AInputQueue_hasEvents(queue);
    __android_log_print(ANDROID_LOG_INFO, TAG,
                        "input poll ident=%d fd=%d events=0x%x data_ok=%d has=%d",
                        ident, out_fd, out_events,
                        state->input_last_data_ok, has_before);

    if (ident != 234 || !state->input_last_data_ok)
        return;

    if (AInputQueue_getEvent(queue, &event) < 0 || !event)
        return;

    state->input_last_type = AInputEvent_getType(event);
    state->input_last_source = AInputEvent_getSource(event);
    if (state->input_last_type == AINPUT_EVENT_TYPE_MOTION)
        state->input_last_action = AMotionEvent_getAction(event);
    else
        state->input_last_action = AKeyEvent_getAction(event);

    int pre = AInputQueue_preDispatchEvent(queue, event);
    AInputQueue_finishEvent(queue, event, 1);
    state->input_event_count++;
    state->input_has_after = AInputQueue_hasEvents(queue);

    __android_log_print(ANDROID_LOG_INFO, TAG,
                        "input event type=%d action=%d source=0x%x pre=%d count=%d",
                        state->input_last_type, state->input_last_action,
                        state->input_last_source, pre,
                        state->input_event_count);
}

static void on_input_queue_created(ANativeActivity *activity,
                                   AInputQueue *queue)
{
    GlueState *state = (GlueState *)activity->instance;
    if (!state)
        return;

    state->input_queue = queue;
    state->input_created = 1;

    ALooper *looper = ALooper_prepare(0);
    AInputQueue_attachLooper(queue, looper, 234, 0, state);
    state->input_attached = 1;

    poll_input_once(state, queue);
    poll_input_once(state, queue);

    __android_log_print(ANDROID_LOG_INFO, TAG,
                        "input created attached=%d polls=%d events=%d last_action=%d has_after=%d",
                        state->input_attached, state->input_poll_count,
                        state->input_event_count, state->input_last_action,
                        state->input_has_after);
}

static void on_input_queue_destroyed(ANativeActivity *activity,
                                     AInputQueue *queue)
{
    GlueState *state = (GlueState *)activity->instance;
    if (!state)
        return;

    AInputQueue_detachLooper(queue);
    state->input_destroyed = 1;
    __android_log_print(ANDROID_LOG_INFO, TAG,
                        "input destroyed events=%d polls=%d has_after=%d",
                        state->input_event_count, state->input_poll_count,
                        state->input_has_after);
}

static void on_destroy(ANativeActivity *activity)
{
    GlueState *state = (GlueState *)activity->instance;
    void *ret = 0;

    if (state) {
        pthread_join(state->thread, &ret);
        state->thread_ret = (uintptr_t)ret;
    }

    __android_log_print(ANDROID_LOG_INFO, TAG,
                        "onDestroy thread_started=%d looper=%d callback=%d fd=%d events=0x%x",
                        state ? state->thread_started : -1,
                        state ? state->looper_ready : -1,
                        state ? state->looper_callback_called : -1,
                        state ? state->looper_callback_fd : -1,
                        state ? state->looper_callback_events : 0);
    __android_log_print(ANDROID_LOG_INFO, TAG,
                        "onDestroy data_ok=%d ret=0x%lx",
                        state ? state->looper_callback_data_ok : 0,
                        state ? (unsigned long)state->thread_ret : 0UL);
    __android_log_print(ANDROID_LOG_INFO, TAG,
                        "onDestroy input_created=%d destroyed=%d events=%d last_action=%d",
                        state ? state->input_created : -1,
                        state ? state->input_destroyed : -1,
                        state ? state->input_event_count : -1,
                        state ? state->input_last_action : -1);
    __android_log_print(ANDROID_LOG_INFO, TAG,
                        "onDestroy frames=%d data_ok=%d first=%lld last=%lld",
                        state ? state->frame_count : -1,
                        state ? state->frame_data_ok : -1,
                        state ? (long long)state->first_frame_time : 0LL,
                        state ? (long long)state->last_frame_time : 0LL);
}

void ANativeActivity_onCreate(ANativeActivity *activity,
                              void *savedState,
                              size_t savedStateSize)
{
    (void)savedState;
    (void)savedStateSize;

    static GlueState state;
    memset(&state, 0, sizeof(state));
    state.activity = activity;

    pthread_mutex_init(&state.mutex, 0);
    pthread_cond_init(&state.cond, 0);

    activity->instance = &state;
    activity->callbacks->onStart = on_start;
    activity->callbacks->onResume = on_resume;
    activity->callbacks->onDestroy = on_destroy;
    activity->callbacks->onInputQueueCreated = on_input_queue_created;
    activity->callbacks->onInputQueueDestroyed = on_input_queue_destroyed;

    int rc = pthread_create(&state.thread, 0, glue_thread_main, &state);
    __android_log_print(ANDROID_LOG_INFO, TAG,
                        "onCreate pthread_create rc=%d", rc);
}
