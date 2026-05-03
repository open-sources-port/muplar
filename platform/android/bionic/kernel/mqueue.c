#include "linux/mqueue.h"
#include "linux/mqueue_shm.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys_compat/muplar_errno.h>
#include <stdio.h>
#include <string.h>

#define MAX_MQUEUE 64

struct mq_handle {
    char name[MQ_NAME_MAX];
    struct mq_shared *shm;
};

static struct mq_handle mq_table[MAX_MQUEUE];
static int mq_used[MAX_MQUEUE];

extern void mq_notify_trigger(struct mq_shared *shm);

static int alloc_slot(void) {
    for (int i = 0; i < MAX_MQUEUE; i++) {
        if (!mq_used[i]) {
            mq_used[i] = 1;
            return i;
        }
    }
    return -1;
}

int mq_open(const char *name, int oflag, ...) {
    (void)oflag;

    int slot = alloc_slot();
    if (slot < 0) {
        errno = EMFILE;
        return -1;
    }

    int fd = shm_open(name, O_CREAT | O_RDWR, 0600);
    if (fd < 0)
        goto fail;

    ftruncate(fd, sizeof(struct mq_shared));

    struct mq_shared *shm = mmap(NULL, sizeof(*shm),
                                 PROT_READ | PROT_WRITE,
                                 MAP_SHARED, fd, 0);
    close(fd);

    if (shm == MAP_FAILED)
        goto fail;

    pthread_mutexattr_t mattr;
    pthread_mutexattr_init(&mattr);
    pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);

    pthread_mutex_init(&shm->lock, &mattr);

    shm->head = shm->tail = shm->count = 0;
    shm->msgsize = MQ_MAX_MSGSIZE;
    shm->maxmsg = MQ_MAX_MSG;
    shm->notify_enabled = 0;

    snprintf(mq_table[slot].name, sizeof(mq_table[slot].name), "%s", name);
    mq_table[slot].shm = shm;

    return slot;

fail:
    mq_used[slot] = 0;
    return -1;
}

int mq_close(mqd_t mqdes) {
    if (mqdes < 0 || mqdes >= MAX_MQUEUE || !mq_used[mqdes]) {
        errno = EBADF;
        return -1;
    }

    munmap(mq_table[mqdes].shm, sizeof(struct mq_shared));
    mq_used[mqdes] = 0;
    return 0;
}

int mq_unlink(const char *name) {
    return shm_unlink(name);
}

int mq_getattr(mqd_t mqdes, struct mq_attr *attr) {
    if (!attr || mqdes < 0 || mqdes >= MAX_MQUEUE || !mq_used[mqdes]) {
        errno = EBADF;
        return -1;
    }

    struct mq_shared *shm = mq_table[mqdes].shm;

    pthread_mutex_lock(&shm->lock);
    attr->mq_flags = 0;
    attr->mq_maxmsg = shm->maxmsg;
    attr->mq_msgsize = shm->msgsize;
    attr->mq_curmsgs = shm->count;
    pthread_mutex_unlock(&shm->lock);

    return 0;
}

int mq_setattr(mqd_t mqdes,
               const struct mq_attr *newattr,
               struct mq_attr *oldattr) {
    (void) newattr;
    if (mqdes < 0 || mqdes >= MAX_MQUEUE || !mq_used[mqdes]) {
        errno = EBADF;
        return -1;
    }

    if (oldattr)
        mq_getattr(mqdes, oldattr);

    /* Ignore newattr->mq_flags for now */

    return 0;
}

int mq_send(mqd_t mqdes, const char *msg, size_t len, unsigned prio) {
    (void)prio;

    if (mqdes < 0 || mqdes >= MAX_MQUEUE || !mq_used[mqdes]) {
        errno = EBADF;
        return -1;
    }

    struct mq_shared *shm = mq_table[mqdes].shm;

    pthread_mutex_lock(&shm->lock);

    if (shm->count >= shm->maxmsg || len > shm->msgsize) {
        pthread_mutex_unlock(&shm->lock);
        errno = EMSGSIZE;
        return -1;
    }

    memcpy(shm->messages[shm->tail], msg, len);
    shm->tail = (shm->tail + 1) % shm->maxmsg;
    shm->count++;

    int notify = (shm->count == 1 && shm->notify_enabled);
    pthread_mutex_unlock(&shm->lock);

    if (notify)
        mq_notify_trigger(shm);

    return 0;
}

ssize_t mq_receive(mqd_t mqdes, char *msg, size_t len, unsigned *prio) {
    (void)prio;

    if (mqdes < 0 || mqdes >= MAX_MQUEUE || !mq_used[mqdes]) {
        errno = EBADF;
        return -1;
    }

    struct mq_shared *shm = mq_table[mqdes].shm;

    pthread_mutex_lock(&shm->lock);

    if (shm->count == 0) {
        pthread_mutex_unlock(&shm->lock);
        errno = EAGAIN;
        return -1;
    }

    size_t copy = shm->msgsize < len ? shm->msgsize : len;
    memcpy(msg, shm->messages[shm->head], copy);

    shm->head = (shm->head + 1) % shm->maxmsg;
    shm->count--;

    pthread_mutex_unlock(&shm->lock);
    return copy;
}

int mq_notify(mqd_t mqdes, const struct sigevent *sev) {
    (void)sev;

    if (mqdes < 0 || mqdes >= MAX_MQUEUE || !mq_used[mqdes]) {
        errno = EBADF;
        return -1;
    }

    struct mq_shared *shm = mq_table[mqdes].shm;

    pthread_mutex_lock(&shm->lock);

    if (shm->notify_enabled) {
        pthread_mutex_unlock(&shm->lock);
        errno = EBUSY;
        return -1;
    }

    shm->notify_pid = getpid();
    shm->notify_enabled = 1;

    pthread_mutex_unlock(&shm->lock);
    return 0;
}
