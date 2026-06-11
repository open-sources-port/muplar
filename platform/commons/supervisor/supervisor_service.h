#pragma once

// platform/commons/supervisor/supervisor_service.h
//
// SupervisorService: keeps Wawona (global Wayland compositor) and per-prefix
// Windows compatibility services alive for the lifetime of the instance manager.
//
// Architecture:
//   - WawonaGuard   — one global instance; restarts wawona on crash with
//                     exponential backoff; uses kqueue EVFILT_PROC/NOTE_EXIT
//                     for zero-latency crash detection on macOS.
//   - WineServerGuard — one per Windows instance; same restart logic scoped to
//                     the backend service process.
//   - SupervisorService — owns the guards; created by the instance manager
//                     on launch, torn down on quit.
//
// Thread safety: all public methods are safe to call from any thread.
// Internally each guard runs a dedicated monitor thread.

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "prefix.h"

namespace muplar::supervisor {

// ---------------------------------------------------------------------------
// Restart policy
// ---------------------------------------------------------------------------

struct RestartPolicy {
    // Delay before first restart (ms).
    int initial_delay_ms = 0;
    // Multiply delay by this factor after each crash.
    double backoff_factor = 2.0;
    // Maximum delay between restarts (ms).
    int max_delay_ms = 8000;
    // Stop restarting after this many consecutive crashes within
    // crash_window_sec seconds. 0 = restart forever.
    int max_crashes_in_window = 5;
    int crash_window_sec = 10;
};

// ---------------------------------------------------------------------------
// WawonaGuard
// ---------------------------------------------------------------------------
// Keeps a single global wawona process alive. The Wayland socket appears at
// /tmp/wawona-<uid>/wayland-0 and is shared by all Linux instances.

class WawonaGuard {
public:
    explicit WawonaGuard(RestartPolicy policy = {});
    ~WawonaGuard();

    // Start supervising wawona. Spawns the process immediately, then watches
    // for exit via kqueue. Non-blocking: monitor thread runs in background.
    void start();

    // Stop the guard: send SIGTERM to wawona, wait for it to exit, join the
    // monitor thread. Blocks for up to 5 seconds then escalates to SIGKILL.
    void stop();

    // True if wawona is currently running (pid > 0 and alive).
    bool is_running() const;

    // Path of the Wayland socket wawona will create.
    static std::filesystem::path wayland_socket_path();

    // Block until the wayland socket appears or timeout_ms elapses.
    // Returns true if the socket became available.
    bool wait_for_socket(int timeout_ms = 5000) const;

    // Optional callback invoked on the monitor thread after each (re)start.
    void set_on_start(std::function<void(pid_t)> cb) { on_start_ = std::move(cb); }
    // Optional callback invoked on the monitor thread after each crash.
    void set_on_crash(std::function<void(int /*exit_status*/)> cb) { on_crash_ = std::move(cb); }

private:
    pid_t spawn();
    void  monitor_loop();
    void  apply_backoff();

    RestartPolicy policy_;
    std::atomic<pid_t> pid_{0};
    std::atomic<bool>  stop_requested_{false};
    std::thread        monitor_thread_;
    mutable std::mutex mu_;

    // Crash-rate tracking for the backoff window.
    std::vector<std::chrono::steady_clock::time_point> crash_times_;
    int current_delay_ms_ = 0;

    std::function<void(pid_t)> on_start_;
    std::function<void(int)>   on_crash_;
};

// ---------------------------------------------------------------------------
// WineServerGuard
// ---------------------------------------------------------------------------
// Keeps the Windows compatibility backend service running for a single
// Windows instance. The backend stays in the foreground; we monitor its exit
// via kqueue and restart it so subsequent launches avoid a cold start.

class WineServerGuard {
public:
    explicit WineServerGuard(runtime::prefix::PrefixLayout layout,
                             RestartPolicy policy = {});
    ~WineServerGuard();

    void start();
    void stop();
    bool is_running() const;

    const runtime::prefix::PrefixLayout& layout() const { return layout_; }

    // Block until the wineserver socket appears or timeout_ms elapses.
    bool wait_for_socket(int timeout_ms = 5000) const;

    void set_on_start(std::function<void(pid_t)> cb) { on_start_ = std::move(cb); }
    void set_on_crash(std::function<void(int)>   cb) { on_crash_ = std::move(cb); }

private:
    // Derive the wineserver socket path from WINEPREFIX.
    // Wine hashes the device+inode of the prefix directory:
    //   /tmp/.wine-<uid>/server-<dev_hex>-<ino_hex>
    std::filesystem::path wineserver_socket_path() const;

    // Locate the backend service binary next to the Windows compatibility runtime.
    std::filesystem::path resolve_wineserver_bin() const;

    pid_t spawn();
    void  monitor_loop();

    runtime::prefix::PrefixLayout layout_;
    RestartPolicy                  policy_;
    std::atomic<pid_t>             pid_{0};
    std::atomic<bool>              stop_requested_{false};
    std::thread                    monitor_thread_;
    mutable std::mutex             mu_;

    std::vector<std::chrono::steady_clock::time_point> crash_times_;
    int current_delay_ms_ = 0;

    std::function<void(pid_t)> on_start_;
    std::function<void(int)>   on_crash_;
};

// ---------------------------------------------------------------------------
// SupervisorService
// ---------------------------------------------------------------------------
// Owned by the instance manager. Created on app launch, destroyed on quit.

class SupervisorService {
public:
    SupervisorService();
    ~SupervisorService();

    // Start the supervisor: launch Wawona, then start Windows compatibility
    // services for all existing Windows instances. Kicks off a background
    // thread that polls for newly-created Windows instances every
    // poll_interval_ms milliseconds.
    void start(int poll_interval_ms = 2000);

    // Stop everything cleanly.
    void stop();

    // Called by the instance manager when a new prefix is created.
    void on_prefix_created(const runtime::prefix::PrefixLayout& layout);

    // Called by the instance manager when a prefix is deleted.
    void on_prefix_deleted(const std::string& prefix_name);

    // Access to the global Wawona guard (e.g. to wait for the socket).
    WawonaGuard& wawona() { return *wawona_guard_; }
    const WawonaGuard& wawona() const { return *wawona_guard_; }

    // Access to a specific WineServerGuard (nullptr if not found).
    WineServerGuard* wine_server(const std::string& prefix_name);

    bool is_running() const { return running_.load(); }

private:
    void poll_loop(int interval_ms);
    void sync_wine_guards();

    std::unique_ptr<WawonaGuard> wawona_guard_;

    mutable std::mutex guards_mu_;
    std::unordered_map<std::string, std::unique_ptr<WineServerGuard>> wine_guards_;

    std::atomic<bool> running_{false};
    std::thread       poll_thread_;
};

} // namespace muplar::supervisor
