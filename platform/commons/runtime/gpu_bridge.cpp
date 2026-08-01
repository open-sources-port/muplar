#include "gpu_bridge.h"

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <dlfcn.h>
#include <stdexcept>

extern "C" {
#include "debug/log.h"
}

namespace muplar::runtime
{

GpuBridge::GpuBridge(guest_t *guest,
                     uint64_t stub_arena_gpa,
                     bool host_window_enabled)
    : guest_(guest),
      arena_gpa_(stub_arena_gpa),
      host_window_enabled_(host_window_enabled)
{
}

GpuBridge::~GpuBridge()
{
    if (egl_surface_ != EGL_NO_SURFACE && egl_display_ != EGL_NO_DISPLAY)
        eglDestroySurface(egl_display_, egl_surface_);
    if (egl_context_ != EGL_NO_CONTEXT && egl_display_ != EGL_NO_DISPLAY)
        eglDestroyContext(egl_display_, egl_context_);
    if (egl_display_ != EGL_NO_DISPLAY)
        eglTerminate(egl_display_);
    if (angle_gles_lib_)
        ::dlclose(angle_gles_lib_);
    if (angle_egl_lib_)
        ::dlclose(angle_egl_lib_);
}

void GpuBridge::install()
{
    // Initialize heap for GLES/EGL stubs
    heap_base_ = 0x1F0000000ULL;
    heap_bump_ = heap_base_;

    // Allocate guest physical memory for the heap and map it stage-2.
    // Without this, the first guest write to a malloc'd pointer or mapped
    // GLES buffer will trigger a stage-2 translation fault and crash.
    guest_extend_page_tables(guest_, heap_base_, heap_base_ + HEAP_SIZE,
                             MEM_PERM_RW);

    log_info("[GpuBridge] mapped 8MB GLES heap at [0x%llx,0x%llx)",
             (unsigned long long) heap_base_,
             (unsigned long long) (heap_base_ + HEAP_SIZE));

    // Initialize next_stub_gpa_
    next_stub_gpa_ = arena_gpa_;

    load_angle();

    register_libegl_stubs();
    register_libgles_stubs();

    installed_ = true;
}

bool GpuBridge::load_angle()
{
    // Try @rpath first (set by CMakeLists), then fallback to relative path.
    const char *egl_paths[] = {"@rpath/libEGL.dylib",
                               "third_party/angle-bin/libEGL.dylib", nullptr};
    const char *gles_paths[] = {"@rpath/libGLESv2.dylib",
                                "third_party/angle-bin/libGLESv2.dylib",
                                nullptr};

    for (int i = 0; egl_paths[i] && !angle_egl_lib_; ++i)
        angle_egl_lib_ = ::dlopen(egl_paths[i], RTLD_NOW | RTLD_LOCAL);

    for (int i = 0; gles_paths[i] && !angle_gles_lib_; ++i)
        angle_gles_lib_ = ::dlopen(gles_paths[i], RTLD_NOW | RTLD_LOCAL);

    if (!angle_egl_lib_ || !angle_gles_lib_) {
        log_error("[ANGLE] failed to load: egl=%p gles=%p — %s", angle_egl_lib_,
                  angle_gles_lib_, ::dlerror());
        return false;
    }
    log_info("[ANGLE] loaded libEGL + libGLESv2 ✓");
    return true;
}

void *GpuBridge::angle_sym(const char *name) const
{
    void *sym = nullptr;
    if (angle_egl_lib_)
        sym = ::dlsym(angle_egl_lib_, name);
    if (!sym && angle_gles_lib_)
        sym = ::dlsym(angle_gles_lib_, name);
    return sym;
}

bool GpuBridge::host_window_active() const
{
    return host_window_ && host_window_->valid() && !host_window_->closed();
}

void GpuBridge::run_host_window_after_guest(int linger_ms)
{
    if (!host_window_active())
        return;

    if (linger_ms >= 0) {
        host_window_->run_for_ms(linger_ms);
    } else {
        log_info("[HostWindow] close the Muplar window to exit");
        host_window_->run_until_closed();
    }
}

bool GpuBridge::pump_host_app_events()
{
    if (!host_window_enabled_)
        return false;

    bool did_work = collect_host_input_events();
    return did_work;
}

void GpuBridge::ensure_host_window()
{
    if (!host_window_enabled_)
        return;
    if (host_window_active())
        return;

    host_window_ = std::make_unique<HostWindow>(native_window_.width,
                                                native_window_.height);
    if (!host_window_->valid()) {
        log_warn("[HostWindow] unavailable on this host/thread");
        host_window_.reset();
    }
}

bool GpuBridge::collect_host_input_events()
{
    if (!host_window_enabled_)
        return false;

    ensure_host_window();
    if (!host_window_active())
        return false;

    host_window_->pump_events();
    auto events = host_window_->take_input_events();
    return !events.empty();
}

void GpuBridge::present_native_window_buffer()
{
    if (!host_window_enabled_ || !native_window_.bits_gpa)
        return;
    ensure_host_window();
    if (!host_window_active())
        return;

    size_t bytes = static_cast<size_t>(native_window_.stride) *
                   static_cast<size_t>(native_window_.height) * 4;
    if (bytes == 0 || bytes > native_window_.bits_size)
        return;

    std::vector<uint8_t> pixels(bytes);
    guest_read(guest_, native_window_.bits_gpa, pixels.data(), bytes);
    host_window_->present_rgba(pixels.data(), native_window_.width,
                               native_window_.height, native_window_.stride);
}

void GpuBridge::present_egl_surface()
{
    if (!host_window_enabled_)
        return;
    ensure_host_window();
    if (!host_window_active())
        return;

    int width = native_window_.width;
    int height = native_window_.height;
    if (width <= 0 || height <= 0)
        return;

    std::vector<uint8_t> pixels(static_cast<size_t>(width) *
                                static_cast<size_t>(height) * 4);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    host_window_->present_rgba(pixels.data(), width, height, width);
}

uint64_t GpuBridge::write_stub(uint32_t hvc_nr)
{
    uint8_t stub[HVC_STUB_SIZE];
    encode_stub(stub, hvc_nr);
    uint64_t gpa = next_stub_gpa_;
    guest_write(guest_, gpa, stub, sizeof(stub));
    next_stub_gpa_ += HVC_STUB_SIZE;
    return gpa;
}

uint64_t GpuBridge::add(const std::string &soname,
                        const std::string &symbol,
                        uint32_t hvc_nr,
                        StubHandler handler)
{
    uint64_t gpa = write_stub(hvc_nr);
    handlers_[hvc_nr] = std::move(handler);
    sym_tables_[soname][symbol] = gpa;
    return gpa;
}

bool GpuBridge::try_dispatch(uint32_t hvc_nr,
                             const uint64_t regs[8],
                             uint64_t *x0_out)
{
    auto it = handlers_.find(hvc_nr);
    if (it == handlers_.end())
        return false;
    *x0_out = it->second(guest_, regs);
    return true;
}

BuiltinSymbols GpuBridge::builtin_symbols(const std::string &soname) const
{
    auto it = sym_tables_.find(soname);
    if (it != sym_tables_.end())
        return it->second;
    return {};
}

uint64_t GpuBridge::unsupported_import_stub(const std::string &soname,
                                            const std::string &symbol)
{
    std::string key = soname + ":" + symbol;
    auto it = unsupported_import_stubs_.find(key);
    if (it != unsupported_import_stubs_.end())
        return it->second;

    if (next_unsupported_import_hvc_ >= HVC_UNSUPPORTED_IMPORT_LIMIT) {
        log_warn(
            "[GpuBridge] warning: unsupported import stub arena exhausted");
        return 0;
    }

    uint32_t hvc_nr = next_unsupported_import_hvc_++;
    uint64_t gpa = write_stub(hvc_nr);

    handlers_[hvc_nr] = [key](guest_t *, const uint64_t[8]) -> uint64_t {
        log_error("[GpuBridge] FATAL: guest called unsupported import: %s",
                  key.c_str());
        std::abort();
        return 0;
    };

    unsupported_import_stubs_[key] = gpa;
    return gpa;
}

// ── Protected guest memory helpers ───────────────────────────────────────────

void GpuBridge::encode_stub(uint8_t *out, uint32_t hvc_nr)
{
    uint32_t movz = 0xD2800008u | ((hvc_nr & 0xFFFF) << 5);
    uint32_t hvc = 0xD4000002u | (6u << 5);  // hvc #6
    uint32_t fmov =
        0x1E270000u;  // fmov s0, w0 (copy x0 float bits into s0 register)
    uint32_t ret = 0xD65F03C0u;
    std::memcpy(out + 0, &movz, 4);
    std::memcpy(out + 4, &hvc, 4);
    std::memcpy(out + 8, &fmov, 4);
    std::memcpy(out + 12, &ret, 4);
}

std::string GpuBridge::guest_read_string(guest_t *g, uint64_t gpa)
{
    if (!gpa)
        return {};
    char buf[512] = {};
    guest_read_str(g, gpa, buf, sizeof(buf));
    return {buf};
}

void GpuBridge::guest_write_u64(guest_t *g, uint64_t gpa, uint64_t v)
{
    guest_write(g, gpa, &v, 8);
}

uint64_t GpuBridge::guest_read_u64(guest_t *g, uint64_t gpa)
{
    uint64_t v = 0;
    guest_read(g, gpa, &v, 8);
    return v;
}

uint32_t GpuBridge::guest_read_u32(guest_t *g, uint64_t gpa)
{
    uint32_t v = 0;
    guest_read(g, gpa, &v, 4);
    return v;
}

void GpuBridge::guest_write_u32(guest_t *g, uint64_t gpa, uint32_t v)
{
    guest_write(g, gpa, &v, 4);
}

void GpuBridge::register_libegl_stubs()
{
    // eglGetDisplay(EGLNativeDisplayType)
    add("libEGL.so", "eglGetDisplay", HVC_EGL_GET_DISPLAY,
        [this](guest_t *, const uint64_t[8]) -> uint64_t {
            if (egl_display_ == EGL_NO_DISPLAY) {
                egl_display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
                log_debug("[EGL] eglGetDisplay → %p", egl_display_);
            }
            return (egl_display_ != EGL_NO_DISPLAY) ? GUEST_EGL_DISPLAY : 0;
        });

    // eglInitialize(EGLDisplay, EGLint *major, EGLint *minor)
    add("libEGL.so", "eglInitialize", HVC_EGL_INITIALIZE,
        [this](guest_t *g, const uint64_t a[8]) -> uint64_t {
            if (a[0] != GUEST_EGL_DISPLAY || egl_display_ == EGL_NO_DISPLAY)
                return 0;
            EGLint major = 0, minor = 0;
            EGLBoolean ok = eglInitialize(egl_display_, &major, &minor);
            log_debug("[EGL] eglInitialize → %d (v%d.%d)", ok, major, minor);
            if (a[1])
                guest_write_u32(g, a[1], (uint32_t) major);
            if (a[2])
                guest_write_u32(g, a[2], (uint32_t) minor);
            return ok ? 1 : 0;
        });

    // eglBindAPI(EGLenum api)  — always OpenGL ES
    add("libEGL.so", "eglBindAPI", HVC_EGL_BIND_API,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            EGLBoolean ok = eglBindAPI((EGLenum) a[0]);
            log_debug("[EGL] eglBindAPI(0x%x) → %d", (unsigned) a[0], ok);
            return ok ? 1 : 0;
        });

    // eglChooseConfig(display, attrib_list, configs, config_size, num_config)
    add("libEGL.so", "eglChooseConfig", HVC_EGL_CHOOSE_CONFIG,
        [this](guest_t *g, const uint64_t a[8]) -> uint64_t {
            if (a[0] != GUEST_EGL_DISPLAY || egl_display_ == EGL_NO_DISPLAY)
                return 0;

            // Read attrib list from guest (list of EGLint pairs, terminated by
            // EGL_NONE=0x3038)
            std::vector<EGLint> attribs;
            uint64_t gpa = a[1];
            if (gpa) {
                for (int i = 0; i < 64; ++i) {
                    EGLint v = (EGLint) guest_read_u32(g, gpa + i * 4);
                    attribs.push_back(v);
                    if (v == EGL_NONE)
                        break;
                }
            } else {
                attribs.push_back(EGL_NONE);
            }
            for (size_t i = 0; i + 1 < attribs.size(); i += 2) {
                if (attribs[i] == EGL_NONE)
                    break;
                if (attribs[i] == EGL_SURFACE_TYPE)
                    attribs[i + 1] |= EGL_PBUFFER_BIT;
            }

            EGLint num = 0;
            EGLBoolean ok = eglChooseConfig(egl_display_, attribs.data(),
                                            &egl_config_, 1, &num);
            log_debug("[EGL] eglChooseConfig → %d (num=%d)", ok, num);

            // Write guest config handle
            if (a[2] && ok && num > 0)
                guest_write_u64(g, a[2], GUEST_EGL_CONFIG);
            if (a[4])
                guest_write_u32(g, a[4], (uint32_t) (ok ? num : 0));
            return ok ? 1 : 0;
        });

    // eglCreateContext(display, config, share_context, attrib_list)
    add("libEGL.so", "eglCreateContext", HVC_EGL_CREATE_CONTEXT,
        [this](guest_t *g, const uint64_t a[8]) -> uint64_t {
            if (a[0] != GUEST_EGL_DISPLAY || egl_display_ == EGL_NO_DISPLAY)
                return 0;

            // Read context attribs (e.g. EGL_CONTEXT_CLIENT_VERSION = 2 or 3)
            std::vector<EGLint> attribs;
            uint64_t gpa = a[3];
            if (gpa) {
                for (int i = 0; i < 16; ++i) {
                    EGLint v = (EGLint) guest_read_u32(g, gpa + i * 4);
                    attribs.push_back(v);
                    if (v == EGL_NONE)
                        break;
                }
            } else {
                attribs.push_back(EGL_NONE);
            }

            egl_context_ = eglCreateContext(egl_display_, egl_config_,
                                            EGL_NO_CONTEXT, attribs.data());
            log_debug("[EGL] eglCreateContext → %p", egl_context_);
            return (egl_context_ != EGL_NO_CONTEXT) ? GUEST_EGL_CONTEXT : 0;
        });

    // eglCreateWindowSurface(display, config, native_window, attrib_list)
    // Backed by a pbuffer sized to our current fake NativeWindow.
    add("libEGL.so", "eglCreateWindowSurface", HVC_EGL_CREATE_WIN_SURFACE,
        [this](guest_t *, const uint64_t a[8]) -> uint64_t {
            if (a[0] != GUEST_EGL_DISPLAY || egl_display_ == EGL_NO_DISPLAY)
                return 0;
            if (a[2] != GUEST_NATIVE_WINDOW) {
                log_warn(
                    "[EGL] eglCreateWindowSurface: unknown native "
                    "window 0x%llx",
                    (unsigned long long) a[2]);
                return 0;
            }
            if (egl_surface_ == EGL_NO_SURFACE) {
                EGLint pbuf_attribs[] = {EGL_WIDTH, native_window_.width,
                                         EGL_HEIGHT, native_window_.height,
                                         EGL_NONE};
                egl_surface_ = eglCreatePbufferSurface(
                    egl_display_, egl_config_, pbuf_attribs);
                log_debug("[EGL] eglCreateWindowSurface → pbuffer %p (%dx%d)",
                          egl_surface_, native_window_.width,
                          native_window_.height);
            }
            return (egl_surface_ != EGL_NO_SURFACE) ? GUEST_EGL_SURFACE : 0;
        });

    // eglCreatePbufferSurface(display, config, attrib_list)
    add("libEGL.so", "eglCreatePbufferSurface", HVC_EGL_CREATE_PBUF_SURFACE,
        [this](guest_t *g, const uint64_t a[8]) -> uint64_t {
            if (a[0] != GUEST_EGL_DISPLAY || egl_display_ == EGL_NO_DISPLAY)
                return 0;
            std::vector<EGLint> attribs;
            uint64_t gpa = a[2];
            if (gpa) {
                for (int i = 0; i < 16; ++i) {
                    EGLint v = (EGLint) guest_read_u32(g, gpa + i * 4);
                    attribs.push_back(v);
                    if (v == EGL_NONE)
                        break;
                }
            } else {
                attribs.push_back(EGL_NONE);
            }
            egl_surface_ = eglCreatePbufferSurface(egl_display_, egl_config_,
                                                   attribs.data());
            log_debug("[EGL] eglCreatePbufferSurface → %p", egl_surface_);
            return (egl_surface_ != EGL_NO_SURFACE) ? GUEST_EGL_SURFACE : 0;
        });

    // eglMakeCurrent(display, draw, read, context)
    add("libEGL.so", "eglMakeCurrent", HVC_EGL_MAKE_CURRENT,
        [this](guest_t *, const uint64_t a[8]) -> uint64_t {
            if (egl_display_ == EGL_NO_DISPLAY)
                return 0;
            EGLSurface draw =
                (a[1] == GUEST_EGL_SURFACE) ? egl_surface_ : EGL_NO_SURFACE;
            EGLSurface read =
                (a[2] == GUEST_EGL_SURFACE) ? egl_surface_ : EGL_NO_SURFACE;
            EGLContext ctx =
                (a[3] == GUEST_EGL_CONTEXT) ? egl_context_ : EGL_NO_CONTEXT;
            EGLBoolean ok = eglMakeCurrent(egl_display_, draw, read, ctx);
            log_debug("[EGL] eglMakeCurrent → %d", ok);
            return ok ? 1 : 0;
        });

    // eglSwapBuffers(display, surface)
    add("libEGL.so", "eglSwapBuffers", HVC_EGL_SWAP_BUFFERS,
        [this](guest_t *, const uint64_t[8]) -> uint64_t {
            if (egl_display_ == EGL_NO_DISPLAY ||
                egl_surface_ == EGL_NO_SURFACE)
                return 0;
            EGLBoolean ok = eglSwapBuffers(egl_display_, egl_surface_);
            log_debug("[EGL] eglSwapBuffers → %d", ok);
            if (ok)
                present_egl_surface();
            return ok ? 1 : 0;
        });

    // eglDestroyContext / eglDestroySurface
    add("libEGL.so", "eglDestroyContext", HVC_EGL_DESTROY_CONTEXT,
        [this](guest_t *, const uint64_t[8]) -> uint64_t {
            if (egl_context_ != EGL_NO_CONTEXT &&
                egl_display_ != EGL_NO_DISPLAY) {
                eglDestroyContext(egl_display_, egl_context_);
                egl_context_ = EGL_NO_CONTEXT;
            }
            return 1;
        });

    add("libEGL.so", "eglDestroySurface", HVC_EGL_DESTROY_SURFACE,
        [this](guest_t *, const uint64_t[8]) -> uint64_t {
            if (egl_surface_ != EGL_NO_SURFACE &&
                egl_display_ != EGL_NO_DISPLAY) {
                eglDestroySurface(egl_display_, egl_surface_);
                egl_surface_ = EGL_NO_SURFACE;
            }
            return 1;
        });

    // eglGetError()
    add("libEGL.so", "eglGetError", HVC_EGL_GET_ERROR,
        [](guest_t *, const uint64_t[8]) -> uint64_t {
            return (uint64_t) eglGetError();
        });

    // eglQueryString(display, name)
    add("libEGL.so", "eglQueryString", HVC_EGL_QUERY_STRING,
        [this](guest_t *, const uint64_t a[8]) -> uint64_t {
            if (egl_display_ == EGL_NO_DISPLAY)
                return 0;
            const char *s = eglQueryString(egl_display_, (EGLint) a[1]);
            // Return host pointer — guest reads it via HVC so it's fine
            return s ? (uint64_t) (uintptr_t) s : 0;
        });

    // eglSwapInterval(display, interval)
    add("libEGL.so", "eglSwapInterval", HVC_EGL_SWAP_INTERVAL,
        [this](guest_t *, const uint64_t a[8]) -> uint64_t {
            if (egl_display_ == EGL_NO_DISPLAY)
                return 0;
            return eglSwapInterval(egl_display_, (EGLint) a[1]) ? 1 : 0;
        });

    add("libEGL.so", "eglSurfaceAttrib", HVC_EGL_SURFACE_ATTRIB,
        [this](guest_t *, const uint64_t a[8]) -> uint64_t {
            if (egl_display_ == EGL_NO_DISPLAY)
                return 0;
            EGLSurface surface = a[1] == GUEST_EGL_SURFACE
                                     ? egl_surface_
                                     : (EGLSurface) (uintptr_t) a[1];
            if (surface == EGL_NO_SURFACE)
                return 1;
            return eglSurfaceAttrib(egl_display_, surface, (EGLint) a[2],
                                    (EGLint) a[3])
                       ? 1
                       : 0;
        });

    // eglTerminate(display)
    add("libEGL.so", "eglTerminate", HVC_EGL_TERMINATE,
        [this](guest_t *, const uint64_t[8]) -> uint64_t {
            if (egl_display_ != EGL_NO_DISPLAY) {
                eglTerminate(egl_display_);
                egl_display_ = EGL_NO_DISPLAY;
            }
            return 1;
        });

    // eglReleaseThread()
    add("libEGL.so", "eglReleaseThread", HVC_EGL_RELEASE_THREAD,
        [](guest_t *, const uint64_t[8]) -> uint64_t {
            eglReleaseThread();
            return 1;
        });

    // eglGetProcAddress(const char* procname)
    // This is the key entry point — games resolve all their GL symbols through
    // it.
    add("libEGL.so", "eglGetProcAddress", HVC_EGL_GET_PROC_ADDRESS,
        [this](guest_t *g, const uint64_t a[8]) -> uint64_t {
            auto name = guest_read_string(g, a[0]);
            if (name.empty())
                return 0;

            // Check if we already allocated a stub for this proc
            for (auto &[nr, fn] : proc_addr_handlers_) {
                (void) fn;
                // We store name in sym_tables_ under a synthetic soname
                auto it = sym_tables_["__procaddr__"].find(name);
                if (it != sym_tables_["__procaddr__"].end()) {
                    return it->second;
                }
            }

            // Look up the real host function via ANGLE
            void *host_fn = angle_sym(name.c_str());
            if (!host_fn) {
                // Try via eglGetProcAddress on the host (covers extensions)
                using ProcFn = void *(EGLAPIENTRYP) (const char *);
                auto host_gpa =
                    (ProcFn)::dlsym(angle_egl_lib_, "eglGetProcAddress");
                if (host_gpa)
                    host_fn = host_gpa(name.c_str());
            }

            if (!host_fn) {
                log_warn("[EGL] eglGetProcAddress(%s) → NOT FOUND",
                         name.c_str());
                return 0;
            }

            if (next_proc_hvc_ >= HVC_GL_PROC_LIMIT) {
                log_debug(
                    "[EGL] eglGetProcAddress(%s) → procaddr stub "
                    "arena exhausted",
                    name.c_str());
                return 0;
            }

            // Allocate a new HVC stub number and write the stub
            uint32_t nr = next_proc_hvc_++;
            uint64_t gpa = write_stub(nr);

            // Capture host_fn in the handler — called when the guest executes
            // the stub
            void *captured_fn = host_fn;
            handlers_[nr] = [captured_fn, name](guest_t *,
                                                const uint64_t[8]) -> uint64_t {
                // We can't safely forward AArch64 register args to an arbitrary
                // C function pointer via a generic handler. Log it for now.
                // TODO Phase 5: emit a real trampoline stub that forwards
                // X0..X7
                log_debug("[GL] %s() called via procaddr stub", name.c_str());
                (void) captured_fn;
                return 0;
            };

            // Record for deduplication
            sym_tables_["__procaddr__"][name] = gpa;
            proc_addr_handlers_[nr] = host_fn;

            log_debug("[EGL] eglGetProcAddress(%s) → stub GPA 0x%llx",
                      name.c_str(), (unsigned long long) gpa);
            return gpa;
        });

    // Mirror stubs under libGLESv2.so for the EGL symbols some games dlopen
    // directly
    for (auto &[sym, gpa] : sym_tables_["libEGL.so"])
        sym_tables_["libGLESv2.so"][sym] = gpa;

    // eglGetConfigs(display, configs, config_size, num_config)
    add("libEGL.so", "eglGetConfigs", HVC_EGL_GET_CONFIGS,
        [this](guest_t *g, const uint64_t a[8]) -> uint64_t {
            if (a[0] != GUEST_EGL_DISPLAY || egl_display_ == EGL_NO_DISPLAY)
                return 0;
            EGLint num = 0;
            EGLBoolean ok = eglGetConfigs(egl_display_, nullptr, 0, &num);
            if (a[4])
                guest_write_u32(g, a[4], (uint32_t) (ok ? num : 0));
            // If guest wants the config array, write our single config
            if (a[1] && a[2] && ok && num > 0) {
                guest_write_u64(g, a[1], GUEST_EGL_CONFIG);
            }
            return ok ? 1 : 0;
        });

    // eglGetConfigAttrib(display, config, attribute, value)
    add("libEGL.so", "eglGetConfigAttrib", HVC_EGL_GET_CONFIG_ATTRIB,
        [this](guest_t *g, const uint64_t a[8]) -> uint64_t {
            if (a[0] != GUEST_EGL_DISPLAY || egl_display_ == EGL_NO_DISPLAY)
                return 0;
            EGLConfig cfg = (a[1] == GUEST_EGL_CONFIG)
                                ? egl_config_
                                : (EGLConfig) (uintptr_t) a[1];
            EGLint value = 0;
            EGLBoolean ok =
                eglGetConfigAttrib(egl_display_, cfg, (EGLint) a[2], &value);
            if (a[3])
                guest_write_u32(g, a[3], (uint32_t) value);
            return ok ? 1 : 0;
        });

    // eglGetCurrentContext()
    add("libEGL.so", "eglGetCurrentContext", HVC_EGL_GET_CURRENT_CONTEXT,
        [this](guest_t *, const uint64_t[8]) -> uint64_t {
            EGLContext ctx = eglGetCurrentContext();
            if (ctx == EGL_NO_CONTEXT)
                return 0;
            return (ctx == egl_context_) ? GUEST_EGL_CONTEXT
                                         : (uint64_t) (uintptr_t) ctx;
        });

    // eglCreateImage(display, context, target, buffer, attrib_list) - EGL 1.5
    // Returns EGL_NO_IMAGE (0) for unsupported targets; extensions handled via
    // GetProcAddress
    add("libEGL.so", "eglCreateImage", HVC_EGL_CREATE_IMAGE,
        [](guest_t *, const uint64_t[8]) -> uint64_t {
            // Stub: eglCreateImage requires KHR extension on ANGLE
            // Callers must use eglGetProcAddress("eglCreateImageKHR")
            log_debug(
                "[EGL] eglCreateImage called (stub returns EGL_NO_IMAGE)");
            return 0;  // EGL_NO_IMAGE
        });

    // eglDestroyImage(display, image) - EGL 1.5
    add("libEGL.so", "eglDestroyImage", HVC_EGL_DESTROY_IMAGE,
        [](guest_t *, const uint64_t[8]) -> uint64_t {
            return 1;  // EGL_TRUE
        });

    // eglGetPlatformDisplay(platform, native_display, attrib_list) - EGL 1.5
    add("libEGL.so", "eglGetPlatformDisplay", HVC_EGL_GET_PLATFORM_DISPLAY,
        [this](guest_t *, const uint64_t[8]) -> uint64_t {
            // Fall back to the default display we already have
            if (egl_display_ == EGL_NO_DISPLAY)
                egl_display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
            return (egl_display_ != EGL_NO_DISPLAY) ? GUEST_EGL_DISPLAY : 0;
        });

    // eglQueryContext(display, context, attribute, value)
    add("libEGL.so", "eglQueryContext", HVC_EGL_QUERY_CONTEXT,
        [this](guest_t *g, const uint64_t a[8]) -> uint64_t {
            if (a[0] != GUEST_EGL_DISPLAY || egl_display_ == EGL_NO_DISPLAY)
                return 0;
            EGLContext ctx = (a[1] == GUEST_EGL_CONTEXT)
                                 ? egl_context_
                                 : (EGLContext) (uintptr_t) a[1];
            EGLint value = 0;
            EGLBoolean ok =
                eglQueryContext(egl_display_, ctx, (EGLint) a[2], &value);
            if (a[3])
                guest_write_u32(g, a[3], (uint32_t) value);
            return ok ? 1 : 0;
        });
}

void GpuBridge::register_libgles_stubs()
{
    // Convenience macro-style lambdas for the common zero-arg GL calls
    auto gl_void0 = [](auto fn) {
        return [fn](guest_t *, const uint64_t[8]) -> uint64_t {
            fn();
            return 0;
        };
    };

    add("libGLESv2.so", "glGetError", HVC_GL_GET_ERROR,
        [](guest_t *, const uint64_t[8]) -> uint64_t {
            return (uint64_t) glGetError();
        });

    add("libGLESv2.so", "glViewport", HVC_GL_VIEWPORT,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glViewport((GLint) a[0], (GLint) a[1], (GLsizei) a[2],
                       (GLsizei) a[3]);
            return 0;
        });

    add("libGLESv2.so", "glClear", HVC_GL_CLEAR,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glClear((GLbitfield) a[0]);
            return 0;
        });

    add("libGLESv2.so", "glClearColor", HVC_GL_CLEAR_COLOR,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            float r, g, b, al;
            memcpy(&r, &a[0], 4);
            memcpy(&g, &a[1], 4);
            memcpy(&b, &a[2], 4);
            memcpy(&al, &a[3], 4);
            glClearColor(r, g, b, al);
            return 0;
        });

    add("libGLESv2.so", "glClearDepthf", HVC_GL_CLEAR_DEPTH,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            float d;
            memcpy(&d, &a[0], 4);
            glClearDepthf(d);
            return 0;
        });

    add("libGLESv2.so", "glEnable", HVC_GL_ENABLE,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glEnable((GLenum) a[0]);
            return 0;
        });
    add("libGLESv2.so", "glDisable", HVC_GL_DISABLE,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glDisable((GLenum) a[0]);
            return 0;
        });
    add("libGLESv2.so", "glBlendFunc", HVC_GL_BLEND_FUNC,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glBlendFunc((GLenum) a[0], (GLenum) a[1]);
            return 0;
        });
    add("libGLESv2.so", "glBlendFuncSeparate", HVC_GL_BLEND_FUNC_SEPARATE,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glBlendFuncSeparate((GLenum) a[0], (GLenum) a[1], (GLenum) a[2],
                                (GLenum) a[3]);
            return 0;
        });
    add("libGLESv2.so", "glBlendEquationSeparate",
        HVC_GL_BLEND_EQUATION_SEPARATE,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glBlendEquationSeparate((GLenum) a[0], (GLenum) a[1]);
            return 0;
        });
    add("libGLESv2.so", "glDepthFunc", HVC_GL_DEPTH_FUNC,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glDepthFunc((GLenum) a[0]);
            return 0;
        });
    add("libGLESv2.so", "glDepthMask", HVC_GL_DEPTH_MASK,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glDepthMask((GLboolean) a[0]);
            return 0;
        });
    add("libGLESv2.so", "glCullFace", HVC_GL_CULL_FACE,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glCullFace((GLenum) a[0]);
            return 0;
        });
    add("libGLESv2.so", "glFrontFace", HVC_GL_FRONT_FACE,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glFrontFace((GLenum) a[0]);
            return 0;
        });
    add("libGLESv2.so", "glPolygonOffset", HVC_GL_POLYGON_OFFSET,
        [](guest_t *, const uint64_t[8]) -> uint64_t {
            glPolygonOffset(0.0f, 0.0f);
            return 0;
        });
    add("libGLESv2.so", "glScissor", HVC_GL_SCISSOR,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glScissor((GLint) a[0], (GLint) a[1], (GLsizei) a[2],
                      (GLsizei) a[3]);
            return 0;
        });
    add("libGLESv2.so", "glColorMask", HVC_GL_COLOR_MASK,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glColorMask((GLboolean) a[0], (GLboolean) a[1], (GLboolean) a[2],
                        (GLboolean) a[3]);
            return 0;
        });
    add("libGLESv2.so", "glPixelStorei", HVC_GL_PIXEL_STOREI,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glPixelStorei((GLenum) a[0], (GLint) a[1]);
            return 0;
        });
    add("libGLESv2.so", "glActiveTexture", HVC_GL_ACTIVE_TEXTURE,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glActiveTexture((GLenum) a[0]);
            return 0;
        });
    add("libGLESv2.so", "glFinish", HVC_GL_FINISH,
        [&](guest_t *, const uint64_t[8]) -> uint64_t {
            glFinish();
            return 0;
        });
    add("libGLESv2.so", "glFlush", HVC_GL_FLUSH,
        [&](guest_t *, const uint64_t[8]) -> uint64_t {
            glFlush();
            return 0;
        });
    (void) gl_void0;  // suppress unused warning

    // Textures
    add("libGLESv2.so", "glGenTextures", HVC_GL_GEN_TEXTURES,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            std::vector<GLuint> ids(a[0]);
            glGenTextures((GLsizei) a[0], ids.data());
            for (GLsizei i = 0; i < (GLsizei) a[0]; ++i)
                guest_write_u32(g, a[1] + i * 4, ids[i]);
            return 0;
        });

    add("libGLESv2.so", "glBindTexture", HVC_GL_BIND_TEXTURE,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glBindTexture((GLenum) a[0], (GLuint) a[1]);
            return 0;
        });
    add("libGLESv2.so", "glTexParameteri", HVC_GL_TEX_PARAMETERI,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glTexParameteri((GLenum) a[0], (GLenum) a[1], (GLint) a[2]);
            return 0;
        });
    add("libGLESv2.so", "glGenerateMipmap", HVC_GL_GENERATE_MIPMAP,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glGenerateMipmap((GLenum) a[0]);
            return 0;
        });
    add("libGLESv2.so", "glDeleteTextures", HVC_GL_DELETE_TEXTURES,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            std::vector<GLuint> ids(a[0]);
            for (GLsizei i = 0; i < (GLsizei) a[0]; ++i)
                ids[i] = (GLuint) guest_read_u32(g, a[1] + i * 4);
            glDeleteTextures((GLsizei) a[0], ids.data());
            return 0;
        });

    add("libGLESv2.so", "glTexImage2D", HVC_GL_TEX_IMAGE2D,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            // a[0]=target a[1]=level a[2]=internalformat a[3]=width a[4]=height
            // a[5]=border a[6]=format a[7]=type  (pixels ptr lost — need extra
            // read) For now call with null pixels (enough to allocate the
            // texture storage)
            glTexImage2D((GLenum) a[0], (GLint) a[1], (GLint) a[2],
                         (GLsizei) a[3], (GLsizei) a[4], (GLint) a[5],
                         (GLenum) a[6], (GLenum) a[7], nullptr);
            (void) g;
            return 0;
        });
    add("libGLESv2.so", "glTexSubImage2D", HVC_GL_TEX_SUB_IMAGE_2D,
        [](guest_t *, const uint64_t[8]) -> uint64_t {
            // Nine ABI arguments; trailing pixels pointer is not preserved.
            return 0;
        });

    add("libGLESv2.so", "glTexStorage2D", HVC_GL_TEX_STORAGE_2D,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glTexStorage2D((GLenum) a[0], (GLsizei) a[1], (GLenum) a[2],
                           (GLsizei) a[3], (GLsizei) a[4]);
            return 0;
        });
    add("libGLESv2.so", "glTexStorage3D", HVC_GL_TEX_STORAGE_3D,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glTexStorage3D((GLenum) a[0], (GLsizei) a[1], (GLenum) a[2],
                           (GLsizei) a[3], (GLsizei) a[4], (GLsizei) a[5]);
            return 0;
        });
    add("libGLESv2.so", "glCompressedTexSubImage3D",
        HVC_GL_COMPRESSED_TEX_SUB_IMAGE_3D,
        [](guest_t *, const uint64_t[8]) -> uint64_t {
            // This has more than eight ABI arguments; the current HVC shim
            // does not preserve the trailing image pointer, so keep it a no-op.
            return 0;
        });
    add("libGLESv2.so", "glCompressedTexSubImage2D",
        HVC_GL_COMPRESSED_TEX_SUB_IMAGE_2D,
        [](guest_t *, const uint64_t[8]) -> uint64_t {
            // Nine ABI arguments; trailing image pointer is not preserved.
            return 0;
        });
    add("libGLESv2.so", "glTexSubImage3D", HVC_GL_TEX_SUB_IMAGE_3D,
        [](guest_t *, const uint64_t[8]) -> uint64_t {
            // This also has more than eight ABI arguments; see compressed path.
            return 0;
        });

    // Buffers
    add("libGLESv2.so", "glGenBuffers", HVC_GL_GEN_BUFFERS,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            std::vector<GLuint> ids(a[0]);
            glGenBuffers((GLsizei) a[0], ids.data());
            for (GLsizei i = 0; i < (GLsizei) a[0]; ++i)
                guest_write_u32(g, a[1] + i * 4, ids[i]);
            return 0;
        });

    add("libGLESv2.so", "glBindBuffer", HVC_GL_BIND_BUFFER,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glBindBuffer((GLenum) a[0], (GLuint) a[1]);
            return 0;
        });
    add("libGLESv2.so", "glBindBufferRange", HVC_GL_BIND_BUFFER_RANGE,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glBindBufferRange((GLenum) a[0], (GLuint) a[1], (GLuint) a[2],
                              (GLintptr) a[3], (GLsizeiptr) a[4]);
            return 0;
        });
    add("libGLESv2.so", "glDeleteBuffers", HVC_GL_DELETE_BUFFERS,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            std::vector<GLuint> ids(a[0]);
            for (GLsizei i = 0; i < (GLsizei) a[0]; ++i)
                ids[i] = guest_read_u32(g, a[1] + i * 4);
            glDeleteBuffers((GLsizei) a[0], ids.data());
            return 0;
        });

    add("libGLESv2.so", "glBufferData", HVC_GL_BUFFER_DATA,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            // a[0]=target a[1]=size a[2]=data_gpa a[3]=usage
            std::vector<uint8_t> buf;
            if (a[2] && a[1]) {
                buf.resize(a[1]);
                guest_read(g, a[2], buf.data(), a[1]);
            }
            glBufferData((GLenum) a[0], (GLsizeiptr) a[1],
                         buf.empty() ? nullptr : buf.data(), (GLenum) a[3]);
            return 0;
        });

    add("libGLESv2.so", "glBufferSubData", HVC_GL_BUFFER_SUB_DATA,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            // a[0]=target a[1]=offset a[2]=size a[3]=data_gpa
            std::vector<uint8_t> buf;
            if (a[3] && a[2]) {
                buf.resize(static_cast<size_t>(a[2]));
                guest_read(g, a[3], buf.data(), a[2]);
            }
            glBufferSubData((GLenum) a[0], (GLintptr) a[1], (GLsizeiptr) a[2],
                            buf.empty() ? nullptr : buf.data());
            return 0;
        });

    add("libGLESv2.so", "glMapBufferRange", HVC_GL_MAP_BUFFER_RANGE,
        [this](guest_t *g, const uint64_t a[8]) -> uint64_t {
            (void) a;
            uint64_t len = std::min<uint64_t>(a[2], 1024 * 1024);
            if (!len)
                return 0;
            uint64_t ptr = (heap_bump_ + 15) & ~15ULL;
            uint64_t sz = (len + 15) & ~15ULL;
            if (ptr + sz > heap_base_ + HEAP_SIZE)
                return 0;
            heap_bump_ = ptr + sz;
            std::vector<uint8_t> zero(static_cast<size_t>(sz), 0);
            guest_write(g, ptr, zero.data(), sz);
            return ptr;
        });
    add("libGLESv2.so", "glUnmapBuffer", HVC_GL_UNMAP_BUFFER,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return GL_TRUE; });

    // Shaders & programs
    add("libGLESv2.so", "glCreateShader", HVC_GL_CREATE_SHADER,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            return (uint64_t) glCreateShader((GLenum) a[0]);
        });
    add("libGLESv2.so", "glDeleteShader", HVC_GL_DELETE_SHADER,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glDeleteShader((GLuint) a[0]);
            return 0;
        });
    add("libGLESv2.so", "glCreateProgram", HVC_GL_CREATE_PROGRAM,
        [](guest_t *, const uint64_t[8]) -> uint64_t {
            return (uint64_t) glCreateProgram();
        });
    add("libGLESv2.so", "glDeleteProgram", HVC_GL_DELETE_PROGRAM,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glDeleteProgram((GLuint) a[0]);
            return 0;
        });
    add("libGLESv2.so", "glAttachShader", HVC_GL_ATTACH_SHADER,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glAttachShader((GLuint) a[0], (GLuint) a[1]);
            return 0;
        });
    add("libGLESv2.so", "glDetachShader", HVC_GL_DETACH_SHADER,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glDetachShader((GLuint) a[0], (GLuint) a[1]);
            return 0;
        });
    add("libGLESv2.so", "glLinkProgram", HVC_GL_LINK_PROGRAM,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glLinkProgram((GLuint) a[0]);
            return 0;
        });
    add("libGLESv2.so", "glUseProgram", HVC_GL_USE_PROGRAM,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glUseProgram((GLuint) a[0]);
            return 0;
        });

    add("libGLESv2.so", "glShaderSource", HVC_GL_SHADER_SOURCE,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            // a[0]=shader a[1]=count a[2]=string_ptrs_gpa a[3]=lengths_gpa
            GLsizei count = (GLsizei) a[1];
            std::vector<std::string> srcs(count);
            std::vector<const char *> ptrs(count);
            for (GLsizei i = 0; i < count; ++i) {
                uint64_t ptr_gpa = guest_read_u64(g, a[2] + i * 8);
                srcs[i] = guest_read_string(g, ptr_gpa);
                ptrs[i] = srcs[i].c_str();
            }
            glShaderSource((GLuint) a[0], count, ptrs.data(), nullptr);
            return 0;
        });

    add("libGLESv2.so", "glCompileShader", HVC_GL_COMPILE_SHADER,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glCompileShader((GLuint) a[0]);
            return 0;
        });

    add("libGLESv2.so", "glGetShaderiv", HVC_GL_GET_SHADER_IV,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            GLint v = 0;
            glGetShaderiv((GLuint) a[0], (GLenum) a[1], &v);
            if (a[2])
                guest_write_u32(g, a[2], (uint32_t) v);
            return 0;
        });

    add("libGLESv2.so", "glGetProgramiv", HVC_GL_GET_PROGRAM_IV,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            GLint v = 0;
            glGetProgramiv((GLuint) a[0], (GLenum) a[1], &v);
            if (a[2])
                guest_write_u32(g, a[2], (uint32_t) v);
            return 0;
        });

    add("libGLESv2.so", "glGetShaderInfoLog", HVC_GL_GET_SHADER_INFO_LOG,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            std::vector<char> log(a[1] ? a[1] : 512);
            GLsizei len = 0;
            glGetShaderInfoLog((GLuint) a[0], (GLsizei) a[1], &len, log.data());
            if (a[3])
                guest_write(g, a[3], log.data(), len + 1);
            if (a[2])
                guest_write_u32(g, a[2], (uint32_t) len);
            return 0;
        });

    add("libGLESv2.so", "glGetProgramInfoLog", HVC_GL_GET_PROGRAM_INFO_LOG,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            std::vector<char> log(a[1] ? a[1] : 512);
            GLsizei len = 0;
            glGetProgramInfoLog((GLuint) a[0], (GLsizei) a[1], &len,
                                log.data());
            if (a[3])
                guest_write(g, a[3], log.data(), len + 1);
            if (a[2])
                guest_write_u32(g, a[2], (uint32_t) len);
            return 0;
        });

    // Attributes & uniforms
    add("libGLESv2.so", "glGetAttribLocation", HVC_GL_GET_ATTRIB_LOC,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            auto name = guest_read_string(g, a[1]);
            return (uint64_t) (int64_t) glGetAttribLocation((GLuint) a[0],
                                                            name.c_str());
        });
    add("libGLESv2.so", "glGetUniformLocation", HVC_GL_GET_UNIFORM_LOC,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            auto name = guest_read_string(g, a[1]);
            return (uint64_t) (int64_t) glGetUniformLocation((GLuint) a[0],
                                                             name.c_str());
        });
    add("libGLESv2.so", "glEnableVertexAttribArray", HVC_GL_ENABLE_VERT_ATTRIB,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glEnableVertexAttribArray((GLuint) a[0]);
            return 0;
        });
    add("libGLESv2.so", "glDisableVertexAttribArray",
        HVC_GL_DISABLE_VERT_ATTRIB,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glDisableVertexAttribArray((GLuint) a[0]);
            return 0;
        });

    add("libGLESv2.so", "glGenVertexArrays", HVC_GL_GEN_VERTEX_ARRAYS,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            GLsizei count =
                static_cast<GLsizei>(std::min<uint64_t>(a[0], 4096));
            std::vector<GLuint> ids(static_cast<size_t>(count));
            glGenVertexArrays(count, ids.data());
            for (GLsizei i = 0; i < count; ++i)
                guest_write_u32(g, a[1] + i * 4, ids[static_cast<size_t>(i)]);
            return 0;
        });
    add("libGLESv2.so", "glBindVertexArray", HVC_GL_BIND_VERTEX_ARRAY,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glBindVertexArray((GLuint) a[0]);
            return 0;
        });
    add("libGLESv2.so", "glDeleteVertexArrays", HVC_GL_DELETE_VERTEX_ARRAYS,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            GLsizei count =
                static_cast<GLsizei>(std::min<uint64_t>(a[0], 4096));
            std::vector<GLuint> ids(static_cast<size_t>(count));
            for (GLsizei i = 0; i < count; ++i)
                ids[static_cast<size_t>(i)] = guest_read_u32(g, a[1] + i * 4);
            glDeleteVertexArrays(count, ids.data());
            return 0;
        });

    add("libGLESv2.so", "glVertexAttribPointer", HVC_GL_VERT_ATTRIB_PTR,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glVertexAttribPointer(
                (GLuint) a[0], (GLint) a[1], (GLenum) a[2], (GLboolean) a[3],
                (GLsizei) a[4],
                reinterpret_cast<const void *>((uintptr_t) a[5]));
            return 0;
        });
    add("libGLESv2.so", "glVertexAttribIPointer", HVC_GL_VERTEX_ATTRIB_IPOINTER,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glVertexAttribIPointer(
                (GLuint) a[0], (GLint) a[1], (GLenum) a[2], (GLsizei) a[3],
                reinterpret_cast<const void *>((uintptr_t) a[4]));
            return 0;
        });
    add("libGLESv2.so", "glVertexAttrib4f", HVC_GL_VERTEX_ATTRIB4F,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            float x = 0, y = 0, z = 0, w = 0;
            memcpy(&x, &a[1], 4);
            memcpy(&y, &a[2], 4);
            memcpy(&z, &a[3], 4);
            memcpy(&w, &a[4], 4);
            glVertexAttrib4f((GLuint) a[0], x, y, z, w);
            return 0;
        });
    add("libGLESv2.so", "glVertexAttribI4ui", HVC_GL_VERTEX_ATTRIB_I4UI,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glVertexAttribI4ui((GLuint) a[0], (GLuint) a[1], (GLuint) a[2],
                               (GLuint) a[3], (GLuint) a[4]);
            return 0;
        });

    // Draw calls
    add("libGLESv2.so", "glDrawArrays", HVC_GL_DRAW_ARRAYS,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glDrawArrays((GLenum) a[0], (GLint) a[1], (GLsizei) a[2]);
            return 0;
        });
    add("libGLESv2.so", "glDrawElements", HVC_GL_DRAW_ELEMENTS,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glDrawElements((GLenum) a[0], (GLsizei) a[1], (GLenum) a[2],
                           reinterpret_cast<const void *>((uintptr_t) a[3]));
            return 0;
        });
    add("libGLESv2.so", "glDrawRangeElements", HVC_GL_DRAW_RANGE_ELEMENTS,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glDrawRangeElements(
                (GLenum) a[0], (GLuint) a[1], (GLuint) a[2], (GLsizei) a[3],
                (GLenum) a[4],
                reinterpret_cast<const void *>((uintptr_t) a[5]));
            return 0;
        });
    add("libGLESv2.so", "glBlitFramebuffer", HVC_GL_BLIT_FRAMEBUFFER,
        [](guest_t *, const uint64_t[8]) -> uint64_t {
            // This has ten scalar arguments; keep it a no-op under the current
            // shim.
            return 0;
        });

    // Uniforms
    add("libGLESv2.so", "glUniform1i", HVC_GL_UNIFORM1I,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glUniform1i((GLint) a[0], (GLint) a[1]);
            return 0;
        });
    add("libGLESv2.so", "glUniform1f", HVC_GL_UNIFORM1F,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            float v;
            memcpy(&v, &a[1], 4);
            glUniform1f((GLint) a[0], v);
            return 0;
        });
    add("libGLESv2.so", "glUniform4f", HVC_GL_UNIFORM4F,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            float x, y, z, w;
            memcpy(&x, &a[1], 4);
            memcpy(&y, &a[2], 4);
            memcpy(&z, &a[3], 4);
            memcpy(&w, &a[4], 4);
            glUniform4f((GLint) a[0], x, y, z, w);
            return 0;
        });
    add("libGLESv2.so", "glUniformBlockBinding", HVC_GL_UNIFORM_BLOCK_BINDING,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glUniformBlockBinding((GLuint) a[0], (GLuint) a[1], (GLuint) a[2]);
            return 0;
        });
    add("libGLESv2.so", "glGetUniformBlockIndex",
        HVC_GL_GET_UNIFORM_BLOCK_INDEX,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            auto name = guest_read_string(g, a[1]);
            GLuint idx = glGetUniformBlockIndex((GLuint) a[0], name.c_str());
            return idx == GL_INVALID_INDEX
                       ? static_cast<uint64_t>(GL_INVALID_INDEX)
                       : idx;
        });
    add("libGLESv2.so", "glSamplerParameterf", HVC_GL_SAMPLER_PARAMETERF,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glSamplerParameterf((GLuint) a[0], (GLenum) a[1], 0.0f);
            return 0;
        });
    add("libGLESv2.so", "glSamplerParameteri", HVC_GL_SAMPLER_PARAMETERI,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glSamplerParameteri((GLuint) a[0], (GLenum) a[1], (GLint) a[2]);
            return 0;
        });
    add("libGLESv2.so", "glBindSampler", HVC_GL_BIND_SAMPLER,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glBindSampler((GLuint) a[0], (GLuint) a[1]);
            return 0;
        });
    add("libGLESv2.so", "glGenSamplers", HVC_GL_GEN_SAMPLERS,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            GLsizei count =
                static_cast<GLsizei>(std::min<uint64_t>(a[0], 4096));
            std::vector<GLuint> ids(static_cast<size_t>(count));
            glGenSamplers(count, ids.data());
            for (GLsizei i = 0; i < count; ++i)
                guest_write_u32(g, a[1] + i * 4, ids[static_cast<size_t>(i)]);
            return 0;
        });
    add("libGLESv2.so", "glDeleteSamplers", HVC_GL_DELETE_SAMPLERS,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            GLsizei count =
                static_cast<GLsizei>(std::min<uint64_t>(a[0], 4096));
            std::vector<GLuint> ids(static_cast<size_t>(count));
            for (GLsizei i = 0; i < count; ++i)
                ids[static_cast<size_t>(i)] = guest_read_u32(g, a[1] + i * 4);
            glDeleteSamplers(count, ids.data());
            return 0;
        });
    add("libGLESv2.so", "glUniformMatrix4fv", HVC_GL_UNIFORM_MATRIX4FV,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            // a[0]=location a[1]=count a[2]=transpose a[3]=value_gpa
            GLsizei count = (GLsizei) a[1];
            std::vector<float> mat(count * 16);
            guest_read(g, a[3], mat.data(), mat.size() * 4);
            glUniformMatrix4fv((GLint) a[0], count, (GLboolean) a[2],
                               mat.data());
            return 0;
        });

    // Queries
    add("libGLESv2.so", "glGetIntegerv", HVC_GL_GET_INTEGERV,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            GLint v = 0;
            glGetIntegerv((GLenum) a[0], &v);
            if (a[1])
                guest_write_u32(g, a[1], (uint32_t) v);
            return 0;
        });
    add("libGLESv2.so", "glGetFloatv", HVC_GL_GET_FLOATV,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            GLfloat v = 0.0f;
            if (a[1])
                guest_write(g, a[1], &v, sizeof(v));
            return 0;
        });
    add("libGLESv2.so", "glGetString", HVC_GL_GET_STRING,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            const GLubyte *s = glGetString((GLenum) a[0]);
            return s ? (uint64_t) (uintptr_t) s : 0;
        });
    add("libGLESv2.so", "glIsEnabled", HVC_GL_IS_ENABLED,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            return glIsEnabled((GLenum) a[0]) ? GL_TRUE : GL_FALSE;
        });
    add("libGLESv2.so", "glGetStringi", HVC_GL_GET_STRINGI,
        [this](guest_t *g, const uint64_t a[8]) -> uint64_t {
            const GLubyte *raw = glGetStringi((GLenum) a[0], (GLuint) a[1]);
            if (!raw)
                return 0;
            std::string text = reinterpret_cast<const char *>(raw);
            uint64_t sz = (text.size() + 1 + 15) & ~15ULL;
            if (heap_bump_ + sz > heap_base_ + HEAP_SIZE)
                return 0;
            uint64_t ptr = heap_bump_;
            heap_bump_ += sz;
            guest_write(g, ptr, text.c_str(), text.size() + 1);
            return ptr;
        });
    add("libGLESv2.so", "glGetVertexAttribiv", HVC_GL_GET_VERTEX_ATTRIB_IV,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            GLint value = 0;
            glGetVertexAttribiv((GLuint) a[0], (GLenum) a[1], &value);
            if (a[2])
                guest_write_u32(g, a[2], static_cast<uint32_t>(value));
            return 0;
        });
    add("libGLESv2.so", "glClientWaitSync", HVC_GL_CLIENT_WAIT_SYNC,
        [](guest_t *, const uint64_t[8]) -> uint64_t {
            return GL_ALREADY_SIGNALED;
        });
    add("libGLESv2.so", "glFenceSync", HVC_GL_FENCE_SYNC,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 1; });
    add("libGLESv2.so", "glDeleteSync", HVC_GL_DELETE_SYNC,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });
    add("libGLESv2.so", "glWaitSync", HVC_GL_WAIT_SYNC,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });
    add("libGLESv2.so", "glDrawBuffers", HVC_GL_DRAW_BUFFERS,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            GLsizei count = static_cast<GLsizei>(std::min<uint64_t>(a[0], 64));
            std::vector<GLenum> bufs(static_cast<size_t>(count));
            for (GLsizei i = 0; i < count; ++i)
                bufs[static_cast<size_t>(i)] =
                    static_cast<GLenum>(guest_read_u32(g, a[1] + i * 4));
            glDrawBuffers(count, bufs.data());
            return 0;
        });
    add("libGLESv2.so", "glReadPixels", HVC_GL_READ_PIXELS,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            if (!a[6])
                return 0;
            uint64_t width = a[2];
            uint64_t height = a[3];
            uint64_t bytes =
                std::min<uint64_t>(width * height * 4, 1024 * 1024);
            std::vector<uint8_t> zero(static_cast<size_t>(bytes), 0);
            if (bytes)
                guest_write(g, a[6], zero.data(), bytes);
            return 0;
        });
    add("libGLESv2.so", "glClearBufferfv", HVC_GL_CLEAR_BUFFER_FV,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            GLfloat value[4] = {0, 0, 0, 0};
            if (a[2])
                guest_read(g, a[2], value, sizeof(value));
            glClearBufferfv((GLenum) a[0], (GLint) a[1], value);
            return 0;
        });
    add("libGLESv2.so", "glClearBufferfi", HVC_GL_CLEAR_BUFFER_FI,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glClearBufferfi((GLenum) a[0], (GLint) a[1], 0.0f, (GLint) a[2]);
            return 0;
        });
    add("libGLESv2.so", "glClearBufferiv", HVC_GL_CLEAR_BUFFER_IV,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            GLint value[4] = {0, 0, 0, 0};
            if (a[2])
                guest_read(g, a[2], value, sizeof(value));
            glClearBufferiv((GLenum) a[0], (GLint) a[1], value);
            return 0;
        });
    add("libGLESv2.so", "glGenQueries", HVC_GL_GEN_QUERIES,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            GLsizei count =
                static_cast<GLsizei>(std::min<uint64_t>(a[0], 4096));
            std::vector<GLuint> ids(static_cast<size_t>(count));
            glGenQueries(count, ids.data());
            for (GLsizei i = 0; i < count; ++i)
                guest_write_u32(g, a[1] + i * 4, ids[static_cast<size_t>(i)]);
            return 0;
        });
    add("libGLESv2.so", "glBeginQuery", HVC_GL_BEGIN_QUERY,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glBeginQuery((GLenum) a[0], (GLuint) a[1]);
            return 0;
        });
    add("libGLESv2.so", "glDeleteQueries", HVC_GL_DELETE_QUERIES,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            GLsizei count =
                static_cast<GLsizei>(std::min<uint64_t>(a[0], 4096));
            std::vector<GLuint> ids(static_cast<size_t>(count));
            for (GLsizei i = 0; i < count; ++i)
                ids[static_cast<size_t>(i)] = guest_read_u32(g, a[1] + i * 4);
            glDeleteQueries(count, ids.data());
            return 0;
        });
    add("libGLESv2.so", "glGetQueryObjectuiv", HVC_GL_GET_QUERY_OBJECT_UIV,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            if (a[2])
                guest_write_u32(g, a[2], 0);
            return 0;
        });
    add("libGLESv2.so", "glHint", HVC_GL_HINT,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glHint((GLenum) a[0], (GLenum) a[1]);
            return 0;
        });
    add("libGLESv2.so", "glInvalidateFramebuffer",
        HVC_GL_INVALIDATE_FRAMEBUFFER,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            GLsizei count = static_cast<GLsizei>(std::min<uint64_t>(a[1], 64));
            std::vector<GLenum> attachments(static_cast<size_t>(count));
            for (GLsizei i = 0; i < count; ++i)
                attachments[static_cast<size_t>(i)] =
                    static_cast<GLenum>(guest_read_u32(g, a[2] + i * 4));
            glInvalidateFramebuffer((GLenum) a[0], count, attachments.data());
            return 0;
        });
    add("libGLESv2.so", "glEndQuery", HVC_GL_END_QUERY,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glEndQuery((GLenum) a[0]);
            return 0;
        });

    // Framebuffers & renderbuffers
    add("libGLESv2.so", "glGenFramebuffers", HVC_GL_GEN_FRAMEBUFFERS,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            std::vector<GLuint> ids(a[0]);
            glGenFramebuffers((GLsizei) a[0], ids.data());
            for (GLsizei i = 0; i < (GLsizei) a[0]; ++i)
                guest_write_u32(g, a[1] + i * 4, ids[i]);
            return 0;
        });
    add("libGLESv2.so", "glDeleteFramebuffers", HVC_GL_DELETE_FRAMEBUFFERS2,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            GLsizei count =
                static_cast<GLsizei>(std::min<uint64_t>(a[0], 4096));
            std::vector<GLuint> ids(static_cast<size_t>(count));
            for (GLsizei i = 0; i < count; ++i)
                ids[static_cast<size_t>(i)] = guest_read_u32(g, a[1] + i * 4);
            glDeleteFramebuffers(count, ids.data());
            return 0;
        });
    add("libGLESv2.so", "glBindFramebuffer", HVC_GL_BIND_FRAMEBUFFER,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glBindFramebuffer((GLenum) a[0], (GLuint) a[1]);
            return 0;
        });
    add("libGLESv2.so", "glFramebufferTexture2D", HVC_GL_FRAMEBUFFER_TEXTURE2D,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glFramebufferTexture2D((GLenum) a[0], (GLenum) a[1], (GLenum) a[2],
                                   (GLuint) a[3], (GLint) a[4]);
            return 0;
        });
    add("libGLESv2.so", "glFramebufferTextureLayer",
        HVC_GL_FRAMEBUFFER_TEXTURE_LAYER,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glFramebufferTextureLayer((GLenum) a[0], (GLenum) a[1],
                                      (GLuint) a[2], (GLint) a[3],
                                      (GLint) a[4]);
            return 0;
        });
    add("libGLESv2.so", "glGenRenderbuffers", HVC_GL_GEN_RENDERBUFFERS,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            std::vector<GLuint> ids(a[0]);
            glGenRenderbuffers((GLsizei) a[0], ids.data());
            for (GLsizei i = 0; i < (GLsizei) a[0]; ++i)
                guest_write_u32(g, a[1] + i * 4, ids[i]);
            return 0;
        });
    add("libGLESv2.so", "glDeleteRenderbuffers", HVC_GL_DELETE_RENDERBUFFERS2,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            GLsizei count =
                static_cast<GLsizei>(std::min<uint64_t>(a[0], 4096));
            std::vector<GLuint> ids(static_cast<size_t>(count));
            for (GLsizei i = 0; i < count; ++i)
                ids[static_cast<size_t>(i)] = guest_read_u32(g, a[1] + i * 4);
            glDeleteRenderbuffers(count, ids.data());
            return 0;
        });
    add("libGLESv2.so", "glBindRenderbuffer", HVC_GL_BIND_RENDERBUFFER,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glBindRenderbuffer((GLenum) a[0], (GLuint) a[1]);
            return 0;
        });
    add("libGLESv2.so", "glRenderbufferStorage", HVC_GL_RENDERBUFFER_STORAGE,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glRenderbufferStorage((GLenum) a[0], (GLenum) a[1], (GLsizei) a[2],
                                  (GLsizei) a[3]);
            return 0;
        });
    add("libGLESv2.so", "glRenderbufferStorageMultisample",
        HVC_GL_RENDERBUFFER_STORAGE_MS,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glRenderbufferStorageMultisample((GLenum) a[0], (GLsizei) a[1],
                                             (GLenum) a[2], (GLsizei) a[3],
                                             (GLsizei) a[4]);
            return 0;
        });
    add("libGLESv2.so", "glFramebufferRenderbuffer",
        HVC_GL_FRAMEBUFFER_RENDERBUF,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            glFramebufferRenderbuffer((GLenum) a[0], (GLenum) a[1],
                                      (GLenum) a[2], (GLuint) a[3]);
            return 0;
        });

    // Mirror under libGLESv3.so — GLES3 is a superset
    sym_tables_["libGLESv3.so"] = sym_tables_["libGLESv2.so"];
}

}  // namespace muplar::runtime
