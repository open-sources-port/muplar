#pragma once

#include <sys/types.h>
#include <signal.h>
#include <stddef.h>
#include <linux/types.h>

#ifdef __APPLE__

struct mq_attr {
    long mq_flags;
    long mq_maxmsg;
    long mq_msgsize;
    long mq_curmsgs;
};

int mq_open(const char *name, int oflag, ...);
int mq_close(mqd_t mqdes);
int mq_unlink(const char *name);

int mq_getattr(mqd_t mqdes, struct mq_attr *attr);
int mq_setattr(mqd_t mqdes,
               const struct mq_attr *newattr,
               struct mq_attr *oldattr);

int mq_send(mqd_t mqdes, const char *msg, size_t len, unsigned prio);
ssize_t mq_receive(mqd_t mqdes, char *msg, size_t len, unsigned *prio);

int mq_notify(mqd_t mqdes, const struct sigevent *sev);

#endif
