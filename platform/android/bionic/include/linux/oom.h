#ifndef __linux__
struct oom_control { int dummy; };

static inline int oom_kill_process(struct oom_control *oc) {
    return 0; // no-op on macOS
}
#endif
