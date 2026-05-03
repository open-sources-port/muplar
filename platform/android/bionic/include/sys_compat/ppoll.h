#ifdef __APPLE__
#include <poll.h>
#include <time.h>
#include <signal.h>

static inline int ppoll(struct pollfd *fds, nfds_t nfds,
                        const struct timespec *timeout_ts, const sigset_t *sigmask) {
    (void)sigmask; // macOS doesn't support this
    int timeout_ms = -1;
    if (timeout_ts) {
        timeout_ms = (int)(timeout_ts->tv_sec * 1000 + timeout_ts->tv_nsec / 1000000);
    }
    return poll(fds, nfds, timeout_ms);
}
#endif
