#pragma once

#ifndef HAVE_THREADS_H
#include <pthread.h>
#include <time.h>

typedef pthread_t thrd_t;
typedef void *(*thrd_start_t)(void*);

static inline int thrd_create(thrd_t *thr, thrd_start_t func, void *arg) {
    return pthread_create(thr, NULL, func, arg) == 0 ? 0 : -1;
}

static inline void thrd_yield(void) {
    sched_yield();
}

typedef pthread_mutex_t mtx_t;
static inline int mtx_init(mtx_t *mtx, int type) {
    return pthread_mutex_init(mtx, NULL);
}
static inline void mtx_destroy(mtx_t *mtx) {
    pthread_mutex_destroy(mtx);
}
static inline int mtx_lock(mtx_t *mtx) {
    return pthread_mutex_lock(mtx);
}
static inline int mtx_unlock(mtx_t *mtx) {
    return pthread_mutex_unlock(mtx);
}

typedef pthread_cond_t cnd_t;
static inline int cnd_init(cnd_t *c) {
    return pthread_cond_init(c, NULL);
}
static inline void cnd_destroy(cnd_t *c) {
    pthread_cond_destroy(c);
}
static inline int cnd_wait(cnd_t *c, mtx_t *mtx) {
    return pthread_cond_wait(c, mtx);
}
static inline int cnd_signal(cnd_t *c) {
    return pthread_cond_signal(c);
}
static inline int cnd_broadcast(cnd_t *c) {
    return pthread_cond_broadcast(c);
}

#endif /* HAVE_THREADS_H */
