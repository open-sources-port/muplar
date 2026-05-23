// runtime/android/android_runtime.cpp
#include "android_runtime.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>
#include <dlfcn.h>
#include <vector>

// EGL / GLES headers (macOS host — provided by ANGLE)
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

extern "C" {
    #include "core/guest.h"
}

namespace muplar::runtime::android {

// ── HVC call number ranges ────────────────────────────────────────────────────
// libc      : 0x2000–0x20FF
// liblog    : 0x2100–0x21FF
// libandroid: 0x2200–0x22FF
// libdl     : 0x2300–0x23FF
// libEGL    : 0x2400–0x24FF
// libGLESv2 : 0x2500–0x25FF
// procaddr  : 0x2600–0x2FFF  (dynamic, one per eglGetProcAddress result)

// libc
static constexpr uint32_t HVC_MALLOC              = 0x2000;
static constexpr uint32_t HVC_FREE                = 0x2001;
static constexpr uint32_t HVC_CALLOC              = 0x2002;
static constexpr uint32_t HVC_REALLOC             = 0x2003;
static constexpr uint32_t HVC_MEMCPY              = 0x2004;
static constexpr uint32_t HVC_MEMMOVE             = 0x2005;
static constexpr uint32_t HVC_MEMSET              = 0x2006;
static constexpr uint32_t HVC_MEMCMP              = 0x2007;
static constexpr uint32_t HVC_STRLEN              = 0x2008;
static constexpr uint32_t HVC_STRCMP              = 0x2009;
static constexpr uint32_t HVC_STRNCMP             = 0x200A;
static constexpr uint32_t HVC_STRCPY              = 0x200B;
static constexpr uint32_t HVC_STRNCPY             = 0x200C;
static constexpr uint32_t HVC_STRCAT              = 0x200D;
static constexpr uint32_t HVC_STRDUP              = 0x200E;
[[maybe_unused]] static constexpr uint32_t HVC_SPRINTF  = 0x200F;
[[maybe_unused]] static constexpr uint32_t HVC_SNPRINTF = 0x2010;
static constexpr uint32_t HVC_PRINTF              = 0x2011;
static constexpr uint32_t HVC_ABORT               = 0x2012;
static constexpr uint32_t HVC_EXIT                = 0x2013;
static constexpr uint32_t HVC_PTHREAD_CREATE      = 0x2020;
static constexpr uint32_t HVC_PTHREAD_JOIN        = 0x2021;
static constexpr uint32_t HVC_PTHREAD_MUTEX_INIT  = 0x2022;
static constexpr uint32_t HVC_PTHREAD_MUTEX_LOCK  = 0x2023;
static constexpr uint32_t HVC_PTHREAD_MUTEX_UNLOCK= 0x2024;
static constexpr uint32_t HVC_PTHREAD_MUTEX_DESTROY=0x2025;
static constexpr uint32_t HVC_PTHREAD_KEY_CREATE  = 0x2026;
static constexpr uint32_t HVC_PTHREAD_GETSPECIFIC = 0x2027;
static constexpr uint32_t HVC_PTHREAD_SETSPECIFIC = 0x2028;
static constexpr uint32_t HVC_PTHREAD_ONCE        = 0x2029;
static constexpr uint32_t HVC_PTHREAD_SELF        = 0x202A;
static constexpr uint32_t HVC_GETPID              = 0x2030;
static constexpr uint32_t HVC_GETENV_LIBC         = 0x2031;
static constexpr uint32_t HVC_CLOCK_GETTIME       = 0x2032;
static constexpr uint32_t HVC_GETTIMEOFDAY        = 0x2033;
static constexpr uint32_t HVC_USLEEP              = 0x2034;
static constexpr uint32_t HVC_NANOSLEEP           = 0x2035;
[[maybe_unused]] static constexpr uint32_t HVC_STRTOL  = 0x2036;
[[maybe_unused]] static constexpr uint32_t HVC_STRTOD  = 0x2037;
static constexpr uint32_t HVC_ATOI                = 0x2038;
[[maybe_unused]] static constexpr uint32_t HVC_ATOF    = 0x2039;
static constexpr uint32_t HVC_RAND                = 0x203A;
static constexpr uint32_t HVC_SRAND               = 0x203B;

// liblog
static constexpr uint32_t HVC_LOG_PRINT           = 0x2100;
static constexpr uint32_t HVC_LOG_WRITE           = 0x2101;
static constexpr uint32_t HVC_LOG_BUF_WRITE       = 0x2102;

// libandroid
static constexpr uint32_t HVC_ALOOPER_PREPARE     = 0x2200;
static constexpr uint32_t HVC_ALOOPER_ACQUIRE     = 0x2201;
static constexpr uint32_t HVC_ALOOPER_RELEASE     = 0x2202;
static constexpr uint32_t HVC_ALOOPER_POLL_ONCE   = 0x2203;
static constexpr uint32_t HVC_ALOOPER_POLL_ALL    = 0x2204;
static constexpr uint32_t HVC_ALOOPER_ADD_FD      = 0x2205;
static constexpr uint32_t HVC_ALOOPER_REMOVE_FD   = 0x2206;
static constexpr uint32_t HVC_ALOOPER_WAKE        = 0x2207;
[[maybe_unused]] static constexpr uint32_t HVC_ASSET_MGR_OPEN = 0x2210;
[[maybe_unused]] static constexpr uint32_t HVC_ASSET_OPEN     = 0x2211;
[[maybe_unused]] static constexpr uint32_t HVC_ASSET_READ     = 0x2212;
[[maybe_unused]] static constexpr uint32_t HVC_ASSET_CLOSE    = 0x2213;
[[maybe_unused]] static constexpr uint32_t HVC_ASSET_LENGTH   = 0x2214;
static constexpr uint32_t HVC_CHOREOGRAPHER_GET   = 0x2220;
static constexpr uint32_t HVC_CHOREOGRAPHER_CB    = 0x2221;
static constexpr uint32_t HVC_NATIVE_WINDOW_SET_BUF = 0x2230;
static constexpr uint32_t HVC_NATIVE_WINDOW_LOCK    = 0x2231;
static constexpr uint32_t HVC_NATIVE_WINDOW_UNLOCK  = 0x2232;
static constexpr uint32_t HVC_PROP_GET              = 0x2240;

// libdl
static constexpr uint32_t HVC_DLOPEN              = 0x2300;
static constexpr uint32_t HVC_DLSYM               = 0x2301;
static constexpr uint32_t HVC_DLCLOSE             = 0x2302;
static constexpr uint32_t HVC_DLERROR             = 0x2303;

// libEGL
static constexpr uint32_t HVC_EGL_GET_DISPLAY         = 0x2400;
static constexpr uint32_t HVC_EGL_INITIALIZE           = 0x2401;
static constexpr uint32_t HVC_EGL_BIND_API             = 0x2402;
static constexpr uint32_t HVC_EGL_CHOOSE_CONFIG        = 0x2403;
static constexpr uint32_t HVC_EGL_CREATE_CONTEXT       = 0x2404;
static constexpr uint32_t HVC_EGL_CREATE_WIN_SURFACE   = 0x2405;
static constexpr uint32_t HVC_EGL_CREATE_PBUF_SURFACE  = 0x2406;
static constexpr uint32_t HVC_EGL_MAKE_CURRENT         = 0x2407;
static constexpr uint32_t HVC_EGL_SWAP_BUFFERS         = 0x2408;
static constexpr uint32_t HVC_EGL_DESTROY_CONTEXT      = 0x2409;
static constexpr uint32_t HVC_EGL_DESTROY_SURFACE      = 0x240A;
static constexpr uint32_t HVC_EGL_GET_ERROR            = 0x240B;
static constexpr uint32_t HVC_EGL_QUERY_STRING         = 0x240C;
static constexpr uint32_t HVC_EGL_SWAP_INTERVAL        = 0x240D;
static constexpr uint32_t HVC_EGL_TERMINATE            = 0x240E;
static constexpr uint32_t HVC_EGL_RELEASE_THREAD       = 0x240F;
static constexpr uint32_t HVC_EGL_GET_PROC_ADDRESS     = 0x2410;

// libGLESv2 — direct wrappers for the most common draw-loop calls.
// Less common calls come through eglGetProcAddress (0x2600+).
static constexpr uint32_t HVC_GL_GET_ERROR            = 0x2500;
static constexpr uint32_t HVC_GL_VIEWPORT             = 0x2501;
static constexpr uint32_t HVC_GL_CLEAR                = 0x2502;
static constexpr uint32_t HVC_GL_CLEAR_COLOR          = 0x2503;
static constexpr uint32_t HVC_GL_CLEAR_DEPTH          = 0x2504;
static constexpr uint32_t HVC_GL_ENABLE               = 0x2505;
static constexpr uint32_t HVC_GL_DISABLE              = 0x2506;
static constexpr uint32_t HVC_GL_BLEND_FUNC           = 0x2507;
static constexpr uint32_t HVC_GL_DEPTH_FUNC           = 0x2508;
static constexpr uint32_t HVC_GL_DEPTH_MASK           = 0x2509;
static constexpr uint32_t HVC_GL_CULL_FACE            = 0x250A;
static constexpr uint32_t HVC_GL_FRONT_FACE           = 0x250B;
static constexpr uint32_t HVC_GL_GEN_TEXTURES         = 0x250C;
static constexpr uint32_t HVC_GL_BIND_TEXTURE         = 0x250D;
static constexpr uint32_t HVC_GL_TEX_IMAGE2D          = 0x250E;
static constexpr uint32_t HVC_GL_TEX_PARAMETERI       = 0x250F;
static constexpr uint32_t HVC_GL_GEN_BUFFERS          = 0x2510;
static constexpr uint32_t HVC_GL_BIND_BUFFER          = 0x2511;
static constexpr uint32_t HVC_GL_BUFFER_DATA          = 0x2512;
static constexpr uint32_t HVC_GL_CREATE_SHADER        = 0x2513;
static constexpr uint32_t HVC_GL_SHADER_SOURCE        = 0x2514;
static constexpr uint32_t HVC_GL_COMPILE_SHADER       = 0x2515;
static constexpr uint32_t HVC_GL_CREATE_PROGRAM       = 0x2516;
static constexpr uint32_t HVC_GL_ATTACH_SHADER        = 0x2517;
static constexpr uint32_t HVC_GL_LINK_PROGRAM         = 0x2518;
static constexpr uint32_t HVC_GL_USE_PROGRAM          = 0x2519;
static constexpr uint32_t HVC_GL_GET_ATTRIB_LOC       = 0x251A;
static constexpr uint32_t HVC_GL_GET_UNIFORM_LOC      = 0x251B;
static constexpr uint32_t HVC_GL_ENABLE_VERT_ATTRIB   = 0x251C;
static constexpr uint32_t HVC_GL_VERT_ATTRIB_PTR      = 0x251D;
static constexpr uint32_t HVC_GL_DRAW_ARRAYS          = 0x251E;
static constexpr uint32_t HVC_GL_DRAW_ELEMENTS        = 0x251F;
static constexpr uint32_t HVC_GL_UNIFORM1I            = 0x2520;
static constexpr uint32_t HVC_GL_UNIFORM1F            = 0x2521;
static constexpr uint32_t HVC_GL_UNIFORM4F            = 0x2522;
static constexpr uint32_t HVC_GL_UNIFORM_MATRIX4FV    = 0x2523;
static constexpr uint32_t HVC_GL_GET_INTEGERV         = 0x2524;
static constexpr uint32_t HVC_GL_GET_STRING           = 0x2525;
static constexpr uint32_t HVC_GL_DELETE_TEXTURES      = 0x2526;
static constexpr uint32_t HVC_GL_DELETE_BUFFERS       = 0x2527;
static constexpr uint32_t HVC_GL_DELETE_SHADER        = 0x2528;
static constexpr uint32_t HVC_GL_DELETE_PROGRAM       = 0x2529;
static constexpr uint32_t HVC_GL_GEN_FRAMEBUFFERS     = 0x252A;
static constexpr uint32_t HVC_GL_BIND_FRAMEBUFFER     = 0x252B;
static constexpr uint32_t HVC_GL_FRAMEBUFFER_TEXTURE2D = 0x252C;
static constexpr uint32_t HVC_GL_GEN_RENDERBUFFERS    = 0x252D;
static constexpr uint32_t HVC_GL_BIND_RENDERBUFFER    = 0x252E;
static constexpr uint32_t HVC_GL_RENDERBUFFER_STORAGE = 0x252F;
static constexpr uint32_t HVC_GL_FRAMEBUFFER_RENDERBUF = 0x2530;
static constexpr uint32_t HVC_GL_FINISH               = 0x2531;
static constexpr uint32_t HVC_GL_FLUSH                = 0x2532;
static constexpr uint32_t HVC_GL_SCISSOR              = 0x2533;
static constexpr uint32_t HVC_GL_COLOR_MASK           = 0x2534;
static constexpr uint32_t HVC_GL_PIXEL_STOREI         = 0x2535;
static constexpr uint32_t HVC_GL_ACTIVE_TEXTURE       = 0x2536;
static constexpr uint32_t HVC_GL_GET_SHADER_IV        = 0x2537;
static constexpr uint32_t HVC_GL_GET_PROGRAM_IV       = 0x2538;
static constexpr uint32_t HVC_GL_GET_SHADER_INFO_LOG  = 0x2539;
static constexpr uint32_t HVC_GL_GET_PROGRAM_INFO_LOG = 0x253A;

// ── AArch64 HVC shim stub layout ─────────────────────────────────────────────
//   movz x8, #<hvc_nr>   ; 4 bytes
//   hvc  #6              ; 4 bytes
//   ret                  ; 4 bytes
static constexpr uint64_t HVC_STUB_SIZE = 12;

static void encode_stub(uint8_t* out, uint32_t hvc_nr)
{
    uint32_t movz = 0xD2800008u | ((hvc_nr & 0xFFFF) << 5);
    uint32_t hvc  = 0xD4000002u | (6u << 5);   // hvc #6
    uint32_t ret  = 0xD65F03C0u;
    memcpy(out + 0, &movz, 4);
    memcpy(out + 4, &hvc,  4);
    memcpy(out + 8, &ret,  4);
}

// ── Helpers ───────────────────────────────────────────────────────────────────

static std::string guest_read_string(guest_t* g, uint64_t gpa)
{
    if (!gpa) return {};
    char buf[512] = {};
    guest_read_str(g, gpa, buf, sizeof(buf));
    return { buf };
}

static void guest_write_u64(guest_t* g, uint64_t gpa, uint64_t v)
{
    guest_write(g, gpa, &v, 8);
}

static uint64_t guest_read_u64(guest_t* g, uint64_t gpa)
{
    uint64_t v = 0;
    guest_read(g, gpa, &v, 8);
    return v;
}

static uint32_t guest_read_u32(guest_t* g, uint64_t gpa)
{
    uint32_t v = 0;
    guest_read(g, gpa, &v, 4);
    return v;
}

static void guest_write_u32(guest_t* g, uint64_t gpa, uint32_t v)
{
    guest_write(g, gpa, &v, 4);
}

static std::string format_guest_log(guest_t* g,
                                    const std::string& fmt,
                                    const uint64_t* varargs,
                                    size_t vararg_count)
{
    std::string out;
    size_t arg = 0;

    for (size_t i = 0; i < fmt.size(); ++i) {
        if (fmt[i] != '%' || i + 1 >= fmt.size()) {
            out.push_back(fmt[i]);
            continue;
        }

        char tmp[64] = {};
        char spec = fmt[++i];
        if (spec == '%') { out.push_back('%'); continue; }
        if (spec == 'l' && i + 2 < fmt.size() && fmt[i + 1] == 'l') {
            i += 2; spec = fmt[i];
        }
        if (arg >= vararg_count) { out.push_back('%'); out.push_back(spec); continue; }

        uint64_t value = varargs[arg++];
        switch (spec) {
        case 'd': case 'i':
            std::snprintf(tmp, sizeof(tmp), "%lld", (long long)value); out += tmp; break;
        case 'u':
            std::snprintf(tmp, sizeof(tmp), "%llu", (unsigned long long)value); out += tmp; break;
        case 'x': case 'X':
            std::snprintf(tmp, sizeof(tmp), "%llx", (unsigned long long)value); out += tmp; break;
        case 'p':
            std::snprintf(tmp, sizeof(tmp), "0x%llx", (unsigned long long)value); out += tmp; break;
        case 's':
            out += guest_read_string(g, value); break;
        case 'f': case 'g': case 'e': {
            // float passed as bits in a uint64 — reinterpret
            float f; memcpy(&f, &value, 4);
            std::snprintf(tmp, sizeof(tmp), "%f", (double)f); out += tmp; break;
        }
        default: out.push_back('%'); out.push_back(spec); break;
        }
    }
    return out;
}

// ── AndroidRuntime ────────────────────────────────────────────────────────────

AndroidRuntime::AndroidRuntime(guest_t* guest, uint64_t stub_arena_gpa)
    : guest_(guest), arena_gpa_(stub_arena_gpa)
{}

AndroidRuntime::~AndroidRuntime()
{
    if (egl_surface_ != EGL_NO_SURFACE && egl_display_ != EGL_NO_DISPLAY)
        eglDestroySurface(egl_display_, egl_surface_);
    if (egl_context_ != EGL_NO_CONTEXT && egl_display_ != EGL_NO_DISPLAY)
        eglDestroyContext(egl_display_, egl_context_);
    if (egl_display_ != EGL_NO_DISPLAY)
        eglTerminate(egl_display_);
    if (angle_gles_lib_) ::dlclose(angle_gles_lib_);
    if (angle_egl_lib_)  ::dlclose(angle_egl_lib_);
}

bool AndroidRuntime::load_angle()
{
    // Try @rpath first (set by CMakeLists), then fallback to relative path.
    const char* egl_paths[]  = { "@rpath/libEGL.dylib",
                                  "third_party/angle-bin/libEGL.dylib", nullptr };
    const char* gles_paths[] = { "@rpath/libGLESv2.dylib",
                                  "third_party/angle-bin/libGLESv2.dylib", nullptr };

    for (int i = 0; egl_paths[i] && !angle_egl_lib_; ++i)
        angle_egl_lib_ = ::dlopen(egl_paths[i], RTLD_NOW | RTLD_LOCAL);

    for (int i = 0; gles_paths[i] && !angle_gles_lib_; ++i)
        angle_gles_lib_ = ::dlopen(gles_paths[i], RTLD_NOW | RTLD_LOCAL);

    if (!angle_egl_lib_ || !angle_gles_lib_) {
        std::fprintf(stderr, "[ANGLE] failed to load: egl=%p gles=%p — %s\n",
                     angle_egl_lib_, angle_gles_lib_, ::dlerror());
        return false;
    }
    std::fprintf(stderr, "[ANGLE] loaded libEGL + libGLESv2 ✓\n");
    return true;
}

void* AndroidRuntime::angle_sym(const char* name) const
{
    void* sym = nullptr;
    if (angle_egl_lib_)  sym = ::dlsym(angle_egl_lib_,  name);
    if (!sym && angle_gles_lib_) sym = ::dlsym(angle_gles_lib_, name);
    return sym;
}

uint64_t AndroidRuntime::write_stub(uint32_t hvc_nr)
{
    uint8_t stub[HVC_STUB_SIZE];
    encode_stub(stub, hvc_nr);
    uint64_t gpa = next_stub_gpa_;
    guest_write(guest_, gpa, stub, sizeof(stub));
    next_stub_gpa_ += HVC_STUB_SIZE;
    return gpa;
}

uint64_t AndroidRuntime::add(const std::string& soname,
                              const std::string& symbol,
                              uint32_t           hvc_nr,
                              StubHandler        handler)
{
    uint64_t gpa = write_stub(hvc_nr);
    handlers_[hvc_nr] = std::move(handler);
    sym_tables_[soname][symbol] = gpa;
    return gpa;
}

void AndroidRuntime::install()
{
    if (installed_) return;
    installed_     = true;
    next_stub_gpa_ = arena_gpa_;

    heap_base_ = arena_gpa_ + 0x10000;
    heap_bump_ = heap_base_;

    load_angle();   // non-fatal — stubs still register, calls log + return 0

    register_libc_stubs();
    register_liblog_stubs();
    register_libandroid_stubs();
    register_libdl_stubs();
    register_libegl_stubs();
    register_libgles_stubs();

    std::fprintf(stderr,
        "[ART] installed %zu stub sonames, %zu handlers, arena=0x%llx\n",
        sym_tables_.size(), handlers_.size(),
        (unsigned long long)arena_gpa_);
}

BuiltinSymbols AndroidRuntime::builtin_symbols(const std::string& soname) const
{
    auto it = sym_tables_.find(soname);
    return (it != sym_tables_.end()) ? it->second : BuiltinSymbols{};
}

bool AndroidRuntime::try_dispatch(uint32_t        hvc_nr,
                                   const uint64_t  regs[8],
                                   uint64_t*       x0_out)
{
    // Static range check covers all registered stubs + dynamic proc stubs
    if (hvc_nr < 0x2000 || hvc_nr > 0x2FFF) return false;

    auto it = handlers_.find(hvc_nr);
    if (it == handlers_.end()) {
        std::fprintf(stderr, "[ART] unhandled HVC 0x%X\n", hvc_nr);
        *x0_out = 0;
        return true;
    }
    *x0_out = it->second(guest_, regs);
    return true;
}

// ── libc stubs ────────────────────────────────────────────────────────────────

void AndroidRuntime::register_libc_stubs()
{
    add("libc.so", "malloc", HVC_MALLOC,
        [this](guest_t*, const uint64_t a[8]) -> uint64_t {
            uint64_t size = (a[0] + 15) & ~15ULL;
            if (!a[0] || heap_bump_ + size > heap_base_ + HEAP_SIZE) return 0;
            uint64_t ptr = heap_bump_; heap_bump_ += size;
            return ptr;
        });

    add("libc.so", "calloc", HVC_CALLOC,
        [this](guest_t* g, const uint64_t a[8]) -> uint64_t {
            uint64_t total = ((a[0] * a[1]) + 15) & ~15ULL;
            if (!total || heap_bump_ + total > heap_base_ + HEAP_SIZE) return 0;
            uint64_t ptr = heap_bump_; heap_bump_ += total;
            std::vector<uint8_t> z(total, 0);
            guest_write(g, ptr, z.data(), total);
            return ptr;
        });

    add("libc.so", "realloc", HVC_REALLOC,
        [this](guest_t*, const uint64_t a[8]) -> uint64_t {
            uint64_t size = (a[1] + 15) & ~15ULL;
            if (!size || heap_bump_ + size > heap_base_ + HEAP_SIZE) return 0;
            uint64_t ptr = heap_bump_; heap_bump_ += size;
            return ptr;
        });

    add("libc.so", "free", HVC_FREE,
        [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; });

    add("libc.so", "memcpy", HVC_MEMCPY,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            if (!a[0] || !a[1] || !a[2]) return a[0];
            std::vector<uint8_t> buf(a[2]);
            guest_read(g, a[1], buf.data(), a[2]);
            guest_write(g, a[0], buf.data(), a[2]);
            return a[0];
        });

    add("libc.so", "memmove", HVC_MEMMOVE,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            if (!a[0] || !a[1] || !a[2]) return a[0];
            std::vector<uint8_t> buf(a[2]);
            guest_read(g, a[1], buf.data(), a[2]);
            guest_write(g, a[0], buf.data(), a[2]);
            return a[0];
        });

    add("libc.so", "memset", HVC_MEMSET,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            if (!a[0] || !a[2]) return a[0];
            std::vector<uint8_t> buf(a[2], (uint8_t)a[1]);
            guest_write(g, a[0], buf.data(), a[2]);
            return a[0];
        });

    add("libc.so", "memcmp", HVC_MEMCMP,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            if (!a[2]) return 0;
            std::vector<uint8_t> b1(a[2]), b2(a[2]);
            guest_read(g, a[0], b1.data(), a[2]);
            guest_read(g, a[1], b2.data(), a[2]);
            return (uint64_t)memcmp(b1.data(), b2.data(), a[2]);
        });

    add("libc.so", "strlen", HVC_STRLEN,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            return guest_read_string(g, a[0]).size();
        });

    add("libc.so", "strcmp", HVC_STRCMP,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            return (uint64_t)guest_read_string(g,a[0]).compare(guest_read_string(g,a[1]));
        });

    add("libc.so", "strncmp", HVC_STRNCMP,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            auto s1 = guest_read_string(g,a[0]); auto s2 = guest_read_string(g,a[1]);
            return (uint64_t)s1.compare(0,a[2],s2,0,a[2]);
        });

    add("libc.so", "strcpy", HVC_STRCPY,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            auto s = guest_read_string(g,a[1]);
            guest_write(g, a[0], s.c_str(), s.size()+1);
            return a[0];
        });

    add("libc.so", "strncpy", HVC_STRNCPY,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            auto s = guest_read_string(g,a[1]); s.resize(a[2],'\0');
            guest_write(g, a[0], s.c_str(), a[2]);
            return a[0];
        });

    add("libc.so", "strcat", HVC_STRCAT,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            auto d = guest_read_string(g,a[0]); d += guest_read_string(g,a[1]);
            guest_write(g, a[0], d.c_str(), d.size()+1);
            return a[0];
        });

    add("libc.so", "strdup", HVC_STRDUP,
        [this](guest_t* g, const uint64_t a[8]) -> uint64_t {
            auto s = guest_read_string(g,a[0]);
            uint64_t sz = (s.size()+1+15)&~15ULL;
            if (heap_bump_+sz > heap_base_+HEAP_SIZE) return 0;
            uint64_t ptr = heap_bump_; heap_bump_ += sz;
            guest_write(g, ptr, s.c_str(), s.size()+1);
            return ptr;
        });

    add("libc.so", "printf", HVC_PRINTF,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            auto fmt = guest_read_string(g,a[0]);
            std::fprintf(stderr, "[guest printf] %s\n", fmt.c_str());
            return (uint64_t)fmt.size();
        });

    add("libc.so", "abort", HVC_ABORT,
        [](guest_t*, const uint64_t[8]) -> uint64_t {
            std::fprintf(stderr, "[ART] guest called abort()\n"); ::abort(); return 0;
        });

    add("libc.so", "exit", HVC_EXIT,
        [](guest_t*, const uint64_t a[8]) -> uint64_t {
            std::fprintf(stderr, "[ART] guest called exit(%lld)\n", (long long)a[0]);
            ::exit((int)a[0]); return 0;
        });

    add("libc.so", "getpid", HVC_GETPID,
        [](guest_t*, const uint64_t[8]) -> uint64_t { return (uint64_t)::getpid(); });

    add("libc.so", "getenv", HVC_GETENV_LIBC,
        [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; });

    add("libc.so", "pthread_create", HVC_PTHREAD_CREATE,
        [this](guest_t* g, const uint64_t a[8]) -> uint64_t {
            uint64_t h = next_thread_handle_++;
            guest_write_u64(g, a[0], h);
            threads_[h] = {0, 0};
            std::fprintf(stderr, "[ART] pthread_create fn=0x%llx handle=0x%llx (stub)\n",
                         (unsigned long long)a[2], (unsigned long long)h);
            return 0;
        });

    add("libc.so", "pthread_join",
        HVC_PTHREAD_JOIN, [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; });
    add("libc.so", "pthread_self",
        HVC_PTHREAD_SELF, [](guest_t*, const uint64_t[8]) -> uint64_t { return 1; });
    add("libc.so", "pthread_mutex_init",
        HVC_PTHREAD_MUTEX_INIT,    [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; });
    add("libc.so", "pthread_mutex_lock",
        HVC_PTHREAD_MUTEX_LOCK,    [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; });
    add("libc.so", "pthread_mutex_unlock",
        HVC_PTHREAD_MUTEX_UNLOCK,  [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; });
    add("libc.so", "pthread_mutex_destroy",
        HVC_PTHREAD_MUTEX_DESTROY, [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; });

    add("libc.so", "pthread_once", HVC_PTHREAD_ONCE,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            if (!guest_read_u64(g, a[0])) guest_write_u64(g, a[0], 1);
            return 0;
        });

    add("libc.so", "pthread_key_create",  HVC_PTHREAD_KEY_CREATE,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t { guest_write_u64(g,a[0],1); return 0; });
    add("libc.so", "pthread_getspecific", HVC_PTHREAD_GETSPECIFIC,
        [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; });
    add("libc.so", "pthread_setspecific", HVC_PTHREAD_SETSPECIFIC,
        [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; });

    add("libc.so", "clock_gettime", HVC_CLOCK_GETTIME,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            struct timespec ts; ::clock_gettime((clockid_t)a[0], &ts);
            uint64_t sec = ts.tv_sec, nsec = ts.tv_nsec;
            if (a[1]) { guest_write(g,a[1],&sec,8); guest_write(g,a[1]+8,&nsec,8); }
            return 0;
        });

    add("libc.so", "gettimeofday", HVC_GETTIMEOFDAY,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            struct timeval tv; ::gettimeofday(&tv, nullptr);
            uint64_t sec = tv.tv_sec, usec = tv.tv_usec;
            if (a[0]) { guest_write(g,a[0],&sec,8); guest_write(g,a[0]+8,&usec,8); }
            return 0;
        });

    add("libc.so", "usleep",    HVC_USLEEP,    [](guest_t*, const uint64_t a[8]) -> uint64_t { ::usleep((useconds_t)a[0]); return 0; });
    add("libc.so", "nanosleep", HVC_NANOSLEEP, [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; });
    add("libc.so", "atoi",      HVC_ATOI,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            auto s = guest_read_string(g,a[0]); return s.empty() ? 0 : (uint64_t)std::stol(s);
        });
    add("libc.so", "rand",  HVC_RAND,  [](guest_t*, const uint64_t[8]) -> uint64_t { return (uint64_t)::rand(); });
    add("libc.so", "srand", HVC_SRAND, [](guest_t*, const uint64_t a[8]) -> uint64_t { ::srand((unsigned)a[0]); return 0; });

    sym_tables_["libm.so"]    = sym_tables_["libc.so"];
    sym_tables_["libstdc++.so"] = sym_tables_["libc.so"];
}

// ── liblog stubs ──────────────────────────────────────────────────────────────

void AndroidRuntime::register_liblog_stubs()
{
    add("liblog.so", "__android_log_print", HVC_LOG_PRINT,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            auto tag = guest_read_string(g,a[1]);
            auto fmt = guest_read_string(g,a[2]);
            auto msg = format_guest_log(g, fmt, a+3, 5);
            std::fprintf(stderr, "[logcat/%s] %s\n", tag.c_str(), msg.c_str());
            return (uint64_t)msg.size();
        });

    add("liblog.so", "__android_log_write", HVC_LOG_WRITE,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            auto tag = guest_read_string(g,a[1]);
            auto msg = guest_read_string(g,a[2]);
            std::fprintf(stderr, "[logcat/%s] %s\n", tag.c_str(), msg.c_str());
            return 0;
        });

    add("liblog.so", "__android_log_buf_write", HVC_LOG_BUF_WRITE,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            auto tag = guest_read_string(g,a[2]);
            auto msg = guest_read_string(g,a[3]);
            std::fprintf(stderr, "[logcat/%s] %s\n", tag.c_str(), msg.c_str());
            return 0;
        });
}

// ── libandroid stubs ──────────────────────────────────────────────────────────

void AndroidRuntime::register_libandroid_stubs()
{
    add("libandroid.so", "ALooper_prepare",  HVC_ALOOPER_PREPARE,
        [this](guest_t*, const uint64_t[8]) -> uint64_t { return arena_gpa_ + 0x100; });
    add("libandroid.so", "ALooper_acquire",  HVC_ALOOPER_ACQUIRE,  [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; });
    add("libandroid.so", "ALooper_release",  HVC_ALOOPER_RELEASE,  [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; });
    add("libandroid.so", "ALooper_pollOnce", HVC_ALOOPER_POLL_ONCE,[](guest_t*, const uint64_t[8]) -> uint64_t { return (uint64_t)-1; });
    add("libandroid.so", "ALooper_pollAll",  HVC_ALOOPER_POLL_ALL, [](guest_t*, const uint64_t[8]) -> uint64_t { return (uint64_t)-1; });
    add("libandroid.so", "ALooper_addFd",    HVC_ALOOPER_ADD_FD,   [](guest_t*, const uint64_t[8]) -> uint64_t { return 1; });
    add("libandroid.so", "ALooper_removeFd", HVC_ALOOPER_REMOVE_FD,[](guest_t*, const uint64_t[8]) -> uint64_t { return 1; });
    add("libandroid.so", "ALooper_wake",     HVC_ALOOPER_WAKE,     [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; });

    // ANativeWindow — return fake window handle; EGL will use GUEST_EGL_SURFACE
    add("libandroid.so", "ANativeWindow_setBuffersGeometry", HVC_NATIVE_WINDOW_SET_BUF,
        [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; });
    add("libandroid.so", "ANativeWindow_lock",         HVC_NATIVE_WINDOW_LOCK,   [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; });
    add("libandroid.so", "ANativeWindow_unlockAndPost", HVC_NATIVE_WINDOW_UNLOCK, [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; });

    add("libandroid.so", "AChoreographer_getInstance", HVC_CHOREOGRAPHER_GET,
        [this](guest_t*, const uint64_t[8]) -> uint64_t { return arena_gpa_ + 0x200; });
    add("libandroid.so", "AChoreographer_postFrameCallback", HVC_CHOREOGRAPHER_CB,
        [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; });

    add("libandroid.so", "__system_property_get", HVC_PROP_GET,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            auto name = guest_read_string(g,a[0]);
            std::fprintf(stderr, "[ART] __system_property_get(%s) → ''\n", name.c_str());
            if (a[1]) { uint8_t nul = 0; guest_write(g,a[1],&nul,1); }
            return 0;
        });
}

// ── libdl stubs ───────────────────────────────────────────────────────────────

void AndroidRuntime::register_libdl_stubs()
{
    add("libdl.so", "dlopen", HVC_DLOPEN,
        [this](guest_t* g, const uint64_t a[8]) -> uint64_t {
            auto path = guest_read_string(g,a[0]);
            std::fprintf(stderr, "[ART] dlopen(%s)\n", path.c_str());
            uint64_t h = next_dl_handle_++;
            dl_handles_[h] = {path, 0};
            return h;
        });

    add("libdl.so", "dlsym", HVC_DLSYM,
        [this](guest_t* g, const uint64_t a[8]) -> uint64_t {
            auto sym = guest_read_string(g,a[1]);
            // Search our stub tables first
            for (auto& [soname, syms] : sym_tables_) {
                auto it = syms.find(sym);
                if (it != syms.end()) return it->second;
            }
            // Then try ANGLE directly
            void* fn = angle_sym(sym.c_str());
            if (fn) {
                // Allocate a passthrough HVC stub for this symbol
                uint32_t nr = next_proc_hvc_++;
                uint64_t gpa = write_stub(nr);
                handlers_[nr] = [fn](guest_t*, const uint64_t[8]) -> uint64_t {
                    // Direct host call — args already in regs, but we can't
                    // forward variadic AArch64 args from C++ safely here.
                    // Log and return 0 for now; procaddr path is the right one.
                    (void)fn;
                    return 0;
                };
                std::fprintf(stderr, "[ART] dlsym(%s) → stub 0x%llx\n",
                             sym.c_str(), (unsigned long long)gpa);
                return gpa;
            }
            std::fprintf(stderr, "[ART] dlsym(%s) → NOT FOUND\n", sym.c_str());
            return 0;
        });

    add("libdl.so", "dlclose", HVC_DLCLOSE,
        [this](guest_t*, const uint64_t a[8]) -> uint64_t { dl_handles_.erase(a[0]); return 0; });
    add("libdl.so", "dlerror", HVC_DLERROR,
        [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; });

    sym_tables_["libdl_android.so"] = sym_tables_["libdl.so"];
}

// ── libEGL stubs ──────────────────────────────────────────────────────────────
//
// Strategy: map Android guest EGLDisplay/EGLContext/EGLSurface opaque handles
// to real ANGLE host objects.  The guest never dereferences these — it just
// passes them back to subsequent EGL calls — so we can use any stable integer.
//
// GUEST_EGL_DISPLAY / GUEST_EGL_CONTEXT / GUEST_EGL_SURFACE are the constants
// returned to the guest.  Internally we keep the real ANGLE objects in members.

void AndroidRuntime::register_libegl_stubs()
{
    // eglGetDisplay(EGLNativeDisplayType)
    add("libEGL.so", "eglGetDisplay", HVC_EGL_GET_DISPLAY,
        [this](guest_t*, const uint64_t[8]) -> uint64_t {
            if (egl_display_ == EGL_NO_DISPLAY) {
                egl_display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
                std::fprintf(stderr, "[EGL] eglGetDisplay → %p\n", egl_display_);
            }
            return (egl_display_ != EGL_NO_DISPLAY) ? GUEST_EGL_DISPLAY : 0;
        });

    // eglInitialize(EGLDisplay, EGLint *major, EGLint *minor)
    add("libEGL.so", "eglInitialize", HVC_EGL_INITIALIZE,
        [this](guest_t* g, const uint64_t a[8]) -> uint64_t {
            if (a[0] != GUEST_EGL_DISPLAY || egl_display_ == EGL_NO_DISPLAY) return 0;
            EGLint major = 0, minor = 0;
            EGLBoolean ok = eglInitialize(egl_display_, &major, &minor);
            std::fprintf(stderr, "[EGL] eglInitialize → %d (v%d.%d)\n", ok, major, minor);
            if (a[1]) guest_write_u32(g, a[1], (uint32_t)major);
            if (a[2]) guest_write_u32(g, a[2], (uint32_t)minor);
            return ok ? 1 : 0;
        });

    // eglBindAPI(EGLenum api)  — always OpenGL ES
    add("libEGL.so", "eglBindAPI", HVC_EGL_BIND_API,
        [](guest_t*, const uint64_t a[8]) -> uint64_t {
            EGLBoolean ok = eglBindAPI((EGLenum)a[0]);
            std::fprintf(stderr, "[EGL] eglBindAPI(0x%x) → %d\n", (unsigned)a[0], ok);
            return ok ? 1 : 0;
        });

    // eglChooseConfig(display, attrib_list, configs, config_size, num_config)
    add("libEGL.so", "eglChooseConfig", HVC_EGL_CHOOSE_CONFIG,
        [this](guest_t* g, const uint64_t a[8]) -> uint64_t {
            if (a[0] != GUEST_EGL_DISPLAY || egl_display_ == EGL_NO_DISPLAY) return 0;

            // Read attrib list from guest (list of EGLint pairs, terminated by EGL_NONE=0x3038)
            std::vector<EGLint> attribs;
            uint64_t gpa = a[1];
            if (gpa) {
                for (int i = 0; i < 64; ++i) {
                    EGLint v = (EGLint)guest_read_u32(g, gpa + i*4);
                    attribs.push_back(v);
                    if (v == EGL_NONE) break;
                }
            } else {
                attribs.push_back(EGL_NONE);
            }

            EGLint num = 0;
            EGLBoolean ok = eglChooseConfig(egl_display_, attribs.data(),
                                            &egl_config_, 1, &num);
            std::fprintf(stderr, "[EGL] eglChooseConfig → %d (num=%d)\n", ok, num);

            // Write guest config handle
            if (a[2] && ok && num > 0) guest_write_u64(g, a[2], GUEST_EGL_CONFIG);
            if (a[4]) guest_write_u32(g, a[4], (uint32_t)(ok ? num : 0));
            return ok ? 1 : 0;
        });

    // eglCreateContext(display, config, share_context, attrib_list)
    add("libEGL.so", "eglCreateContext", HVC_EGL_CREATE_CONTEXT,
        [this](guest_t* g, const uint64_t a[8]) -> uint64_t {
            if (a[0] != GUEST_EGL_DISPLAY || egl_display_ == EGL_NO_DISPLAY) return 0;

            // Read context attribs (e.g. EGL_CONTEXT_CLIENT_VERSION = 2 or 3)
            std::vector<EGLint> attribs;
            uint64_t gpa = a[3];
            if (gpa) {
                for (int i = 0; i < 16; ++i) {
                    EGLint v = (EGLint)guest_read_u32(g, gpa + i*4);
                    attribs.push_back(v);
                    if (v == EGL_NONE) break;
                }
            } else {
                attribs.push_back(EGL_NONE);
            }

            egl_context_ = eglCreateContext(egl_display_, egl_config_,
                                            EGL_NO_CONTEXT, attribs.data());
            std::fprintf(stderr, "[EGL] eglCreateContext → %p\n", egl_context_);
            return (egl_context_ != EGL_NO_CONTEXT) ? GUEST_EGL_CONTEXT : 0;
        });

    // eglCreateWindowSurface(display, config, native_window, attrib_list)
    // For now we create a 1×1 pbuffer — NativeWindow integration comes in Phase 5.
    add("libEGL.so", "eglCreateWindowSurface", HVC_EGL_CREATE_WIN_SURFACE,
        [this](guest_t*, const uint64_t a[8]) -> uint64_t {
            if (a[0] != GUEST_EGL_DISPLAY || egl_display_ == EGL_NO_DISPLAY) return 0;
            if (egl_surface_ == EGL_NO_SURFACE) {
                // Create a pbuffer surface as a stand-in until we have a real window
                EGLint pbuf_attribs[] = {
                    EGL_WIDTH,  1280, EGL_HEIGHT, 720, EGL_NONE
                };
                egl_surface_ = eglCreatePbufferSurface(egl_display_, egl_config_, pbuf_attribs);
                std::fprintf(stderr, "[EGL] eglCreateWindowSurface → pbuffer %p (Phase 5: replace with Metal layer)\n",
                             egl_surface_);
            }
            return (egl_surface_ != EGL_NO_SURFACE) ? GUEST_EGL_SURFACE : 0;
        });

    // eglCreatePbufferSurface(display, config, attrib_list)
    add("libEGL.so", "eglCreatePbufferSurface", HVC_EGL_CREATE_PBUF_SURFACE,
        [this](guest_t* g, const uint64_t a[8]) -> uint64_t {
            if (a[0] != GUEST_EGL_DISPLAY || egl_display_ == EGL_NO_DISPLAY) return 0;
            std::vector<EGLint> attribs;
            uint64_t gpa = a[2];
            if (gpa) {
                for (int i = 0; i < 16; ++i) {
                    EGLint v = (EGLint)guest_read_u32(g, gpa + i*4);
                    attribs.push_back(v);
                    if (v == EGL_NONE) break;
                }
            } else { attribs.push_back(EGL_NONE); }
            egl_surface_ = eglCreatePbufferSurface(egl_display_, egl_config_, attribs.data());
            std::fprintf(stderr, "[EGL] eglCreatePbufferSurface → %p\n", egl_surface_);
            return (egl_surface_ != EGL_NO_SURFACE) ? GUEST_EGL_SURFACE : 0;
        });

    // eglMakeCurrent(display, draw, read, context)
    add("libEGL.so", "eglMakeCurrent", HVC_EGL_MAKE_CURRENT,
        [this](guest_t*, const uint64_t a[8]) -> uint64_t {
            if (egl_display_ == EGL_NO_DISPLAY) return 0;
            EGLSurface draw = (a[1] == GUEST_EGL_SURFACE) ? egl_surface_ : EGL_NO_SURFACE;
            EGLSurface read = (a[2] == GUEST_EGL_SURFACE) ? egl_surface_ : EGL_NO_SURFACE;
            EGLContext ctx  = (a[3] == GUEST_EGL_CONTEXT) ? egl_context_ : EGL_NO_CONTEXT;
            EGLBoolean ok = eglMakeCurrent(egl_display_, draw, read, ctx);
            std::fprintf(stderr, "[EGL] eglMakeCurrent → %d\n", ok);
            return ok ? 1 : 0;
        });

    // eglSwapBuffers(display, surface)
    add("libEGL.so", "eglSwapBuffers", HVC_EGL_SWAP_BUFFERS,
        [this](guest_t*, const uint64_t[8]) -> uint64_t {
            if (egl_display_ == EGL_NO_DISPLAY || egl_surface_ == EGL_NO_SURFACE) return 0;
            EGLBoolean ok = eglSwapBuffers(egl_display_, egl_surface_);
            return ok ? 1 : 0;
        });

    // eglDestroyContext / eglDestroySurface
    add("libEGL.so", "eglDestroyContext", HVC_EGL_DESTROY_CONTEXT,
        [this](guest_t*, const uint64_t[8]) -> uint64_t {
            if (egl_context_ != EGL_NO_CONTEXT && egl_display_ != EGL_NO_DISPLAY) {
                eglDestroyContext(egl_display_, egl_context_);
                egl_context_ = EGL_NO_CONTEXT;
            }
            return 1;
        });

    add("libEGL.so", "eglDestroySurface", HVC_EGL_DESTROY_SURFACE,
        [this](guest_t*, const uint64_t[8]) -> uint64_t {
            if (egl_surface_ != EGL_NO_SURFACE && egl_display_ != EGL_NO_DISPLAY) {
                eglDestroySurface(egl_display_, egl_surface_);
                egl_surface_ = EGL_NO_SURFACE;
            }
            return 1;
        });

    // eglGetError()
    add("libEGL.so", "eglGetError", HVC_EGL_GET_ERROR,
        [](guest_t*, const uint64_t[8]) -> uint64_t {
            return (uint64_t)eglGetError();
        });

    // eglQueryString(display, name)
    add("libEGL.so", "eglQueryString", HVC_EGL_QUERY_STRING,
        [this](guest_t*, const uint64_t a[8]) -> uint64_t {
            if (egl_display_ == EGL_NO_DISPLAY) return 0;
            const char* s = eglQueryString(egl_display_, (EGLint)a[1]);
            // Return host pointer — guest reads it via HVC so it's fine
            return s ? (uint64_t)(uintptr_t)s : 0;
        });

    // eglSwapInterval(display, interval)
    add("libEGL.so", "eglSwapInterval", HVC_EGL_SWAP_INTERVAL,
        [this](guest_t*, const uint64_t a[8]) -> uint64_t {
            if (egl_display_ == EGL_NO_DISPLAY) return 0;
            return eglSwapInterval(egl_display_, (EGLint)a[1]) ? 1 : 0;
        });

    // eglTerminate(display)
    add("libEGL.so", "eglTerminate", HVC_EGL_TERMINATE,
        [this](guest_t*, const uint64_t[8]) -> uint64_t {
            if (egl_display_ != EGL_NO_DISPLAY) {
                eglTerminate(egl_display_); egl_display_ = EGL_NO_DISPLAY;
            }
            return 1;
        });

    // eglReleaseThread()
    add("libEGL.so", "eglReleaseThread", HVC_EGL_RELEASE_THREAD,
        [](guest_t*, const uint64_t[8]) -> uint64_t {
            eglReleaseThread(); return 1;
        });

    // eglGetProcAddress(const char* procname)
    // This is the key entry point — games resolve all their GL symbols through it.
    add("libEGL.so", "eglGetProcAddress", HVC_EGL_GET_PROC_ADDRESS,
        [this](guest_t* g, const uint64_t a[8]) -> uint64_t {
            auto name = guest_read_string(g, a[0]);
            if (name.empty()) return 0;

            // Check if we already allocated a stub for this proc
            for (auto& [nr, fn] : proc_addr_handlers_) {
                (void)fn;
                // We store name in sym_tables_ under a synthetic soname
                auto it = sym_tables_["__procaddr__"].find(name);
                if (it != sym_tables_["__procaddr__"].end()) {
                    return it->second;
                }
            }

            // Look up the real host function via ANGLE
            void* host_fn = angle_sym(name.c_str());
            if (!host_fn) {
                // Try via eglGetProcAddress on the host (covers extensions)
                using ProcFn = void*(EGLAPIENTRYP)(const char*);
                auto host_gpa = (ProcFn)::dlsym(angle_egl_lib_, "eglGetProcAddress");
                if (host_gpa) host_fn = host_gpa(name.c_str());
            }

            if (!host_fn) {
                std::fprintf(stderr, "[EGL] eglGetProcAddress(%s) → NOT FOUND\n", name.c_str());
                return 0;
            }

            // Allocate a new HVC stub number and write the stub
            uint32_t nr = next_proc_hvc_++;
            uint64_t gpa = write_stub(nr);

            // Capture host_fn in the handler — called when the guest executes the stub
            void* captured_fn = host_fn;
            handlers_[nr] = [captured_fn, name](guest_t*, const uint64_t[8]) -> uint64_t {
                // We can't safely forward AArch64 register args to an arbitrary C
                // function pointer via a generic handler. Log it for now.
                // TODO Phase 5: emit a real trampoline stub that forwards X0..X7
                std::fprintf(stderr, "[GL] %s() called via procaddr stub\n", name.c_str());
                (void)captured_fn;
                return 0;
            };

            // Record for deduplication
            sym_tables_["__procaddr__"][name] = gpa;
            proc_addr_handlers_[nr] = host_fn;

            std::fprintf(stderr, "[EGL] eglGetProcAddress(%s) → stub GPA 0x%llx\n",
                         name.c_str(), (unsigned long long)gpa);
            return gpa;
        });

    // Mirror stubs under libGLESv2.so for the EGL symbols some games dlopen directly
    for (auto& [sym, gpa] : sym_tables_["libEGL.so"])
        sym_tables_["libGLESv2.so"][sym] = gpa;
}

// ── libGLESv2 stubs ───────────────────────────────────────────────────────────
//
// These call ANGLE directly on the host — no guest memory translation needed
// for most GL calls since the params are just integers / float bits.
// Buffer data (glBufferData, glTexImage2D) copies from guest memory first.

void AndroidRuntime::register_libgles_stubs()
{
    // Convenience macro-style lambdas for the common zero-arg GL calls
    auto gl_void0 = [](auto fn) {
        return [fn](guest_t*, const uint64_t[8]) -> uint64_t { fn(); return 0; };
    };

    add("libGLESv2.so", "glGetError", HVC_GL_GET_ERROR,
        [](guest_t*, const uint64_t[8]) -> uint64_t { return (uint64_t)glGetError(); });

    add("libGLESv2.so", "glViewport", HVC_GL_VIEWPORT,
        [](guest_t*, const uint64_t a[8]) -> uint64_t {
            glViewport((GLint)a[0], (GLint)a[1], (GLsizei)a[2], (GLsizei)a[3]); return 0;
        });

    add("libGLESv2.so", "glClear", HVC_GL_CLEAR,
        [](guest_t*, const uint64_t a[8]) -> uint64_t { glClear((GLbitfield)a[0]); return 0; });

    add("libGLESv2.so", "glClearColor", HVC_GL_CLEAR_COLOR,
        [](guest_t*, const uint64_t a[8]) -> uint64_t {
            float r, g, b, al;
            memcpy(&r, &a[0], 4); memcpy(&g, &a[1], 4);
            memcpy(&b, &a[2], 4); memcpy(&al, &a[3], 4);
            glClearColor(r, g, b, al); return 0;
        });

    add("libGLESv2.so", "glClearDepthf", HVC_GL_CLEAR_DEPTH,
        [](guest_t*, const uint64_t a[8]) -> uint64_t {
            float d; memcpy(&d, &a[0], 4); glClearDepthf(d); return 0;
        });

    add("libGLESv2.so", "glEnable",   HVC_GL_ENABLE,   [](guest_t*, const uint64_t a[8]) -> uint64_t { glEnable((GLenum)a[0]);  return 0; });
    add("libGLESv2.so", "glDisable",  HVC_GL_DISABLE,  [](guest_t*, const uint64_t a[8]) -> uint64_t { glDisable((GLenum)a[0]); return 0; });
    add("libGLESv2.so", "glBlendFunc",HVC_GL_BLEND_FUNC,[](guest_t*, const uint64_t a[8]) -> uint64_t { glBlendFunc((GLenum)a[0],(GLenum)a[1]); return 0; });
    add("libGLESv2.so", "glDepthFunc",HVC_GL_DEPTH_FUNC,[](guest_t*, const uint64_t a[8]) -> uint64_t { glDepthFunc((GLenum)a[0]); return 0; });
    add("libGLESv2.so", "glDepthMask",HVC_GL_DEPTH_MASK,[](guest_t*, const uint64_t a[8]) -> uint64_t { glDepthMask((GLboolean)a[0]); return 0; });
    add("libGLESv2.so", "glCullFace", HVC_GL_CULL_FACE, [](guest_t*, const uint64_t a[8]) -> uint64_t { glCullFace((GLenum)a[0]); return 0; });
    add("libGLESv2.so", "glFrontFace",HVC_GL_FRONT_FACE,[](guest_t*, const uint64_t a[8]) -> uint64_t { glFrontFace((GLenum)a[0]); return 0; });
    add("libGLESv2.so", "glScissor",  HVC_GL_SCISSOR,   [](guest_t*, const uint64_t a[8]) -> uint64_t { glScissor((GLint)a[0],(GLint)a[1],(GLsizei)a[2],(GLsizei)a[3]); return 0; });
    add("libGLESv2.so", "glColorMask",HVC_GL_COLOR_MASK, [](guest_t*, const uint64_t a[8]) -> uint64_t { glColorMask((GLboolean)a[0],(GLboolean)a[1],(GLboolean)a[2],(GLboolean)a[3]); return 0; });
    add("libGLESv2.so", "glPixelStorei",HVC_GL_PIXEL_STOREI,[](guest_t*, const uint64_t a[8]) -> uint64_t { glPixelStorei((GLenum)a[0],(GLint)a[1]); return 0; });
    add("libGLESv2.so", "glActiveTexture",HVC_GL_ACTIVE_TEXTURE,[](guest_t*, const uint64_t a[8]) -> uint64_t { glActiveTexture((GLenum)a[0]); return 0; });
    add("libGLESv2.so", "glFinish",   HVC_GL_FINISH,    [&](guest_t*, const uint64_t[8]) -> uint64_t { glFinish(); return 0; });
    add("libGLESv2.so", "glFlush",    HVC_GL_FLUSH,     [&](guest_t*, const uint64_t[8]) -> uint64_t { glFlush(); return 0; });
    (void)gl_void0; // suppress unused warning

    // Textures
    add("libGLESv2.so", "glGenTextures", HVC_GL_GEN_TEXTURES,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            std::vector<GLuint> ids(a[0]);
            glGenTextures((GLsizei)a[0], ids.data());
            for (GLsizei i = 0; i < (GLsizei)a[0]; ++i)
                guest_write_u32(g, a[1] + i*4, ids[i]);
            return 0;
        });

    add("libGLESv2.so", "glBindTexture",   HVC_GL_BIND_TEXTURE,
        [](guest_t*, const uint64_t a[8]) -> uint64_t { glBindTexture((GLenum)a[0],(GLuint)a[1]); return 0; });
    add("libGLESv2.so", "glTexParameteri", HVC_GL_TEX_PARAMETERI,
        [](guest_t*, const uint64_t a[8]) -> uint64_t { glTexParameteri((GLenum)a[0],(GLenum)a[1],(GLint)a[2]); return 0; });
    add("libGLESv2.so", "glDeleteTextures",HVC_GL_DELETE_TEXTURES,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            std::vector<GLuint> ids(a[0]);
            for (GLsizei i = 0; i < (GLsizei)a[0]; ++i)
                ids[i] = (GLuint)guest_read_u32(g, a[1] + i*4);
            glDeleteTextures((GLsizei)a[0], ids.data());
            return 0;
        });

    add("libGLESv2.so", "glTexImage2D", HVC_GL_TEX_IMAGE2D,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            // a[0]=target a[1]=level a[2]=internalformat a[3]=width a[4]=height
            // a[5]=border a[6]=format a[7]=type  (pixels ptr lost — need extra read)
            // For now call with null pixels (enough to allocate the texture storage)
            glTexImage2D((GLenum)a[0],(GLint)a[1],(GLint)a[2],
                         (GLsizei)a[3],(GLsizei)a[4],(GLint)a[5],
                         (GLenum)a[6],(GLenum)a[7], nullptr);
            (void)g;
            return 0;
        });

    // Buffers
    add("libGLESv2.so", "glGenBuffers", HVC_GL_GEN_BUFFERS,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            std::vector<GLuint> ids(a[0]);
            glGenBuffers((GLsizei)a[0], ids.data());
            for (GLsizei i = 0; i < (GLsizei)a[0]; ++i)
                guest_write_u32(g, a[1] + i*4, ids[i]);
            return 0;
        });

    add("libGLESv2.so", "glBindBuffer", HVC_GL_BIND_BUFFER,
        [](guest_t*, const uint64_t a[8]) -> uint64_t { glBindBuffer((GLenum)a[0],(GLuint)a[1]); return 0; });
    add("libGLESv2.so", "glDeleteBuffers", HVC_GL_DELETE_BUFFERS,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            std::vector<GLuint> ids(a[0]);
            for (GLsizei i = 0; i < (GLsizei)a[0]; ++i)
                ids[i] = guest_read_u32(g, a[1] + i*4);
            glDeleteBuffers((GLsizei)a[0], ids.data());
            return 0;
        });

    add("libGLESv2.so", "glBufferData", HVC_GL_BUFFER_DATA,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            // a[0]=target a[1]=size a[2]=data_gpa a[3]=usage
            std::vector<uint8_t> buf;
            if (a[2] && a[1]) {
                buf.resize(a[1]);
                guest_read(g, a[2], buf.data(), a[1]);
            }
            glBufferData((GLenum)a[0], (GLsizeiptr)a[1],
                         buf.empty() ? nullptr : buf.data(), (GLenum)a[3]);
            return 0;
        });

    // Shaders & programs
    add("libGLESv2.so", "glCreateShader",  HVC_GL_CREATE_SHADER,
        [](guest_t*, const uint64_t a[8]) -> uint64_t { return (uint64_t)glCreateShader((GLenum)a[0]); });
    add("libGLESv2.so", "glDeleteShader",  HVC_GL_DELETE_SHADER,
        [](guest_t*, const uint64_t a[8]) -> uint64_t { glDeleteShader((GLuint)a[0]); return 0; });
    add("libGLESv2.so", "glCreateProgram", HVC_GL_CREATE_PROGRAM,
        [](guest_t*, const uint64_t[8]) -> uint64_t { return (uint64_t)glCreateProgram(); });
    add("libGLESv2.so", "glDeleteProgram", HVC_GL_DELETE_PROGRAM,
        [](guest_t*, const uint64_t a[8]) -> uint64_t { glDeleteProgram((GLuint)a[0]); return 0; });
    add("libGLESv2.so", "glAttachShader",  HVC_GL_ATTACH_SHADER,
        [](guest_t*, const uint64_t a[8]) -> uint64_t { glAttachShader((GLuint)a[0],(GLuint)a[1]); return 0; });
    add("libGLESv2.so", "glLinkProgram",   HVC_GL_LINK_PROGRAM,
        [](guest_t*, const uint64_t a[8]) -> uint64_t { glLinkProgram((GLuint)a[0]); return 0; });
    add("libGLESv2.so", "glUseProgram",    HVC_GL_USE_PROGRAM,
        [](guest_t*, const uint64_t a[8]) -> uint64_t { glUseProgram((GLuint)a[0]); return 0; });

    add("libGLESv2.so", "glShaderSource", HVC_GL_SHADER_SOURCE,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            // a[0]=shader a[1]=count a[2]=string_ptrs_gpa a[3]=lengths_gpa
            GLsizei count = (GLsizei)a[1];
            std::vector<std::string> srcs(count);
            std::vector<const char*> ptrs(count);
            for (GLsizei i = 0; i < count; ++i) {
                uint64_t ptr_gpa = guest_read_u64(g, a[2] + i*8);
                srcs[i] = guest_read_string(g, ptr_gpa);
                ptrs[i] = srcs[i].c_str();
            }
            glShaderSource((GLuint)a[0], count, ptrs.data(), nullptr);
            return 0;
        });

    add("libGLESv2.so", "glCompileShader", HVC_GL_COMPILE_SHADER,
        [](guest_t*, const uint64_t a[8]) -> uint64_t { glCompileShader((GLuint)a[0]); return 0; });

    add("libGLESv2.so", "glGetShaderiv", HVC_GL_GET_SHADER_IV,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            GLint v = 0; glGetShaderiv((GLuint)a[0],(GLenum)a[1],&v);
            if (a[2]) guest_write_u32(g, a[2], (uint32_t)v);
            return 0;
        });

    add("libGLESv2.so", "glGetProgramiv", HVC_GL_GET_PROGRAM_IV,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            GLint v = 0; glGetProgramiv((GLuint)a[0],(GLenum)a[1],&v);
            if (a[2]) guest_write_u32(g, a[2], (uint32_t)v);
            return 0;
        });

    add("libGLESv2.so", "glGetShaderInfoLog", HVC_GL_GET_SHADER_INFO_LOG,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            std::vector<char> log(a[1] ? a[1] : 512);
            GLsizei len = 0;
            glGetShaderInfoLog((GLuint)a[0],(GLsizei)a[1],&len,log.data());
            if (a[3]) guest_write(g, a[3], log.data(), len+1);
            if (a[2]) guest_write_u32(g, a[2], (uint32_t)len);
            return 0;
        });

    add("libGLESv2.so", "glGetProgramInfoLog", HVC_GL_GET_PROGRAM_INFO_LOG,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            std::vector<char> log(a[1] ? a[1] : 512);
            GLsizei len = 0;
            glGetProgramInfoLog((GLuint)a[0],(GLsizei)a[1],&len,log.data());
            if (a[3]) guest_write(g, a[3], log.data(), len+1);
            if (a[2]) guest_write_u32(g, a[2], (uint32_t)len);
            return 0;
        });

    // Attributes & uniforms
    add("libGLESv2.so", "glGetAttribLocation",  HVC_GL_GET_ATTRIB_LOC,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            auto name = guest_read_string(g,a[1]);
            return (uint64_t)(int64_t)glGetAttribLocation((GLuint)a[0],name.c_str());
        });
    add("libGLESv2.so", "glGetUniformLocation", HVC_GL_GET_UNIFORM_LOC,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            auto name = guest_read_string(g,a[1]);
            return (uint64_t)(int64_t)glGetUniformLocation((GLuint)a[0],name.c_str());
        });
    add("libGLESv2.so", "glEnableVertexAttribArray", HVC_GL_ENABLE_VERT_ATTRIB,
        [](guest_t*, const uint64_t a[8]) -> uint64_t { glEnableVertexAttribArray((GLuint)a[0]); return 0; });

    add("libGLESv2.so", "glVertexAttribPointer", HVC_GL_VERT_ATTRIB_PTR,
        [](guest_t*, const uint64_t a[8]) -> uint64_t {
            glVertexAttribPointer((GLuint)a[0],(GLint)a[1],(GLenum)a[2],
                                  (GLboolean)a[3],(GLsizei)a[4],
                                  reinterpret_cast<const void*>((uintptr_t)a[5]));
            return 0;
        });

    // Draw calls
    add("libGLESv2.so", "glDrawArrays", HVC_GL_DRAW_ARRAYS,
        [](guest_t*, const uint64_t a[8]) -> uint64_t {
            glDrawArrays((GLenum)a[0],(GLint)a[1],(GLsizei)a[2]); return 0;
        });
    add("libGLESv2.so", "glDrawElements", HVC_GL_DRAW_ELEMENTS,
        [](guest_t*, const uint64_t a[8]) -> uint64_t {
            glDrawElements((GLenum)a[0],(GLsizei)a[1],(GLenum)a[2],
                           reinterpret_cast<const void*>((uintptr_t)a[3]));
            return 0;
        });

    // Uniforms
    add("libGLESv2.so", "glUniform1i", HVC_GL_UNIFORM1I,
        [](guest_t*, const uint64_t a[8]) -> uint64_t { glUniform1i((GLint)a[0],(GLint)a[1]); return 0; });
    add("libGLESv2.so", "glUniform1f", HVC_GL_UNIFORM1F,
        [](guest_t*, const uint64_t a[8]) -> uint64_t {
            float v; memcpy(&v,&a[1],4); glUniform1f((GLint)a[0],v); return 0;
        });
    add("libGLESv2.so", "glUniform4f", HVC_GL_UNIFORM4F,
        [](guest_t*, const uint64_t a[8]) -> uint64_t {
            float x,y,z,w;
            memcpy(&x,&a[1],4); memcpy(&y,&a[2],4);
            memcpy(&z,&a[3],4); memcpy(&w,&a[4],4);
            glUniform4f((GLint)a[0],x,y,z,w); return 0;
        });
    add("libGLESv2.so", "glUniformMatrix4fv", HVC_GL_UNIFORM_MATRIX4FV,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            // a[0]=location a[1]=count a[2]=transpose a[3]=value_gpa
            GLsizei count = (GLsizei)a[1];
            std::vector<float> mat(count * 16);
            guest_read(g, a[3], mat.data(), mat.size()*4);
            glUniformMatrix4fv((GLint)a[0],count,(GLboolean)a[2],mat.data());
            return 0;
        });

    // Queries
    add("libGLESv2.so", "glGetIntegerv", HVC_GL_GET_INTEGERV,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            GLint v = 0; glGetIntegerv((GLenum)a[0],&v);
            if (a[1]) guest_write_u32(g,a[1],(uint32_t)v);
            return 0;
        });
    add("libGLESv2.so", "glGetString", HVC_GL_GET_STRING,
        [](guest_t*, const uint64_t a[8]) -> uint64_t {
            const GLubyte* s = glGetString((GLenum)a[0]);
            return s ? (uint64_t)(uintptr_t)s : 0;
        });

    // Framebuffers & renderbuffers
    add("libGLESv2.so", "glGenFramebuffers",  HVC_GL_GEN_FRAMEBUFFERS,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            std::vector<GLuint> ids(a[0]); glGenFramebuffers((GLsizei)a[0],ids.data());
            for (GLsizei i=0;i<(GLsizei)a[0];++i) guest_write_u32(g,a[1]+i*4,ids[i]);
            return 0;
        });
    add("libGLESv2.so", "glBindFramebuffer",  HVC_GL_BIND_FRAMEBUFFER,
        [](guest_t*, const uint64_t a[8]) -> uint64_t { glBindFramebuffer((GLenum)a[0],(GLuint)a[1]); return 0; });
    add("libGLESv2.so", "glFramebufferTexture2D", HVC_GL_FRAMEBUFFER_TEXTURE2D,
        [](guest_t*, const uint64_t a[8]) -> uint64_t {
            glFramebufferTexture2D((GLenum)a[0],(GLenum)a[1],(GLenum)a[2],(GLuint)a[3],(GLint)a[4]);
            return 0;
        });
    add("libGLESv2.so", "glGenRenderbuffers",  HVC_GL_GEN_RENDERBUFFERS,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            std::vector<GLuint> ids(a[0]); glGenRenderbuffers((GLsizei)a[0],ids.data());
            for (GLsizei i=0;i<(GLsizei)a[0];++i) guest_write_u32(g,a[1]+i*4,ids[i]);
            return 0;
        });
    add("libGLESv2.so", "glBindRenderbuffer",  HVC_GL_BIND_RENDERBUFFER,
        [](guest_t*, const uint64_t a[8]) -> uint64_t { glBindRenderbuffer((GLenum)a[0],(GLuint)a[1]); return 0; });
    add("libGLESv2.so", "glRenderbufferStorage", HVC_GL_RENDERBUFFER_STORAGE,
        [](guest_t*, const uint64_t a[8]) -> uint64_t {
            glRenderbufferStorage((GLenum)a[0],(GLenum)a[1],(GLsizei)a[2],(GLsizei)a[3]);
            return 0;
        });
    add("libGLESv2.so", "glFramebufferRenderbuffer", HVC_GL_FRAMEBUFFER_RENDERBUF,
        [](guest_t*, const uint64_t a[8]) -> uint64_t {
            glFramebufferRenderbuffer((GLenum)a[0],(GLenum)a[1],(GLenum)a[2],(GLuint)a[3]);
            return 0;
        });

    // Mirror under libGLESv3.so — GLES3 is a superset
    sym_tables_["libGLESv3.so"] = sym_tables_["libGLESv2.so"];
}

} // namespace muplar::runtime::android
