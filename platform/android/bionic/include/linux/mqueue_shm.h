#pragma once

#include <pthread.h>
#include <sys/types.h>

#define MQ_NAME_MAX      64
#define MQ_MAX_MSG       32
#define MQ_MAX_MSGSIZE   1024

struct mq_shared {
    pthread_mutex_t lock;

    size_t head;
    size_t tail;
    size_t count;

    size_t msgsize;
    size_t maxmsg;

    pid_t notify_pid;
    int notify_enabled;

    char messages[MQ_MAX_MSG][MQ_MAX_MSGSIZE];
};

void mq_notify_trigger(struct mq_shared *shm);
