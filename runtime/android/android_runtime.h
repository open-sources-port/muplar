// runtime/android/android_runtime.h
//
// Phase 4 — Android Runtime Stubs
//
// HVC call number ranges:
//   0x2000–0x20FF : libc stubs  (malloc, free, pthread_*, etc.)
//   0x2100–0x21FF : liblog      (__android_log_print, etc.)
//   0x2200–0x22FF : libandroid  (ALooper, AChoreographer, ANativeWindow, etc.)
//   0x2300–0x23FF : libdl       (dlopen, dlsym, dlclose, dlerror)
//   0x2400–0x24FF : libEGL      (eglGetDisplay, eglInitialize, …)
//   0x2500–0x25FF : libGLESv2   (eglGetProcAddress passthrough + GLES dispatch)
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <functional>
#include <optional>

// ANGLE / EGL / GLES headers (macOS host-side)
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

extern "C" {
    #include "core/guest.h"
}

namespace muplar::runtime::android {

using BuiltinSymbols = std::unordered_map<std::string, uint64_t>;

using StubHandler = std::function<uint64_t(guest_t*, const uint64_t[8])>;

struct StubEntry {
    std::string  soname;
    std::string  symbol;
    uint32_t     hvc_nr;
    StubHandler  handler;
};

class AndroidRuntime {
public:
    AndroidRuntime(guest_t* guest, uint64_t stub_arena_gpa);
    ~AndroidRuntime();

    // Write HVC shim stubs into guest memory and build the symbol tables.
    void install();

    // Return the builtin symbol map for a given soname.
    BuiltinSymbols builtin_symbols(const std::string& soname) const;

    static constexpr const char* KNOWN_SONAMES[] = {
        "libc.so", "libm.so", "libdl.so", "libdl_android.so", "liblog.so",
        "libandroid.so", "libstdc++.so",
        "libEGL.so", "libGLESv2.so", "libGLESv3.so",
        nullptr
    };

    // HVC dispatch — call from the HVC exit handler when X8 is in [0x2000, 0x25FF].
    bool try_dispatch(uint32_t hvc_nr, const uint64_t regs[8], uint64_t* x0_out);

    // EGL state accessors (used by NativeWindow bridge)
    EGLDisplay egl_display() const { return egl_display_; }
    EGLContext egl_context() const { return egl_context_; }
    EGLSurface egl_surface() const { return egl_surface_; }

private:
    void register_libc_stubs();
    void register_liblog_stubs();
    void register_libandroid_stubs();
    void register_libdl_stubs();
    void register_libegl_stubs();
    void register_libgles_stubs();

    // Load ANGLE dylibs from third_party/angle-bin/
    bool load_angle();

    // Write one 12-byte HVC shim stub (movz x8,#nr / hvc #6 / ret).
    uint64_t write_stub(uint32_t hvc_nr);

    // Allocate a passthrough stub that saves/restores and calls a host fn ptr.
    // Used by eglGetProcAddress so the guest can resolve arbitrary GLES symbols.
    uint64_t alloc_passthrough_stub(void* host_fn, uint32_t hvc_nr);

    uint64_t add(const std::string& soname,
                 const std::string& symbol,
                 uint32_t           hvc_nr,
                 StubHandler        handler);

    guest_t*  guest_;
    uint64_t  arena_gpa_;
    uint64_t  next_stub_gpa_ = 0;
    bool      installed_     = false;

    std::unordered_map<uint32_t, StubHandler> handlers_;
    std::unordered_map<std::string, BuiltinSymbols> sym_tables_;

    // ── Bump allocator for malloc stubs ──────────────────────────────────────
    uint64_t heap_base_  = 0;
    uint64_t heap_bump_  = 0;
    static constexpr uint64_t HEAP_SIZE = 512 * 1024;

    // ── pthread handle table ──────────────────────────────────────────────────
    struct PthreadEntry { uint64_t stack_gpa; uint64_t stack_size; };
    std::unordered_map<uint64_t, PthreadEntry> threads_;
    uint64_t next_thread_handle_ = 0x8000'0001ULL;

    // ── dlopen handle table ───────────────────────────────────────────────────
    struct DlopenEntry { std::string path; uint64_t load_base; };
    std::unordered_map<uint64_t, DlopenEntry> dl_handles_;
    uint64_t next_dl_handle_ = 0x9000'0001ULL;

    // ── ANGLE / EGL host state ────────────────────────────────────────────────
    void*      angle_egl_lib_  = nullptr;   // dlopen handle for libEGL.dylib
    void*      angle_gles_lib_ = nullptr;   // dlopen handle for libGLESv2.dylib

    // Host-side EGL objects created during eglInitialize / eglCreateContext
    EGLDisplay egl_display_ = EGL_NO_DISPLAY;
    EGLContext egl_context_ = EGL_NO_CONTEXT;
    EGLSurface egl_surface_ = EGL_NO_SURFACE;
    EGLConfig  egl_config_  = nullptr;

    // Guest-side opaque handles (returned to the game as EGLDisplay*, etc.)
    // We use small integers so they fit in 64-bit guest registers.
    static constexpr uint64_t GUEST_EGL_DISPLAY = 0xE61D0001ULL;
    static constexpr uint64_t GUEST_EGL_CONTEXT = 0xE61C0001ULL;
    static constexpr uint64_t GUEST_EGL_SURFACE = 0xE615F001ULL;
    static constexpr uint64_t GUEST_EGL_CONFIG  = 0xE61CF001ULL;
    static constexpr uint64_t EGL_SUCCESS_VAL   = 0x3000ULL; // EGL_SUCCESS

    // eglGetProcAddress dispatch: guest hvc_nr → host fn ptr
    // Allocated dynamically starting at HVC_GL_PROC_BASE.
    static constexpr uint32_t HVC_GL_PROC_BASE = 0x2600;
    std::unordered_map<uint32_t, void*> proc_addr_handlers_;
    uint32_t next_proc_hvc_ = HVC_GL_PROC_BASE;

    // Helper: resolve a symbol from ANGLE (tries EGL then GLES lib)
    void* angle_sym(const char* name) const;
};

} // namespace muplar::runtime::android
