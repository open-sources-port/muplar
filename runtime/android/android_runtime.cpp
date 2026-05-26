// runtime/android/android_runtime.cpp
#include "android_runtime.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <unistd.h>
#include <dlfcn.h>
#include <vector>
#include <algorithm>
#include <utility>

// EGL / GLES headers (macOS host — provided by ANGLE)
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

extern "C" {
    #include "core/guest.h"
    #include "syscall/proc.h"
}

namespace muplar::runtime::android {

// ── HVC call number ranges ────────────────────────────────────────────────────
// libc      : 0x2000–0x20FF
// liblog    : 0x2100–0x21FF
// libandroid: 0x2200–0x22FF
// libdl     : 0x2300–0x23FF
// libEGL    : 0x2400–0x24FF
// libGLESv2 : 0x2500–0x25FF
// libbinder_ndk: 0x2700–0x27FF
// procaddr  : 0x2800–0x2FFF  (dynamic, one per eglGetProcAddress result)

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
static constexpr uint32_t HVC_PTHREAD_COND_INIT   = 0x2040;
static constexpr uint32_t HVC_PTHREAD_COND_WAIT   = 0x2041;
static constexpr uint32_t HVC_PTHREAD_COND_SIGNAL = 0x2042;
static constexpr uint32_t HVC_PTHREAD_COND_BCAST  = 0x2043;
static constexpr uint32_t HVC_PTHREAD_COND_DESTROY= 0x2044;
static constexpr uint32_t HVC_PTHREAD_ATTR_INIT   = 0x2045;
static constexpr uint32_t HVC_PTHREAD_ATTR_DESTROY= 0x2046;
static constexpr uint32_t HVC_PTHREAD_ATTR_SETDETACH = 0x2047;
static constexpr uint32_t HVC_PTHREAD_DETACH      = 0x2048;
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
static constexpr uint32_t HVC_PIPE                = 0x203C;
static constexpr uint32_t HVC_PIPE2               = 0x203D;
static constexpr uint32_t HVC_READ                = 0x203E;
static constexpr uint32_t HVC_WRITE               = 0x203F;
static constexpr uint32_t HVC_CLOSE               = 0x2049;

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
static constexpr uint32_t HVC_ASSET_MGR_FROM_JAVA = 0x2210;
static constexpr uint32_t HVC_ASSET_OPEN          = 0x2211;
static constexpr uint32_t HVC_ASSET_READ          = 0x2212;
static constexpr uint32_t HVC_ASSET_CLOSE         = 0x2213;
static constexpr uint32_t HVC_ASSET_LENGTH        = 0x2214;
static constexpr uint32_t HVC_ASSET_REMAINING     = 0x2215;
static constexpr uint32_t HVC_ASSET_SEEK          = 0x2216;
static constexpr uint32_t HVC_CHOREOGRAPHER_GET   = 0x2220;
static constexpr uint32_t HVC_CHOREOGRAPHER_CB    = 0x2221;
static constexpr uint32_t HVC_NATIVE_WINDOW_SET_BUF = 0x2230;
static constexpr uint32_t HVC_NATIVE_WINDOW_LOCK    = 0x2231;
static constexpr uint32_t HVC_NATIVE_WINDOW_UNLOCK  = 0x2232;
static constexpr uint32_t HVC_NATIVE_WINDOW_ACQUIRE = 0x2233;
static constexpr uint32_t HVC_NATIVE_WINDOW_RELEASE = 0x2234;
static constexpr uint32_t HVC_NATIVE_WINDOW_WIDTH   = 0x2235;
static constexpr uint32_t HVC_NATIVE_WINDOW_HEIGHT  = 0x2236;
static constexpr uint32_t HVC_NATIVE_WINDOW_FORMAT  = 0x2237;
static constexpr uint32_t HVC_PROP_GET              = 0x2240;
static constexpr uint32_t HVC_INPUT_QUEUE_ATTACH    = 0x2250;
static constexpr uint32_t HVC_INPUT_QUEUE_DETACH    = 0x2251;
static constexpr uint32_t HVC_INPUT_QUEUE_HAS_EVENTS= 0x2252;
static constexpr uint32_t HVC_INPUT_QUEUE_GET_EVENT = 0x2253;
static constexpr uint32_t HVC_INPUT_QUEUE_PRE_DISPATCH = 0x2254;
static constexpr uint32_t HVC_INPUT_QUEUE_FINISH    = 0x2255;
static constexpr uint32_t HVC_INPUT_EVENT_GET_TYPE  = 0x2260;
static constexpr uint32_t HVC_INPUT_EVENT_GET_DEVICE_ID = 0x2261;
static constexpr uint32_t HVC_INPUT_EVENT_GET_SOURCE = 0x2262;
static constexpr uint32_t HVC_MOTION_EVENT_GET_ACTION = 0x2263;
static constexpr uint32_t HVC_KEY_EVENT_GET_ACTION  = 0x2264;
static constexpr uint32_t HVC_KEY_EVENT_GET_KEYCODE = 0x2265;
static constexpr uint32_t HVC_CHOREOGRAPHER_CB_DELAYED = 0x2270;
static constexpr uint32_t HVC_CHOREOGRAPHER_CB64       = 0x2271;
static constexpr uint32_t HVC_CHOREOGRAPHER_CB64_DELAYED = 0x2272;

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
// Less common calls come through eglGetProcAddress (0x2800+).
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

// libbinder_ndk
static constexpr uint32_t HVC_SERVICE_CHECK           = 0x2700;
static constexpr uint32_t HVC_SERVICE_GET             = 0x2701;
static constexpr uint32_t HVC_SERVICE_WAIT            = 0x2702;
static constexpr uint32_t HVC_SERVICE_IS_DECLARED     = 0x2703;
static constexpr uint32_t HVC_SERVICE_ADD             = 0x2704;
static constexpr uint32_t HVC_BINDER_IS_REMOTE        = 0x2710;
static constexpr uint32_t HVC_BINDER_IS_ALIVE         = 0x2711;
static constexpr uint32_t HVC_BINDER_PING             = 0x2712;
static constexpr uint32_t HVC_BINDER_INC_STRONG       = 0x2713;
static constexpr uint32_t HVC_BINDER_DEC_STRONG       = 0x2714;
static constexpr uint32_t HVC_BINDER_REF_COUNT        = 0x2715;
static constexpr uint32_t HVC_BINDER_CALLING_UID      = 0x2716;
static constexpr uint32_t HVC_BINDER_CALLING_PID      = 0x2717;
static constexpr uint32_t HVC_BINDER_HANDLING_TX      = 0x2718;
static constexpr uint32_t HVC_BINDER_DUMP             = 0x2719;
static constexpr uint32_t HVC_BINDER_LINK_DEATH       = 0x271A;
static constexpr uint32_t HVC_BINDER_UNLINK_DEATH     = 0x271B;
static constexpr uint32_t HVC_BINDER_PREPARE_TX       = 0x271C;
static constexpr uint32_t HVC_BINDER_TRANSACT         = 0x271D;
static constexpr uint32_t HVC_PARCEL_DELETE           = 0x2720;
static constexpr uint32_t HVC_PARCEL_SET_POS          = 0x2721;
static constexpr uint32_t HVC_PARCEL_GET_POS          = 0x2722;
static constexpr uint32_t HVC_PARCEL_WRITE_BINDER     = 0x2723;
static constexpr uint32_t HVC_PARCEL_READ_BINDER      = 0x2724;
static constexpr uint32_t HVC_PARCEL_WRITE_STATUS     = 0x2725;
static constexpr uint32_t HVC_PARCEL_READ_STATUS      = 0x2726;
static constexpr uint32_t HVC_PARCEL_WRITE_STRING     = 0x2727;
static constexpr uint32_t HVC_PARCEL_READ_STRING      = 0x2728;
static constexpr uint32_t HVC_PARCEL_WRITE_STRING_ARRAY = 0x2729;
static constexpr uint32_t HVC_PARCEL_READ_STRING_ARRAY  = 0x272A;
static constexpr uint32_t HVC_PARCEL_WRITE_PARCELABLE_ARRAY = 0x272B;
static constexpr uint32_t HVC_PARCEL_READ_PARCELABLE_ARRAY  = 0x272C;
static constexpr uint32_t HVC_PARCEL_WRITE_FD        = 0x272D;
static constexpr uint32_t HVC_PARCEL_READ_FD         = 0x272E;
static constexpr uint32_t HVC_PARCEL_WRITE_I32        = 0x2730;
static constexpr uint32_t HVC_PARCEL_WRITE_U32        = 0x2731;
static constexpr uint32_t HVC_PARCEL_WRITE_I64        = 0x2732;
static constexpr uint32_t HVC_PARCEL_WRITE_U64        = 0x2733;
static constexpr uint32_t HVC_PARCEL_WRITE_FLOAT      = 0x2734;
static constexpr uint32_t HVC_PARCEL_WRITE_DOUBLE     = 0x2735;
static constexpr uint32_t HVC_PARCEL_WRITE_BOOL       = 0x2736;
static constexpr uint32_t HVC_PARCEL_WRITE_CHAR       = 0x2737;
static constexpr uint32_t HVC_PARCEL_WRITE_BYTE       = 0x2738;
static constexpr uint32_t HVC_PARCEL_WRITE_I32_ARRAY  = 0x2739;
static constexpr uint32_t HVC_PARCEL_READ_I32         = 0x2740;
static constexpr uint32_t HVC_PARCEL_READ_U32         = 0x2741;
static constexpr uint32_t HVC_PARCEL_READ_I64         = 0x2742;
static constexpr uint32_t HVC_PARCEL_READ_U64         = 0x2743;
static constexpr uint32_t HVC_PARCEL_READ_FLOAT       = 0x2744;
static constexpr uint32_t HVC_PARCEL_READ_DOUBLE      = 0x2745;
static constexpr uint32_t HVC_PARCEL_READ_BOOL        = 0x2746;
static constexpr uint32_t HVC_PARCEL_READ_CHAR        = 0x2747;
static constexpr uint32_t HVC_PARCEL_READ_BYTE        = 0x2748;
static constexpr uint32_t HVC_PARCEL_READ_I32_ARRAY   = 0x2749;
static constexpr uint32_t HVC_BINDER_CLASS_DEFINE     = 0x2750;
static constexpr uint32_t HVC_BINDER_CLASS_SET_DUMP   = 0x2751;
static constexpr uint32_t HVC_BINDER_CLASS_SET_NAMES  = 0x2752;
static constexpr uint32_t HVC_BINDER_CLASS_GET_NAME   = 0x2753;
static constexpr uint32_t HVC_BINDER_NEW              = 0x2754;
static constexpr uint32_t HVC_BINDER_ASSOC_CLASS      = 0x2755;
static constexpr uint32_t HVC_BINDER_GET_CLASS        = 0x2756;
static constexpr uint32_t HVC_BINDER_GET_USER_DATA    = 0x2757;
static constexpr uint32_t HVC_BINDER_CLASS_GET_FN_NAME = 0x2758;
static constexpr uint32_t HVC_STATUS_NEW_OK           = 0x2760;
static constexpr uint32_t HVC_STATUS_FROM_STATUS      = 0x2761;
static constexpr uint32_t HVC_STATUS_IS_OK            = 0x2762;
static constexpr uint32_t HVC_STATUS_EXCEPTION        = 0x2763;
static constexpr uint32_t HVC_STATUS_SERVICE_ERR      = 0x2764;
static constexpr uint32_t HVC_STATUS_STATUS           = 0x2765;
static constexpr uint32_t HVC_STATUS_MESSAGE          = 0x2766;
static constexpr uint32_t HVC_STATUS_DELETE           = 0x2767;
static constexpr uint32_t HVC_BINDER_DEATH_NEW        = 0x2768;
static constexpr uint32_t HVC_BINDER_DEATH_SET_UNLINK = 0x2769;
static constexpr uint32_t HVC_BINDER_DEATH_DELETE     = 0x276A;

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

static uint64_t guest_neg_errno(int err)
{
    return static_cast<uint64_t>(-static_cast<int64_t>(err));
}

static uint64_t guest_negative_one()
{
    return static_cast<uint64_t>(static_cast<int64_t>(-1));
}

static bool safe_asset_name(const std::string& name)
{
    if (name.empty() || name.front() == '/' ||
        name.find('\\') != std::string::npos ||
        name.find(':') != std::string::npos) {
        return false;
    }

    size_t start = 0;
    while (start <= name.size()) {
        size_t end = name.find('/', start);
        std::string part = name.substr(
            start, end == std::string::npos ? std::string::npos : end - start);
        if (part == "..")
            return false;
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return true;
}

static std::vector<uint8_t> read_asset_file(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};

    in.seekg(0, std::ios::end);
    std::streamoff size = in.tellg();
    if (size < 0)
        return {};
    in.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(static_cast<size_t>(size));
    if (!data.empty())
        in.read(reinterpret_cast<char*>(data.data()), size);
    if (!in && size > 0)
        return {};
    return data;
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
        if (spec == 'l' && i + 1 < fmt.size()) {
            if (fmt[i + 1] == 'l' && i + 2 < fmt.size()) {
                i += 2;
            } else {
                i += 1;
            }
            spec = fmt[i];
        }
        if (arg >= vararg_count) { out.push_back('%'); out.push_back(spec); continue; }

        uint64_t value = varargs[arg++];
        switch (spec) {
        case 'd': case 'i': {
            int64_t signed_value = static_cast<int64_t>(value);
            if ((value & 0xffffffff00000000ULL) == 0 &&
                (value & 0x80000000ULL)) {
                signed_value = static_cast<int32_t>(value);
            }
            std::snprintf(tmp, sizeof(tmp), "%lld",
                          (long long)signed_value);
            out += tmp;
            break;
        }
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

AndroidRuntime::AndroidRuntime(guest_t* guest,
                               uint64_t stub_arena_gpa,
                               bool host_window_enabled)
    : guest_(guest),
      arena_gpa_(stub_arena_gpa),
      host_window_enabled_(host_window_enabled)
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

void AndroidRuntime::set_asset_root(std::string asset_root)
{
    asset_root_ = std::move(asset_root);
}

void AndroidRuntime::set_guest_function_invoker(GuestFunctionInvoker invoker)
{
    guest_function_invoker_ = std::move(invoker);
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

bool AndroidRuntime::host_window_active() const
{
    return host_window_ && host_window_->valid() && !host_window_->closed();
}

void AndroidRuntime::run_host_window_after_guest(int linger_ms)
{
    if (!host_window_active()) return;

    if (linger_ms >= 0) {
        host_window_->run_for_ms(linger_ms);
    } else {
        std::fprintf(stderr, "[HostWindow] close the Muplar window to exit\n");
        host_window_->run_until_closed();
    }
}

bool AndroidRuntime::pump_host_app_events()
{
    if (!host_window_enabled_)
        return false;

    bool did_work = collect_host_input_events();
    did_work |= queue_ready_looper_callbacks();
    if (input_queue_.attached && next_input_event_ < input_events_.size())
        did_work = true;
    return did_work;
}

void AndroidRuntime::set_thread_yield_enabled(bool enabled)
{
    thread_yield_enabled_ = enabled;
    if (!enabled)
        thread_yielded_ = false;
}

bool AndroidRuntime::consume_thread_yield()
{
    bool yielded = thread_yielded_;
    thread_yielded_ = false;
    return yielded;
}

std::vector<AndroidRuntime::PendingPthreadCall>
AndroidRuntime::take_pending_pthread_calls()
{
    std::vector<PendingPthreadCall> out;
    out.swap(pending_pthreads_);
    return out;
}

void AndroidRuntime::complete_pthread_call(uint64_t handle, uint64_t retval)
{
    pthread_returns_[handle] = retval;

    auto join_it = pending_pthread_join_retvals_.find(handle);
    if (join_it != pending_pthread_join_retvals_.end()) {
        if (join_it->second)
            guest_write_u64(guest_, join_it->second, retval);
        pending_pthread_join_retvals_.erase(join_it);
    }
}

std::vector<AndroidRuntime::PendingLooperCallback>
AndroidRuntime::take_pending_looper_callbacks()
{
    std::vector<PendingLooperCallback> out;
    out.swap(pending_looper_callbacks_);
    return out;
}

std::vector<AndroidRuntime::PendingFrameCallback>
AndroidRuntime::take_pending_frame_callbacks()
{
    std::vector<PendingFrameCallback> out;
    out.swap(pending_frame_callbacks_);
    return out;
}

void AndroidRuntime::ensure_host_window()
{
    if (!host_window_enabled_) return;
    if (host_window_active()) return;

    host_window_ = std::make_unique<HostWindow>(
        native_window_.width, native_window_.height);
    if (!host_window_->valid()) {
        std::fprintf(stderr, "[HostWindow] unavailable on this host/thread\n");
        host_window_.reset();
    }
}

void AndroidRuntime::rearm_looper_fd(int32_t fd)
{
    for (auto& reg : looper_regs_) {
        if (reg.fd == fd)
            reg.delivered = false;
    }
}

AndroidRuntime::HostPipe* AndroidRuntime::pipe_for_fd(int32_t fd)
{
    for (auto& pipe : pipes_) {
        if (pipe.read_fd == fd || pipe.write_fd == fd)
            return &pipe;
    }
    return nullptr;
}

const AndroidRuntime::HostPipe* AndroidRuntime::pipe_for_fd(int32_t fd) const
{
    for (const auto& pipe : pipes_) {
        if (pipe.read_fd == fd || pipe.write_fd == fd)
            return &pipe;
    }
    return nullptr;
}

bool AndroidRuntime::looper_fd_ready(int32_t fd) const
{
    const HostPipe* pipe = pipe_for_fd(fd);
    if (!pipe)
        return true;
    if (fd == pipe->read_fd)
        return !pipe->buffer.empty() || !pipe->write_open;
    return pipe->write_open;
}

bool AndroidRuntime::collect_host_input_events()
{
    if (!host_window_enabled_)
        return false;

    ensure_host_window();
    if (!host_window_active())
        return false;

    host_window_->pump_events();
    auto events = host_window_->take_input_events();
    if (!input_queue_.attached || events.empty())
        return false;

    for (const auto& host_event : events) {
        uint64_t handle = GUEST_INPUT_EVENT_BASE +
            static_cast<uint64_t>(input_events_.size()) * 0x100ULL;
        input_events_.push_back({
            handle,
            host_event.type,
            host_event.action,
            host_event.source,
            host_event.device_id,
            host_event.key_code,
            host_event.x,
            host_event.y,
            false,
            false
        });
        std::fprintf(stderr,
            "[InputQueue] host event handle=0x%llx type=%d action=%d source=0x%x key=%d x=%.1f y=%.1f\n",
            (unsigned long long)handle,
            host_event.type,
            host_event.action,
            host_event.source,
            host_event.key_code,
            static_cast<double>(host_event.x),
            static_cast<double>(host_event.y));
    }

    rearm_looper_fd(INPUT_QUEUE_FD);
    return true;
}

bool AndroidRuntime::queue_ready_looper_callbacks()
{
    bool did_work = false;
    for (auto& reg : looper_regs_) {
        if (reg.delivered || !reg.callback)
            continue;

        if (reg.fd == INPUT_QUEUE_FD &&
            (!input_queue_.attached || next_input_event_ >= input_events_.size())) {
            continue;
        }
        if (!looper_fd_ready(reg.fd))
            continue;

        reg.delivered = true;
        pending_looper_callbacks_.push_back({
            reg.callback, reg.fd, reg.events, reg.data, reg.ident
        });
        did_work = true;

        std::fprintf(stderr,
            "[ALooper] queued callback fd=%d ident=%d events=0x%x callback=0x%llx data=0x%llx\n",
            reg.fd, reg.ident, reg.events,
            (unsigned long long)reg.callback,
            (unsigned long long)reg.data);
    }
    return did_work;
}

void AndroidRuntime::present_native_window_buffer()
{
    if (!host_window_enabled_ || !native_window_.bits_gpa) return;
    ensure_host_window();
    if (!host_window_active()) return;

    size_t bytes = static_cast<size_t>(native_window_.stride) *
                   static_cast<size_t>(native_window_.height) * 4;
    if (bytes == 0 || bytes > native_window_.bits_size) return;

    std::vector<uint8_t> pixels(bytes);
    guest_read(guest_, native_window_.bits_gpa, pixels.data(), bytes);
    host_window_->present_rgba(
        pixels.data(), native_window_.width, native_window_.height,
        native_window_.stride);
}

void AndroidRuntime::present_egl_surface()
{
    if (!host_window_enabled_) return;
    ensure_host_window();
    if (!host_window_active()) return;

    int width = native_window_.width;
    int height = native_window_.height;
    if (width <= 0 || height <= 0) return;

    std::vector<uint8_t> pixels(static_cast<size_t>(width) *
                                static_cast<size_t>(height) * 4);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    host_window_->present_rgba(pixels.data(), width, height, width);
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
    native_window_.bits_gpa = arena_gpa_ + 0x0A0000;
    native_window_.bits_size =
        static_cast<uint64_t>(MAX_NATIVE_WINDOW_WIDTH) *
        static_cast<uint64_t>(MAX_NATIVE_WINDOW_HEIGHT) * 4;
    input_queue_ = {};
    input_events_ = {
        { GUEST_INPUT_EVENT_BASE + 0x00, 2, 0, 0x1002, 1, 0, 160.0f, 120.0f, false, false },
        { GUEST_INPUT_EVENT_BASE + 0x100, 2, 1, 0x1002, 1, 0, 160.0f, 120.0f, false, false },
    };
    next_input_event_ = 0;
    current_input_event_ = 0;
    pipes_.clear();
    next_pipe_fd_ = 200;
    pending_frame_callbacks_.clear();
    next_frame_time_nanos_ = 16'666'666ULL;

    load_angle();   // non-fatal — stubs still register, calls log + return 0

    register_libc_stubs();
    register_liblog_stubs();
    register_libandroid_stubs();
    register_libdl_stubs();
    register_libbinder_stubs();
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
            constexpr uint64_t kGuestThreadStackSize = 64 * 1024;
            uint64_t h = next_thread_handle_++;
            uint64_t stack_base = (heap_bump_ + 15) & ~15ULL;
            uint64_t stack_top = stack_base + kGuestThreadStackSize;
            if (stack_top > heap_base_ + HEAP_SIZE)
                return 11; // EAGAIN

            heap_bump_ = stack_top;
            std::vector<uint8_t> zero(kGuestThreadStackSize, 0);
            guest_write(g, stack_base, zero.data(), zero.size());

            if (a[0]) guest_write_u64(g, a[0], h);
            threads_[h] = {stack_base, kGuestThreadStackSize};
            pending_pthreads_.push_back({h, a[2], a[3], stack_top});
            std::fprintf(stderr, "[ART] pthread_create fn=0x%llx arg=0x%llx handle=0x%llx stack=0x%llx..0x%llx (queued)\n",
                         (unsigned long long)a[2], (unsigned long long)a[3],
                         (unsigned long long)h,
                         (unsigned long long)stack_base,
                         (unsigned long long)stack_top);
            return 0;
        });

    add("libc.so", "pthread_join", HVC_PTHREAD_JOIN,
        [this](guest_t* g, const uint64_t a[8]) -> uint64_t {
            auto ret = pthread_returns_.find(a[0]);
            if (ret != pthread_returns_.end()) {
                if (a[1]) guest_write_u64(g, a[1], ret->second);
            } else if (a[1]) {
                pending_pthread_join_retvals_[a[0]] = a[1];
            }
            std::fprintf(stderr, "[ART] pthread_join handle=0x%llx\n",
                         (unsigned long long)a[0]);
            return 0;
        });

    add("libc.so", "pthread_detach", HVC_PTHREAD_DETACH,
        [](guest_t*, const uint64_t a[8]) -> uint64_t {
            std::fprintf(stderr, "[ART] pthread_detach handle=0x%llx\n",
                         (unsigned long long)a[0]);
            return 0;
        });

    add("libc.so", "pthread_attr_init", HVC_PTHREAD_ATTR_INIT,
        [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; });
    add("libc.so", "pthread_attr_destroy", HVC_PTHREAD_ATTR_DESTROY,
        [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; });
    add("libc.so", "pthread_attr_setdetachstate", HVC_PTHREAD_ATTR_SETDETACH,
        [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; });

    add("libc.so", "pthread_cond_init", HVC_PTHREAD_COND_INIT,
        [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; });
    add("libc.so", "pthread_cond_wait", HVC_PTHREAD_COND_WAIT,
        [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; });
    add("libc.so", "pthread_cond_signal", HVC_PTHREAD_COND_SIGNAL,
        [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; });
    add("libc.so", "pthread_cond_broadcast", HVC_PTHREAD_COND_BCAST,
        [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; });
    add("libc.so", "pthread_cond_destroy", HVC_PTHREAD_COND_DESTROY,
        [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; });

    add("libc.so", "pthread_self", HVC_PTHREAD_SELF,
        [](guest_t*, const uint64_t[8]) -> uint64_t { return 1; });
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

    auto create_pipe = [this](guest_t* g, uint64_t fds_gpa) -> uint64_t {
        if (!fds_gpa)
            return guest_neg_errno(14); // EFAULT

        HostPipe pipe;
        pipe.read_fd = next_pipe_fd_++;
        pipe.write_fd = next_pipe_fd_++;
        pipes_.push_back(pipe);

        guest_write_u32(g, fds_gpa + 0, static_cast<uint32_t>(pipe.read_fd));
        guest_write_u32(g, fds_gpa + 4, static_cast<uint32_t>(pipe.write_fd));
        std::fprintf(stderr, "[ART] pipe -> [%d,%d]\n",
                     pipe.read_fd, pipe.write_fd);
        return 0;
    };

    add("libc.so", "pipe", HVC_PIPE,
        [create_pipe](guest_t* g, const uint64_t a[8]) -> uint64_t {
            return create_pipe(g, a[0]);
        });
    add("libc.so", "pipe2", HVC_PIPE2,
        [create_pipe](guest_t* g, const uint64_t a[8]) -> uint64_t {
            (void)a[1];
            return create_pipe(g, a[0]);
        });
    add("libc.so", "read", HVC_READ,
        [this](guest_t* g, const uint64_t a[8]) -> uint64_t {
            int32_t fd = static_cast<int32_t>(a[0]);
            HostPipe* pipe = pipe_for_fd(fd);
            if (!pipe || fd != pipe->read_fd || !pipe->read_open)
                return guest_neg_errno(9); // EBADF
            if (!a[1] && a[2])
                return guest_neg_errno(14); // EFAULT

            size_t requested = a[2] > SIZE_MAX ? SIZE_MAX : static_cast<size_t>(a[2]);
            size_t to_copy = std::min(requested, pipe->buffer.size());
            if (to_copy == 0)
                return pipe->write_open ? guest_neg_errno(11) : 0; // EAGAIN or EOF

            if (guest_write(g, a[1], pipe->buffer.data(), to_copy) != 0)
                return guest_neg_errno(14);
            pipe->buffer.erase(pipe->buffer.begin(), pipe->buffer.begin() + to_copy);
            if (!pipe->buffer.empty())
                rearm_looper_fd(pipe->read_fd);

            std::fprintf(stderr, "[ART] read pipe fd=%d bytes=%zu remaining=%zu\n",
                         fd, to_copy, pipe->buffer.size());
            return static_cast<uint64_t>(to_copy);
        });
    add("libc.so", "write", HVC_WRITE,
        [this](guest_t* g, const uint64_t a[8]) -> uint64_t {
            int32_t fd = static_cast<int32_t>(a[0]);
            if (!a[1] && a[2])
                return guest_neg_errno(14); // EFAULT

            size_t count = a[2] > SIZE_MAX ? SIZE_MAX : static_cast<size_t>(a[2]);
            std::vector<uint8_t> bytes(count);
            if (count && guest_read(g, a[1], bytes.data(), count) != 0)
                return guest_neg_errno(14);

            if (fd == 1 || fd == 2) {
                if (!bytes.empty())
                    std::fwrite(bytes.data(), 1, bytes.size(), stderr);
                return static_cast<uint64_t>(count);
            }

            HostPipe* pipe = pipe_for_fd(fd);
            if (!pipe || fd != pipe->write_fd || !pipe->write_open)
                return guest_neg_errno(9); // EBADF

            pipe->buffer.insert(pipe->buffer.end(), bytes.begin(), bytes.end());
            rearm_looper_fd(pipe->read_fd);
            std::fprintf(stderr, "[ART] write pipe fd=%d bytes=%zu queued=%zu\n",
                         fd, count, pipe->buffer.size());
            return static_cast<uint64_t>(count);
        });
    add("libc.so", "close", HVC_CLOSE,
        [this](guest_t*, const uint64_t a[8]) -> uint64_t {
            int32_t fd = static_cast<int32_t>(a[0]);
            HostPipe* pipe = pipe_for_fd(fd);
            if (!pipe)
                return guest_neg_errno(9); // EBADF
            if (fd == pipe->read_fd)
                pipe->read_open = false;
            if (fd == pipe->write_fd) {
                pipe->write_open = false;
                rearm_looper_fd(pipe->read_fd);
            }
            std::fprintf(stderr, "[ART] close fd=%d\n", fd);
            return 0;
        });

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
    auto input_event_pending = [this]() -> bool {
        collect_host_input_events();
        return next_input_event_ < input_events_.size();
    };
    auto find_input_event = [this](uint64_t handle) -> InputEventState* {
        auto it = std::find_if(input_events_.begin(), input_events_.end(),
            [handle](const InputEventState& event) {
                return event.handle == handle;
            });
        return it == input_events_.end() ? nullptr : &*it;
    };

    add("libandroid.so", "ALooper_prepare",  HVC_ALOOPER_PREPARE,
        [this](guest_t*, const uint64_t[8]) -> uint64_t {
            return arena_gpa_ + 0x100;
        });
    add("libandroid.so", "ALooper_acquire",  HVC_ALOOPER_ACQUIRE,
        [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; });
    add("libandroid.so", "ALooper_release",  HVC_ALOOPER_RELEASE,
        [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; });

    auto poll_looper = [this, input_event_pending](guest_t* g, const uint64_t a[8]) -> uint64_t {
        constexpr uint64_t ALOOPER_POLL_TIMEOUT =
            static_cast<uint64_t>(static_cast<int64_t>(-3));
        constexpr uint64_t ALOOPER_POLL_CALLBACK =
            static_cast<uint64_t>(static_cast<int64_t>(-2));

        for (auto& reg : looper_regs_) {
            if (reg.delivered) continue;
            if (reg.fd == INPUT_QUEUE_FD &&
                (!input_queue_.attached || !input_event_pending())) {
                continue;
            }
            if (!looper_fd_ready(reg.fd))
                continue;
            reg.delivered = true;

            if (a[1]) guest_write_u32(g, a[1], static_cast<uint32_t>(reg.fd));
            if (a[2]) guest_write_u32(g, a[2], static_cast<uint32_t>(reg.events));
            if (a[3]) guest_write_u64(g, a[3], reg.data);

            std::fprintf(stderr,
                "[ALooper] poll fd=%d ident=%d events=0x%x callback=0x%llx data=0x%llx\n",
                reg.fd, reg.ident, reg.events,
                (unsigned long long)reg.callback,
                (unsigned long long)reg.data);

            if (reg.callback) {
                pending_looper_callbacks_.push_back({
                    reg.callback, reg.fd, reg.events, reg.data, reg.ident
                });
                return ALOOPER_POLL_CALLBACK;
            }
            return static_cast<uint64_t>(static_cast<int64_t>(reg.ident));
        }

        int32_t timeout_ms = static_cast<int32_t>(a[0]);
        if (timeout_ms < 0 && thread_yield_enabled_) {
            thread_yielded_ = true;
            proc_request_hvc6_yield();
        }

        return ALOOPER_POLL_TIMEOUT;
    };

    add("libandroid.so", "ALooper_pollOnce", HVC_ALOOPER_POLL_ONCE,
        poll_looper);
    add("libandroid.so", "ALooper_pollAll",  HVC_ALOOPER_POLL_ALL,
        poll_looper);
    add("libandroid.so", "ALooper_addFd",    HVC_ALOOPER_ADD_FD,
        [this](guest_t*, const uint64_t a[8]) -> uint64_t {
            int32_t fd = static_cast<int32_t>(a[1]);
            int32_t ident = static_cast<int32_t>(a[2]);
            int32_t events = static_cast<int32_t>(a[3]);
            uint64_t callback = a[4];
            uint64_t data = a[5];

            if (fd < 0 || (!callback && ident < 0))
                return 0;

            auto it = std::find_if(looper_regs_.begin(), looper_regs_.end(),
                [fd](const LooperRegistration& reg) { return reg.fd == fd; });
            if (it == looper_regs_.end()) {
                looper_regs_.push_back({fd, ident, events, callback, data, false});
            } else {
                *it = {fd, ident, events, callback, data, false};
            }

            std::fprintf(stderr,
                "[ALooper] addFd fd=%d ident=%d events=0x%x callback=0x%llx data=0x%llx\n",
                fd, ident, events,
                (unsigned long long)callback,
                (unsigned long long)data);
            return 1;
        });
    add("libandroid.so", "ALooper_removeFd", HVC_ALOOPER_REMOVE_FD,
        [this](guest_t*, const uint64_t a[8]) -> uint64_t {
            int32_t fd = static_cast<int32_t>(a[1]);
            auto old_size = looper_regs_.size();
            looper_regs_.erase(std::remove_if(looper_regs_.begin(), looper_regs_.end(),
                [fd](const LooperRegistration& reg) { return reg.fd == fd; }),
                looper_regs_.end());
            return looper_regs_.size() != old_size ? 1 : 0;
        });
    add("libandroid.so", "ALooper_wake",     HVC_ALOOPER_WAKE,
        [this](guest_t*, const uint64_t[8]) -> uint64_t {
            for (auto& reg : looper_regs_)
                reg.delivered = false;
            return 0;
        });

    // AAssetManager/AAsset — host-backed reads from extracted APK assets/.
    add("libandroid.so", "AAssetManager_fromJava", HVC_ASSET_MGR_FROM_JAVA,
        [](guest_t*, const uint64_t[8]) -> uint64_t {
            return GUEST_ASSET_MANAGER;
        });

    add("libandroid.so", "AAssetManager_open", HVC_ASSET_OPEN,
        [this](guest_t* g, const uint64_t a[8]) -> uint64_t {
            if (a[0] != GUEST_ASSET_MANAGER || asset_root_.empty())
                return 0;

            std::string name = guest_read_string(g, a[1]);
            if (!safe_asset_name(name))
                return 0;

            std::filesystem::path path =
                std::filesystem::path(asset_root_) / std::filesystem::path(name);
            std::error_code ec;
            if (!std::filesystem::is_regular_file(path, ec))
                return 0;

            std::vector<uint8_t> bytes = read_asset_file(path);
            if (bytes.empty() && std::filesystem::file_size(path, ec) != 0)
                return 0;

            uint64_t handle = next_asset_handle_++;
            assets_[handle] = { name, std::move(bytes), 0 };
            std::fprintf(stderr,
                "[AssetManager] open %s size=%zu mode=%lld -> 0x%llx\n",
                name.c_str(),
                assets_[handle].bytes.size(),
                (long long)static_cast<int64_t>(a[2]),
                (unsigned long long)handle);
            return handle;
        });

    add("libandroid.so", "AAsset_read", HVC_ASSET_READ,
        [this](guest_t* g, const uint64_t a[8]) -> uint64_t {
            auto it = assets_.find(a[0]);
            if (it == assets_.end() || !a[1])
                return guest_negative_one();

            AssetState& asset = it->second;
            size_t remaining = asset.offset < asset.bytes.size()
                ? asset.bytes.size() - asset.offset
                : 0;
            size_t requested = static_cast<size_t>(
                std::min<uint64_t>(a[2], 0x7fffffffULL));
            size_t n = std::min(remaining, requested);
            if (n > 0 &&
                guest_write(g, a[1], asset.bytes.data() + asset.offset, n) != 0) {
                return guest_negative_one();
            }
            asset.offset += n;
            return n;
        });

    add("libandroid.so", "AAsset_close", HVC_ASSET_CLOSE,
        [this](guest_t*, const uint64_t a[8]) -> uint64_t {
            assets_.erase(a[0]);
            return 0;
        });

    auto asset_length = [this](guest_t*, const uint64_t a[8]) -> uint64_t {
        auto it = assets_.find(a[0]);
        return it == assets_.end() ? 0 : it->second.bytes.size();
    };
    add("libandroid.so", "AAsset_getLength", HVC_ASSET_LENGTH, asset_length);
    add("libandroid.so", "AAsset_getLength64", HVC_ASSET_LENGTH, asset_length);

    auto asset_remaining = [this](guest_t*, const uint64_t a[8]) -> uint64_t {
        auto it = assets_.find(a[0]);
        if (it == assets_.end())
            return 0;
        const AssetState& asset = it->second;
        return asset.offset < asset.bytes.size()
            ? asset.bytes.size() - asset.offset
            : 0;
    };
    add("libandroid.so", "AAsset_getRemainingLength", HVC_ASSET_REMAINING,
        asset_remaining);
    add("libandroid.so", "AAsset_getRemainingLength64", HVC_ASSET_REMAINING,
        asset_remaining);

    auto asset_seek = [this](guest_t*, const uint64_t a[8]) -> uint64_t {
        auto it = assets_.find(a[0]);
        if (it == assets_.end())
            return guest_negative_one();

        AssetState& asset = it->second;
        int64_t offset = static_cast<int64_t>(a[1]);
        int32_t whence = static_cast<int32_t>(a[2]);
        int64_t base = 0;
        if (whence == SEEK_SET) {
            base = 0;
        } else if (whence == SEEK_CUR) {
            base = static_cast<int64_t>(asset.offset);
        } else if (whence == SEEK_END) {
            base = static_cast<int64_t>(asset.bytes.size());
        } else {
            return guest_negative_one();
        }

        int64_t next = base + offset;
        if (next < 0 || static_cast<uint64_t>(next) > asset.bytes.size())
            return guest_negative_one();

        asset.offset = static_cast<size_t>(next);
        return asset.offset;
    };
    add("libandroid.so", "AAsset_seek", HVC_ASSET_SEEK, asset_seek);
    add("libandroid.so", "AAsset_seek64", HVC_ASSET_SEEK, asset_seek);

    // AInputQueue — minimal queued touch events routed through ALooper.
    add("libandroid.so", "AInputQueue_attachLooper", HVC_INPUT_QUEUE_ATTACH,
        [this](guest_t*, const uint64_t a[8]) -> uint64_t {
            if (a[0] != GUEST_INPUT_QUEUE)
                return 0;

            input_queue_.attached = true;
            input_queue_.looper = a[1];
            input_queue_.ident = static_cast<int32_t>(a[2]);
            input_queue_.callback = a[3];
            input_queue_.data = a[4];

            auto it = std::find_if(looper_regs_.begin(), looper_regs_.end(),
                [](const LooperRegistration& reg) {
                    return reg.fd == INPUT_QUEUE_FD;
                });
            LooperRegistration reg{
                INPUT_QUEUE_FD,
                input_queue_.ident,
                1,
                input_queue_.callback,
                input_queue_.data,
                false
            };
            if (it == looper_regs_.end())
                looper_regs_.push_back(reg);
            else
                *it = reg;

            std::fprintf(stderr,
                "[InputQueue] attach looper=0x%llx ident=%d callback=0x%llx data=0x%llx pending=%zu\n",
                (unsigned long long)input_queue_.looper,
                input_queue_.ident,
                (unsigned long long)input_queue_.callback,
                (unsigned long long)input_queue_.data,
                input_events_.size() - next_input_event_);
            return 0;
        });

    add("libandroid.so", "AInputQueue_detachLooper", HVC_INPUT_QUEUE_DETACH,
        [this](guest_t*, const uint64_t a[8]) -> uint64_t {
            if (a[0] == GUEST_INPUT_QUEUE) {
                input_queue_ = {};
                looper_regs_.erase(std::remove_if(looper_regs_.begin(), looper_regs_.end(),
                    [](const LooperRegistration& reg) {
                        return reg.fd == INPUT_QUEUE_FD;
                    }),
                    looper_regs_.end());
                std::fprintf(stderr, "[InputQueue] detach\n");
            }
            return 0;
        });

    add("libandroid.so", "AInputQueue_hasEvents", HVC_INPUT_QUEUE_HAS_EVENTS,
        [input_event_pending](guest_t*, const uint64_t[8]) -> uint64_t {
            return input_event_pending() ? 1 : 0;
        });

    add("libandroid.so", "AInputQueue_getEvent", HVC_INPUT_QUEUE_GET_EVENT,
        [this, input_event_pending](guest_t* g, const uint64_t a[8]) -> uint64_t {
            constexpr uint64_t INPUT_QUEUE_EMPTY =
                static_cast<uint64_t>(static_cast<int64_t>(-1));
            if (a[0] != GUEST_INPUT_QUEUE || !a[1] || !input_event_pending())
                return INPUT_QUEUE_EMPTY;

            InputEventState& event = input_events_[next_input_event_];
            event.offered = true;
            current_input_event_ = event.handle;
            guest_write_u64(g, a[1], event.handle);

            std::fprintf(stderr,
                "[InputQueue] getEvent handle=0x%llx type=%d action=%d\n",
                (unsigned long long)event.handle, event.type, event.action);
            return 0;
        });

    add("libandroid.so", "AInputQueue_preDispatchEvent", HVC_INPUT_QUEUE_PRE_DISPATCH,
        [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; });

    add("libandroid.so", "AInputQueue_finishEvent", HVC_INPUT_QUEUE_FINISH,
        [this, input_event_pending, find_input_event]
        (guest_t*, const uint64_t a[8]) -> uint64_t {
            if (a[0] != GUEST_INPUT_QUEUE)
                return 0;

            InputEventState* event = find_input_event(a[1]);
            if (event && !event->finished) {
                event->finished = true;
                if (next_input_event_ < input_events_.size() &&
                    input_events_[next_input_event_].handle == a[1]) {
                    next_input_event_++;
                }
                current_input_event_ = 0;
                std::fprintf(stderr,
                    "[InputQueue] finishEvent handle=0x%llx handled=%lld remaining=%zu\n",
                    (unsigned long long)a[1],
                    (long long)static_cast<int64_t>(a[2]),
                    input_events_.size() - next_input_event_);
            }

            if (input_queue_.attached && input_event_pending())
                rearm_looper_fd(INPUT_QUEUE_FD);
            return 0;
        });

    add("libandroid.so", "AInputEvent_getType", HVC_INPUT_EVENT_GET_TYPE,
        [find_input_event](guest_t*, const uint64_t a[8]) -> uint64_t {
            auto* event = find_input_event(a[0]);
            return event ? static_cast<uint64_t>(static_cast<int64_t>(event->type)) : 0;
        });
    add("libandroid.so", "AInputEvent_getDeviceId", HVC_INPUT_EVENT_GET_DEVICE_ID,
        [find_input_event](guest_t*, const uint64_t a[8]) -> uint64_t {
            auto* event = find_input_event(a[0]);
            return event ? static_cast<uint64_t>(static_cast<int64_t>(event->device_id)) : 0;
        });
    add("libandroid.so", "AInputEvent_getSource", HVC_INPUT_EVENT_GET_SOURCE,
        [find_input_event](guest_t*, const uint64_t a[8]) -> uint64_t {
            auto* event = find_input_event(a[0]);
            return event ? static_cast<uint64_t>(static_cast<int64_t>(event->source)) : 0;
        });
    add("libandroid.so", "AMotionEvent_getAction", HVC_MOTION_EVENT_GET_ACTION,
        [find_input_event](guest_t*, const uint64_t a[8]) -> uint64_t {
            auto* event = find_input_event(a[0]);
            return event ? static_cast<uint64_t>(static_cast<int64_t>(event->action)) : 0;
        });
    add("libandroid.so", "AKeyEvent_getAction", HVC_KEY_EVENT_GET_ACTION,
        [find_input_event](guest_t*, const uint64_t a[8]) -> uint64_t {
            auto* event = find_input_event(a[0]);
            return event ? static_cast<uint64_t>(static_cast<int64_t>(event->action)) : 0;
        });
    add("libandroid.so", "AKeyEvent_getKeyCode", HVC_KEY_EVENT_GET_KEYCODE,
        [find_input_event](guest_t*, const uint64_t a[8]) -> uint64_t {
            auto* event = find_input_event(a[0]);
            return event ? static_cast<uint64_t>(static_cast<int64_t>(event->key_code)) : 0;
        });

    // ANativeWindow — stable opaque handle plus a guest-visible software buffer.
    add("libandroid.so", "ANativeWindow_acquire", HVC_NATIVE_WINDOW_ACQUIRE,
        [this](guest_t*, const uint64_t a[8]) -> uint64_t {
            if (a[0] == GUEST_NATIVE_WINDOW) native_window_.ref_count++;
            return 0;
        });

    add("libandroid.so", "ANativeWindow_release", HVC_NATIVE_WINDOW_RELEASE,
        [this](guest_t*, const uint64_t a[8]) -> uint64_t {
            if (a[0] == GUEST_NATIVE_WINDOW && native_window_.ref_count > 0)
                native_window_.ref_count--;
            return 0;
        });

    add("libandroid.so", "ANativeWindow_getWidth", HVC_NATIVE_WINDOW_WIDTH,
        [this](guest_t*, const uint64_t a[8]) -> uint64_t {
            return a[0] == GUEST_NATIVE_WINDOW
                ? static_cast<uint64_t>(static_cast<uint32_t>(native_window_.width))
                : static_cast<uint64_t>(static_cast<uint32_t>(-22));
        });

    add("libandroid.so", "ANativeWindow_getHeight", HVC_NATIVE_WINDOW_HEIGHT,
        [this](guest_t*, const uint64_t a[8]) -> uint64_t {
            return a[0] == GUEST_NATIVE_WINDOW
                ? static_cast<uint64_t>(static_cast<uint32_t>(native_window_.height))
                : static_cast<uint64_t>(static_cast<uint32_t>(-22));
        });

    add("libandroid.so", "ANativeWindow_getFormat", HVC_NATIVE_WINDOW_FORMAT,
        [this](guest_t*, const uint64_t a[8]) -> uint64_t {
            return a[0] == GUEST_NATIVE_WINDOW
                ? static_cast<uint64_t>(static_cast<uint32_t>(native_window_.format))
                : static_cast<uint64_t>(static_cast<uint32_t>(-22));
        });

    add("libandroid.so", "ANativeWindow_setBuffersGeometry", HVC_NATIVE_WINDOW_SET_BUF,
        [this](guest_t*, const uint64_t a[8]) -> uint64_t {
            if (a[0] != GUEST_NATIVE_WINDOW) return static_cast<uint32_t>(-22);

            int32_t width = static_cast<int32_t>(a[1]);
            int32_t height = static_cast<int32_t>(a[2]);
            int32_t format = static_cast<int32_t>(a[3]);

            if (width > 0) native_window_.width = width;
            if (height > 0) native_window_.height = height;
            if (format > 0) native_window_.format = format;

            if (native_window_.width > MAX_NATIVE_WINDOW_WIDTH)
                native_window_.width = MAX_NATIVE_WINDOW_WIDTH;
            if (native_window_.height > MAX_NATIVE_WINDOW_HEIGHT)
                native_window_.height = MAX_NATIVE_WINDOW_HEIGHT;
            native_window_.stride = native_window_.width;

            std::fprintf(stderr,
                "[NativeWindow] setBuffersGeometry %dx%d fmt=%d\n",
                native_window_.width, native_window_.height, native_window_.format);
            return 0;
        });

    add("libandroid.so", "ANativeWindow_lock", HVC_NATIVE_WINDOW_LOCK,
        [this](guest_t* g, const uint64_t a[8]) -> uint64_t {
            if (a[0] != GUEST_NATIVE_WINDOW || !a[1]) return static_cast<uint32_t>(-22);

            // ANativeWindow_Buffer: width, height, stride, format, bits, reserved[6].
            guest_write_u32(g, a[1] + 0x00, static_cast<uint32_t>(native_window_.width));
            guest_write_u32(g, a[1] + 0x04, static_cast<uint32_t>(native_window_.height));
            guest_write_u32(g, a[1] + 0x08, static_cast<uint32_t>(native_window_.stride));
            guest_write_u32(g, a[1] + 0x0C, static_cast<uint32_t>(native_window_.format));
            guest_write_u64(g, a[1] + 0x10, native_window_.bits_gpa);

            if (a[2]) {
                guest_write_u32(g, a[2] + 0x00, 0);
                guest_write_u32(g, a[2] + 0x04, 0);
                guest_write_u32(g, a[2] + 0x08, static_cast<uint32_t>(native_window_.width));
                guest_write_u32(g, a[2] + 0x0C, static_cast<uint32_t>(native_window_.height));
            }

            native_window_.locked = true;
            std::fprintf(stderr,
                "[NativeWindow] lock buffer=%dx%d bits=0x%llx\n",
                native_window_.width, native_window_.height,
                (unsigned long long)native_window_.bits_gpa);
            return 0;
        });

    add("libandroid.so", "ANativeWindow_unlockAndPost", HVC_NATIVE_WINDOW_UNLOCK,
        [this](guest_t*, const uint64_t a[8]) -> uint64_t {
            if (a[0] != GUEST_NATIVE_WINDOW) return static_cast<uint32_t>(-22);
            native_window_.locked = false;
            std::fprintf(stderr, "[NativeWindow] unlockAndPost\n");
            present_native_window_buffer();
            return 0;
        });

    add("libandroid.so", "AChoreographer_getInstance", HVC_CHOREOGRAPHER_GET,
        [this](guest_t*, const uint64_t[8]) -> uint64_t { return arena_gpa_ + 0x200; });

    auto post_frame_callback =
        [this](const char* symbol, const uint64_t a[8], uint64_t delay_ms) -> uint64_t {
            (void)a[0];
            uint64_t callback = a[1];
            uint64_t data = a[2];
            if (!callback)
                return 0;

            uint64_t frame_time = next_frame_time_nanos_ + delay_ms * 1'000'000ULL;
            next_frame_time_nanos_ = frame_time + 16'666'666ULL;
            pending_frame_callbacks_.push_back({callback, frame_time, data});
            std::fprintf(stderr,
                "[Choreographer] %s callback=0x%llx data=0x%llx frame=%llu delay_ms=%llu\n",
                symbol,
                (unsigned long long)callback,
                (unsigned long long)data,
                (unsigned long long)frame_time,
                (unsigned long long)delay_ms);
            return 0;
        };

    add("libandroid.so", "AChoreographer_postFrameCallback", HVC_CHOREOGRAPHER_CB,
        [post_frame_callback](guest_t*, const uint64_t a[8]) -> uint64_t {
            return post_frame_callback("postFrameCallback", a, 0);
        });
    add("libandroid.so", "AChoreographer_postFrameCallbackDelayed",
        HVC_CHOREOGRAPHER_CB_DELAYED,
        [post_frame_callback](guest_t*, const uint64_t a[8]) -> uint64_t {
            return post_frame_callback("postFrameCallbackDelayed", a, a[3]);
        });
    add("libandroid.so", "AChoreographer_postFrameCallback64",
        HVC_CHOREOGRAPHER_CB64,
        [post_frame_callback](guest_t*, const uint64_t a[8]) -> uint64_t {
            return post_frame_callback("postFrameCallback64", a, 0);
        });
    add("libandroid.so", "AChoreographer_postFrameCallbackDelayed64",
        HVC_CHOREOGRAPHER_CB64_DELAYED,
        [post_frame_callback](guest_t*, const uint64_t a[8]) -> uint64_t {
            return post_frame_callback("postFrameCallbackDelayed64", a, a[3]);
        });

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

// ── libbinder_ndk stubs ──────────────────────────────────────────────────────

void AndroidRuntime::register_libbinder_stubs()
{
    constexpr uint64_t STATUS_OK = 0;
    constexpr uint64_t STATUS_BAD_VALUE =
        static_cast<uint64_t>(static_cast<int64_t>(-22));
    constexpr uint64_t STATUS_INVALID_OPERATION =
        static_cast<uint64_t>(static_cast<int64_t>(-38));
    constexpr uint64_t STATUS_NO_MEMORY =
        static_cast<uint64_t>(static_cast<int64_t>(-12));
    constexpr uint64_t STATUS_NAME_NOT_FOUND =
        static_cast<uint64_t>(static_cast<int64_t>(-2));
    constexpr uint64_t STATUS_ALREADY_EXISTS =
        static_cast<uint64_t>(static_cast<int64_t>(-17));
    constexpr uint64_t STATUS_DEAD_OBJECT =
        static_cast<uint64_t>(static_cast<int64_t>(-32));
    constexpr uint64_t STATUS_NOT_ENOUGH_DATA =
        static_cast<uint64_t>(static_cast<int64_t>(-61));

    auto find_service = [this](uint64_t handle) -> BinderService* {
        auto it = binder_services_.find(handle);
        return it == binder_services_.end() ? nullptr : &it->second;
    };
    auto make_binder_service =
        [](std::string name,
           uint32_t ref_count,
           bool alive,
           bool remote,
           uint64_t class_handle,
           uint64_t user_data) -> BinderService {
            BinderService service;
            service.name = std::move(name);
            service.ref_count = ref_count;
            service.alive = alive;
            service.remote = remote;
            service.class_handle = class_handle;
            service.user_data = user_data;
            return service;
        };
    auto find_death_recipient =
        [this](uint64_t handle) -> BinderDeathRecipient* {
            auto it = binder_death_recipients_.find(handle);
            return it == binder_death_recipients_.end() ? nullptr : &it->second;
        };
    auto notify_death_unlinked =
        [this](const BinderService::DeathLink& link) {
            if (link.on_unlinked && guest_function_invoker_)
                guest_function_invoker_(link.on_unlinked, { link.cookie });
        };

    auto service_handle_for_name =
        [this, make_binder_service](const std::string& name) -> uint64_t {
            if (name.empty())
                return 0;

            auto existing = binder_service_by_name_.find(name);
            if (existing != binder_service_by_name_.end())
                return existing->second;

            uint64_t handle = next_binder_handle_++;
            binder_services_[handle] =
                make_binder_service(name, 1, true, true, 0, 0);
            binder_service_by_name_[name] = handle;
            return handle;
        };

    auto service_lookup =
        [service_handle_for_name](const char* op,
                                  guest_t* g,
                                  const uint64_t a[8]) -> uint64_t {
            std::string name = guest_read_string(g, a[0]);
            uint64_t handle = service_handle_for_name(name);
            std::fprintf(stderr, "[Binder] %s(%s) -> 0x%llx\n",
                         op,
                         name.empty() ? "<empty>" : name.c_str(),
                         (unsigned long long)handle);
            return handle;
        };

    add("libbinder_ndk.so", "AServiceManager_checkService", HVC_SERVICE_CHECK,
        [service_lookup](guest_t* g, const uint64_t a[8]) -> uint64_t {
            return service_lookup("checkService", g, a);
        });
    add("libbinder_ndk.so", "AServiceManager_getService", HVC_SERVICE_GET,
        [service_lookup](guest_t* g, const uint64_t a[8]) -> uint64_t {
            return service_lookup("getService", g, a);
        });
    add("libbinder_ndk.so", "AServiceManager_waitForService", HVC_SERVICE_WAIT,
        [service_lookup](guest_t* g, const uint64_t a[8]) -> uint64_t {
            return service_lookup("waitForService", g, a);
        });
    add("libbinder_ndk.so", "AServiceManager_isDeclared", HVC_SERVICE_IS_DECLARED,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            std::string name = guest_read_string(g, a[0]);
            uint64_t declared = name.empty() ? 0 : 1;
            std::fprintf(stderr, "[Binder] isDeclared(%s) -> %llu\n",
                         name.empty() ? "<empty>" : name.c_str(),
                         (unsigned long long)declared);
            return declared;
        });
    add("libbinder_ndk.so", "AServiceManager_addService", HVC_SERVICE_ADD,
        [this, service_handle_for_name, make_binder_service]
        (guest_t* g, const uint64_t a[8]) -> uint64_t {
            std::string name = guest_read_string(g, a[1]);
            uint64_t handle = a[0] ? a[0] : service_handle_for_name(name);
            if (name.empty() || !handle)
                return STATUS_BAD_VALUE;

            if (binder_services_.find(handle) == binder_services_.end())
                binder_services_[handle] =
                    make_binder_service(name, 1, true, true, 0, 0);
            binder_service_by_name_[name] = handle;
            std::fprintf(stderr, "[Binder] addService(%s, 0x%llx) -> OK\n",
                         name.c_str(), (unsigned long long)handle);
            return STATUS_OK;
        });

    add("libbinder_ndk.so", "AIBinder_isRemote", HVC_BINDER_IS_REMOTE,
        [find_service](guest_t*, const uint64_t a[8]) -> uint64_t {
            BinderService* service = find_service(a[0]);
            return service && service->remote ? 1 : 0;
        });
    add("libbinder_ndk.so", "AIBinder_isAlive", HVC_BINDER_IS_ALIVE,
        [find_service](guest_t*, const uint64_t a[8]) -> uint64_t {
            BinderService* service = find_service(a[0]);
            return service && service->alive ? 1 : 0;
        });
    add("libbinder_ndk.so", "AIBinder_ping", HVC_BINDER_PING,
        [find_service](guest_t*, const uint64_t a[8]) -> uint64_t {
            BinderService* service = find_service(a[0]);
            if (!service)
                return STATUS_BAD_VALUE;
            return service->alive ? STATUS_OK : STATUS_DEAD_OBJECT;
        });
    add("libbinder_ndk.so", "AIBinder_incStrong", HVC_BINDER_INC_STRONG,
        [find_service](guest_t*, const uint64_t a[8]) -> uint64_t {
            if (BinderService* service = find_service(a[0]))
                service->ref_count++;
            return 0;
        });
    add("libbinder_ndk.so", "AIBinder_decStrong", HVC_BINDER_DEC_STRONG,
        [this, find_service, notify_death_unlinked]
        (guest_t*, const uint64_t a[8]) -> uint64_t {
            BinderService* service = find_service(a[0]);
            if (!service)
                return 0;

            if (service->remote) {
                if (service->ref_count > 1)
                    service->ref_count--;
                return 0;
            }

            if (service->ref_count > 1) {
                service->ref_count--;
                return 0;
            }

            uint64_t on_destroy = 0;
            uint64_t user_data = service->user_data;
            auto class_it = binder_classes_.find(service->class_handle);
            if (class_it != binder_classes_.end())
                on_destroy = class_it->second.on_destroy;
            if (on_destroy && guest_function_invoker_)
                guest_function_invoker_(on_destroy, { user_data });
            for (const BinderService::DeathLink& link : service->death_links)
                notify_death_unlinked(link);
            binder_services_.erase(a[0]);
            return 0;
        });
    add("libbinder_ndk.so", "AIBinder_debugGetRefCount", HVC_BINDER_REF_COUNT,
        [find_service](guest_t*, const uint64_t a[8]) -> uint64_t {
            BinderService* service = find_service(a[0]);
            if (!a[0])
                return static_cast<uint64_t>(static_cast<int64_t>(-1));
            return service ? service->ref_count : 0;
        });
    add("libbinder_ndk.so", "AIBinder_getCallingUid", HVC_BINDER_CALLING_UID,
        [](guest_t*, const uint64_t[8]) -> uint64_t { return 10000; });
    add("libbinder_ndk.so", "AIBinder_getCallingPid", HVC_BINDER_CALLING_PID,
        [](guest_t*, const uint64_t[8]) -> uint64_t { return 1; });
    add("libbinder_ndk.so", "AIBinder_isHandlingTransaction",
        HVC_BINDER_HANDLING_TX,
        [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; });
    add("libbinder_ndk.so", "AIBinder_dump", HVC_BINDER_DUMP,
        [find_service](guest_t*, const uint64_t a[8]) -> uint64_t {
            return find_service(a[0]) ? STATUS_OK : STATUS_BAD_VALUE;
        });

    constexpr int32_t EX_NONE = 0;
    constexpr int32_t EX_TRANSACTION_FAILED = -129;
    constexpr uint32_t FLAG_ONEWAY = 0x01;
    constexpr int32_t MAX_PARCEL_ARRAY_LENGTH = 1024;
    constexpr int32_t MAX_PARCEL_STRING_LENGTH = 4096;

    auto find_parcel = [this](uint64_t handle) -> BinderParcel* {
        auto it = binder_parcels_.find(handle);
        return it == binder_parcels_.end() ? nullptr : &it->second;
    };
    auto create_parcel =
        [this](uint64_t target_binder, uint32_t code, bool reply) -> uint64_t {
            uint64_t handle = next_binder_parcel_handle_++;
            binder_parcels_[handle] = { target_binder, code, reply, 0, {} };
            return handle;
        };
    auto create_status =
        [this](int32_t exception,
               int32_t service_error,
               int32_t status,
               std::string message) -> uint64_t {
            uint64_t handle = next_binder_status_handle_++;
            binder_statuses_[handle] = {
                exception, service_error, status, std::move(message)
            };
            return handle;
        };
    auto find_status = [this](uint64_t handle) -> BinderStatus* {
        auto it = binder_statuses_.find(handle);
        return it == binder_statuses_.end() ? nullptr : &it->second;
    };
    auto find_class = [this](uint64_t handle) -> BinderClass* {
        auto it = binder_classes_.find(handle);
        return it == binder_classes_.end() ? nullptr : &it->second;
    };
    auto remove_death_link =
        [notify_death_unlinked](BinderService* service,
                                uint64_t recipient_handle,
                                uint64_t cookie) -> bool {
            if (!service)
                return false;
            for (auto it = service->death_links.begin();
                 it != service->death_links.end();
                 ++it) {
                if (it->recipient_handle == recipient_handle &&
                    it->cookie == cookie) {
                    BinderService::DeathLink link = *it;
                    service->death_links.erase(it);
                    notify_death_unlinked(link);
                    return true;
                }
            }
            return false;
        };
    auto remove_recipient_links =
        [this, notify_death_unlinked](uint64_t recipient_handle) {
            for (auto& entry : binder_services_) {
                auto& links = entry.second.death_links;
                for (auto it = links.begin(); it != links.end();) {
                    if (it->recipient_handle == recipient_handle) {
                        BinderService::DeathLink link = *it;
                        it = links.erase(it);
                        notify_death_unlinked(link);
                    } else {
                        ++it;
                    }
                }
            }
        };
    auto alloc_guest = [this](size_t size, size_t align = 16) -> uint64_t {
        uint64_t mask = static_cast<uint64_t>(align ? align - 1 : 0);
        uint64_t ptr = (heap_bump_ + mask) & ~mask;
        size_t aligned_size = (size + static_cast<size_t>(mask)) &
                              ~static_cast<size_t>(mask);
        if (ptr + aligned_size > heap_base_ + HEAP_SIZE)
            return 0;
        heap_bump_ = ptr + aligned_size;
        return ptr;
    };
    auto alloc_guest_string = [alloc_guest](guest_t* g, const std::string& text) -> uint64_t {
        size_t size = text.size() + 1;
        uint64_t ptr = alloc_guest(size);
        if (!ptr)
            return 0;
        guest_write(g, ptr, text.c_str(), size);
        return ptr;
    };
    auto append_value =
        [find_parcel](uint64_t parcel_handle,
                      BinderParcelKind kind,
                      uint64_t value,
                      std::string text = {}) -> uint64_t {
            BinderParcel* parcel = find_parcel(parcel_handle);
            if (!parcel)
                return STATUS_BAD_VALUE;
            BinderParcelValue parcel_value;
            parcel_value.kind = kind;
            parcel_value.value = value;
            parcel_value.text = std::move(text);
            parcel->values.push_back(std::move(parcel_value));
            parcel->cursor = parcel->values.size();
            return STATUS_OK;
        };
    auto append_parcel_value =
        [find_parcel](uint64_t parcel_handle,
                      BinderParcelValue value) -> uint64_t {
            BinderParcel* parcel = find_parcel(parcel_handle);
            if (!parcel)
                return STATUS_BAD_VALUE;
            parcel->values.push_back(std::move(value));
            parcel->cursor = parcel->values.size();
            return STATUS_OK;
        };
    auto read_value =
        [find_parcel](uint64_t parcel_handle,
                      BinderParcelKind expected,
                      BinderParcelValue* out) -> uint64_t {
            BinderParcel* parcel = find_parcel(parcel_handle);
            if (!parcel)
                return STATUS_BAD_VALUE;
            if (parcel->cursor >= parcel->values.size())
                return STATUS_NOT_ENOUGH_DATA;
            BinderParcelValue value = parcel->values[parcel->cursor];
            if (value.kind != expected)
                return STATUS_BAD_VALUE;
            parcel->cursor++;
            if (out)
                *out = std::move(value);
            return STATUS_OK;
        };

    add("libbinder_ndk.so", "AIBinder_DeathRecipient_new",
        HVC_BINDER_DEATH_NEW,
        [this](guest_t*, const uint64_t a[8]) -> uint64_t {
            if (!a[0])
                return 0;
            uint64_t handle = next_binder_death_handle_++;
            binder_death_recipients_[handle] = { a[0], 0 };
            std::fprintf(stderr,
                "[Binder] DeathRecipient_new -> 0x%llx onDied=0x%llx\n",
                (unsigned long long)handle,
                (unsigned long long)a[0]);
            return handle;
        });
    add("libbinder_ndk.so", "AIBinder_DeathRecipient_setOnUnlinked",
        HVC_BINDER_DEATH_SET_UNLINK,
        [find_death_recipient](guest_t*, const uint64_t a[8]) -> uint64_t {
            if (BinderDeathRecipient* recipient = find_death_recipient(a[0]))
                recipient->on_unlinked = a[1];
            return 0;
        });
    add("libbinder_ndk.so", "AIBinder_DeathRecipient_delete",
        HVC_BINDER_DEATH_DELETE,
        [this, find_death_recipient, remove_recipient_links]
        (guest_t*, const uint64_t a[8]) -> uint64_t {
            if (!find_death_recipient(a[0]))
                return 0;
            remove_recipient_links(a[0]);
            binder_death_recipients_.erase(a[0]);
            std::fprintf(stderr,
                "[Binder] DeathRecipient_delete 0x%llx\n",
                (unsigned long long)a[0]);
            return 0;
        });
    add("libbinder_ndk.so", "AIBinder_linkToDeath", HVC_BINDER_LINK_DEATH,
        [find_service, find_death_recipient, notify_death_unlinked]
        (guest_t*, const uint64_t a[8]) -> uint64_t {
            BinderService* service = find_service(a[0]);
            BinderDeathRecipient* recipient = find_death_recipient(a[1]);
            if (!service || !recipient)
                return STATUS_BAD_VALUE;

            BinderService::DeathLink link = {
                a[1],
                a[2],
                recipient->on_unlinked,
            };
            if (!service->alive) {
                notify_death_unlinked(link);
                return STATUS_DEAD_OBJECT;
            }
            if (!service->remote) {
                notify_death_unlinked(link);
                return STATUS_INVALID_OPERATION;
            }
            for (const BinderService::DeathLink& existing :
                 service->death_links) {
                if (existing.recipient_handle == a[1] &&
                    existing.cookie == a[2]) {
                    notify_death_unlinked(link);
                    return STATUS_ALREADY_EXISTS;
                }
            }

            service->death_links.push_back(link);
            std::fprintf(stderr,
                "[Binder] linkToDeath binder=0x%llx recipient=0x%llx cookie=0x%llx\n",
                (unsigned long long)a[0],
                (unsigned long long)a[1],
                (unsigned long long)a[2]);
            return STATUS_OK;
        });
    add("libbinder_ndk.so", "AIBinder_unlinkToDeath", HVC_BINDER_UNLINK_DEATH,
        [find_service, find_death_recipient, remove_death_link]
        (guest_t*, const uint64_t a[8]) -> uint64_t {
            BinderService* service = find_service(a[0]);
            if (!service || !find_death_recipient(a[1]))
                return STATUS_BAD_VALUE;
            if (remove_death_link(service, a[1], a[2])) {
                std::fprintf(stderr,
                    "[Binder] unlinkToDeath binder=0x%llx recipient=0x%llx cookie=0x%llx\n",
                    (unsigned long long)a[0],
                    (unsigned long long)a[1],
                    (unsigned long long)a[2]);
                return STATUS_OK;
            }
            return STATUS_NAME_NOT_FOUND;
        });

    add("libbinder_ndk.so", "AIBinder_Class_define", HVC_BINDER_CLASS_DEFINE,
        [this](guest_t* g, const uint64_t a[8]) -> uint64_t {
            std::string descriptor = guest_read_string(g, a[0]);
            if (descriptor.empty())
                return 0;

            uint64_t handle = next_binder_class_handle_++;
            binder_classes_[handle] = {
                descriptor,
                0,
                a[1],
                a[2],
                a[3]
            };
            std::fprintf(stderr,
                "[Binder] Class_define(%s) -> 0x%llx onTransact=0x%llx\n",
                descriptor.c_str(),
                (unsigned long long)handle,
                (unsigned long long)a[3]);
            return handle;
        });
    add("libbinder_ndk.so", "AIBinder_Class_setOnDump",
        HVC_BINDER_CLASS_SET_DUMP,
        [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; });
    add("libbinder_ndk.so", "AIBinder_Class_setTransactionCodeToFunctionNameMap",
        HVC_BINDER_CLASS_SET_NAMES,
        [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; });
    add("libbinder_ndk.so", "AIBinder_Class_getDescriptor",
        HVC_BINDER_CLASS_GET_NAME,
        [find_class, alloc_guest_string](guest_t* g, const uint64_t a[8]) -> uint64_t {
            BinderClass* clazz = find_class(a[0]);
            if (!clazz)
                return 0;
            if (!clazz->descriptor_gpa)
                clazz->descriptor_gpa = alloc_guest_string(g, clazz->descriptor);
            return clazz->descriptor_gpa;
        });
    add("libbinder_ndk.so", "AIBinder_Class_getFunctionName",
        HVC_BINDER_CLASS_GET_FN_NAME,
        [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; });

    add("libbinder_ndk.so", "AIBinder_new", HVC_BINDER_NEW,
        [this, find_class, make_binder_service]
        (guest_t*, const uint64_t a[8]) -> uint64_t {
            BinderClass* clazz = find_class(a[0]);
            if (!clazz)
                return 0;

            uint64_t user_data = a[1];
            if (clazz->on_create && guest_function_invoker_)
                user_data = static_cast<uint64_t>(
                    guest_function_invoker_(clazz->on_create, { a[1] }));

            uint64_t handle = next_binder_handle_++;
            binder_services_[handle] =
                make_binder_service(clazz->descriptor, 1, true, false,
                                    a[0], user_data);
            std::fprintf(stderr,
                "[Binder] AIBinder_new class=0x%llx -> binder=0x%llx user=0x%llx\n",
                (unsigned long long)a[0],
                (unsigned long long)handle,
                (unsigned long long)user_data);
            return handle;
        });
    add("libbinder_ndk.so", "AIBinder_associateClass", HVC_BINDER_ASSOC_CLASS,
        [find_service, find_class](guest_t*, const uint64_t a[8]) -> uint64_t {
            BinderService* service = find_service(a[0]);
            if (!service || !find_class(a[1]))
                return 0;
            service->class_handle = a[1];
            return 1;
        });
    add("libbinder_ndk.so", "AIBinder_getClass", HVC_BINDER_GET_CLASS,
        [find_service](guest_t*, const uint64_t a[8]) -> uint64_t {
            BinderService* service = find_service(a[0]);
            return service ? service->class_handle : 0;
        });
    add("libbinder_ndk.so", "AIBinder_getUserData", HVC_BINDER_GET_USER_DATA,
        [find_service](guest_t*, const uint64_t a[8]) -> uint64_t {
            BinderService* service = find_service(a[0]);
            return service ? service->user_data : 0;
        });

    add("libbinder_ndk.so", "AIBinder_prepareTransaction", HVC_BINDER_PREPARE_TX,
        [find_service, create_parcel](guest_t* g, const uint64_t a[8]) -> uint64_t {
            if (!find_service(a[0]) || !a[1])
                return STATUS_BAD_VALUE;
            uint64_t parcel = create_parcel(a[0], 0, false);
            guest_write_u64(g, a[1], parcel);
            std::fprintf(stderr,
                "[Binder] prepareTransaction binder=0x%llx parcel=0x%llx\n",
                (unsigned long long)a[0],
                (unsigned long long)parcel);
            return STATUS_OK;
        });

    add("libbinder_ndk.so", "AIBinder_transact", HVC_BINDER_TRANSACT,
        [this, find_service, find_parcel, create_parcel]
        (guest_t* g, const uint64_t a[8]) -> uint64_t {
            uint64_t binder = a[0];
            uint32_t code = static_cast<uint32_t>(a[1]);
            uint64_t in_handle = a[2] ? guest_read_u64(g, a[2]) : 0;
            uint64_t out_gpa = a[3];
            uint32_t flags = static_cast<uint32_t>(a[4]);

            BinderService* service = find_service(binder);
            if (!service)
                return STATUS_BAD_VALUE;
            BinderParcel* in = in_handle ? find_parcel(in_handle) : nullptr;
            if (in_handle && !in)
                return STATUS_BAD_VALUE;

            uint64_t out_handle = 0;
            uint64_t status = STATUS_OK;

            if (!service->remote &&
                service->class_handle &&
                guest_function_invoker_) {
                auto class_it = binder_classes_.find(service->class_handle);
                if (class_it == binder_classes_.end() ||
                    !class_it->second.on_transact) {
                    return STATUS_INVALID_OPERATION;
                }

                if (!(flags & FLAG_ONEWAY) && out_gpa) {
                    out_handle = create_parcel(binder, code, true);
                    guest_write_u64(g, out_gpa, out_handle);
                } else if (out_gpa) {
                    guest_write_u64(g, out_gpa, 0);
                }

                if (in)
                    in->cursor = 0;

                status = static_cast<uint64_t>(guest_function_invoker_(
                    class_it->second.on_transact,
                    { binder, code, in_handle, out_handle }));

                if (out_handle)
                    binder_parcels_[out_handle].cursor = 0;

                if (in_handle) {
                    binder_parcels_.erase(in_handle);
                    if (a[2])
                        guest_write_u64(g, a[2], 0);
                }

                std::fprintf(stderr,
                    "[Binder] local transact binder=0x%llx code=%u in=0x%llx out=0x%llx status=%lld\n",
                    (unsigned long long)binder,
                    code,
                    (unsigned long long)in_handle,
                    (unsigned long long)out_handle,
                    (long long)static_cast<int64_t>(status));
                return status;
            }

            if (!(flags & FLAG_ONEWAY) && out_gpa) {
                out_handle = create_parcel(binder, code, true);
                BinderParcel& out = binder_parcels_[out_handle];
                BinderParcelValue status_value;
                status_value.kind = BinderParcelKind::Status;
                out.values.push_back(std::move(status_value));

                bool have_i32 = false;
                int32_t first_i32 = 0;
                if (in) {
                    for (const BinderParcelValue& value : in->values) {
                        if (value.kind == BinderParcelKind::Int32 ||
                            value.kind == BinderParcelKind::Uint32) {
                            first_i32 = static_cast<int32_t>(value.value);
                            have_i32 = true;
                            break;
                        }
                    }
                }

                int32_t reply_value = have_i32
                    ? first_i32 + static_cast<int32_t>(code)
                    : static_cast<int32_t>(code);
                BinderParcelValue reply;
                reply.kind = BinderParcelKind::Int32;
                reply.value =
                    static_cast<uint64_t>(static_cast<int64_t>(reply_value));
                out.values.push_back(std::move(reply));
                out.cursor = 0;
                guest_write_u64(g, out_gpa, out_handle);
            } else if (out_gpa) {
                guest_write_u64(g, out_gpa, 0);
            }

            if (in_handle) {
                binder_parcels_.erase(in_handle);
                if (a[2])
                    guest_write_u64(g, a[2], 0);
            }

            std::fprintf(stderr,
                "[Binder] transact binder=0x%llx code=%u in=0x%llx out=0x%llx flags=0x%x\n",
                (unsigned long long)binder,
                code,
                (unsigned long long)in_handle,
                (unsigned long long)out_handle,
                flags);
            return STATUS_OK;
        });

    add("libbinder_ndk.so", "AParcel_delete", HVC_PARCEL_DELETE,
        [this](guest_t*, const uint64_t a[8]) -> uint64_t {
            binder_parcels_.erase(a[0]);
            return 0;
        });
    add("libbinder_ndk.so", "AParcel_setDataPosition", HVC_PARCEL_SET_POS,
        [find_parcel](guest_t*, const uint64_t a[8]) -> uint64_t {
            BinderParcel* parcel = find_parcel(a[0]);
            int32_t position = static_cast<int32_t>(a[1]);
            if (!parcel || position < 0 ||
                static_cast<size_t>(position) > parcel->values.size()) {
                return STATUS_BAD_VALUE;
            }
            parcel->cursor = static_cast<size_t>(position);
            return STATUS_OK;
        });
    add("libbinder_ndk.so", "AParcel_getDataPosition", HVC_PARCEL_GET_POS,
        [find_parcel](guest_t*, const uint64_t a[8]) -> uint64_t {
            BinderParcel* parcel = find_parcel(a[0]);
            return parcel ? parcel->cursor : 0;
        });

    add("libbinder_ndk.so", "AParcel_writeStrongBinder", HVC_PARCEL_WRITE_BINDER,
        [append_value](guest_t*, const uint64_t a[8]) -> uint64_t {
            return append_value(a[0], BinderParcelKind::StrongBinder, a[1]);
        });
    add("libbinder_ndk.so", "AParcel_readStrongBinder", HVC_PARCEL_READ_BINDER,
        [read_value, find_service](guest_t* g, const uint64_t a[8]) -> uint64_t {
            BinderParcelValue value;
            uint64_t rc = read_value(a[0], BinderParcelKind::StrongBinder, &value);
            if (rc != STATUS_OK)
                return rc;
            if (BinderService* service = find_service(value.value))
                service->ref_count++;
            if (a[1])
                guest_write_u64(g, a[1], value.value);
            return STATUS_OK;
        });
    add("libbinder_ndk.so", "AParcel_writeParcelFileDescriptor",
        HVC_PARCEL_WRITE_FD,
        [append_value](guest_t*, const uint64_t a[8]) -> uint64_t {
            int32_t fd = static_cast<int32_t>(a[1]);
            return append_value(
                a[0],
                BinderParcelKind::ParcelFileDescriptor,
                static_cast<uint64_t>(static_cast<int64_t>(fd)));
        });
    add("libbinder_ndk.so", "AParcel_readParcelFileDescriptor",
        HVC_PARCEL_READ_FD,
        [read_value](guest_t* g, const uint64_t a[8]) -> uint64_t {
            if (!a[1])
                return STATUS_BAD_VALUE;

            BinderParcelValue value;
            uint64_t rc = read_value(
                a[0], BinderParcelKind::ParcelFileDescriptor, &value);
            if (rc == STATUS_OK) {
                int32_t fd =
                    static_cast<int32_t>(static_cast<int64_t>(value.value));
                guest_write_u32(g, a[1], static_cast<uint32_t>(fd));
            }
            return rc;
        });
    add("libbinder_ndk.so", "AParcel_writeStatusHeader", HVC_PARCEL_WRITE_STATUS,
        [append_value, find_status](guest_t*, const uint64_t a[8]) -> uint64_t {
            BinderStatus* status = find_status(a[1]);
            int32_t low_status = status ? status->status : 0;
            return append_value(
                a[0],
                BinderParcelKind::Status,
                static_cast<uint64_t>(static_cast<int64_t>(low_status)));
        });
    add("libbinder_ndk.so", "AParcel_readStatusHeader", HVC_PARCEL_READ_STATUS,
        [find_parcel, create_status](guest_t* g, const uint64_t a[8]) -> uint64_t {
            BinderParcel* parcel = find_parcel(a[0]);
            if (!parcel || !a[1])
                return STATUS_BAD_VALUE;
            int32_t low_status = 0;
            if (parcel->cursor < parcel->values.size() &&
                parcel->values[parcel->cursor].kind == BinderParcelKind::Status) {
                low_status = static_cast<int32_t>(parcel->values[parcel->cursor].value);
                parcel->cursor++;
            }
            int32_t exception = low_status == 0 ? EX_NONE : EX_TRANSACTION_FAILED;
            uint64_t status_handle = create_status(exception, 0, low_status, {});
            guest_write_u64(g, a[1], status_handle);
            return STATUS_OK;
        });
    add("libbinder_ndk.so", "AParcel_writeString", HVC_PARCEL_WRITE_STRING,
        [append_value](guest_t* g, const uint64_t a[8]) -> uint64_t {
            int32_t length = static_cast<int32_t>(a[2]);
            if (length < 0)
                return append_value(a[0], BinderParcelKind::String, 1, {});
            if (!a[1])
                return STATUS_BAD_VALUE;
            size_t size = static_cast<size_t>(std::min<int32_t>(length, 4096));
            std::string text(size, '\0');
            if (size > 0 && guest_read(g, a[1], text.data(), size) != 0)
                return STATUS_BAD_VALUE;
            return append_value(a[0], BinderParcelKind::String, 0, std::move(text));
        });
    add("libbinder_ndk.so", "AParcel_readString", HVC_PARCEL_READ_STRING,
        [read_value, alloc_guest, this](guest_t* g, const uint64_t a[8]) -> uint64_t {
            if (!a[2] || !guest_function_invoker_)
                return STATUS_BAD_VALUE;

            BinderParcelValue value;
            uint64_t rc = read_value(a[0], BinderParcelKind::String, &value);
            if (rc != STATUS_OK)
                return rc;

            bool is_null = value.value != 0;
            int32_t length = is_null
                ? -1
                : static_cast<int32_t>(std::min<size_t>(
                      value.text.size() + 1,
                      static_cast<size_t>(INT32_MAX)));
            uint64_t out_buffer_gpa = alloc_guest(sizeof(uint64_t), alignof(uint64_t));
            if (!out_buffer_gpa)
                return STATUS_NO_MEMORY;
            guest_write_u64(g, out_buffer_gpa, 0);

            int64_t ok = guest_function_invoker_(
                a[2],
                {
                    a[1],
                    static_cast<uint64_t>(static_cast<int64_t>(length)),
                    out_buffer_gpa,
                });
            if (!ok)
                return STATUS_NO_MEMORY;

            uint64_t buffer_gpa = guest_read_u64(g, out_buffer_gpa);
            if (!is_null) {
                if (!buffer_gpa)
                    return STATUS_NO_MEMORY;
                if (guest_write(g, buffer_gpa, value.text.c_str(), value.text.size()) != 0)
                    return STATUS_BAD_VALUE;
                char nul = '\0';
                if (guest_write(g, buffer_gpa + value.text.size(), &nul, sizeof(nul)) != 0)
                    return STATUS_BAD_VALUE;
            }

            std::fprintf(stderr,
                "[Binder] readString parcel=0x%llx len=%d buffer=0x%llx\n",
                (unsigned long long)a[0],
                length,
                (unsigned long long)buffer_gpa);
            return STATUS_OK;
        });
    add("libbinder_ndk.so", "AParcel_writeStringArray",
        HVC_PARCEL_WRITE_STRING_ARRAY,
        [append_parcel_value, alloc_guest, this](guest_t* g, const uint64_t a[8]) -> uint64_t {
            int32_t length = static_cast<int32_t>(a[2]);
            if (length < -1 || length > MAX_PARCEL_ARRAY_LENGTH)
                return STATUS_BAD_VALUE;

            BinderParcelValue value;
            value.kind = BinderParcelKind::StringArray;
            if (length < 0) {
                value.value = 1;
                return append_parcel_value(a[0], std::move(value));
            }
            if (length == 0)
                return append_parcel_value(a[0], std::move(value));
            if (!a[3] || !guest_function_invoker_)
                return STATUS_BAD_VALUE;

            uint64_t out_length_gpa = alloc_guest(sizeof(uint32_t), alignof(uint32_t));
            if (!out_length_gpa)
                return STATUS_NO_MEMORY;

            value.elements.reserve(static_cast<size_t>(length));
            value.strings.reserve(static_cast<size_t>(length));
            for (int32_t i = 0; i < length; ++i) {
                guest_write_u32(g, out_length_gpa, 0);
                int64_t buffer_gpa = guest_function_invoker_(
                    a[3],
                    {
                        a[1],
                        static_cast<uint64_t>(static_cast<size_t>(i)),
                        out_length_gpa,
                    });
                int32_t element_length =
                    static_cast<int32_t>(guest_read_u32(g, out_length_gpa));
                if (element_length == -1) {
                    value.elements.push_back(1);
                    value.strings.emplace_back();
                    continue;
                }
                if (element_length < 0 || element_length > MAX_PARCEL_STRING_LENGTH)
                    return STATUS_BAD_VALUE;
                if (element_length > 0 && buffer_gpa == 0)
                    return STATUS_BAD_VALUE;

                std::string text(static_cast<size_t>(element_length), '\0');
                if (element_length > 0 &&
                    guest_read(g,
                               static_cast<uint64_t>(buffer_gpa),
                               text.data(),
                               text.size()) != 0) {
                    return STATUS_BAD_VALUE;
                }
                value.elements.push_back(0);
                value.strings.push_back(std::move(text));
            }

            return append_parcel_value(a[0], std::move(value));
        });
    add("libbinder_ndk.so", "AParcel_readStringArray",
        HVC_PARCEL_READ_STRING_ARRAY,
        [read_value, alloc_guest, this](guest_t* g, const uint64_t a[8]) -> uint64_t {
            if (!a[2] || !a[3] || !guest_function_invoker_)
                return STATUS_BAD_VALUE;

            BinderParcelValue value;
            uint64_t rc = read_value(a[0], BinderParcelKind::StringArray, &value);
            if (rc != STATUS_OK)
                return rc;

            bool is_null = value.value != 0;
            int32_t length = is_null
                ? -1
                : static_cast<int32_t>(std::min<size_t>(
                      value.strings.size(),
                      static_cast<size_t>(INT32_MAX)));
            int64_t array_ok = guest_function_invoker_(
                a[2],
                {
                    a[1],
                    static_cast<uint64_t>(static_cast<int64_t>(length)),
                });
            if (!array_ok)
                return STATUS_NO_MEMORY;
            if (is_null)
                return STATUS_OK;

            uint64_t out_buffer_gpa = alloc_guest(sizeof(uint64_t), alignof(uint64_t));
            if (!out_buffer_gpa)
                return STATUS_NO_MEMORY;

            for (int32_t i = 0; i < length; ++i) {
                bool element_null =
                    i < static_cast<int32_t>(value.elements.size()) &&
                    value.elements[static_cast<size_t>(i)] != 0;
                int32_t element_length = element_null
                    ? -1
                    : static_cast<int32_t>(std::min<size_t>(
                          value.strings[static_cast<size_t>(i)].size() + 1,
                          static_cast<size_t>(INT32_MAX)));
                guest_write_u64(g, out_buffer_gpa, 0);
                int64_t element_ok = guest_function_invoker_(
                    a[3],
                    {
                        a[1],
                        static_cast<uint64_t>(static_cast<size_t>(i)),
                        static_cast<uint64_t>(static_cast<int64_t>(element_length)),
                        out_buffer_gpa,
                    });
                if (!element_ok)
                    return STATUS_NO_MEMORY;
                if (element_null)
                    continue;

                uint64_t buffer_gpa = guest_read_u64(g, out_buffer_gpa);
                if (!buffer_gpa)
                    return STATUS_NO_MEMORY;
                const std::string& text = value.strings[static_cast<size_t>(i)];
                if (!text.empty() &&
                    guest_write(g, buffer_gpa, text.data(), text.size()) != 0) {
                    return STATUS_BAD_VALUE;
                }
                char nul = '\0';
                if (guest_write(g, buffer_gpa + text.size(), &nul, sizeof(nul)) != 0)
                    return STATUS_BAD_VALUE;
            }

            std::fprintf(stderr,
                "[Binder] readStringArray parcel=0x%llx len=%d\n",
                (unsigned long long)a[0],
                length);
            return STATUS_OK;
        });
    add("libbinder_ndk.so", "AParcel_writeParcelableArray",
        HVC_PARCEL_WRITE_PARCELABLE_ARRAY,
        [append_parcel_value, this](guest_t*, const uint64_t a[8]) -> uint64_t {
            int32_t length = static_cast<int32_t>(a[2]);
            if (length < -1 || length > MAX_PARCEL_ARRAY_LENGTH)
                return STATUS_BAD_VALUE;

            BinderParcelValue marker;
            marker.kind = BinderParcelKind::ParcelableArray;
            if (length < 0) {
                marker.value = 1;
                return append_parcel_value(a[0], std::move(marker));
            }
            if (length > 0 && (!a[3] || !guest_function_invoker_))
                return STATUS_BAD_VALUE;

            marker.elements.resize(static_cast<size_t>(length));
            uint64_t rc = append_parcel_value(a[0], std::move(marker));
            if (rc != STATUS_OK)
                return rc;
            if (length == 0)
                return STATUS_OK;

            for (int32_t i = 0; i < length; ++i) {
                int64_t element_status = guest_function_invoker_(
                    a[3],
                    {
                        a[0],
                        a[1],
                        static_cast<uint64_t>(static_cast<size_t>(i)),
                    });
                if (element_status != static_cast<int64_t>(STATUS_OK))
                    return static_cast<uint64_t>(element_status);
            }

            std::fprintf(stderr,
                "[Binder] writeParcelableArray parcel=0x%llx len=%d\n",
                (unsigned long long)a[0],
                length);
            return STATUS_OK;
        });
    add("libbinder_ndk.so", "AParcel_readParcelableArray",
        HVC_PARCEL_READ_PARCELABLE_ARRAY,
        [read_value, this](guest_t*, const uint64_t a[8]) -> uint64_t {
            if (!a[2] || !a[3] || !guest_function_invoker_)
                return STATUS_BAD_VALUE;

            BinderParcelValue marker;
            uint64_t rc = read_value(a[0], BinderParcelKind::ParcelableArray,
                                     &marker);
            if (rc != STATUS_OK)
                return rc;

            bool is_null = marker.value != 0;
            int32_t length = is_null
                ? -1
                : static_cast<int32_t>(std::min<size_t>(
                      marker.elements.size(),
                      static_cast<size_t>(INT32_MAX)));
            int64_t array_ok = guest_function_invoker_(
                a[2],
                {
                    a[1],
                    static_cast<uint64_t>(static_cast<int64_t>(length)),
                });
            if (!array_ok)
                return STATUS_NO_MEMORY;
            if (is_null)
                return STATUS_OK;

            for (int32_t i = 0; i < length; ++i) {
                int64_t element_status = guest_function_invoker_(
                    a[3],
                    {
                        a[0],
                        a[1],
                        static_cast<uint64_t>(static_cast<size_t>(i)),
                    });
                if (element_status != static_cast<int64_t>(STATUS_OK))
                    return static_cast<uint64_t>(element_status);
            }

            std::fprintf(stderr,
                "[Binder] readParcelableArray parcel=0x%llx len=%d\n",
                (unsigned long long)a[0],
                length);
            return STATUS_OK;
        });

    add("libbinder_ndk.so", "AParcel_writeInt32", HVC_PARCEL_WRITE_I32,
        [append_value](guest_t*, const uint64_t a[8]) -> uint64_t {
            return append_value(a[0], BinderParcelKind::Int32,
                static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(a[1]))));
        });
    add("libbinder_ndk.so", "AParcel_writeUint32", HVC_PARCEL_WRITE_U32,
        [append_value](guest_t*, const uint64_t a[8]) -> uint64_t {
            return append_value(a[0], BinderParcelKind::Uint32,
                static_cast<uint32_t>(a[1]));
        });
    add("libbinder_ndk.so", "AParcel_writeInt64", HVC_PARCEL_WRITE_I64,
        [append_value](guest_t*, const uint64_t a[8]) -> uint64_t {
            return append_value(a[0], BinderParcelKind::Int64, a[1]);
        });
    add("libbinder_ndk.so", "AParcel_writeUint64", HVC_PARCEL_WRITE_U64,
        [append_value](guest_t*, const uint64_t a[8]) -> uint64_t {
            return append_value(a[0], BinderParcelKind::Uint64, a[1]);
        });
    add("libbinder_ndk.so", "AParcel_writeFloat", HVC_PARCEL_WRITE_FLOAT,
        [append_value](guest_t*, const uint64_t a[8]) -> uint64_t {
            return append_value(a[0], BinderParcelKind::Float,
                static_cast<uint32_t>(a[1]));
        });
    add("libbinder_ndk.so", "AParcel_writeDouble", HVC_PARCEL_WRITE_DOUBLE,
        [append_value](guest_t*, const uint64_t a[8]) -> uint64_t {
            return append_value(a[0], BinderParcelKind::Double, a[1]);
        });
    add("libbinder_ndk.so", "AParcel_writeBool", HVC_PARCEL_WRITE_BOOL,
        [append_value](guest_t*, const uint64_t a[8]) -> uint64_t {
            return append_value(a[0], BinderParcelKind::Bool, a[1] ? 1 : 0);
        });
    add("libbinder_ndk.so", "AParcel_writeChar", HVC_PARCEL_WRITE_CHAR,
        [append_value](guest_t*, const uint64_t a[8]) -> uint64_t {
            return append_value(a[0], BinderParcelKind::Char,
                static_cast<uint16_t>(a[1]));
        });
    add("libbinder_ndk.so", "AParcel_writeByte", HVC_PARCEL_WRITE_BYTE,
        [append_value](guest_t*, const uint64_t a[8]) -> uint64_t {
            return append_value(a[0], BinderParcelKind::Byte,
                static_cast<uint8_t>(a[1]));
        });
    add("libbinder_ndk.so", "AParcel_writeInt32Array",
        HVC_PARCEL_WRITE_I32_ARRAY,
        [append_parcel_value](guest_t* g, const uint64_t a[8]) -> uint64_t {
            int32_t length = static_cast<int32_t>(a[2]);
            if (length < -1 || length > MAX_PARCEL_ARRAY_LENGTH)
                return STATUS_BAD_VALUE;

            BinderParcelValue value;
            value.kind = BinderParcelKind::Int32Array;
            if (length < 0) {
                value.value = 1;
                return append_parcel_value(a[0], std::move(value));
            }
            if (length > 0 && !a[1])
                return STATUS_BAD_VALUE;

            value.elements.reserve(static_cast<size_t>(length));
            for (int32_t i = 0; i < length; ++i) {
                int32_t item =
                    static_cast<int32_t>(guest_read_u32(
                        g, a[1] + static_cast<uint64_t>(i) * sizeof(uint32_t)));
                value.elements.push_back(
                    static_cast<uint64_t>(static_cast<int64_t>(item)));
            }
            return append_parcel_value(a[0], std::move(value));
        });

    add("libbinder_ndk.so", "AParcel_readInt32", HVC_PARCEL_READ_I32,
        [read_value](guest_t* g, const uint64_t a[8]) -> uint64_t {
            BinderParcelValue value;
            uint64_t rc = read_value(a[0], BinderParcelKind::Int32, &value);
            if (rc == STATUS_OK && a[1])
                guest_write_u32(g, a[1], static_cast<uint32_t>(value.value));
            return rc;
        });
    add("libbinder_ndk.so", "AParcel_readUint32", HVC_PARCEL_READ_U32,
        [read_value](guest_t* g, const uint64_t a[8]) -> uint64_t {
            BinderParcelValue value;
            uint64_t rc = read_value(a[0], BinderParcelKind::Uint32, &value);
            if (rc == STATUS_OK && a[1])
                guest_write_u32(g, a[1], static_cast<uint32_t>(value.value));
            return rc;
        });
    add("libbinder_ndk.so", "AParcel_readInt64", HVC_PARCEL_READ_I64,
        [read_value](guest_t* g, const uint64_t a[8]) -> uint64_t {
            BinderParcelValue value;
            uint64_t rc = read_value(a[0], BinderParcelKind::Int64, &value);
            if (rc == STATUS_OK && a[1])
                guest_write_u64(g, a[1], value.value);
            return rc;
        });
    add("libbinder_ndk.so", "AParcel_readUint64", HVC_PARCEL_READ_U64,
        [read_value](guest_t* g, const uint64_t a[8]) -> uint64_t {
            BinderParcelValue value;
            uint64_t rc = read_value(a[0], BinderParcelKind::Uint64, &value);
            if (rc == STATUS_OK && a[1])
                guest_write_u64(g, a[1], value.value);
            return rc;
        });
    add("libbinder_ndk.so", "AParcel_readFloat", HVC_PARCEL_READ_FLOAT,
        [read_value](guest_t* g, const uint64_t a[8]) -> uint64_t {
            BinderParcelValue value;
            uint64_t rc = read_value(a[0], BinderParcelKind::Float, &value);
            if (rc == STATUS_OK && a[1]) {
                uint32_t bits = static_cast<uint32_t>(value.value);
                guest_write(g, a[1], &bits, sizeof(bits));
            }
            return rc;
        });
    add("libbinder_ndk.so", "AParcel_readDouble", HVC_PARCEL_READ_DOUBLE,
        [read_value](guest_t* g, const uint64_t a[8]) -> uint64_t {
            BinderParcelValue value;
            uint64_t rc = read_value(a[0], BinderParcelKind::Double, &value);
            if (rc == STATUS_OK && a[1])
                guest_write_u64(g, a[1], value.value);
            return rc;
        });
    add("libbinder_ndk.so", "AParcel_readBool", HVC_PARCEL_READ_BOOL,
        [read_value](guest_t* g, const uint64_t a[8]) -> uint64_t {
            BinderParcelValue value;
            uint64_t rc = read_value(a[0], BinderParcelKind::Bool, &value);
            if (rc == STATUS_OK && a[1]) {
                uint8_t b = value.value ? 1 : 0;
                guest_write(g, a[1], &b, sizeof(b));
            }
            return rc;
        });
    add("libbinder_ndk.so", "AParcel_readChar", HVC_PARCEL_READ_CHAR,
        [read_value](guest_t* g, const uint64_t a[8]) -> uint64_t {
            BinderParcelValue value;
            uint64_t rc = read_value(a[0], BinderParcelKind::Char, &value);
            if (rc == STATUS_OK && a[1]) {
                uint16_t ch = static_cast<uint16_t>(value.value);
                guest_write(g, a[1], &ch, sizeof(ch));
            }
            return rc;
        });
    add("libbinder_ndk.so", "AParcel_readByte", HVC_PARCEL_READ_BYTE,
        [read_value](guest_t* g, const uint64_t a[8]) -> uint64_t {
            BinderParcelValue value;
            uint64_t rc = read_value(a[0], BinderParcelKind::Byte, &value);
            if (rc == STATUS_OK && a[1]) {
                uint8_t b = static_cast<uint8_t>(value.value);
                guest_write(g, a[1], &b, sizeof(b));
            }
            return rc;
        });
    add("libbinder_ndk.so", "AParcel_readInt32Array",
        HVC_PARCEL_READ_I32_ARRAY,
        [read_value, alloc_guest, this](guest_t* g, const uint64_t a[8]) -> uint64_t {
            if (!a[2] || !guest_function_invoker_)
                return STATUS_BAD_VALUE;

            BinderParcelValue value;
            uint64_t rc = read_value(a[0], BinderParcelKind::Int32Array, &value);
            if (rc != STATUS_OK)
                return rc;

            bool is_null = value.value != 0;
            int32_t length = is_null
                ? -1
                : static_cast<int32_t>(std::min<size_t>(
                      value.elements.size(),
                      static_cast<size_t>(INT32_MAX)));
            uint64_t out_buffer_gpa = alloc_guest(sizeof(uint64_t), alignof(uint64_t));
            if (!out_buffer_gpa)
                return STATUS_NO_MEMORY;
            guest_write_u64(g, out_buffer_gpa, 0);

            int64_t ok = guest_function_invoker_(
                a[2],
                {
                    a[1],
                    static_cast<uint64_t>(static_cast<int64_t>(length)),
                    out_buffer_gpa,
                });
            if (!ok)
                return STATUS_NO_MEMORY;
            if (is_null || length == 0)
                return STATUS_OK;

            uint64_t buffer_gpa = guest_read_u64(g, out_buffer_gpa);
            if (!buffer_gpa)
                return STATUS_NO_MEMORY;
            for (int32_t i = 0; i < length; ++i) {
                uint32_t item = static_cast<uint32_t>(
                    static_cast<int32_t>(
                        static_cast<int64_t>(value.elements[static_cast<size_t>(i)])));
                guest_write_u32(
                    g,
                    buffer_gpa + static_cast<uint64_t>(i) * sizeof(uint32_t),
                    item);
            }

            std::fprintf(stderr,
                "[Binder] readInt32Array parcel=0x%llx len=%d\n",
                (unsigned long long)a[0],
                length);
            return STATUS_OK;
        });

    add("libbinder_ndk.so", "AStatus_newOk", HVC_STATUS_NEW_OK,
        [create_status](guest_t*, const uint64_t[8]) -> uint64_t {
            return create_status(EX_NONE, 0, 0, {});
        });
    add("libbinder_ndk.so", "AStatus_fromStatus", HVC_STATUS_FROM_STATUS,
        [create_status](guest_t*, const uint64_t a[8]) -> uint64_t {
            int32_t status = static_cast<int32_t>(a[0]);
            int32_t exception = status == 0 ? EX_NONE : EX_TRANSACTION_FAILED;
            return create_status(exception, 0, status, {});
        });
    add("libbinder_ndk.so", "AStatus_isOk", HVC_STATUS_IS_OK,
        [find_status](guest_t*, const uint64_t a[8]) -> uint64_t {
            BinderStatus* status = find_status(a[0]);
            return status && status->exception == 0 && status->status == 0 ? 1 : 0;
        });
    add("libbinder_ndk.so", "AStatus_getExceptionCode", HVC_STATUS_EXCEPTION,
        [find_status](guest_t*, const uint64_t a[8]) -> uint64_t {
            BinderStatus* status = find_status(a[0]);
            return status ? static_cast<uint64_t>(static_cast<int64_t>(status->exception))
                          : static_cast<uint64_t>(static_cast<int64_t>(EX_TRANSACTION_FAILED));
        });
    add("libbinder_ndk.so", "AStatus_getServiceSpecificError",
        HVC_STATUS_SERVICE_ERR,
        [find_status](guest_t*, const uint64_t a[8]) -> uint64_t {
            BinderStatus* status = find_status(a[0]);
            return status ? static_cast<uint64_t>(static_cast<int64_t>(status->service_error))
                          : 0;
        });
    add("libbinder_ndk.so", "AStatus_getStatus", HVC_STATUS_STATUS,
        [find_status](guest_t*, const uint64_t a[8]) -> uint64_t {
            BinderStatus* status = find_status(a[0]);
            return status ? static_cast<uint64_t>(static_cast<int64_t>(status->status))
                          : STATUS_BAD_VALUE;
        });
    add("libbinder_ndk.so", "AStatus_getMessage", HVC_STATUS_MESSAGE,
        [this, find_status](guest_t* g, const uint64_t a[8]) -> uint64_t {
            BinderStatus* status = find_status(a[0]);
            if (!status)
                return 0;
            std::string message = status->message;
            size_t size = message.size() + 1;
            size_t aligned = (size + 15) & ~15ULL;
            if (heap_bump_ + aligned > heap_base_ + HEAP_SIZE)
                return 0;
            uint64_t ptr = heap_bump_;
            heap_bump_ += aligned;
            guest_write(g, ptr, message.c_str(), size);
            return ptr;
        });
    add("libbinder_ndk.so", "AStatus_delete", HVC_STATUS_DELETE,
        [this](guest_t*, const uint64_t a[8]) -> uint64_t {
            binder_statuses_.erase(a[0]);
            return 0;
        });
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
            for (size_t i = 0; i + 1 < attribs.size(); i += 2) {
                if (attribs[i] == EGL_NONE) break;
                if (attribs[i] == EGL_SURFACE_TYPE)
                    attribs[i + 1] |= EGL_PBUFFER_BIT;
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
    // Backed by a pbuffer sized to our current fake NativeWindow.
    add("libEGL.so", "eglCreateWindowSurface", HVC_EGL_CREATE_WIN_SURFACE,
        [this](guest_t*, const uint64_t a[8]) -> uint64_t {
            if (a[0] != GUEST_EGL_DISPLAY || egl_display_ == EGL_NO_DISPLAY) return 0;
            if (a[2] != GUEST_NATIVE_WINDOW) {
                std::fprintf(stderr, "[EGL] eglCreateWindowSurface: unknown native window 0x%llx\n",
                             (unsigned long long)a[2]);
                return 0;
            }
            if (egl_surface_ == EGL_NO_SURFACE) {
                EGLint pbuf_attribs[] = {
                    EGL_WIDTH, native_window_.width,
                    EGL_HEIGHT, native_window_.height,
                    EGL_NONE
                };
                egl_surface_ = eglCreatePbufferSurface(egl_display_, egl_config_, pbuf_attribs);
                std::fprintf(stderr, "[EGL] eglCreateWindowSurface → pbuffer %p (%dx%d)\n",
                             egl_surface_, native_window_.width, native_window_.height);
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
            std::fprintf(stderr, "[EGL] eglSwapBuffers → %d\n", ok);
            if (ok) present_egl_surface();
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
