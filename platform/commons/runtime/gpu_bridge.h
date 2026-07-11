#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <memory>
#include <vector>

#include "host_window.h"

// ANGLE / EGL / GLES headers (macOS host-side)
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

extern "C" {
#include "core/guest.h"
}

namespace muplar::runtime
{

using BuiltinSymbols = std::unordered_map<std::string, uint64_t>;
using StubHandler = std::function<uint64_t(guest_t *, const uint64_t[8])>;


// HVC constants for EGL and GLESv2
static constexpr uint32_t HVC_EGL_GET_DISPLAY = 0x2400;
static constexpr uint32_t HVC_EGL_INITIALIZE = 0x2401;
static constexpr uint32_t HVC_EGL_BIND_API = 0x2402;
static constexpr uint32_t HVC_EGL_CHOOSE_CONFIG = 0x2403;
static constexpr uint32_t HVC_EGL_CREATE_CONTEXT = 0x2404;
static constexpr uint32_t HVC_EGL_CREATE_WIN_SURFACE = 0x2405;
static constexpr uint32_t HVC_EGL_CREATE_PBUF_SURFACE = 0x2406;
static constexpr uint32_t HVC_EGL_MAKE_CURRENT = 0x2407;
static constexpr uint32_t HVC_EGL_SWAP_BUFFERS = 0x2408;
static constexpr uint32_t HVC_EGL_DESTROY_CONTEXT = 0x2409;
static constexpr uint32_t HVC_EGL_DESTROY_SURFACE = 0x240A;
static constexpr uint32_t HVC_EGL_GET_ERROR = 0x240B;
static constexpr uint32_t HVC_EGL_QUERY_STRING = 0x240C;
static constexpr uint32_t HVC_EGL_SWAP_INTERVAL = 0x240D;
static constexpr uint32_t HVC_EGL_TERMINATE = 0x240E;
static constexpr uint32_t HVC_EGL_RELEASE_THREAD = 0x240F;
static constexpr uint32_t HVC_EGL_GET_PROC_ADDRESS = 0x2410;
static constexpr uint32_t HVC_EGL_SURFACE_ATTRIB = 0x2411;
static constexpr uint32_t HVC_EGL_GET_CONFIGS = 0x2412;
static constexpr uint32_t HVC_EGL_GET_CONFIG_ATTRIB = 0x2413;
static constexpr uint32_t HVC_EGL_GET_CURRENT_CONTEXT = 0x2414;
static constexpr uint32_t HVC_EGL_CREATE_IMAGE = 0x2415;
static constexpr uint32_t HVC_EGL_DESTROY_IMAGE = 0x2416;
static constexpr uint32_t HVC_EGL_GET_PLATFORM_DISPLAY = 0x2417;
static constexpr uint32_t HVC_EGL_QUERY_CONTEXT = 0x2418;
static constexpr uint32_t HVC_GL_GET_ERROR = 0x2500;
static constexpr uint32_t HVC_GL_VIEWPORT = 0x2501;
static constexpr uint32_t HVC_GL_CLEAR = 0x2502;
static constexpr uint32_t HVC_GL_CLEAR_COLOR = 0x2503;
static constexpr uint32_t HVC_GL_CLEAR_DEPTH = 0x2504;
static constexpr uint32_t HVC_GL_ENABLE = 0x2505;
static constexpr uint32_t HVC_GL_DISABLE = 0x2506;
static constexpr uint32_t HVC_GL_BLEND_FUNC = 0x2507;
static constexpr uint32_t HVC_GL_DEPTH_FUNC = 0x2508;
static constexpr uint32_t HVC_GL_DEPTH_MASK = 0x2509;
static constexpr uint32_t HVC_GL_CULL_FACE = 0x250A;
static constexpr uint32_t HVC_GL_FRONT_FACE = 0x250B;
static constexpr uint32_t HVC_GL_GEN_TEXTURES = 0x250C;
static constexpr uint32_t HVC_GL_BIND_TEXTURE = 0x250D;
static constexpr uint32_t HVC_GL_TEX_IMAGE2D = 0x250E;
static constexpr uint32_t HVC_GL_TEX_PARAMETERI = 0x250F;
static constexpr uint32_t HVC_GL_GEN_BUFFERS = 0x2510;
static constexpr uint32_t HVC_GL_BIND_BUFFER = 0x2511;
static constexpr uint32_t HVC_GL_BUFFER_DATA = 0x2512;
static constexpr uint32_t HVC_GL_CREATE_SHADER = 0x2513;
static constexpr uint32_t HVC_GL_SHADER_SOURCE = 0x2514;
static constexpr uint32_t HVC_GL_COMPILE_SHADER = 0x2515;
static constexpr uint32_t HVC_GL_CREATE_PROGRAM = 0x2516;
static constexpr uint32_t HVC_GL_ATTACH_SHADER = 0x2517;
static constexpr uint32_t HVC_GL_LINK_PROGRAM = 0x2518;
static constexpr uint32_t HVC_GL_USE_PROGRAM = 0x2519;
static constexpr uint32_t HVC_GL_GET_ATTRIB_LOC = 0x251A;
static constexpr uint32_t HVC_GL_GET_UNIFORM_LOC = 0x251B;
static constexpr uint32_t HVC_GL_ENABLE_VERT_ATTRIB = 0x251C;
static constexpr uint32_t HVC_GL_VERT_ATTRIB_PTR = 0x251D;
static constexpr uint32_t HVC_GL_DRAW_ARRAYS = 0x251E;
static constexpr uint32_t HVC_GL_DRAW_ELEMENTS = 0x251F;
static constexpr uint32_t HVC_GL_UNIFORM1I = 0x2520;
static constexpr uint32_t HVC_GL_UNIFORM1F = 0x2521;
static constexpr uint32_t HVC_GL_UNIFORM4F = 0x2522;
static constexpr uint32_t HVC_GL_UNIFORM_MATRIX4FV = 0x2523;
static constexpr uint32_t HVC_GL_GET_INTEGERV = 0x2524;
static constexpr uint32_t HVC_GL_GET_STRING = 0x2525;
static constexpr uint32_t HVC_GL_DELETE_TEXTURES = 0x2526;
static constexpr uint32_t HVC_GL_DELETE_BUFFERS = 0x2527;
static constexpr uint32_t HVC_GL_DELETE_SHADER = 0x2528;
static constexpr uint32_t HVC_GL_DELETE_PROGRAM = 0x2529;
static constexpr uint32_t HVC_GL_GEN_FRAMEBUFFERS = 0x252A;
static constexpr uint32_t HVC_GL_BIND_FRAMEBUFFER = 0x252B;
static constexpr uint32_t HVC_GL_FRAMEBUFFER_TEXTURE2D = 0x252C;
static constexpr uint32_t HVC_GL_GEN_RENDERBUFFERS = 0x252D;
static constexpr uint32_t HVC_GL_BIND_RENDERBUFFER = 0x252E;
static constexpr uint32_t HVC_GL_RENDERBUFFER_STORAGE = 0x252F;
static constexpr uint32_t HVC_GL_FRAMEBUFFER_RENDERBUF = 0x2530;
static constexpr uint32_t HVC_GL_FINISH = 0x2531;
static constexpr uint32_t HVC_GL_FLUSH = 0x2532;
static constexpr uint32_t HVC_GL_SCISSOR = 0x2533;
static constexpr uint32_t HVC_GL_COLOR_MASK = 0x2534;
static constexpr uint32_t HVC_GL_PIXEL_STOREI = 0x2535;
static constexpr uint32_t HVC_GL_ACTIVE_TEXTURE = 0x2536;
static constexpr uint32_t HVC_GL_GET_SHADER_IV = 0x2537;
static constexpr uint32_t HVC_GL_GET_PROGRAM_IV = 0x2538;
static constexpr uint32_t HVC_GL_GET_SHADER_INFO_LOG = 0x2539;
static constexpr uint32_t HVC_GL_GET_PROGRAM_INFO_LOG = 0x253A;
static constexpr uint32_t HVC_GL_BUFFER_SUB_DATA = 0x253B;
static constexpr uint32_t HVC_GL_DRAW_RANGE_ELEMENTS = 0x253C;
static constexpr uint32_t HVC_GL_GEN_VERTEX_ARRAYS = 0x253D;
static constexpr uint32_t HVC_GL_TEX_STORAGE_2D = 0x253E;
static constexpr uint32_t HVC_GL_BLEND_FUNC_SEPARATE = 0x253F;
static constexpr uint32_t HVC_GL_CLIENT_WAIT_SYNC = 0x2540;
static constexpr uint32_t HVC_GL_DISABLE_VERT_ATTRIB = 0x2541;
static constexpr uint32_t HVC_GL_END_QUERY = 0x2542;
static constexpr uint32_t HVC_GL_GET_STRINGI = 0x2543;
static constexpr uint32_t HVC_GL_GET_VERTEX_ATTRIB_IV = 0x2544;
static constexpr uint32_t HVC_GL_MAP_BUFFER_RANGE = 0x2545;
static constexpr uint32_t HVC_GL_UNIFORM_BLOCK_BINDING = 0x2546;
static constexpr uint32_t HVC_GL_COMPRESSED_TEX_SUB_IMAGE_3D = 0x2547;
static constexpr uint32_t HVC_GL_FENCE_SYNC = 0x2548;
static constexpr uint32_t HVC_GL_GENERATE_MIPMAP = 0x2549;
static constexpr uint32_t HVC_GL_POLYGON_OFFSET = 0x254A;
static constexpr uint32_t HVC_GL_BIND_VERTEX_ARRAY = 0x254B;
static constexpr uint32_t HVC_GL_BLEND_EQUATION_SEPARATE = 0x254C;
static constexpr uint32_t HVC_GL_CLEAR_BUFFER_FV = 0x254D;
static constexpr uint32_t HVC_GL_GEN_QUERIES = 0x254E;
static constexpr uint32_t HVC_GL_TEX_SUB_IMAGE_3D = 0x254F;
static constexpr uint32_t HVC_GL_VERTEX_ATTRIB_I4UI = 0x2550;
static constexpr uint32_t HVC_GL_CLEAR_BUFFER_FI = 0x2551;
static constexpr uint32_t HVC_GL_CLEAR_BUFFER_IV = 0x2552;
static constexpr uint32_t HVC_GL_DELETE_VERTEX_ARRAYS = 0x2553;
static constexpr uint32_t HVC_GL_INVALIDATE_FRAMEBUFFER = 0x2554;
static constexpr uint32_t HVC_GL_SAMPLER_PARAMETERF = 0x2555;
static constexpr uint32_t HVC_GL_TEX_STORAGE_3D = 0x2556;
static constexpr uint32_t HVC_GL_UNMAP_BUFFER = 0x2557;
static constexpr uint32_t HVC_GL_COMPRESSED_TEX_SUB_IMAGE_2D = 0x2558;
static constexpr uint32_t HVC_GL_DELETE_SAMPLERS = 0x2559;
static constexpr uint32_t HVC_GL_DRAW_BUFFERS = 0x255A;
static constexpr uint32_t HVC_GL_IS_ENABLED = 0x255B;
static constexpr uint32_t HVC_GL_READ_PIXELS = 0x255C;
static constexpr uint32_t HVC_GL_WAIT_SYNC = 0x255D;
static constexpr uint32_t HVC_GL_BEGIN_QUERY = 0x255E;
static constexpr uint32_t HVC_GL_DELETE_QUERIES = 0x255F;
static constexpr uint32_t HVC_GL_GET_QUERY_OBJECT_UIV = 0x2560;
static constexpr uint32_t HVC_GL_HINT = 0x2561;
static constexpr uint32_t HVC_GL_RENDERBUFFER_STORAGE_MS = 0x2562;
static constexpr uint32_t HVC_GL_BIND_BUFFER_RANGE = 0x2563;
static constexpr uint32_t HVC_GL_BIND_SAMPLER = 0x2564;
static constexpr uint32_t HVC_GL_DETACH_SHADER = 0x2565;
static constexpr uint32_t HVC_GL_TEX_SUB_IMAGE_2D = 0x2566;
static constexpr uint32_t HVC_GL_BLIT_FRAMEBUFFER = 0x2567;
static constexpr uint32_t HVC_GL_DELETE_FRAMEBUFFERS2 = 0x2568;
static constexpr uint32_t HVC_GL_DELETE_RENDERBUFFERS2 = 0x2569;
static constexpr uint32_t HVC_GL_DELETE_SYNC = 0x256A;
static constexpr uint32_t HVC_GL_FRAMEBUFFER_TEXTURE_LAYER = 0x256B;
static constexpr uint32_t HVC_GL_GEN_SAMPLERS = 0x256C;
static constexpr uint32_t HVC_GL_GET_FLOATV = 0x256D;
static constexpr uint32_t HVC_GL_GET_UNIFORM_BLOCK_INDEX = 0x256E;
static constexpr uint32_t HVC_GL_SAMPLER_PARAMETERI = 0x256F;
static constexpr uint32_t HVC_GL_VERTEX_ATTRIB4F = 0x2570;
static constexpr uint32_t HVC_GL_VERTEX_ATTRIB_IPOINTER = 0x2571;

class GpuBridge
{
public:
    GpuBridge(guest_t *guest,
              uint64_t stub_arena_gpa,
              bool host_window_enabled = false);
    virtual ~GpuBridge();

    virtual void install();
    virtual bool try_dispatch(uint32_t hvc_nr,
                              const uint64_t regs[8],
                              uint64_t *x0_out);
    virtual BuiltinSymbols builtin_symbols(const std::string &soname) const;
    virtual uint64_t unsupported_import_stub(const std::string &soname,
                                             const std::string &symbol);

    // EGL state accessors (used by NativeWindow bridge)
    EGLDisplay egl_display() const { return egl_display_; }
    EGLContext egl_context() const { return egl_context_; }
    EGLSurface egl_surface() const { return egl_surface_; }
    uint64_t native_window_handle() const { return GUEST_NATIVE_WINDOW; }
    virtual bool host_window_active() const;
    virtual void run_host_window_after_guest(int linger_ms);
    virtual bool pump_host_app_events();

protected:
    void register_libegl_stubs();
    void register_libgles_stubs();

    // Load ANGLE dylibs from third_party/angle-bin/
    bool load_angle();

    // Helper: resolve a symbol from ANGLE (tries EGL then GLES lib)
    void *angle_sym(const char *name) const;

    // Write one 12-byte HVC shim stub (movz x8,#nr / hvc #6 / ret).
    uint64_t write_stub(uint32_t hvc_nr);

    uint64_t add(const std::string &soname,
                 const std::string &symbol,
                 uint32_t hvc_nr,
                 StubHandler handler);

    void ensure_host_window();
    void present_native_window_buffer();
    void present_egl_surface();
    virtual bool collect_host_input_events();

    // Protected guest memory helpers
    static constexpr uint64_t HVC_STUB_SIZE = 12;
    static void encode_stub(uint8_t *out, uint32_t hvc_nr);
    static std::string guest_read_string(guest_t *g, uint64_t gpa);
    static void guest_write_u64(guest_t *g, uint64_t gpa, uint64_t v);
    static uint64_t guest_read_u64(guest_t *g, uint64_t gpa);
    static uint32_t guest_read_u32(guest_t *g, uint64_t gpa);
    static void guest_write_u32(guest_t *g, uint64_t gpa, uint32_t v);

    guest_t *guest_;
    uint64_t arena_gpa_;
    uint64_t next_stub_gpa_ = 0;
    bool installed_ = false;

    std::unordered_map<uint32_t, StubHandler> handlers_;
    std::unordered_map<std::string, BuiltinSymbols> sym_tables_;

    // ── Bump allocator for GLES/EGL stubs ────────────────────────────────────
    uint64_t heap_base_ = 0;
    uint64_t heap_bump_ = 0;
    static constexpr uint64_t HEAP_SIZE =
        8 * 1024 * 1024;  // 8MB for GLES/EGL stubs/objects

    // ── Native window state
    // ───────────────────────────────────────────────────
    struct NativeWindowState {
        int32_t width = 320;
        int32_t height = 240;
        int32_t stride = 320;
        int32_t format = 1;  // AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM
        uint64_t bits_gpa = 0;
        uint64_t bits_size = 0;
        uint32_t ref_count = 1;
        bool locked = false;
    };
    NativeWindowState native_window_;
    static constexpr uint64_t GUEST_NATIVE_WINDOW = 0xA11D0001ULL;
    static constexpr int32_t MAX_NATIVE_WINDOW_WIDTH = 640;
    static constexpr int32_t MAX_NATIVE_WINDOW_HEIGHT = 480;

    // ── ANGLE / EGL host state
    // ────────────────────────────────────────────────
    void *angle_egl_lib_ = nullptr;   // dlopen handle for libEGL.dylib
    void *angle_gles_lib_ = nullptr;  // dlopen handle for libGLESv2.dylib

    // Host-side EGL objects created during eglInitialize / eglCreateContext
    EGLDisplay egl_display_ = EGL_NO_DISPLAY;
    EGLContext egl_context_ = EGL_NO_CONTEXT;
    EGLSurface egl_surface_ = EGL_NO_SURFACE;
    EGLConfig egl_config_ = nullptr;

    // Guest-side opaque handles (returned to the game as EGLDisplay*, etc.)
    // We use small integers so they fit in 64-bit guest registers.
    static constexpr uint64_t GUEST_EGL_DISPLAY = 0xE61D0001ULL;
    static constexpr uint64_t GUEST_EGL_CONTEXT = 0xE61C0001ULL;
    static constexpr uint64_t GUEST_EGL_SURFACE = 0xE615F001ULL;
    static constexpr uint64_t GUEST_EGL_CONFIG = 0xE61CF001ULL;
    static constexpr uint64_t EGL_SUCCESS_VAL = 0x3000ULL;  // EGL_SUCCESS

    // eglGetProcAddress dispatch: guest hvc_nr → host fn ptr
    // Allocated dynamically in [HVC_GL_PROC_BASE, HVC_GL_PROC_LIMIT).
    static constexpr uint32_t HVC_GL_PROC_BASE = 0x2800;
    static constexpr uint32_t HVC_GL_PROC_LIMIT = 0x2E00;
    std::unordered_map<uint32_t, void *> proc_addr_handlers_;
    uint32_t next_proc_hvc_ = HVC_GL_PROC_BASE;

    static constexpr uint32_t HVC_UNSUPPORTED_IMPORT_BASE = 0x2E00;
    static constexpr uint32_t HVC_UNSUPPORTED_IMPORT_LIMIT = 0x3000;
    std::unordered_map<std::string, uint64_t> unsupported_import_stubs_;
    uint32_t next_unsupported_import_hvc_ = HVC_UNSUPPORTED_IMPORT_BASE;

    bool host_window_enabled_ = false;
    std::unique_ptr<HostWindow> host_window_;
};

}  // namespace muplar::runtime
