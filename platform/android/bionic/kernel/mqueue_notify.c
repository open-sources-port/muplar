#include "linux/mqueue_shm.h"
#include "linux/mqueue.h"
#include <sys/event.h>
#include <unistd.h>

void mq_notify_trigger(struct mq_shared *shm) {
    if (!shm->notify_enabled)
        return;

    int kq = kqueue();
    if (kq < 0)
        return;

    struct kevent kev;
    EV_SET(&kev,
           shm->notify_pid,
           EVFILT_USER,
           EV_ADD | EV_ONESHOT,
           NOTE_TRIGGER,
           0,
           NULL);

    kevent(kq, &kev, 1, NULL, 0, NULL);
    close(kq);

    shm->notify_enabled = 0;
}

