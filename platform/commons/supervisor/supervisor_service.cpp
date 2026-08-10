// platform/commons/supervisor/supervisor_service.cpp
//
// SupervisorService, WaylandGuard, WineServerGuard implementation.
//
// kqueue EVFILT_PROC + NOTE_EXIT gives us instant notification when a child
// exits — no polling, no waitpid loop. The monitor thread blocks on kevent()
// until the watched pid exits, then applies the restart policy and re-spawns.

#include "supervisor_service.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <thread>

#include <fcntl.h>
#include <signal.h>
#include <sys/event.h>  // kqueue, kevent, EVFILT_PROC, NOTE_EXIT

extern "C" {
#include "debug/log.h"
}
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __APPLE__
#include <crt_externs.h>
#include <mach-o/dyld.h>
static char **get_environ()
{
    return *_NSGetEnviron();
}
#else
extern char **environ;
static char **get_environ()
{
    return environ;
}
#endif

#ifdef __APPLE__
extern "C" {
void MuplarWawonaStartInProcess(const char *socket_name);
void MuplarWawonaStopInProcess(void);
bool MuplarWawonaIsRunningInProcess(void);
}
#endif

namespace muplar::supervisor
{

using namespace std::chrono;
using namespace std::chrono_literals;
namespace fs = std::filesystem;
namespace prefix = runtime::prefix;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static fs::path get_executable_dir()
{
    char buf[4096] = {};
#ifdef __APPLE__
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) != 0)
        return {};
#else
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0)
        return {};
    buf[n] = '\0';
#endif
    return fs::path(buf).parent_path();
}

static void ensure_wine_tmp_dir_private()
{
    auto sock_dir = fs::path("/tmp") / (".wine-" + std::to_string(getuid()));
    std::error_code ec;
    fs::create_directories(sock_dir, ec);
    if (ec) {
        log_error("[Muplar Windows Compatibility] failed to create %s: %s",
                  sock_dir.c_str(), ec.message().c_str());
        return;
    }
    if (chmod(sock_dir.c_str(), 0700) != 0) {
        log_warn("[Muplar Windows Compatibility] chmod 0700 %s failed: %s",
                 sock_dir.c_str(), std::strerror(errno));
    }
}

// Locate the muplar-wayland binary: try next to mup, inside the app bundle,
// then from PATH.
static fs::path resolve_muplar_wayland_bin()
{
    auto exec_dir = get_executable_dir();
    std::error_code ec;

    // 1. Next to mup binary (production layout)
    auto candidate = exec_dir / "muplar-wayland";
    if (fs::is_regular_file(candidate, ec))
        return candidate;

    // 2. App bundle Contents/MacOS/../Frameworks/muplar-wayland
    candidate = exec_dir.parent_path() / "Frameworks" / "muplar-wayland";
    if (fs::is_regular_file(candidate, ec))
        return candidate;

    // 3. PATH
    const char *path_env = std::getenv("PATH");
    if (path_env) {
        std::istringstream ss(path_env);
        std::string dir;
        while (std::getline(ss, dir, ':')) {
            auto p = fs::path(dir) / "muplar-wayland";
            if (fs::is_regular_file(p, ec))
                return p;
        }
    }
    return {};
}

// Block on kqueue until pid exits or stop_fd becomes readable.
// Returns the exit status (from waitpid) on normal exit.
// Returns -1 if stop_fd fired (stop requested).
static int kqueue_wait_pid(pid_t pid, int stop_fd)
{
    int kq = kqueue();
    if (kq < 0) {
        log_error("[Supervisor] kqueue() failed: %s", strerror(errno));
        // Fall back to polling
        while (true) {
            int status = 0;
            pid_t r = waitpid(pid, &status, WNOHANG);
            if (r == pid)
                return status;
            if (r < 0 && errno != EINTR)
                return 0;

            // Check stop_fd
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(stop_fd, &fds);
            struct timeval tv = {0, 100000};  // 100ms
            if (select(stop_fd + 1, &fds, nullptr, nullptr, &tv) > 0)
                return -1;
        }
    }

    struct kevent changes[2];
    int nchanges = 0;

    // Watch pid for exit
    EV_SET(&changes[nchanges++], (uintptr_t) pid, EVFILT_PROC,
           EV_ADD | EV_ONESHOT, NOTE_EXIT, 0, nullptr);

    // Watch stop_fd for readability
    EV_SET(&changes[nchanges++], stop_fd, EVFILT_READ, EV_ADD | EV_ONESHOT, 0,
           0, nullptr);

    kevent(kq, changes, nchanges, nullptr, 0, nullptr);

    int result = -1;
    while (true) {
        struct kevent event;
        int n = kevent(kq, nullptr, 0, &event, 1, nullptr);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (n == 0)
            continue;

        if (event.filter == EVFILT_PROC && (uintptr_t) pid == event.ident) {
            // Process exited — reap it
            int status = 0;
            waitpid(pid, &status, 0);
            result = status;
            break;
        }
        if (event.filter == EVFILT_READ && (int) event.ident == stop_fd) {
            // Stop requested
            result = -1;
            break;
        }
    }

    close(kq);
    return result;
}

// Check if a Unix socket path is live (connect succeeds).
static bool socket_is_live(const fs::path &path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return false;

    struct sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    auto path_str = path.string();
    if (path_str.size() >= sizeof(addr.sun_path)) {
        close(fd);
        return false;
    }
    std::strncpy(addr.sun_path, path_str.c_str(), sizeof(addr.sun_path) - 1);

    bool ok = (connect(fd, reinterpret_cast<struct sockaddr *>(&addr),
                       sizeof(addr)) == 0);
    close(fd);
    return ok;
}

// Poll for a socket path to become live.
static bool poll_for_socket(const fs::path &path, int timeout_ms)
{
    auto deadline = steady_clock::now() + milliseconds(timeout_ms);
    while (steady_clock::now() < deadline) {
        if (socket_is_live(path))
            return true;
        std::this_thread::sleep_for(50ms);
    }
    return socket_is_live(path);
}

// Create a self-pipe pair used to wake the kqueue monitor thread on stop().
static bool make_pipe(int fds[2])
{
    if (pipe(fds) < 0)
        return false;
    fcntl(fds[0], F_SETFL, O_NONBLOCK);
    fcntl(fds[1], F_SETFL, O_NONBLOCK);
    return true;
}

// ---------------------------------------------------------------------------
// WaylandGuard
// ---------------------------------------------------------------------------

fs::path WaylandGuard::wayland_socket_path()
{
    auto uid = static_cast<unsigned long>(getuid());
    return fs::path("/tmp") / ("muplar-wayland-" + std::to_string(uid)) /
           "wayland-0";
}

WaylandGuard::WaylandGuard(RestartPolicy policy)
    : policy_(std::move(policy)), current_delay_ms_(policy_.initial_delay_ms)
{
}

WaylandGuard::~WaylandGuard()
{
    stop();
}

bool WaylandGuard::wait_for_socket(int timeout_ms) const
{
    return poll_for_socket(wayland_socket_path(), timeout_ms);
}


#ifdef __APPLE__
pid_t WaylandGuard::spawn()
{
    return 0;
}

void WaylandGuard::start()
{
    bool running = MuplarWawonaIsRunningInProcess();
    if (running && !socket_is_live(wayland_socket_path())) {
        log_warn(
            "[WaylandGuard] in-process compositor is running without live "
            "Wayland socket %s; restarting",
            wayland_socket_path().c_str());
        MuplarWawonaStopInProcess();
        running = false;
    }
    if (!running) {
        log_info("[WaylandGuard] starting in-process compositor socket=%s",
                 wayland_socket_path().c_str());
        MuplarWawonaStartInProcess("wayland-0");
    }
    // Confirming the socket takes as long as the compositor takes to bind it,
    // so waiting here would stall every Linux prefix launch -- start() is
    // documented non-blocking and runs on the macOS launch path. Report from a
    // detached thread instead: the result is a diagnostic, and nothing on the
    // launch path consumes it.
    std::thread([socket = wayland_socket_path()] {
        if (!poll_for_socket(socket, 5000)) {
            log_warn(
                "[WaylandGuard] in-process compositor did not expose live "
                "Wayland socket %s",
                socket.c_str());
        }
    }).detach();
}

void WaylandGuard::stop()
{
    if (MuplarWawonaIsRunningInProcess()) {
        log_info("[WaylandGuard] stopping in-process compositor");
        MuplarWawonaStopInProcess();
    }
}

bool WaylandGuard::is_running() const
{
    return MuplarWawonaIsRunningInProcess() &&
           socket_is_live(wayland_socket_path());
}

void WaylandGuard::apply_backoff() {}
void WaylandGuard::monitor_loop() {}
#else
pid_t WaylandGuard::spawn()
{
    fs::path wayland_bin = resolve_muplar_wayland_bin();
    if (wayland_bin.empty()) {
        log_error("[WaylandGuard] muplar-wayland binary not found");
        return -1;
    }

    // Ensure the runtime dir exists before muplar-wayland tries to create the
    // socket
    auto socket_dir = wayland_socket_path().parent_path();
    auto socket_path = wayland_socket_path();
    std::error_code ec;
    fs::create_directories(socket_dir, ec);
    if (fs::exists(socket_path, ec) && !socket_is_live(socket_path)) {
        fs::remove(socket_path, ec);
        fs::remove(socket_path.string() + ".lock", ec);
    }

    pid_t pid = fork();
    if (pid < 0) {
        log_error("[WaylandGuard] fork() failed: %s", strerror(errno));
        return -1;
    }
    if (pid == 0) {
        // Child: exec muplar-wayland
        std::string socket_dir_str = socket_dir.string();
        std::string socket_name = socket_path.filename().string();
        setenv("XDG_RUNTIME_DIR", socket_dir_str.c_str(), 1);
        setenv("WAYLAND_DISPLAY", socket_name.c_str(), 1);

        std::string wayland_bin_str = wayland_bin.string();
        char *argv[] = {const_cast<char *>(wayland_bin_str.c_str()), nullptr};
        execve(wayland_bin_str.c_str(), argv, get_environ());
        log_error("[WaylandGuard] execve failed: %s", strerror(errno));
        _exit(127);
    }
    log_info("[WaylandGuard] started pid=%d path=%s", (int) pid,
             wayland_bin.c_str());
    return pid;
}

void WaylandGuard::start()
{
    if (monitor_thread_.joinable())
        return;  // already started

    stop_requested_.store(false);
    monitor_thread_ = std::thread([this] { monitor_loop(); });
}

void WaylandGuard::stop()
{
    stop_requested_.store(true);

    pid_t pid = pid_.load();
    if (pid > 0) {
        kill(pid, SIGTERM);
        // Give it 3s to exit gracefully
        for (int i = 0; i < 30; ++i) {
            std::this_thread::sleep_for(100ms);
            if (kill(pid, 0) != 0)
                break;
        }
        if (kill(pid, 0) == 0) {
            log_warn("[WaylandGuard] escalating to SIGKILL pid=%d", (int) pid);
            kill(pid, SIGKILL);
        }
        waitpid(pid, nullptr, 0);
        pid_.store(0);
    }

    if (monitor_thread_.joinable())
        monitor_thread_.join();
}

bool WaylandGuard::is_running() const
{
    pid_t pid = pid_.load();
    return pid > 0 && kill(pid, 0) == 0;
}

void WaylandGuard::apply_backoff()
{
    if (current_delay_ms_ <= 0) {
        current_delay_ms_ =
            policy_.initial_delay_ms > 0 ? policy_.initial_delay_ms : 0;
        return;
    }
    current_delay_ms_ =
        std::min(static_cast<int>(current_delay_ms_ * policy_.backoff_factor),
                 policy_.max_delay_ms);
}

void WaylandGuard::monitor_loop()
{
    int stop_pipe[2] = {-1, -1};
    make_pipe(stop_pipe);

    while (!stop_requested_.load()) {
        // Evict old entries outside the crash window
        if (policy_.max_crashes_in_window > 0) {
            auto cutoff =
                steady_clock::now() - seconds(policy_.crash_window_sec);
            crash_times_.erase(
                std::remove_if(crash_times_.begin(), crash_times_.end(),
                               [&](auto t) { return t < cutoff; }),
                crash_times_.end());

            if ((int) crash_times_.size() >= policy_.max_crashes_in_window) {
                log_warn(
                    "[WaylandGuard] crash rate too high (%zu crashes in %ds), "
                    "stopping restarts",
                    crash_times_.size(), policy_.crash_window_sec);
                break;
            }
        }

        // Apply backoff delay before (re)spawning
        if (current_delay_ms_ > 0 && !crash_times_.empty()) {
            std::this_thread::sleep_for(milliseconds(current_delay_ms_));
            if (stop_requested_.load())
                break;
        }

        while (!stop_requested_.load() && socket_is_live(wayland_socket_path()))
            std::this_thread::sleep_for(1s);
        if (stop_requested_.load())
            break;

        pid_t pid = spawn();
        if (pid <= 0) {
            std::this_thread::sleep_for(2s);
            continue;
        }
        pid_.store(pid);
        if (on_start_)
            on_start_(pid);

        if (!poll_for_socket(wayland_socket_path(), 5000)) {
            log_warn(
                "[WaylandGuard] pid=%d did not create Wayland socket %s; "
                "terminating",
                (int) pid, wayland_socket_path().c_str());
            kill(pid, SIGTERM);
            int status = kqueue_wait_pid(pid, stop_pipe[0]);
            pid_.store(0);
            if (status == -1)
                break;

            crash_times_.push_back(steady_clock::now());
            apply_backoff();
            if (on_crash_)
                on_crash_(status);
            continue;
        }

        // Wait for exit via kqueue
        int status = kqueue_wait_pid(pid, stop_pipe[0]);
        pid_.store(0);

        if (status == -1)
            break;  // stop requested

        int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        log_warn("[WaylandGuard] exited (status=%d), will restart", exit_code);

        crash_times_.push_back(steady_clock::now());
        apply_backoff();
        if (on_crash_)
            on_crash_(status);
    }

    if (stop_pipe[0] >= 0)
        close(stop_pipe[0]);
    if (stop_pipe[1] >= 0)
        close(stop_pipe[1]);
    log_info("[WaylandGuard] monitor thread exiting");
}
#endif

// ---------------------------------------------------------------------------
// WineServerGuard
// ---------------------------------------------------------------------------

WineServerGuard::WineServerGuard(prefix::PrefixLayout layout,
                                 RestartPolicy policy)
    : layout_(std::move(layout)),
      policy_(std::move(policy)),
      current_delay_ms_(policy_.initial_delay_ms)
{
}

WineServerGuard::~WineServerGuard()
{
    stop();
}

fs::path WineServerGuard::wineserver_socket_path() const
{
    // Wine derives its server socket from device+inode of WINEPREFIX:
    //   /tmp/.wine-<uid>/server-<dev_hex>-<ino_hex>
    struct stat st = {};
    if (stat(layout_.rootfs.string().c_str(), &st) != 0)
        return {};

    char buf[256];
    std::snprintf(buf, sizeof(buf), "/tmp/.wine-%u/server-%llx-%llx",
                  static_cast<unsigned>(getuid()),
                  static_cast<unsigned long long>(st.st_dev),
                  static_cast<unsigned long long>(st.st_ino));
    return buf;
}

fs::path WineServerGuard::resolve_wineserver_bin() const
{
    // Try next to wine binary: find wine first, then look for wineserver
    // sibling
    auto exec_dir = get_executable_dir();
    std::error_code ec;

    auto try_dir = [&](const fs::path &dir) -> fs::path {
        for (const char *name : {"wineserver64", "wineserver"}) {
            auto candidate = dir / name;
            if (fs::is_regular_file(candidate, ec))
                return candidate;
        }
        return {};
    };

    // 1. runtime_sysroot / bin
    if (!layout_.runtime_sysroot.empty()) {
        auto found = try_dir(layout_.runtime_sysroot / "bin");
        if (!found.empty())
            return found;
    }

    // 2. Bundle Frameworks/wine/bin
    auto bundle_dir = exec_dir.parent_path() / "Frameworks" / "wine" / "bin";
    auto found = try_dir(bundle_dir);
    if (!found.empty())
        return found;

    // 3. build/wine-prefix/bin
    auto build_dir =
        exec_dir.parent_path().parent_path() / "wine-prefix" / "bin";
    found = try_dir(build_dir);
    if (!found.empty())
        return found;

    // 4. PATH
    const char *path_env = std::getenv("PATH");
    if (path_env) {
        std::istringstream ss(path_env);
        std::string d;
        while (std::getline(ss, d, ':')) {
            found = try_dir(d);
            if (!found.empty())
                return found;
        }
    }
    return {};
}

pid_t WineServerGuard::spawn()
{
    fs::path wineserver = resolve_wineserver_bin();
    if (wineserver.empty()) {
        log_error("[WindowsCompatibilityService:%s] service binary not found",
                  layout_.name.c_str());
        return -1;
    }

    ensure_wine_tmp_dir_private();
    std::error_code ec;

    pid_t pid = fork();
    if (pid < 0) {
        log_error("[WindowsCompatibilityService:%s] fork() failed: %s",
                  layout_.name.c_str(), strerror(errno));
        return -1;
    }
    if (pid == 0) {
        umask(0077);
        // Set WINEPREFIX before exec
        setenv("WINEPREFIX", layout_.rootfs.string().c_str(), 1);
        setenv("WINEDEBUG", "-all", 1);

        // Set up library paths same as WineRuntime
        auto wine_lib = wineserver.parent_path().parent_path() / "lib";
        if (fs::is_directory(wine_lib, ec)) {
            setenv("DYLD_FALLBACK_LIBRARY_PATH",
                   (wine_lib.string() + ":/usr/local/lib:/opt/homebrew/lib")
                       .c_str(),
                   1);
        }

        std::string ws_str = wineserver.string();
        // -f: stay in the foreground so we can monitor it
        char *argv[] = {const_cast<char *>(ws_str.c_str()),
                        const_cast<char *>("-f"), nullptr};
        execve(ws_str.c_str(), argv, get_environ());
        log_error("[WindowsCompatibilityService] execve failed: %s",
                  strerror(errno));
        _exit(127);
    }

    log_info("[WindowsCompatibilityService:%s] started service pid=%d",
             layout_.name.c_str(), (int) pid);
    return pid;
}

void WineServerGuard::start()
{
    if (monitor_thread_.joinable())
        return;
    stop_requested_.store(false);
    monitor_thread_ = std::thread([this] { monitor_loop(); });
}

void WineServerGuard::stop()
{
    stop_requested_.store(true);

    pid_t pid = pid_.load();
    if (pid > 0) {
        kill(pid, SIGTERM);
        for (int i = 0; i < 30; ++i) {
            std::this_thread::sleep_for(100ms);
            if (kill(pid, 0) != 0)
                break;
        }
        if (kill(pid, 0) == 0)
            kill(pid, SIGKILL);
        waitpid(pid, nullptr, 0);
        pid_.store(0);
    }

    if (monitor_thread_.joinable())
        monitor_thread_.join();
}

bool WineServerGuard::is_running() const
{
    pid_t pid = pid_.load();
    return pid > 0 && kill(pid, 0) == 0;
}

bool WineServerGuard::wait_for_socket(int timeout_ms) const
{
    auto sock = wineserver_socket_path();
    if (sock.empty())
        return false;
    return poll_for_socket(sock, timeout_ms);
}

void WineServerGuard::monitor_loop()
{
    int stop_pipe[2] = {-1, -1};
    make_pipe(stop_pipe);

    while (!stop_requested_.load()) {
        if (policy_.max_crashes_in_window > 0) {
            auto cutoff =
                steady_clock::now() - seconds(policy_.crash_window_sec);
            crash_times_.erase(
                std::remove_if(crash_times_.begin(), crash_times_.end(),
                               [&](auto t) { return t < cutoff; }),
                crash_times_.end());
            if ((int) crash_times_.size() >= policy_.max_crashes_in_window) {
                log_warn(
                    "[WindowsCompatibilityService:%s] crash rate too "
                    "high, stopping",
                    layout_.name.c_str());
                break;
            }
        }

        if (current_delay_ms_ > 0 && !crash_times_.empty()) {
            std::this_thread::sleep_for(milliseconds(current_delay_ms_));
            if (stop_requested_.load())
                break;
        }

        pid_t pid = spawn();
        if (pid <= 0) {
            std::this_thread::sleep_for(2s);
            continue;
        }
        pid_.store(pid);
        if (on_start_)
            on_start_(pid);

        int status = kqueue_wait_pid(pid, stop_pipe[0]);
        pid_.store(0);

        if (status == -1)
            break;

        int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        log_warn(
            "[WindowsCompatibilityService:%s] service exited "
            "(code=%d), restarting",
            layout_.name.c_str(), exit_code);

        crash_times_.push_back(steady_clock::now());
        // Backoff: 0 → 1s → 2s → 4s → 8s cap
        current_delay_ms_ =
            current_delay_ms_ <= 0
                ? 1000
                : std::min(static_cast<int>(current_delay_ms_ *
                                            policy_.backoff_factor),
                           policy_.max_delay_ms);
        if (on_crash_)
            on_crash_(status);
    }

    if (stop_pipe[0] >= 0)
        close(stop_pipe[0]);
    if (stop_pipe[1] >= 0)
        close(stop_pipe[1]);
    log_info("[WindowsCompatibilityService:%s] monitor thread exiting",
             layout_.name.c_str());
}

// ---------------------------------------------------------------------------
// SupervisorService
// ---------------------------------------------------------------------------

SupervisorService::SupervisorService()
    : wayland_guard_(std::make_unique<WaylandGuard>())
{
}

SupervisorService::~SupervisorService()
{
    stop();
}

void SupervisorService::start(int poll_interval_ms)
{
    {
        // try_lock, never lock: this runs on the main thread, and stop() runs
        // on a background queue. Blocking here is what turned the earlier
        // crash into a hang -- stop() held the lock while MuplarWawonaStop-
        // InProcess dispatch_sync'd to the main queue, so the two waited on
        // each other. Failing to acquire means a teardown is under way, and
        // declining to start during shutdown is the correct answer anyway.
        std::unique_lock<std::mutex> lifecycle_lk(lifecycle_mu_,
                                                  std::try_to_lock);
        if (!lifecycle_lk.owns_lock())
            return;
        if (running_.exchange(true))
            return;

        // A stop() that cleared running_ but has not finished joining leaves
        // the thread joinable; assigning over it below would call
        // std::terminate(). The lock makes that impossible from a concurrent
        // stop(), and this covers a prior stop() that already returned.
        if (poll_thread_.joinable())
            poll_thread_.join();
    }

    // A stop() can land between releasing the lock above and starting the
    // guards below. Cheap pre-check for the common case; the unwind after
    // covers the rest of the window.
    if (!running_.load())
        return;

    // Both of these can round-trip to the main queue, so neither may run while
    // lifecycle_mu_ is held. See the invariant note on the member.
    wayland_guard_->start();
    sync_wine_guards();

    bool still_running;
    {
        std::lock_guard<std::mutex> lifecycle_lk(lifecycle_mu_);
        // A stop() may have overtaken us while the guards were coming up; its
        // join has already run, so publishing a thread now would leave it
        // unowned.
        still_running = running_.load();
        if (still_running) {
            poll_thread_ = std::thread(
                [this, poll_interval_ms] { poll_loop(poll_interval_ms); });
        }
    }

    // The teardown finished before our guards came up, so its stop_guards()
    // ran against a supervisor that had nothing started yet. Without this the
    // compositor and the Windows compatibility services stay alive with no
    // supervisor owning them, and the next stop() returns early on
    // running_ == false and never reaches them.
    if (!still_running)
        stop_guards();
}

void SupervisorService::stop()
{
    {
        std::lock_guard<std::mutex> lifecycle_lk(lifecycle_mu_);
        if (!running_.exchange(false))
            return;

        if (poll_thread_.joinable())
            poll_thread_.join();
    }

    // Deliberately outside the lock: wayland_guard_->stop() reaches
    // MuplarWawonaStopInProcess, which dispatch_sync's to the main queue when
    // called from a background thread -- and this is called from one, off
    // applicationShouldTerminate:. Holding lifecycle_mu_ across that let the
    // main thread block on the lock while this thread waited on the main
    // queue, which hung the app on quit.
    stop_guards();
}

void SupervisorService::stop_guards()
{
    {
        std::lock_guard<std::mutex> lk(guards_mu_);
        for (auto &[name, guard] : wine_guards_)
            guard->stop();
        wine_guards_.clear();
    }

    wayland_guard_->stop();
}

void SupervisorService::on_prefix_created(const prefix::PrefixLayout &layout)
{
    if (layout.kind != prefix::PrefixKind::Wine)
        return;

    std::lock_guard<std::mutex> lk(guards_mu_);
    if (wine_guards_.count(layout.name))
        return;  // already guarded

    auto guard = std::make_unique<WineServerGuard>(layout);
    guard->start();
    wine_guards_[layout.name] = std::move(guard);
    log_info(
        "[SupervisorService] started Windows compatibility service for '%s'",
        layout.name.c_str());
}

void SupervisorService::on_prefix_deleted(const std::string &prefix_name)
{
    std::lock_guard<std::mutex> lk(guards_mu_);
    auto it = wine_guards_.find(prefix_name);
    if (it == wine_guards_.end())
        return;

    it->second->stop();
    wine_guards_.erase(it);
    log_info(
        "[SupervisorService] stopped Windows compatibility service for '%s'",
        prefix_name.c_str());
}

WineServerGuard *SupervisorService::wine_server(const std::string &prefix_name)
{
    std::lock_guard<std::mutex> lk(guards_mu_);
    auto it = wine_guards_.find(prefix_name);
    return it != wine_guards_.end() ? it->second.get() : nullptr;
}

void SupervisorService::sync_wine_guards()
{
    auto all_prefixes = prefix::list_prefixes();
    std::lock_guard<std::mutex> lk(guards_mu_);

    // Add services for new Windows instances
    for (const auto &p : all_prefixes) {
        if (p.kind != prefix::PrefixKind::Wine)
            continue;
        if (wine_guards_.count(p.name))
            continue;

        auto guard = std::make_unique<WineServerGuard>(p);
        guard->start();
        wine_guards_[p.name] = std::move(guard);
        log_info(
            "[SupervisorService] started Windows compatibility "
            "service for '%s'",
            p.name.c_str());
    }

    // Remove guards for deleted prefixes
    std::vector<std::string> to_remove;
    for (const auto &[name, _] : wine_guards_) {
        const std::string guard_name = name;
        bool still_exists =
            std::any_of(all_prefixes.begin(), all_prefixes.end(),
                        [&](const auto &p) { return p.name == guard_name; });
        if (!still_exists)
            to_remove.push_back(name);
    }
    for (const auto &name : to_remove) {
        wine_guards_[name]->stop();
        wine_guards_.erase(name);
        log_info(
            "[SupervisorService] removed Windows compatibility "
            "service for '%s'",
            name.c_str());
    }
}

void SupervisorService::poll_loop(int interval_ms)
{
    while (running_.load()) {
        std::this_thread::sleep_for(milliseconds(interval_ms));
        if (!running_.load())
            break;
        sync_wine_guards();
    }
}

}  // namespace muplar::supervisor
