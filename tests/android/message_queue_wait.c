/*
 * Regression test for the ART shim's idle message queues.
 * Build from the repository root with the Android NDK compiler
 *
 * "$ANDROID_NDK_CLANG" tests/android/message_queue_wait.c \
 *   build/sysroot/data/local/tmp/muplar/art/libmuplar_android_art_shim.so \
 *   -Wl,-rpath,/data/local/tmp/muplar/art -o build/tests/message_queue_wait
 *
 * Run with elfuse and an Android sysroot containing the rebuilt shim
 *
 * third_party/elfuse/build/elfuse --sysroot "$ANDROID_SYSROOT" \
 *   --env LD_LIBRARY_PATH=/data/local/tmp/muplar/art:/apex/com.android.runtime/lib64/bionic:/system/lib64 \
 *   build/tests/message_queue_wait
 */
#include <assert.h>
#include <jni.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

extern jlong Java_android_os_MessageQueue_nativeInit(JNIEnv *, jclass);
extern void Java_android_os_MessageQueue_nativeDestroy(JNIEnv *, jclass, jlong);
extern void Java_android_os_MessageQueue_nativePollOnce(JNIEnv *, jclass, jlong, jint);
extern void Java_android_os_MessageQueue_nativeWake(JNIEnv *, jclass, jlong);
extern jboolean Java_android_os_MessageQueue_nativeIsPolling(JNIEnv *, jclass, jlong);

static double monotonic_ms(void)
{
    struct timespec now;
    assert(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
    return now.tv_sec * 1000.0 + now.tv_nsec / 1000000.0;
}

static void *wait_for_wake(void *arg)
{
    jlong queue = *(jlong *) arg;
    Java_android_os_MessageQueue_nativePollOnce(NULL, NULL, queue, -1);
    return NULL;
}

int main(void)
{
    // Fail a lost-wakeup regression promptly instead of hanging the suite.
    alarm(10);
    jlong first = Java_android_os_MessageQueue_nativeInit(NULL, NULL);
    jlong second = Java_android_os_MessageQueue_nativeInit(NULL, NULL);
    assert(first && second && first != second);
    double before = monotonic_ms();
    Java_android_os_MessageQueue_nativePollOnce(NULL, NULL, first, 80);
    assert(monotonic_ms() - before >= 60);

    before = monotonic_ms();
    Java_android_os_MessageQueue_nativePollOnce(NULL, NULL, first, 0);
    assert(monotonic_ms() - before < 500);

    // Wakes queued before polling must be retained and drained together.
    Java_android_os_MessageQueue_nativeWake(NULL, NULL, first);
    Java_android_os_MessageQueue_nativeWake(NULL, NULL, first);
    before = monotonic_ms();
    Java_android_os_MessageQueue_nativePollOnce(NULL, NULL, first, -1);
    assert(monotonic_ms() - before < 500);
    before = monotonic_ms();
    Java_android_os_MessageQueue_nativePollOnce(NULL, NULL, first, 80);
    assert(monotonic_ms() - before >= 60);

    for (int i = 0; i < 20; ++i) {
        pthread_t waiter;
        assert(pthread_create(&waiter, NULL, wait_for_wake, &first) == 0);
        double deadline = monotonic_ms() + 1000;
        while (!Java_android_os_MessageQueue_nativeIsPolling(NULL, NULL, first)) {
            assert(monotonic_ms() < deadline);
            usleep(1000);
        }
        assert(!Java_android_os_MessageQueue_nativeIsPolling(NULL, NULL, second));
        Java_android_os_MessageQueue_nativeWake(NULL, NULL, first);
        assert(pthread_join(waiter, NULL) == 0);
        assert(!Java_android_os_MessageQueue_nativeIsPolling(NULL, NULL, first));
    }

    Java_android_os_MessageQueue_nativeWake(NULL, NULL, second);
    before = monotonic_ms();
    Java_android_os_MessageQueue_nativePollOnce(NULL, NULL, first, 80);
    assert(monotonic_ms() - before >= 60);
    Java_android_os_MessageQueue_nativePollOnce(NULL, NULL, second, -1);
    Java_android_os_MessageQueue_nativeDestroy(NULL, NULL, first);
    Java_android_os_MessageQueue_nativeDestroy(NULL, NULL, second);
    puts("message queue timeout, pending wake, cross-thread wake and isolation passed");
    return 0;
}
