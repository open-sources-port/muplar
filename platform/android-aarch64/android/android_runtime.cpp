// platform/android-aarch64/android/android_runtime.cpp
#include "android_runtime.h"
#include "muplard_client.h"

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <sys/time.h>
#include <system_error>
#include <cerrno>
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
#include "syscall/internal.h"
#include "syscall/proc.h"
}

namespace muplar::runtime::android
{
namespace
{

const char *default_android_property(std::string_view name)
{
    if (name == "servicemanager.ready" || name == "servicemanager.installed" ||
        name == "hwservicemanager.ready" || name == "vndservicemanager.ready") {
        return "1";
    }

    if (name == "init.svc.servicemanager" ||
        name == "init.svc.hwservicemanager" ||
        name == "init.svc.vndservicemanager") {
        return "running";
    }

    if (name == "ro.arch")
        return "arm64";
    if (name == "ro.product.cpu.abi")
        return "arm64-v8a";
    if (name == "ro.product.cpu.abilist" ||
        name == "ro.product.cpu.abilist64") {
        return "arm64-v8a";
    }
    if (name == "ro.product.cpu.abilist32")
        return "";

    if (name == "ro.build.version.sdk")
        return "35";
    if (name == "ro.build.version.sdk_full")
        return "35";
    if (name == "ro.build.version.preview_sdk")
        return "0";
    if (name == "ro.build.version.codename" ||
        name == "ro.build.version.release_or_codename") {
        return "REL";
    }
    if (name == "ro.build.version.release" ||
        name == "ro.build.version.release_or_preview_display") {
        return "15";
    }
    if (name == "ro.build.version.incremental")
        return "muplar";
    if (name == "ro.build.version.security_patch")
        return "2026-07-01";
    if (name == "ro.build.type")
        return "userdebug";
    if (name == "ro.build.tags")
        return "test-keys";
    if (name == "ro.build.id")
        return "MUPLAR";
    if (name == "ro.build.fingerprint") {
        return "muplar/android_arm64/muplar:15/MUPLAR/1:userdebug/test-keys";
    }

    if (name == "ro.product.brand" || name == "ro.product.vendor.brand" ||
        name == "ro.product.brand_for_attestation") {
        return "Muplar";
    }
    if (name == "ro.product.manufacturer" ||
        name == "ro.product.vendor.manufacturer" ||
        name == "ro.product.manufacturer_for_attestation") {
        return "Muplar";
    }
    if (name == "ro.product.name" || name == "ro.product.vendor.name" ||
        name == "ro.product.name_for_attestation") {
        return "muplar_arm64";
    }
    if (name == "ro.product.device" || name == "ro.product.vendor.device" ||
        name == "ro.product.device_for_attestation") {
        return "muplar";
    }
    if (name == "ro.product.model" || name == "ro.product.vendor.model" ||
        name == "ro.product.model_for_attestation") {
        return "Muplar Android";
    }
    if (name == "ro.product.locale" || name == "persist.sys.locale") {
        return "en-US";
    }
    if (name == "ro.product.locale.language")
        return "en";
    if (name == "ro.product.locale.region")
        return "US";

    if (name == "ro.debuggable")
        return "1";
    if (name == "ro.treble.enabled")
        return "true";
    if (name == "ro.config.low_ram")
        return "false";
    if (name == "ro.property_service.version")
        return "2";
    if (name == "ro.hardware")
        return "muplar";
    if (name == "persist.sys.dalvik.vm.lib.2")
        return "libart.so";
    if (name == "ro.dalvik.vm.native.bridge")
        return "0";
    if (name == "dalvik.vm.isa.arm64.variant" ||
        name == "dalvik.vm.isa.arm64.features") {
        return "generic";
    }
    if (name == "dalvik.vm.allow_profile_code")
        return "false";
    if (name == "persist.log.level")
        return "I";
    if (name.rfind("log.tag.", 0) == 0 ||
        name.rfind("persist.log.tag", 0) == 0) {
        return "I";
    }

    return nullptr;
}

}  // namespace

// ── HVC call number ranges
// ──────────────────────────────────────────────────── libc      :
// 0x2000–0x20FF liblog    : 0x2100–0x21FF libandroid: 0x2200–0x22FF libdl     :
// 0x2300–0x23FF libEGL    : 0x2400–0x24FF libGLESv2 : 0x2500–0x25FF
// libbinder_ndk: 0x2700–0x27FF
// procaddr  : 0x2800–0x2FFF  (dynamic, one per eglGetProcAddress result)

// libc
static constexpr uint32_t HVC_MALLOC = 0x2000;
static constexpr uint32_t HVC_FREE = 0x2001;
static constexpr uint32_t HVC_CALLOC = 0x2002;
static constexpr uint32_t HVC_REALLOC = 0x2003;
static constexpr uint32_t HVC_MEMCPY = 0x2004;
static constexpr uint32_t HVC_MEMMOVE = 0x2005;
static constexpr uint32_t HVC_MEMSET = 0x2006;
static constexpr uint32_t HVC_MEMCMP = 0x2007;
static constexpr uint32_t HVC_STRLEN = 0x2008;
static constexpr uint32_t HVC_STRCMP = 0x2009;
static constexpr uint32_t HVC_STRNCMP = 0x200A;
static constexpr uint32_t HVC_STRCPY = 0x200B;
static constexpr uint32_t HVC_STRNCPY = 0x200C;
static constexpr uint32_t HVC_STRCAT = 0x200D;
static constexpr uint32_t HVC_STRDUP = 0x200E;
[[maybe_unused]] static constexpr uint32_t HVC_SPRINTF = 0x200F;
[[maybe_unused]] static constexpr uint32_t HVC_SNPRINTF = 0x2010;
static constexpr uint32_t HVC_PRINTF = 0x2011;
static constexpr uint32_t HVC_ABORT = 0x2012;
static constexpr uint32_t HVC_EXIT = 0x2013;
static constexpr uint32_t HVC_PTHREAD_CREATE = 0x2020;
static constexpr uint32_t HVC_PTHREAD_JOIN = 0x2021;
static constexpr uint32_t HVC_PTHREAD_MUTEX_INIT = 0x2022;
static constexpr uint32_t HVC_PTHREAD_MUTEX_LOCK = 0x2023;
static constexpr uint32_t HVC_PTHREAD_MUTEX_UNLOCK = 0x2024;
static constexpr uint32_t HVC_PTHREAD_MUTEX_DESTROY = 0x2025;
static constexpr uint32_t HVC_PTHREAD_KEY_CREATE = 0x2026;
static constexpr uint32_t HVC_PTHREAD_GETSPECIFIC = 0x2027;
static constexpr uint32_t HVC_PTHREAD_SETSPECIFIC = 0x2028;
static constexpr uint32_t HVC_PTHREAD_ONCE = 0x2029;
static constexpr uint32_t HVC_PTHREAD_SELF = 0x202A;
static constexpr uint32_t HVC_PTHREAD_COND_INIT = 0x2040;
static constexpr uint32_t HVC_PTHREAD_COND_WAIT = 0x2041;
static constexpr uint32_t HVC_PTHREAD_COND_SIGNAL = 0x2042;
static constexpr uint32_t HVC_PTHREAD_COND_BCAST = 0x2043;
static constexpr uint32_t HVC_PTHREAD_COND_DESTROY = 0x2044;
static constexpr uint32_t HVC_PTHREAD_ATTR_INIT = 0x2045;
static constexpr uint32_t HVC_PTHREAD_ATTR_DESTROY = 0x2046;
static constexpr uint32_t HVC_PTHREAD_ATTR_SETDETACH = 0x2047;
static constexpr uint32_t HVC_PTHREAD_DETACH = 0x2048;
static constexpr uint32_t HVC_GETPID = 0x2030;
static constexpr uint32_t HVC_GETENV_LIBC = 0x2031;
static constexpr uint32_t HVC_CLOCK_GETTIME = 0x2032;
static constexpr uint32_t HVC_GETTIMEOFDAY = 0x2033;
static constexpr uint32_t HVC_USLEEP = 0x2034;
static constexpr uint32_t HVC_NANOSLEEP = 0x2035;
[[maybe_unused]] static constexpr uint32_t HVC_STRTOL = 0x2036;
[[maybe_unused]] static constexpr uint32_t HVC_STRTOD = 0x2037;
static constexpr uint32_t HVC_ATOI = 0x2038;
[[maybe_unused]] static constexpr uint32_t HVC_ATOF = 0x2039;
static constexpr uint32_t HVC_RAND = 0x203A;
static constexpr uint32_t HVC_SRAND = 0x203B;
static constexpr uint32_t HVC_PIPE = 0x203C;
static constexpr uint32_t HVC_PIPE2 = 0x203D;
static constexpr uint32_t HVC_READ = 0x203E;
static constexpr uint32_t HVC_WRITE = 0x203F;
static constexpr uint32_t HVC_CLOSE = 0x2049;
static constexpr uint32_t HVC_REGISTER_ATFORK = 0x204A;
static constexpr uint32_t HVC_FPRINTF = 0x204B;
static constexpr uint32_t HVC_VFPRINTF = 0x204C;
static constexpr uint32_t HVC_FFLUSH = 0x204D;
static constexpr uint32_t HVC_FWRITE = 0x204E;
static constexpr uint32_t HVC_VASPRINTF = 0x204F;
static constexpr uint32_t HVC_SYSCALL = 0x2050;
static constexpr uint32_t HVC_OPENLOG = 0x2051;
static constexpr uint32_t HVC_SYSLOG = 0x2052;
static constexpr uint32_t HVC_CLOSELOG = 0x2053;
static constexpr uint32_t HVC_ASSERT = 0x2054;
static constexpr uint32_t HVC_ASSERT2 = 0x2055;
static constexpr uint32_t HVC_PTHREAD_RWLOCK_INIT = 0x2056;
static constexpr uint32_t HVC_PTHREAD_RWLOCK_RDLOCK = 0x2057;
static constexpr uint32_t HVC_PTHREAD_RWLOCK_WRLOCK = 0x2058;
static constexpr uint32_t HVC_PTHREAD_RWLOCK_UNLOCK = 0x2059;
static constexpr uint32_t HVC_PTHREAD_RWLOCK_DESTROY = 0x205A;
static constexpr uint32_t HVC_ANDROID_ABORT_MESSAGE = 0x205B;
static constexpr uint32_t HVC_ERRNO = 0x205C;
static constexpr uint32_t HVC_POSIX_MEMALIGN = 0x205D;
static constexpr uint32_t HVC_VSNPRINTF = 0x205E;
static constexpr uint32_t HVC_MEMCHR = 0x205F;
static constexpr uint32_t HVC_ISATTY = 0x2060;
static constexpr uint32_t HVC_FILENO = 0x2061;
static constexpr uint32_t HVC_LIBC_RET0 = 0x2062;
static constexpr uint32_t HVC_LIBC_RET_NEG1 = 0x2063;
static constexpr uint32_t HVC_LIBC_RET_NULL = 0x2064;
static constexpr uint32_t HVC_GETAUXVAL = 0x2065;
static constexpr uint32_t HVC_LOCALECONV = 0x2066;
static constexpr uint32_t HVC_WCSLEN = 0x2067;
static constexpr uint32_t HVC_WMEMCHR = 0x2068;
static constexpr uint32_t HVC_WMEMCMP = 0x2069;
static constexpr uint32_t HVC_FPUTC = 0x206A;
static constexpr uint32_t HVC_GETC = 0x206B;
static constexpr uint32_t HVC_UNGETC = 0x206C;
static constexpr uint32_t HVC_CTYPE_MB_CUR_MAX = 0x206D;
static constexpr uint32_t HVC_STACK_CHK_FAIL = 0x206E;
static constexpr uint32_t HVC_MEMMOVE_CHK = 0x206F;
static constexpr uint32_t HVC_STRLEN_CHK = 0x2070;
static constexpr uint32_t HVC_STRNCPY_CHK2 = 0x2071;
static constexpr uint32_t HVC_STRRCHR_CHK = 0x2072;
static constexpr uint32_t HVC_VSNPRINTF_CHK = 0x2073;
static constexpr uint32_t HVC_ISLOWER = 0x2074;
static constexpr uint32_t HVC_ISUPPER = 0x2075;
static constexpr uint32_t HVC_ISXDIGIT = 0x2076;
static constexpr uint32_t HVC_TOLOWER = 0x2077;
static constexpr uint32_t HVC_TOUPPER = 0x2078;
static constexpr uint32_t HVC_ISWALPHA = 0x2079;
static constexpr uint32_t HVC_ISWBLANK = 0x207A;
static constexpr uint32_t HVC_ISWCNTRL = 0x207B;
static constexpr uint32_t HVC_ISWDIGIT = 0x207C;
static constexpr uint32_t HVC_ISWLOWER = 0x207D;
static constexpr uint32_t HVC_ISWPRINT = 0x207E;
static constexpr uint32_t HVC_ISWPUNCT = 0x207F;
static constexpr uint32_t HVC_ISWSPACE = 0x2080;
static constexpr uint32_t HVC_ISWUPPER = 0x2081;
static constexpr uint32_t HVC_ISWXDIGIT = 0x2082;
static constexpr uint32_t HVC_TOWLOWER = 0x2083;
static constexpr uint32_t HVC_TOWUPPER = 0x2084;
static constexpr uint32_t HVC_STRCOLL = 0x2085;
static constexpr uint32_t HVC_STRXFRM = 0x2086;
static constexpr uint32_t HVC_STRFTIME = 0x2087;
static constexpr uint32_t HVC_WCSCOLL = 0x2088;
static constexpr uint32_t HVC_WCSXFRM = 0x2089;
static constexpr uint32_t HVC_WMEMCPY = 0x208A;
static constexpr uint32_t HVC_WMEMMOVE = 0x208B;
static constexpr uint32_t HVC_WMEMSET = 0x208C;
static constexpr uint32_t HVC_PRCTL = 0x208D;
static constexpr uint32_t HVC_SF = 0x208E;
static constexpr uint32_t HVC_MEMCPY_CHK = 0x208F;
static constexpr uint32_t HVC_STRNCPY_CHK = 0x2090;
static constexpr uint32_t HVC_STRCHR = 0x2091;
static constexpr uint32_t HVC_STRSTR = 0x2092;
static constexpr uint32_t HVC_STRERROR = 0x2093;
static constexpr uint32_t HVC_PTHREAD_EQUAL = 0x2094;
static constexpr uint32_t HVC_PTHREAD_SETNAME_NP = 0x2095;
static constexpr uint32_t HVC_LIBM_RET0 = 0x2096;
static constexpr uint32_t HVC_LIBM_SINCOS = 0x2097;
static constexpr uint32_t HVC_LIBM_SINCOSF = 0x2098;
static constexpr uint32_t HVC_OPEN_2 = 0x2099;
static constexpr uint32_t HVC_GETTID = 0x209A;
static constexpr uint32_t HVC_MUNMAP = 0x209B;
static constexpr uint32_t HVC_SETPRIORITY = 0x209C;
static constexpr uint32_t HVC_MMAP = 0x209D;
static constexpr uint32_t HVC_SCHED_SETAFFINITY = 0x209E;
static constexpr uint32_t HVC_MPROTECT = 0x209F;
static constexpr uint32_t HVC_READ_CHK = 0x20A0;
static constexpr uint32_t HVC_STRLCPY = 0x20A1;

// liblog
static constexpr uint32_t HVC_LOG_PRINT = 0x2100;
static constexpr uint32_t HVC_LOG_WRITE = 0x2101;
static constexpr uint32_t HVC_LOG_BUF_WRITE = 0x2102;
static constexpr uint32_t HVC_LOG_VPRINT = 0x2103;

// libandroid
static constexpr uint32_t HVC_ALOOPER_PREPARE = 0x2200;
static constexpr uint32_t HVC_ALOOPER_ACQUIRE = 0x2201;
static constexpr uint32_t HVC_ALOOPER_RELEASE = 0x2202;
static constexpr uint32_t HVC_ALOOPER_POLL_ONCE = 0x2203;
static constexpr uint32_t HVC_ALOOPER_POLL_ALL = 0x2204;
static constexpr uint32_t HVC_ALOOPER_ADD_FD = 0x2205;
static constexpr uint32_t HVC_ALOOPER_REMOVE_FD = 0x2206;
static constexpr uint32_t HVC_ALOOPER_WAKE = 0x2207;
static constexpr uint32_t HVC_ASSET_MGR_FROM_JAVA = 0x2210;
static constexpr uint32_t HVC_ASSET_OPEN = 0x2211;
static constexpr uint32_t HVC_ASSET_READ = 0x2212;
static constexpr uint32_t HVC_ASSET_CLOSE = 0x2213;
static constexpr uint32_t HVC_ASSET_LENGTH = 0x2214;
static constexpr uint32_t HVC_ASSET_REMAINING = 0x2215;
static constexpr uint32_t HVC_ASSET_SEEK = 0x2216;
static constexpr uint32_t HVC_CHOREOGRAPHER_GET = 0x2220;
static constexpr uint32_t HVC_CHOREOGRAPHER_CB = 0x2221;
static constexpr uint32_t HVC_NATIVE_WINDOW_SET_BUF = 0x2230;
static constexpr uint32_t HVC_NATIVE_WINDOW_LOCK = 0x2231;
static constexpr uint32_t HVC_NATIVE_WINDOW_UNLOCK = 0x2232;
static constexpr uint32_t HVC_NATIVE_WINDOW_ACQUIRE = 0x2233;
static constexpr uint32_t HVC_NATIVE_WINDOW_RELEASE = 0x2234;
static constexpr uint32_t HVC_NATIVE_WINDOW_WIDTH = 0x2235;
static constexpr uint32_t HVC_NATIVE_WINDOW_HEIGHT = 0x2236;
static constexpr uint32_t HVC_NATIVE_WINDOW_FORMAT = 0x2237;
static constexpr uint32_t HVC_NATIVE_WINDOW_FROM_SURFACE = 0x2238;
static constexpr uint32_t HVC_PROP_GET = 0x2240;
static constexpr uint32_t HVC_CUTILS_PROP_GET = 0x2241;
static constexpr uint32_t HVC_INPUT_QUEUE_ATTACH = 0x2250;
static constexpr uint32_t HVC_INPUT_QUEUE_DETACH = 0x2251;
static constexpr uint32_t HVC_INPUT_QUEUE_HAS_EVENTS = 0x2252;
static constexpr uint32_t HVC_INPUT_QUEUE_GET_EVENT = 0x2253;
static constexpr uint32_t HVC_INPUT_QUEUE_PRE_DISPATCH = 0x2254;
static constexpr uint32_t HVC_INPUT_QUEUE_FINISH = 0x2255;
static constexpr uint32_t HVC_INPUT_EVENT_GET_TYPE = 0x2260;
static constexpr uint32_t HVC_INPUT_EVENT_GET_DEVICE_ID = 0x2261;
static constexpr uint32_t HVC_INPUT_EVENT_GET_SOURCE = 0x2262;
static constexpr uint32_t HVC_MOTION_EVENT_GET_ACTION = 0x2263;
static constexpr uint32_t HVC_KEY_EVENT_GET_ACTION = 0x2264;
static constexpr uint32_t HVC_KEY_EVENT_GET_KEYCODE = 0x2265;
static constexpr uint32_t HVC_MOTION_EVENT_GET_X = 0x2266;
static constexpr uint32_t HVC_MOTION_EVENT_GET_Y = 0x2267;
static constexpr uint32_t HVC_MOTION_EVENT_GET_RAW_X = 0x2268;
static constexpr uint32_t HVC_MOTION_EVENT_GET_RAW_Y = 0x2269;
static constexpr uint32_t HVC_MOTION_EVENT_GET_POINTER_COUNT = 0x226A;
static constexpr uint32_t HVC_MOTION_EVENT_GET_POINTER_ID = 0x226B;
static constexpr uint32_t HVC_MOTION_EVENT_GET_EVENT_TIME = 0x226C;
static constexpr uint32_t HVC_MOTION_EVENT_GET_DOWN_TIME = 0x226D;
static constexpr uint32_t HVC_MOTION_EVENT_GET_PRESSURE = 0x226E;
static constexpr uint32_t HVC_MOTION_EVENT_GET_HISTORY_SIZE = 0x2280;
static constexpr uint32_t HVC_MOTION_EVENT_GET_HISTORICAL_EVENT_TIME = 0x2281;
static constexpr uint32_t HVC_MOTION_EVENT_GET_HISTORICAL_X = 0x2282;
static constexpr uint32_t HVC_MOTION_EVENT_GET_HISTORICAL_Y = 0x2283;
static constexpr uint32_t HVC_MOTION_EVENT_GET_HISTORICAL_RAW_X = 0x2284;
static constexpr uint32_t HVC_MOTION_EVENT_GET_HISTORICAL_RAW_Y = 0x2285;
static constexpr uint32_t HVC_MOTION_EVENT_GET_HISTORICAL_PRESSURE = 0x2286;
static constexpr uint32_t HVC_MOTION_EVENT_GET_AXIS_VALUE = 0x2287;
static constexpr uint32_t HVC_MOTION_EVENT_GET_FLAGS = 0x2288;
static constexpr uint32_t HVC_MOTION_EVENT_GET_META_STATE = 0x2289;
static constexpr uint32_t HVC_MOTION_EVENT_GET_BUTTON_STATE = 0x228A;
static constexpr uint32_t HVC_MOTION_EVENT_GET_EDGE_FLAGS = 0x228B;
static constexpr uint32_t HVC_CHOREOGRAPHER_CB_DELAYED = 0x2270;
static constexpr uint32_t HVC_CHOREOGRAPHER_CB64 = 0x2271;
static constexpr uint32_t HVC_CHOREOGRAPHER_CB64_DELAYED = 0x2272;
static constexpr uint32_t HVC_BITMAP_GET_INFO = 0x2290;
static constexpr uint32_t HVC_BITMAP_LOCK_PIXELS = 0x2291;
static constexpr uint32_t HVC_BITMAP_UNLOCK_PIXELS = 0x2292;

// libdl
static constexpr uint32_t HVC_DLOPEN = 0x2300;
static constexpr uint32_t HVC_DLSYM = 0x2301;
static constexpr uint32_t HVC_DLCLOSE = 0x2302;
static constexpr uint32_t HVC_DLERROR = 0x2303;
static constexpr uint32_t HVC_DL_ITERATE_PHDR = 0x2304;
static constexpr uint32_t HVC_DL_ANDROID_GET_NS = 0x2305;
static constexpr uint32_t HVC_DL_ANDROID_CREATE_NS = 0x2306;
static constexpr uint32_t HVC_DL_ANDROID_LINK_NS = 0x2307;
static constexpr uint32_t HVC_NATIVE_BRIDGE_IS_PATH_SUPPORTED = 0x2308;
static constexpr uint32_t HVC_NATIVE_BRIDGE_GET_ERROR = 0x2309;
static constexpr uint32_t HVC_NATIVE_BRIDGE_GET_NS = 0x230a;
static constexpr uint32_t HVC_NATIVE_BRIDGE_CREATE_NS = 0x230b;
static constexpr uint32_t HVC_NATIVE_BRIDGE_LINK_NS = 0x230c;
static constexpr uint32_t HVC_NATIVE_BRIDGE_LOAD_LIBRARY_EXT = 0x230d;

// libEGL

// libGLESv2 — direct wrappers for the most common draw-loop calls.
// Less common calls come through eglGetProcAddress (0x2800+).

// libbinder_ndk
static constexpr uint32_t HVC_SERVICE_CHECK = 0x2700;
static constexpr uint32_t HVC_SERVICE_GET = 0x2701;
static constexpr uint32_t HVC_SERVICE_WAIT = 0x2702;
static constexpr uint32_t HVC_SERVICE_IS_DECLARED = 0x2703;
static constexpr uint32_t HVC_SERVICE_ADD = 0x2704;
static constexpr uint32_t HVC_BINDER_IS_REMOTE = 0x2710;
static constexpr uint32_t HVC_BINDER_IS_ALIVE = 0x2711;
static constexpr uint32_t HVC_BINDER_PING = 0x2712;
static constexpr uint32_t HVC_BINDER_INC_STRONG = 0x2713;
static constexpr uint32_t HVC_BINDER_DEC_STRONG = 0x2714;
static constexpr uint32_t HVC_BINDER_REF_COUNT = 0x2715;
static constexpr uint32_t HVC_BINDER_CALLING_UID = 0x2716;
static constexpr uint32_t HVC_BINDER_CALLING_PID = 0x2717;
static constexpr uint32_t HVC_BINDER_HANDLING_TX = 0x2718;
static constexpr uint32_t HVC_BINDER_DUMP = 0x2719;
static constexpr uint32_t HVC_BINDER_LINK_DEATH = 0x271A;
static constexpr uint32_t HVC_BINDER_UNLINK_DEATH = 0x271B;
static constexpr uint32_t HVC_BINDER_PREPARE_TX = 0x271C;
static constexpr uint32_t HVC_BINDER_TRANSACT = 0x271D;
static constexpr uint32_t HVC_PARCEL_DELETE = 0x2720;
static constexpr uint32_t HVC_PARCEL_SET_POS = 0x2721;
static constexpr uint32_t HVC_PARCEL_GET_POS = 0x2722;
static constexpr uint32_t HVC_PARCEL_WRITE_BINDER = 0x2723;
static constexpr uint32_t HVC_PARCEL_READ_BINDER = 0x2724;
static constexpr uint32_t HVC_PARCEL_WRITE_STATUS = 0x2725;
static constexpr uint32_t HVC_PARCEL_READ_STATUS = 0x2726;
static constexpr uint32_t HVC_PARCEL_WRITE_STRING = 0x2727;
static constexpr uint32_t HVC_PARCEL_READ_STRING = 0x2728;
static constexpr uint32_t HVC_PARCEL_WRITE_STRING_ARRAY = 0x2729;
static constexpr uint32_t HVC_PARCEL_READ_STRING_ARRAY = 0x272A;
static constexpr uint32_t HVC_PARCEL_WRITE_PARCELABLE_ARRAY = 0x272B;
static constexpr uint32_t HVC_PARCEL_READ_PARCELABLE_ARRAY = 0x272C;
static constexpr uint32_t HVC_PARCEL_WRITE_FD = 0x272D;
static constexpr uint32_t HVC_PARCEL_READ_FD = 0x272E;
static constexpr uint32_t HVC_PARCEL_WRITE_I32 = 0x2730;
static constexpr uint32_t HVC_PARCEL_WRITE_U32 = 0x2731;
static constexpr uint32_t HVC_PARCEL_WRITE_I64 = 0x2732;
static constexpr uint32_t HVC_PARCEL_WRITE_U64 = 0x2733;
static constexpr uint32_t HVC_PARCEL_WRITE_FLOAT = 0x2734;
static constexpr uint32_t HVC_PARCEL_WRITE_DOUBLE = 0x2735;
static constexpr uint32_t HVC_PARCEL_WRITE_BOOL = 0x2736;
static constexpr uint32_t HVC_PARCEL_WRITE_CHAR = 0x2737;
static constexpr uint32_t HVC_PARCEL_WRITE_BYTE = 0x2738;
static constexpr uint32_t HVC_PARCEL_WRITE_I32_ARRAY = 0x2739;
static constexpr uint32_t HVC_PARCEL_READ_I32 = 0x2740;
static constexpr uint32_t HVC_PARCEL_READ_U32 = 0x2741;
static constexpr uint32_t HVC_PARCEL_READ_I64 = 0x2742;
static constexpr uint32_t HVC_PARCEL_READ_U64 = 0x2743;
static constexpr uint32_t HVC_PARCEL_READ_FLOAT = 0x2744;
static constexpr uint32_t HVC_PARCEL_READ_DOUBLE = 0x2745;
static constexpr uint32_t HVC_PARCEL_READ_BOOL = 0x2746;
static constexpr uint32_t HVC_PARCEL_READ_CHAR = 0x2747;
static constexpr uint32_t HVC_PARCEL_READ_BYTE = 0x2748;
static constexpr uint32_t HVC_PARCEL_READ_I32_ARRAY = 0x2749;
static constexpr uint32_t HVC_BINDER_CLASS_DEFINE = 0x2750;
static constexpr uint32_t HVC_BINDER_CLASS_SET_DUMP = 0x2751;
static constexpr uint32_t HVC_BINDER_CLASS_SET_NAMES = 0x2752;
static constexpr uint32_t HVC_BINDER_CLASS_GET_NAME = 0x2753;
static constexpr uint32_t HVC_BINDER_NEW = 0x2754;
static constexpr uint32_t HVC_BINDER_ASSOC_CLASS = 0x2755;
static constexpr uint32_t HVC_BINDER_GET_CLASS = 0x2756;
static constexpr uint32_t HVC_BINDER_GET_USER_DATA = 0x2757;
static constexpr uint32_t HVC_BINDER_CLASS_GET_FN_NAME = 0x2758;
static constexpr uint32_t HVC_BINDER_CLASS_DISABLE_TOKEN = 0x2759;
static constexpr uint32_t HVC_STATUS_NEW_OK = 0x2760;
static constexpr uint32_t HVC_STATUS_FROM_STATUS = 0x2761;
static constexpr uint32_t HVC_STATUS_IS_OK = 0x2762;
static constexpr uint32_t HVC_STATUS_EXCEPTION = 0x2763;
static constexpr uint32_t HVC_STATUS_SERVICE_ERR = 0x2764;
static constexpr uint32_t HVC_STATUS_STATUS = 0x2765;
static constexpr uint32_t HVC_STATUS_MESSAGE = 0x2766;
static constexpr uint32_t HVC_STATUS_DELETE = 0x2767;
static constexpr uint32_t HVC_BINDER_DEATH_NEW = 0x2768;
static constexpr uint32_t HVC_BINDER_DEATH_SET_UNLINK = 0x2769;
static constexpr uint32_t HVC_BINDER_DEATH_DELETE = 0x276A;
static constexpr uint32_t HVC_BINDER_MUPLAR_KILL = 0x276B;
static constexpr uint32_t HVC_STATUS_FROM_EXCEPTION = 0x276C;
static constexpr uint32_t HVC_STATUS_FROM_EXCEPTION_MSG = 0x276D;
static constexpr uint32_t HVC_STATUS_FROM_SERVICE = 0x276E;
static constexpr uint32_t HVC_STATUS_FROM_SERVICE_MSG = 0x276F;
static constexpr uint32_t HVC_STATUS_DESCRIPTION = 0x2770;
static constexpr uint32_t HVC_STATUS_DELETE_DESC = 0x2771;
static constexpr uint32_t HVC_BINDER_WEAK_NEW = 0x2772;
static constexpr uint32_t HVC_BINDER_WEAK_DELETE = 0x2773;
static constexpr uint32_t HVC_BINDER_WEAK_PROMOTE = 0x2774;
static constexpr uint32_t HVC_BINDER_SET_EXTENSION = 0x2775;
static constexpr uint32_t HVC_BINDER_GET_EXTENSION = 0x2776;
static constexpr uint32_t HVC_BINDER_LT = 0x2777;
static constexpr uint32_t HVC_BINDER_WEAK_CLONE = 0x2778;
static constexpr uint32_t HVC_BINDER_WEAK_LT = 0x2779;

// libc++ / NDK C++ runtime  : 0x2600–0x26FF
static constexpr uint32_t HVC_CXA_ATEXIT = 0x2600;
static constexpr uint32_t HVC_CXA_FINALIZE = 0x2601;
static constexpr uint32_t HVC_CXA_THROW = 0x2602;
static constexpr uint32_t HVC_CXA_BEGIN_CATCH = 0x2603;
static constexpr uint32_t HVC_CXA_END_CATCH = 0x2604;
static constexpr uint32_t HVC_CXA_RETHROW = 0x2605;
static constexpr uint32_t HVC_CXA_CURRENT_EXCEPTION = 0x2606;
static constexpr uint32_t HVC_CXA_GUARD_ACQUIRE = 0x2607;
static constexpr uint32_t HVC_CXA_GUARD_RELEASE = 0x2608;
static constexpr uint32_t HVC_CXA_GUARD_ABORT = 0x2609;
static constexpr uint32_t HVC_CXA_PURE_VIRTUAL = 0x260A;
static constexpr uint32_t HVC_CXA_DELETED_VIRTUAL = 0x260B;
static constexpr uint32_t HVC_GXX_PERSONALITY = 0x260C;
static constexpr uint32_t HVC_UNWIND_RESUME = 0x260D;
static constexpr uint32_t HVC_OP_NEW = 0x2610;
static constexpr uint32_t HVC_OP_NEW_NOTHROW = 0x2611;
static constexpr uint32_t HVC_OP_NEW_ARRAY = 0x2612;
static constexpr uint32_t HVC_OP_DELETE = 0x2613;
static constexpr uint32_t HVC_OP_DELETE_SIZED = 0x2614;
static constexpr uint32_t HVC_OP_DELETE_ARRAY = 0x2615;
static constexpr uint32_t HVC_STD_TERMINATE = 0x2620;
static constexpr uint32_t HVC_STD_UNEXPECTED = 0x2621;
static constexpr uint32_t HVC_STD_BAD_ALLOC = 0x2622;
static constexpr uint32_t HVC_STD_BAD_CAST = 0x2623;
static constexpr uint32_t HVC_TYPEINFO_NAME = 0x2630;
static constexpr uint32_t HVC_DYNAMIC_CAST = 0x2631;
static constexpr uint32_t HVC_SHARED_COUNT_ADD = 0x2632;
static constexpr uint32_t HVC_SHARED_COUNT_RELEASE = 0x2633;
static constexpr uint32_t HVC_SHARED_WEAK_ADD = 0x2634;
static constexpr uint32_t HVC_SHARED_WEAK_RELEASE_SHARED = 0x2635;
static constexpr uint32_t HVC_SHARED_WEAK_RELEASE_WEAK = 0x2636;
static constexpr uint32_t HVC_SHARED_WEAK_LOCK = 0x2637;
static constexpr uint32_t HVC_SHARED_GET_DELETER = 0x2638;
static constexpr uint32_t HVC_ANDROID_CPUFEATURES = 0x2640;
static constexpr uint32_t HVC_ANDROID_CPUFAMILY = 0x2641;
static constexpr uint32_t HVC_ANDROID_CPUCOUNT = 0x2642;

// ── AArch64 HVC shim stub layout ─────────────────────────────────────────────
// ── Helpers
// ───────────────────────────────────────────────────────────────────

static std::string guest_read_string(guest_t *g, uint64_t gpa)
{
    if (!gpa)
        return {};
    char buf[512] = {};
    guest_read_str(g, gpa, buf, sizeof(buf));
    return {buf};
}

static uint64_t guest_neg_errno(int err)
{
    return static_cast<uint64_t>(-static_cast<int64_t>(err));
}

static uint64_t guest_negative_one()
{
    return static_cast<uint64_t>(static_cast<int64_t>(-1));
}

static bool safe_asset_name(const std::string &name)
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

static std::vector<uint8_t> read_asset_file(const std::filesystem::path &path)
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
        in.read(reinterpret_cast<char *>(data.data()), size);
    if (!in && size > 0)
        return {};
    return data;
}

static std::string format_guest_log(guest_t *g,
                                    const std::string &fmt,
                                    const uint64_t *varargs,
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
        if (spec == '%') {
            out.push_back('%');
            continue;
        }
        int64_t precision = -1;
        if (spec == '.' && i + 2 < fmt.size() && fmt[i + 1] == '*') {
            if (arg >= vararg_count) {
                out += "%.*";
                continue;
            }
            precision = static_cast<int64_t>(varargs[arg++]);
            i += 2;
            spec = fmt[i];
        }
        if (spec == 'l' && i + 1 < fmt.size()) {
            if (fmt[i + 1] == 'l' && i + 2 < fmt.size()) {
                i += 2;
            } else {
                i += 1;
            }
            spec = fmt[i];
        }
        if (arg >= vararg_count) {
            out.push_back('%');
            out.push_back(spec);
            continue;
        }

        uint64_t value = varargs[arg++];
        switch (spec) {
        case 'd':
        case 'i': {
            int64_t signed_value = static_cast<int64_t>(value);
            if ((value & 0xffffffff00000000ULL) == 0 &&
                (value & 0x80000000ULL)) {
                signed_value = static_cast<int32_t>(value);
            }
            std::snprintf(tmp, sizeof(tmp), "%lld", (long long) signed_value);
            out += tmp;
            break;
        }
        case 'u':
            std::snprintf(tmp, sizeof(tmp), "%llu", (unsigned long long) value);
            out += tmp;
            break;
        case 'x':
        case 'X':
            std::snprintf(tmp, sizeof(tmp), "%llx", (unsigned long long) value);
            out += tmp;
            break;
        case 'p':
            std::snprintf(tmp, sizeof(tmp), "0x%llx",
                          (unsigned long long) value);
            out += tmp;
            break;
        case 's': {
            std::string s = guest_read_string(g, value);
            if (precision >= 0 && static_cast<uint64_t>(precision) < s.size()) {
                s.resize(static_cast<size_t>(precision));
            }
            out += s;
            break;
        }
        case 'f':
        case 'g':
        case 'e': {
            // float passed as bits in a uint64 — reinterpret
            float f;
            memcpy(&f, &value, 4);
            std::snprintf(tmp, sizeof(tmp), "%f", (double) f);
            out += tmp;
            break;
        }
        default:
            out.push_back('%');
            out.push_back(spec);
            break;
        }
    }
    return out;
}

// ── AndroidRuntime
// ────────────────────────────────────────────────────────────

AndroidRuntime::AndroidRuntime(guest_t *guest,
                               uint64_t stub_arena_gpa,
                               bool host_window_enabled)
    : GpuBridge(guest, stub_arena_gpa, host_window_enabled)
{
}

AndroidRuntime::~AndroidRuntime() {}

void AndroidRuntime::set_asset_root(std::string asset_root)
{
    asset_root_ = std::move(asset_root);
}

void AndroidRuntime::set_service_socket(std::string service_socket)
{
    service_socket_ = std::move(service_socket);
}

void AndroidRuntime::set_guest_function_invoker(GuestFunctionInvoker invoker)
{
    guest_function_invoker_ = std::move(invoker);
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



void AndroidRuntime::rearm_looper_fd(int32_t fd)
{
    for (auto &reg : looper_regs_) {
        if (reg.fd == fd)
            reg.delivered = false;
    }
}

AndroidRuntime::HostPipe *AndroidRuntime::pipe_for_fd(int32_t fd)
{
    for (auto &pipe : pipes_) {
        if (pipe.read_fd == fd || pipe.write_fd == fd)
            return &pipe;
    }
    return nullptr;
}

const AndroidRuntime::HostPipe *AndroidRuntime::pipe_for_fd(int32_t fd) const
{
    for (const auto &pipe : pipes_) {
        if (pipe.read_fd == fd || pipe.write_fd == fd)
            return &pipe;
    }
    return nullptr;
}

bool AndroidRuntime::looper_fd_ready(int32_t fd) const
{
    const HostPipe *pipe = pipe_for_fd(fd);
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

    for (const auto &host_event : events) {
        uint64_t handle =
            GUEST_INPUT_EVENT_BASE + (next_input_event_handle_++) * 0x100ULL;
        input_events_.push_back({handle, host_event.type, host_event.action,
                                 host_event.source, host_event.device_id,
                                 host_event.key_code, host_event.x,
                                 host_event.y, false, false});
        std::fprintf(stderr,
                     "[InputQueue] host event handle=0x%llx type=%d action=%d "
                     "source=0x%x key=%d x=%.1f y=%.1f\n",
                     (unsigned long long) handle, host_event.type,
                     host_event.action, host_event.source, host_event.key_code,
                     static_cast<double>(host_event.x),
                     static_cast<double>(host_event.y));
    }

    rearm_looper_fd(INPUT_QUEUE_FD);
    return true;
}

bool AndroidRuntime::queue_ready_looper_callbacks()
{
    bool did_work = false;
    for (auto &reg : looper_regs_) {
        if (reg.delivered || !reg.callback)
            continue;

        if (reg.fd == INPUT_QUEUE_FD &&
            (!input_queue_.attached ||
             next_input_event_ >= input_events_.size())) {
            continue;
        }
        if (!looper_fd_ready(reg.fd))
            continue;

        reg.delivered = true;
        pending_looper_callbacks_.push_back(
            {reg.callback, reg.fd, reg.events, reg.data, reg.ident});
        did_work = true;

        std::fprintf(stderr,
                     "[ALooper] queued callback fd=%d ident=%d events=0x%x "
                     "callback=0x%llx data=0x%llx\n",
                     reg.fd, reg.ident, reg.events,
                     (unsigned long long) reg.callback,
                     (unsigned long long) reg.data);
    }
    return did_work;
}



void AndroidRuntime::release_binder_strong(uint64_t handle)
{
    auto it = binder_services_.find(handle);
    if (it == binder_services_.end())
        return;

    BinderService &service = it->second;
    if (service.remote) {
        if (service.ref_count > 1)
            service.ref_count--;
        return;
    }

    if (service.ref_count > 1) {
        service.ref_count--;
        return;
    }

    uint64_t extension_handle = service.extension_handle;
    uint64_t on_destroy = 0;
    uint64_t user_data = service.user_data;
    auto class_it = binder_classes_.find(service.class_handle);
    if (class_it != binder_classes_.end())
        on_destroy = class_it->second.on_destroy;
    if (on_destroy && guest_function_invoker_)
        guest_function_invoker_(on_destroy, {user_data});

    for (const BinderService::DeathLink &link : service.death_links) {
        if (link.on_unlinked && guest_function_invoker_)
            guest_function_invoker_(link.on_unlinked, {link.cookie});
    }

    for (auto name_it = binder_service_by_name_.begin();
         name_it != binder_service_by_name_.end();) {
        if (name_it->second == handle) {
            binder_removed_service_names_.insert(name_it->first);
            name_it = binder_service_by_name_.erase(name_it);
        } else {
            ++name_it;
        }
    }

    binder_services_.erase(it);

    if (extension_handle)
        release_binder_strong(extension_handle);
}

void AndroidRuntime::install()
{
    if (installed_)
        return;

    GpuBridge::install();

    native_window_.bits_gpa = arena_gpa_ + 0x0A0000;
    native_window_.bits_size = static_cast<uint64_t>(MAX_NATIVE_WINDOW_WIDTH) *
                               static_cast<uint64_t>(MAX_NATIVE_WINDOW_HEIGHT) *
                               4;
    input_queue_ = {};
    input_events_ = {
        {GUEST_INPUT_EVENT_BASE + 0x00, 2, 0, 0x1002, 1, 0, 160.0f, 120.0f,
         false, false},
        {GUEST_INPUT_EVENT_BASE + 0x100, 2, 1, 0x1002, 1, 0, 160.0f, 120.0f,
         false, false},
    };
    next_input_event_ = 0;
    current_input_event_ = 0;
    pipes_.clear();
    next_pipe_fd_ = 200;
    pending_frame_callbacks_.clear();
    next_frame_time_nanos_ = 16'666'666ULL;

    register_libc_stubs();
    register_liblog_stubs();
    register_libandroid_stubs();
    register_libjnigraphics_stubs();
    register_libdl_stubs();
    register_libcxx_stubs();
    register_libbinder_stubs();

    std::fprintf(
        stderr,
        "[ART] installed %zu stub sonames, %zu handlers, arena=0x%llx\n",
        sym_tables_.size(), handlers_.size(), (unsigned long long) arena_gpa_);
}

BuiltinSymbols AndroidRuntime::builtin_symbols(const std::string &soname) const
{
    auto s = GpuBridge::builtin_symbols(soname);
    if (!s.empty())
        return s;

    auto it = sym_tables_.find(soname);
    return (it != sym_tables_.end()) ? it->second : BuiltinSymbols{};
}



bool AndroidRuntime::try_dispatch(uint32_t hvc_nr,
                                  const uint64_t regs[8],
                                  uint64_t *x0_out)
{
    // Static range check covers all registered stubs + dynamic proc stubs
    if (hvc_nr < 0x2000 || hvc_nr > 0x2FFF)
        return false;

    auto it = handlers_.find(hvc_nr);
    if (it == handlers_.end()) {
        std::fprintf(stderr, "[ART] unhandled HVC 0x%X\n", hvc_nr);
        *x0_out = 0;
        return true;
    }
    *x0_out = it->second(guest_, regs);
    return true;
}

// ── libc stubs
// ────────────────────────────────────────────────────────────────

void AndroidRuntime::register_libc_stubs()
{
    add("libc.so", "malloc", HVC_MALLOC,
        [this](guest_t *g, const uint64_t a[8]) -> uint64_t {
            uint64_t size = (a[0] + 15) & ~15ULL;
            if (!a[0] || heap_bump_ + size > heap_base_ + HEAP_SIZE)
                return 0;
            uint64_t ptr = heap_bump_;
            heap_bump_ += size;
            std::vector<uint8_t> z(size, 0);
            guest_write(g, ptr, z.data(), size);
            return ptr;
        });

    add("libc.so", "calloc", HVC_CALLOC,
        [this](guest_t *g, const uint64_t a[8]) -> uint64_t {
            uint64_t total = ((a[0] * a[1]) + 15) & ~15ULL;
            if (!total || heap_bump_ + total > heap_base_ + HEAP_SIZE)
                return 0;
            uint64_t ptr = heap_bump_;
            heap_bump_ += total;
            std::vector<uint8_t> z(total, 0);
            guest_write(g, ptr, z.data(), total);
            return ptr;
        });

    add("libc.so", "realloc", HVC_REALLOC,
        [this](guest_t *g, const uint64_t a[8]) -> uint64_t {
            uint64_t size = (a[1] + 15) & ~15ULL;
            if (!size || heap_bump_ + size > heap_base_ + HEAP_SIZE)
                return 0;
            uint64_t ptr = heap_bump_;
            heap_bump_ += size;
            std::vector<uint8_t> buf(size, 0);
            if (a[0] >= heap_base_ && a[0] < heap_bump_) {
                uint64_t copy = std::min(size, ptr - a[0]);
                guest_read(g, a[0], buf.data(), copy);
            }
            guest_write(g, ptr, buf.data(), size);
            return ptr;
        });

    add("libc.so", "free", HVC_FREE,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });

    add("libc.so", "memcpy", HVC_MEMCPY,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            if (!a[0] || !a[1] || !a[2])
                return a[0];
            std::vector<uint8_t> buf(a[2]);
            guest_read(g, a[1], buf.data(), a[2]);
            guest_write(g, a[0], buf.data(), a[2]);
            return a[0];
        });

    add("libc.so", "memmove", HVC_MEMMOVE,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            if (!a[0] || !a[1] || !a[2])
                return a[0];
            std::vector<uint8_t> buf(a[2]);
            guest_read(g, a[1], buf.data(), a[2]);
            guest_write(g, a[0], buf.data(), a[2]);
            return a[0];
        });

    add("libc.so", "memset", HVC_MEMSET,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            if (!a[0] || !a[2])
                return a[0];
            std::vector<uint8_t> buf(a[2], (uint8_t) a[1]);
            guest_write(g, a[0], buf.data(), a[2]);
            return a[0];
        });

    add("libc.so", "memcmp", HVC_MEMCMP,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            if (!a[2])
                return 0;
            std::vector<uint8_t> b1(a[2]), b2(a[2]);
            guest_read(g, a[0], b1.data(), a[2]);
            guest_read(g, a[1], b2.data(), a[2]);
            return (uint64_t) memcmp(b1.data(), b2.data(), a[2]);
        });

    add("libc.so", "strlen", HVC_STRLEN,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            return guest_read_string(g, a[0]).size();
        });

    add("libc.so", "strcmp", HVC_STRCMP,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            return (uint64_t) guest_read_string(g, a[0]).compare(
                guest_read_string(g, a[1]));
        });

    add("libc.so", "strncmp", HVC_STRNCMP,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            auto s1 = guest_read_string(g, a[0]);
            auto s2 = guest_read_string(g, a[1]);
            return (uint64_t) s1.compare(0, a[2], s2, 0, a[2]);
        });

    add("libc.so", "strcpy", HVC_STRCPY,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            auto s = guest_read_string(g, a[1]);
            guest_write(g, a[0], s.c_str(), s.size() + 1);
            return a[0];
        });

    add("libc.so", "strncpy", HVC_STRNCPY,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            auto s = guest_read_string(g, a[1]);
            s.resize(a[2], '\0');
            guest_write(g, a[0], s.c_str(), a[2]);
            return a[0];
        });

    add("libc.so", "strcat", HVC_STRCAT,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            auto d = guest_read_string(g, a[0]);
            d += guest_read_string(g, a[1]);
            guest_write(g, a[0], d.c_str(), d.size() + 1);
            return a[0];
        });

    add("libc.so", "strchr", HVC_STRCHR,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            if (!a[0])
                return 0;
            char needle = static_cast<char>(a[1] & 0xff);
            for (uint64_t i = 0; i < 1024 * 1024; ++i) {
                char ch = 0;
                if (guest_read(g, a[0] + i, &ch, sizeof(ch)) != 0)
                    return 0;
                if (ch == needle)
                    return a[0] + i;
                if (!ch)
                    return 0;
            }
            return 0;
        });

    add("libc.so", "strstr", HVC_STRSTR,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            if (!a[0] || !a[1])
                return 0;
            auto haystack = guest_read_string(g, a[0]);
            auto needle = guest_read_string(g, a[1]);
            if (needle.empty())
                return a[0];
            size_t pos = haystack.find(needle);
            return pos == std::string::npos ? 0 : a[0] + pos;
        });

    add("libc.so", "strlcpy", HVC_STRLCPY,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            auto src = guest_read_string(g, a[1]);
            if (a[0] && a[2]) {
                size_t cap = static_cast<size_t>(
                    std::min<uint64_t>(a[2] - 1, src.size()));
                guest_write(g, a[0], src.data(), cap);
                uint8_t nul = 0;
                guest_write(g, a[0] + cap, &nul, 1);
            }
            return static_cast<uint64_t>(src.size());
        });

    add("libc.so", "strdup", HVC_STRDUP,
        [this](guest_t *g, const uint64_t a[8]) -> uint64_t {
            auto s = guest_read_string(g, a[0]);
            uint64_t sz = (s.size() + 1 + 15) & ~15ULL;
            if (heap_bump_ + sz > heap_base_ + HEAP_SIZE)
                return 0;
            uint64_t ptr = heap_bump_;
            heap_bump_ += sz;
            guest_write(g, ptr, s.c_str(), s.size() + 1);
            return ptr;
        });

    add("libc.so", "strerror", HVC_STRERROR,
        [this](guest_t *g, const uint64_t a[8]) -> uint64_t {
            const char *host = std::strerror(static_cast<int>(a[0]));
            std::string s = host ? host : "unknown error";
            uint64_t sz = (s.size() + 1 + 15) & ~15ULL;
            if (heap_bump_ + sz > heap_base_ + HEAP_SIZE)
                return 0;
            uint64_t ptr = heap_bump_;
            heap_bump_ += sz;
            guest_write(g, ptr, s.c_str(), s.size() + 1);
            return ptr;
        });

    add("libc.so", "printf", HVC_PRINTF,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            auto fmt = guest_read_string(g, a[0]);
            std::fprintf(stderr, "[guest printf] %s\n", fmt.c_str());
            return (uint64_t) fmt.size();
        });

    add("libc.so", "fprintf", HVC_FPRINTF,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            auto fmt = guest_read_string(g, a[1]);
            auto msg = format_guest_log(g, fmt, a + 2, 6);
            std::fprintf(stderr, "[guest fprintf] %s\n", msg.c_str());
            return static_cast<uint64_t>(msg.size());
        });

    add("libc.so", "vfprintf", HVC_VFPRINTF,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            auto fmt = guest_read_string(g, a[1]);
            std::fprintf(stderr, "[guest vfprintf] %s\n", fmt.c_str());
            return static_cast<uint64_t>(fmt.size());
        });

    add("libc.so", "fflush", HVC_FFLUSH,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });

    add("libc.so", "fwrite", HVC_FWRITE,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            uint64_t ptr = a[0];
            uint64_t size = a[1];
            uint64_t nmemb = a[2];
            if (!ptr || !size || !nmemb)
                return 0;
            uint64_t total = size * nmemb;
            if (size != 0 && total / size != nmemb)
                return 0;
            uint64_t preview = std::min<uint64_t>(total, 256);
            if (preview) {
                std::vector<uint8_t> bytes(static_cast<size_t>(preview));
                if (guest_read(g, ptr, bytes.data(), preview) == 0) {
                    std::fprintf(stderr, "[guest fwrite] ");
                    std::fwrite(bytes.data(), 1, bytes.size(), stderr);
                    if (preview < total)
                        std::fprintf(stderr, "...");
                    std::fprintf(stderr, "\n");
                }
            }
            return nmemb;
        });

    add("libc.so", "vasprintf", HVC_VASPRINTF,
        [this](guest_t *g, const uint64_t a[8]) -> uint64_t {
            if (!a[0] || !a[1])
                return guest_negative_one();
            auto fmt = guest_read_string(g, a[1]);
            uint64_t size = (fmt.size() + 1 + 15) & ~15ULL;
            if (!size || heap_bump_ + size > heap_base_ + HEAP_SIZE)
                return guest_negative_one();
            uint64_t ptr = heap_bump_;
            heap_bump_ += size;
            guest_write(g, ptr, fmt.c_str(), fmt.size() + 1);
            guest_write_u64(g, a[0], ptr);
            return static_cast<uint64_t>(fmt.size());
        });

    add("libc.so", "snprintf", HVC_SNPRINTF,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            auto fmt = guest_read_string(g, a[2]);
            auto msg = format_guest_log(g, fmt, a + 3, 5);
            if (a[0] && a[1]) {
                size_t cap = static_cast<size_t>(
                    std::min<uint64_t>(a[1] - 1, msg.size()));
                guest_write(g, a[0], msg.data(), cap);
                uint8_t nul = 0;
                guest_write(g, a[0] + cap, &nul, 1);
            }
            return static_cast<uint64_t>(msg.size());
        });

    add("libc.so", "vsnprintf", HVC_VSNPRINTF,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            auto fmt = guest_read_string(g, a[2]);
            if (a[0] && a[1]) {
                size_t cap = static_cast<size_t>(
                    std::min<uint64_t>(a[1] - 1, fmt.size()));
                guest_write(g, a[0], fmt.data(), cap);
                uint8_t nul = 0;
                guest_write(g, a[0] + cap, &nul, 1);
            }
            return static_cast<uint64_t>(fmt.size());
        });

    add("libc.so", "sprintf", HVC_SPRINTF,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            auto fmt = guest_read_string(g, a[1]);
            auto msg = format_guest_log(g, fmt, a + 2, 6);
            if (a[0])
                guest_write(g, a[0], msg.c_str(), msg.size() + 1);
            return static_cast<uint64_t>(msg.size());
        });

    add("libc.so", "__errno", HVC_ERRNO,
        [this](guest_t *g, const uint64_t[8]) -> uint64_t {
            uint64_t errno_gpa = arena_gpa_ + 0x080800;
            uint32_t zero = 0;
            guest_write(g, errno_gpa, &zero, sizeof(zero));
            return errno_gpa;
        });

    add("libc.so", "posix_memalign", HVC_POSIX_MEMALIGN,
        [this](guest_t *g, const uint64_t a[8]) -> uint64_t {
            if (!a[0] || !a[1])
                return 22;  // EINVAL
            uint64_t align = std::max<uint64_t>(a[1], 16);
            uint64_t ptr = (heap_bump_ + align - 1) & ~(align - 1);
            uint64_t size = (a[2] + 15) & ~15ULL;
            if (ptr + size > heap_base_ + HEAP_SIZE)
                return 12;  // ENOMEM
            heap_bump_ = ptr + size;
            std::vector<uint8_t> z(size, 0);
            if (size)
                guest_write(g, ptr, z.data(), size);
            guest_write_u64(g, a[0], ptr);
            return 0;
        });

    add("libc.so", "memchr", HVC_MEMCHR,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            if (!a[0] || !a[2])
                return 0;
            size_t n =
                static_cast<size_t>(std::min<uint64_t>(a[2], 1024 * 1024));
            std::vector<uint8_t> buf(n);
            if (guest_read(g, a[0], buf.data(), n) != 0)
                return 0;
            uint8_t needle = static_cast<uint8_t>(a[1]);
            for (size_t i = 0; i < buf.size(); ++i)
                if (buf[i] == needle)
                    return a[0] + i;
            return 0;
        });

    add("libc.so", "isatty", HVC_ISATTY,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            int32_t fd = static_cast<int32_t>(a[0]);
            if (fd >= 0 && fd <= 2)
                return ::isatty(fd) ? 1 : 0;
            return 0;
        });
    add("libc.so", "fileno", HVC_FILENO,
        [](guest_t *, const uint64_t[8]) -> uint64_t {
            return guest_negative_one();
        });
    add("libc.so", "getauxval", HVC_GETAUXVAL,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });
    add("libc.so", "localeconv", HVC_LOCALECONV,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });
    add("libc.so", "__ctype_get_mb_cur_max", HVC_CTYPE_MB_CUR_MAX,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 1; });

    add("libc.so", "fputc", HVC_FPUTC,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            uint8_t c = static_cast<uint8_t>(a[0]);
            std::fwrite(&c, 1, 1, stderr);
            return a[0] & 0xff;
        });
    add("libc.so", "getc", HVC_GETC,
        [](guest_t *, const uint64_t[8]) -> uint64_t {
            return guest_negative_one();
        });
    add("libc.so", "ungetc", HVC_UNGETC,
        [](guest_t *, const uint64_t a[8]) -> uint64_t { return a[0] & 0xff; });

    add("libc.so", "strtol", HVC_STRTOL,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            auto s = guest_read_string(g, a[0]);
            char *end = nullptr;
            long value = std::strtol(s.c_str(), &end, static_cast<int>(a[2]));
            if (a[1])
                guest_write_u64(g, a[1],
                                a[0] + static_cast<uint64_t>(end - s.c_str()));
            return static_cast<uint64_t>(value);
        });
    add("libc.so", "strtoll", HVC_STRTOL,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            auto s = guest_read_string(g, a[0]);
            char *end = nullptr;
            long long value =
                std::strtoll(s.c_str(), &end, static_cast<int>(a[2]));
            if (a[1])
                guest_write_u64(g, a[1],
                                a[0] + static_cast<uint64_t>(end - s.c_str()));
            return static_cast<uint64_t>(value);
        });
    add("libc.so", "strtoul", HVC_STRTOL,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            auto s = guest_read_string(g, a[0]);
            char *end = nullptr;
            unsigned long value =
                std::strtoul(s.c_str(), &end, static_cast<int>(a[2]));
            if (a[1])
                guest_write_u64(g, a[1],
                                a[0] + static_cast<uint64_t>(end - s.c_str()));
            return static_cast<uint64_t>(value);
        });
    add("libc.so", "strtoull", HVC_STRTOL,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            auto s = guest_read_string(g, a[0]);
            char *end = nullptr;
            unsigned long long value =
                std::strtoull(s.c_str(), &end, static_cast<int>(a[2]));
            if (a[1])
                guest_write_u64(g, a[1],
                                a[0] + static_cast<uint64_t>(end - s.c_str()));
            return static_cast<uint64_t>(value);
        });

    add("libc.so", "strtod", HVC_STRTOD,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            auto s = guest_read_string(g, a[0]);
            if (a[1])
                guest_write_u64(g, a[1], a[0]);
            (void) s;
            return 0;
        });
    add("libc.so", "strtof", HVC_STRTOD,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            if (a[1])
                guest_write_u64(g, a[1], a[0]);
            return 0;
        });

    add("libc.so", "wcslen", HVC_WCSLEN,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            if (!a[0])
                return 0;
            uint64_t p = a[0];
            uint32_t ch = 0;
            uint64_t len = 0;
            while (len < 1024 * 1024) {
                guest_read(g, p, &ch, sizeof(ch));
                if (!ch)
                    break;
                p += sizeof(ch);
                ++len;
            }
            return len;
        });
    add("libc.so", "wmemchr", HVC_WMEMCHR,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            if (!a[0] || !a[2])
                return 0;
            uint32_t needle = static_cast<uint32_t>(a[1]);
            uint64_t count = std::min<uint64_t>(a[2], 1024 * 1024);
            for (uint64_t i = 0; i < count; ++i) {
                uint32_t ch = 0;
                guest_read(g, a[0] + i * 4, &ch, sizeof(ch));
                if (ch == needle)
                    return a[0] + i * 4;
            }
            return 0;
        });
    add("libc.so", "wmemcmp", HVC_WMEMCMP,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            for (uint64_t i = 0; i < a[2]; ++i) {
                uint32_t lhs = 0, rhs = 0;
                guest_read(g, a[0] + i * 4, &lhs, sizeof(lhs));
                guest_read(g, a[1] + i * 4, &rhs, sizeof(rhs));
                if (lhs != rhs)
                    return lhs < rhs ? guest_negative_one() : 1;
            }
            return 0;
        });

    auto ascii_lower = [](uint64_t c) -> bool { return c >= 'a' && c <= 'z'; };
    auto ascii_upper = [](uint64_t c) -> bool { return c >= 'A' && c <= 'Z'; };
    auto ascii_digit = [](uint64_t c) -> bool { return c >= '0' && c <= '9'; };
    auto ascii_alpha = [ascii_lower, ascii_upper](uint64_t c) -> bool {
        return ascii_lower(c) || ascii_upper(c);
    };
    auto ascii_xdigit = [ascii_digit](uint64_t c) -> bool {
        return ascii_digit(c) || (c >= 'a' && c <= 'f') ||
               (c >= 'A' && c <= 'F');
    };
    auto ascii_print = [](uint64_t c) -> bool { return c >= 0x20 && c < 0x7f; };
    auto ascii_space = [](uint64_t c) -> bool {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
               c == '\v';
    };

    add("libc.so", "__memcpy_chk", HVC_MEMCPY_CHK,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            if (!a[0] || !a[1] || !a[2])
                return a[0];
            uint64_t n = a[2];
            if (a[3])
                n = std::min<uint64_t>(n, a[3]);
            n = std::min<uint64_t>(n, 1024 * 1024);
            std::vector<uint8_t> buf(static_cast<size_t>(n));
            guest_read(g, a[1], buf.data(), n);
            guest_write(g, a[0], buf.data(), n);
            return a[0];
        });
    add("libc.so", "__read_chk", HVC_READ_CHK,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            uint64_t n = a[2];
            if (a[3])
                n = std::min<uint64_t>(n, a[3]);
            n = std::min<uint64_t>(n, 1024 * 1024);
            if (a[1] && n) {
                std::vector<uint8_t> zero(static_cast<size_t>(n), 0);
                guest_write(g, a[1], zero.data(), n);
            }
            return 0;
        });
    add("libc.so", "__memmove_chk", HVC_MEMMOVE_CHK,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            if (!a[0] || !a[1] || !a[2])
                return a[0];
            uint64_t n = a[2];
            if (a[3])
                n = std::min<uint64_t>(n, a[3]);
            n = std::min<uint64_t>(n, 1024 * 1024);
            std::vector<uint8_t> buf(static_cast<size_t>(n));
            guest_read(g, a[1], buf.data(), n);
            guest_write(g, a[0], buf.data(), n);
            return a[0];
        });
    add("libc.so", "__strlen_chk", HVC_STRLEN_CHK,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            if (!a[0])
                return 0;
            uint64_t limit =
                std::min<uint64_t>(a[1] ? a[1] : 1024 * 1024, 1024 * 1024);
            for (uint64_t i = 0; i < limit; ++i) {
                char ch = 0;
                guest_read(g, a[0] + i, &ch, sizeof(ch));
                if (!ch)
                    return i;
            }
            return limit;
        });
    add("libc.so", "__strncpy_chk", HVC_STRNCPY_CHK,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            if (!a[0] || !a[1] || !a[2])
                return a[0];
            uint64_t n = std::min<uint64_t>(a[2], 1024 * 1024);
            if (a[3])
                n = std::min<uint64_t>(n, a[3]);
            std::vector<char> out(static_cast<size_t>(n), '\0');
            for (uint64_t i = 0; i < n; ++i) {
                char ch = 0;
                guest_read(g, a[1] + i, &ch, sizeof(ch));
                out[static_cast<size_t>(i)] = ch;
                if (!ch)
                    break;
            }
            guest_write(g, a[0], out.data(), n);
            return a[0];
        });
    add("libc.so", "__strncpy_chk2", HVC_STRNCPY_CHK2,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            if (!a[0] || !a[1] || !a[2])
                return a[0];
            uint64_t n = std::min<uint64_t>(a[2], 1024 * 1024);
            if (a[3])
                n = std::min<uint64_t>(n, a[3]);
            if (a[4])
                n = std::min<uint64_t>(n, a[4]);
            std::vector<char> out(static_cast<size_t>(n), '\0');
            for (uint64_t i = 0; i < n; ++i) {
                char ch = 0;
                guest_read(g, a[1] + i, &ch, sizeof(ch));
                out[static_cast<size_t>(i)] = ch;
                if (!ch)
                    break;
            }
            guest_write(g, a[0], out.data(), n);
            return a[0];
        });
    add("libc.so", "__strrchr_chk", HVC_STRRCHR_CHK,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            if (!a[0])
                return 0;
            uint64_t limit =
                std::min<uint64_t>(a[2] ? a[2] : 1024 * 1024, 1024 * 1024);
            uint64_t last = 0;
            char needle = static_cast<char>(a[1] & 0xff);
            for (uint64_t i = 0; i < limit; ++i) {
                char ch = 0;
                guest_read(g, a[0] + i, &ch, sizeof(ch));
                if (ch == needle)
                    last = a[0] + i;
                if (!ch)
                    break;
            }
            return last;
        });
    add("libc.so", "__vsnprintf_chk", HVC_VSNPRINTF_CHK,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            auto fmt = guest_read_string(g, a[4]);
            if (a[0] && a[1]) {
                size_t cap = static_cast<size_t>(
                    std::min<uint64_t>(a[1] - 1, fmt.size()));
                guest_write(g, a[0], fmt.data(), cap);
                uint8_t nul = 0;
                guest_write(g, a[0] + cap, &nul, 1);
            }
            return static_cast<uint64_t>(fmt.size());
        });

    add("libc.so", "islower", HVC_ISLOWER,
        [ascii_lower](guest_t *, const uint64_t a[8]) -> uint64_t {
            return ascii_lower(a[0]) ? 1 : 0;
        });
    add("libc.so", "isupper", HVC_ISUPPER,
        [ascii_upper](guest_t *, const uint64_t a[8]) -> uint64_t {
            return ascii_upper(a[0]) ? 1 : 0;
        });
    add("libc.so", "isxdigit", HVC_ISXDIGIT,
        [ascii_xdigit](guest_t *, const uint64_t a[8]) -> uint64_t {
            return ascii_xdigit(a[0]) ? 1 : 0;
        });
    add("libc.so", "tolower", HVC_TOLOWER,
        [ascii_upper](guest_t *, const uint64_t a[8]) -> uint64_t {
            return ascii_upper(a[0]) ? a[0] + 32 : a[0];
        });
    add("libc.so", "toupper", HVC_TOUPPER,
        [ascii_lower](guest_t *, const uint64_t a[8]) -> uint64_t {
            return ascii_lower(a[0]) ? a[0] - 32 : a[0];
        });

    add("libc.so", "iswalpha", HVC_ISWALPHA,
        [ascii_alpha](guest_t *, const uint64_t a[8]) -> uint64_t {
            return ascii_alpha(a[0]) ? 1 : 0;
        });
    add("libc.so", "iswblank", HVC_ISWBLANK,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            return (a[0] == ' ' || a[0] == '\t') ? 1 : 0;
        });
    add("libc.so", "iswcntrl", HVC_ISWCNTRL,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            return (a[0] < 0x20 || a[0] == 0x7f) ? 1 : 0;
        });
    add("libc.so", "iswdigit", HVC_ISWDIGIT,
        [ascii_digit](guest_t *, const uint64_t a[8]) -> uint64_t {
            return ascii_digit(a[0]) ? 1 : 0;
        });
    add("libc.so", "iswlower", HVC_ISWLOWER,
        [ascii_lower](guest_t *, const uint64_t a[8]) -> uint64_t {
            return ascii_lower(a[0]) ? 1 : 0;
        });
    add("libc.so", "iswprint", HVC_ISWPRINT,
        [ascii_print](guest_t *, const uint64_t a[8]) -> uint64_t {
            return ascii_print(a[0]) ? 1 : 0;
        });
    add("libc.so", "iswpunct", HVC_ISWPUNCT,
        [ascii_alpha, ascii_digit, ascii_print, ascii_space](
            guest_t *, const uint64_t a[8]) -> uint64_t {
            return ascii_print(a[0]) && !ascii_alpha(a[0]) &&
                   !ascii_digit(a[0]) && !ascii_space(a[0]);
        });
    add("libc.so", "iswspace", HVC_ISWSPACE,
        [ascii_space](guest_t *, const uint64_t a[8]) -> uint64_t {
            return ascii_space(a[0]) ? 1 : 0;
        });
    add("libc.so", "iswupper", HVC_ISWUPPER,
        [ascii_upper](guest_t *, const uint64_t a[8]) -> uint64_t {
            return ascii_upper(a[0]) ? 1 : 0;
        });
    add("libc.so", "iswxdigit", HVC_ISWXDIGIT,
        [ascii_xdigit](guest_t *, const uint64_t a[8]) -> uint64_t {
            return ascii_xdigit(a[0]) ? 1 : 0;
        });
    add("libc.so", "towlower", HVC_TOWLOWER,
        [ascii_upper](guest_t *, const uint64_t a[8]) -> uint64_t {
            return ascii_upper(a[0]) ? a[0] + 32 : a[0];
        });
    add("libc.so", "towupper", HVC_TOWUPPER,
        [ascii_lower](guest_t *, const uint64_t a[8]) -> uint64_t {
            return ascii_lower(a[0]) ? a[0] - 32 : a[0];
        });

    add("libc.so", "strcoll", HVC_STRCOLL,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            return static_cast<uint64_t>(
                guest_read_string(g, a[0]).compare(guest_read_string(g, a[1])));
        });
    add("libc.so", "strxfrm", HVC_STRXFRM,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            auto src = guest_read_string(g, a[1]);
            if (a[0] && a[2]) {
                size_t cap = static_cast<size_t>(
                    std::min<uint64_t>(a[2] - 1, src.size()));
                guest_write(g, a[0], src.data(), cap);
                uint8_t nul = 0;
                guest_write(g, a[0] + cap, &nul, 1);
            }
            return static_cast<uint64_t>(src.size());
        });
    add("libc.so", "strftime", HVC_STRFTIME,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            if (!a[0] || !a[1] || !a[2] || !a[3])
                return 0;
            std::string fmt = guest_read_string(g, a[2]);
            int32_t fields[9] = {};
            guest_read(g, a[3], fields, sizeof(fields));
            std::tm tm{};
            tm.tm_sec = fields[0];
            tm.tm_min = fields[1];
            tm.tm_hour = fields[2];
            tm.tm_mday = fields[3];
            tm.tm_mon = fields[4];
            tm.tm_year = fields[5];
            tm.tm_wday = fields[6];
            tm.tm_yday = fields[7];
            tm.tm_isdst = fields[8];
            size_t max = static_cast<size_t>(std::min<uint64_t>(a[1], 4096));
            std::vector<char> out(max, '\0');
            size_t n = std::strftime(out.data(), out.size(), fmt.c_str(), &tm);
            if (n)
                guest_write(g, a[0], out.data(), n + 1);
            return static_cast<uint64_t>(n);
        });

    auto guest_wcslen32 = [](guest_t *g, uint64_t ptr) -> uint64_t {
        if (!ptr)
            return 0;
        for (uint64_t i = 0; i < 1024 * 1024; ++i) {
            uint32_t ch = 0;
            guest_read(g, ptr + i * 4, &ch, sizeof(ch));
            if (!ch)
                return i;
        }
        return 1024 * 1024;
    };

    add("libc.so", "wcscoll", HVC_WCSCOLL,
        [guest_wcslen32](guest_t *g, const uint64_t a[8]) -> uint64_t {
            uint64_t n = std::min<uint64_t>(guest_wcslen32(g, a[0]),
                                            guest_wcslen32(g, a[1]));
            for (uint64_t i = 0; i < n; ++i) {
                uint32_t lhs = 0, rhs = 0;
                guest_read(g, a[0] + i * 4, &lhs, sizeof(lhs));
                guest_read(g, a[1] + i * 4, &rhs, sizeof(rhs));
                if (lhs != rhs)
                    return lhs < rhs ? guest_negative_one() : 1;
            }
            uint64_t lhs_len = guest_wcslen32(g, a[0]);
            uint64_t rhs_len = guest_wcslen32(g, a[1]);
            if (lhs_len == rhs_len)
                return 0;
            return lhs_len < rhs_len ? guest_negative_one() : 1;
        });
    add("libc.so", "wcsxfrm", HVC_WCSXFRM,
        [guest_wcslen32](guest_t *g, const uint64_t a[8]) -> uint64_t {
            uint64_t len = guest_wcslen32(g, a[1]);
            if (a[0] && a[2]) {
                uint64_t count = std::min<uint64_t>(a[2] - 1, len);
                std::vector<uint8_t> buf(static_cast<size_t>(count * 4));
                if (count)
                    guest_read(g, a[1], buf.data(), count * 4);
                guest_write(g, a[0], buf.data(), count * 4);
                uint32_t nul = 0;
                guest_write(g, a[0] + count * 4, &nul, sizeof(nul));
            }
            return len;
        });
    add("libc.so", "wmemcpy", HVC_WMEMCPY,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            if (!a[0] || !a[1] || !a[2])
                return a[0];
            uint64_t count = std::min<uint64_t>(a[2], 1024 * 1024);
            std::vector<uint8_t> buf(static_cast<size_t>(count * 4));
            guest_read(g, a[1], buf.data(), count * 4);
            guest_write(g, a[0], buf.data(), count * 4);
            return a[0];
        });
    add("libc.so", "wmemmove", HVC_WMEMMOVE,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            if (!a[0] || !a[1] || !a[2])
                return a[0];
            uint64_t count = std::min<uint64_t>(a[2], 1024 * 1024);
            std::vector<uint8_t> buf(static_cast<size_t>(count * 4));
            guest_read(g, a[1], buf.data(), count * 4);
            guest_write(g, a[0], buf.data(), count * 4);
            return a[0];
        });
    add("libc.so", "wmemset", HVC_WMEMSET,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            if (!a[0] || !a[2])
                return a[0];
            uint64_t count = std::min<uint64_t>(a[2], 1024 * 1024);
            std::vector<uint32_t> buf(static_cast<size_t>(count),
                                      static_cast<uint32_t>(a[1]));
            guest_write(g, a[0], buf.data(), count * 4);
            return a[0];
        });

    add("libc.so", "prctl", HVC_PRCTL,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });
    add("libc.so", "__sF", HVC_SF,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });

    auto libc_ret0 = [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; };
    auto libc_ret_neg1 = [](guest_t *, const uint64_t[8]) -> uint64_t {
        return guest_negative_one();
    };
    auto libc_ret_null = [](guest_t *, const uint64_t[8]) -> uint64_t {
        return 0;
    };

    add("libc.so", "__open_2", HVC_OPEN_2,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            std::fprintf(stderr, "[ART] __open_2(%s, flags=0x%llx) -> ENOENT\n",
                         guest_read_string(g, a[0]).c_str(),
                         (unsigned long long) a[1]);
            return guest_negative_one();
        });
    add("libc.so", "mmap", HVC_MMAP,
        [this](guest_t *g, const uint64_t a[8]) -> uint64_t {
            uint64_t size = (a[1] + 4095) & ~4095ULL;
            if (!size || heap_bump_ + size > heap_base_ + HEAP_SIZE)
                return guest_negative_one();
            uint64_t ptr = (heap_bump_ + 4095) & ~4095ULL;
            if (ptr + size > heap_base_ + HEAP_SIZE)
                return guest_negative_one();
            heap_bump_ = ptr + size;
            std::vector<uint8_t> zero(static_cast<size_t>(size), 0);
            guest_write(g, ptr, zero.data(), size);
            return ptr;
        });
    add("libc.so", "mprotect", HVC_MPROTECT,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });
    add("libc.so", "munmap", HVC_MUNMAP,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });
    add("libc.so", "sched_setaffinity", HVC_SCHED_SETAFFINITY,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });
    add("libc.so", "setpriority", HVC_SETPRIORITY,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });

    for (const char *symbol : {"btowc",
                               "closedir",
                               "fclose",
                               "freelocale",
                               "fputwc",
                               "fread",
                               "fseek",
                               "fseeko",
                               "ftruncate",
                               "getwc",
                               "iswalpha_l",
                               "iswblank_l",
                               "iswcntrl_l",
                               "iswdigit_l",
                               "iswlower_l",
                               "iswprint_l",
                               "iswpunct_l",
                               "iswspace_l",
                               "iswupper_l",
                               "iswxdigit_l",
                               "mbrlen",
                               "mbrtowc",
                               "mbsnrtowcs",
                               "mbsrtowcs",
                               "mbtowc",
                               "pthread_cond_timedwait",
                               "pthread_key_delete",
                               "pthread_mutex_trylock",
                               "pthread_mutexattr_destroy",
                               "pthread_mutexattr_init",
                               "pthread_mutexattr_settype",
                               "setbuf",
                               "strcoll_l",
                               "sscanf",
                               "strerror_r",
                               "strftime_l",
                               "strtold",
                               "strtold_l",
                               "strtoll_l",
                               "strtoull_l",
                               "strxfrm_l",
                               "swprintf",
                               "towlower_l",
                               "towupper_l",
                               "ungetwc",
                               "uselocale",
                               "vsscanf",
                               "wcrtomb",
                               "wcscoll_l",
                               "wcsnrtombs",
                               "wcstod",
                               "wcstof",
                               "wcstol",
                               "wcstold",
                               "wcstoll",
                               "wcstoul",
                               "wcstoull",
                               "wcsxfrm_l",
                               "wctob"}) {
        add("libc.so", symbol, HVC_LIBC_RET0, libc_ret0);
    }

    for (const char *symbol :
         {"chdir",     "fchmod",   "fchmodat", "fstat",    "ftello",
          "ftruncate", "ioctl",    "link",     "lstat",    "mkdir",
          "open",      "openat",   "pathconf", "readlink", "remove",
          "rename",    "sendfile", "stat",     "statvfs",  "symlink",
          "sysconf",   "truncate", "unlinkat", "utimensat"}) {
        add("libc.so", symbol, HVC_LIBC_RET_NEG1, libc_ret_neg1);
    }

    for (const char *symbol :
         {"fdopen", "fdopendir", "fopen", "getcwd", "newlocale", "opendir",
          "readdir", "realpath", "setlocale"}) {
        add("libc.so", symbol, HVC_LIBC_RET_NULL, libc_ret_null);
    }

    add("libc.so", "syscall", HVC_SYSCALL,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            std::fprintf(
                stderr,
                "[ART] libc syscall(%llu) unsupported via direct import\n",
                (unsigned long long) a[0]);
            return guest_negative_one();
        });

    add("libc.so", "openlog", HVC_OPENLOG,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            std::fprintf(stderr, "[guest openlog] %s\n",
                         guest_read_string(g, a[0]).c_str());
            return 0;
        });
    add("libc.so", "syslog", HVC_SYSLOG,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            auto fmt = guest_read_string(g, a[1]);
            auto msg = format_guest_log(g, fmt, a + 2, 6);
            std::fprintf(stderr, "[guest syslog] %s\n", msg.c_str());
            return 0;
        });
    add("libc.so", "closelog", HVC_CLOSELOG,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });

    add("libc.so", "android_set_abort_message", HVC_ANDROID_ABORT_MESSAGE,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            std::fprintf(stderr, "[ART] android_set_abort_message(%s)\n",
                         guest_read_string(g, a[0]).c_str());
            return 0;
        });

    add("libc.so", "__assert", HVC_ASSERT,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            std::fprintf(stderr, "[ART] __assert failed: %s:%llu %s\n",
                         guest_read_string(g, a[0]).c_str(),
                         (unsigned long long) a[1],
                         guest_read_string(g, a[2]).c_str());
            ::abort();
            return 0;
        });

    add("libc.so", "__assert2", HVC_ASSERT2,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            std::fprintf(stderr, "[ART] __assert2 failed: %s:%llu %s %s\n",
                         guest_read_string(g, a[0]).c_str(),
                         (unsigned long long) a[1],
                         guest_read_string(g, a[2]).c_str(),
                         guest_read_string(g, a[3]).c_str());
            ::abort();
            return 0;
        });

    add("libc.so", "__stack_chk_fail", HVC_STACK_CHK_FAIL,
        [](guest_t *, const uint64_t[8]) -> uint64_t {
            std::fprintf(stderr, "[ART] __stack_chk_fail - aborting\n");
            ::abort();
            return 0;
        });

    add("libc.so", "abort", HVC_ABORT,
        [](guest_t *, const uint64_t[8]) -> uint64_t {
            std::fprintf(stderr, "[ART] guest called abort()\n");
            ::abort();
            return 0;
        });

    add("libc.so", "exit", HVC_EXIT,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            std::fprintf(stderr, "[ART] guest called exit(%lld)\n",
                         (long long) a[0]);
            ::exit((int) a[0]);
            return 0;
        });

    add("libc.so", "getpid", HVC_GETPID,
        [](guest_t *, const uint64_t[8]) -> uint64_t {
            return static_cast<uint64_t>(::getpid());
        });
    add("libc.so", "gettid", HVC_GETTID,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 1; });

    add("libc.so", "getenv", HVC_GETENV_LIBC,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });

    add("libc.so", "__register_atfork", HVC_REGISTER_ATFORK,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });

    add("libc.so", "pthread_create", HVC_PTHREAD_CREATE,
        [this](guest_t *g, const uint64_t a[8]) -> uint64_t {
            constexpr uint64_t kGuestThreadStackSize = 64 * 1024;
            uint64_t h = next_thread_handle_++;
            uint64_t stack_base = (heap_bump_ + 15) & ~15ULL;
            uint64_t stack_top = stack_base + kGuestThreadStackSize;
            if (stack_top > heap_base_ + HEAP_SIZE)
                return 11;  // EAGAIN

            heap_bump_ = stack_top;
            std::vector<uint8_t> zero(kGuestThreadStackSize, 0);
            guest_write(g, stack_base, zero.data(), zero.size());

            if (a[0])
                guest_write_u64(g, a[0], h);
            threads_[h] = {stack_base, kGuestThreadStackSize};
            pending_pthreads_.push_back({h, a[2], a[3], stack_top});
            std::fprintf(stderr,
                         "[ART] pthread_create fn=0x%llx arg=0x%llx "
                         "handle=0x%llx stack=0x%llx..0x%llx (queued)\n",
                         (unsigned long long) a[2], (unsigned long long) a[3],
                         (unsigned long long) h,
                         (unsigned long long) stack_base,
                         (unsigned long long) stack_top);
            return 0;
        });

    add("libc.so", "pthread_join", HVC_PTHREAD_JOIN,
        [this](guest_t *g, const uint64_t a[8]) -> uint64_t {
            auto ret = pthread_returns_.find(a[0]);
            if (ret != pthread_returns_.end()) {
                if (a[1])
                    guest_write_u64(g, a[1], ret->second);
            } else if (a[1]) {
                pending_pthread_join_retvals_[a[0]] = a[1];
            }
            std::fprintf(stderr, "[ART] pthread_join handle=0x%llx\n",
                         (unsigned long long) a[0]);
            return 0;
        });

    add("libc.so", "pthread_detach", HVC_PTHREAD_DETACH,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            std::fprintf(stderr, "[ART] pthread_detach handle=0x%llx\n",
                         (unsigned long long) a[0]);
            return 0;
        });

    add("libc.so", "pthread_attr_init", HVC_PTHREAD_ATTR_INIT,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });
    add("libc.so", "pthread_attr_destroy", HVC_PTHREAD_ATTR_DESTROY,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });
    add("libc.so", "pthread_attr_setdetachstate", HVC_PTHREAD_ATTR_SETDETACH,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });

    add("libc.so", "pthread_cond_init", HVC_PTHREAD_COND_INIT,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });
    add("libc.so", "pthread_cond_wait", HVC_PTHREAD_COND_WAIT,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });
    add("libc.so", "pthread_cond_signal", HVC_PTHREAD_COND_SIGNAL,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });
    add("libc.so", "pthread_cond_broadcast", HVC_PTHREAD_COND_BCAST,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });
    add("libc.so", "pthread_cond_destroy", HVC_PTHREAD_COND_DESTROY,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });

    add("libc.so", "pthread_self", HVC_PTHREAD_SELF,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 1; });
    add("libc.so", "pthread_equal", HVC_PTHREAD_EQUAL,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            return a[0] == a[1] ? 1 : 0;
        });
    add("libc.so", "pthread_setname_np", HVC_PTHREAD_SETNAME_NP,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            std::fprintf(
                stderr, "[ART] pthread_setname_np thread=0x%llx name=%s\n",
                (unsigned long long) a[0], guest_read_string(g, a[1]).c_str());
            return 0;
        });
    add("libc.so", "pthread_mutex_init", HVC_PTHREAD_MUTEX_INIT,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });
    add("libc.so", "pthread_mutex_lock", HVC_PTHREAD_MUTEX_LOCK,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });
    add("libc.so", "pthread_mutex_unlock", HVC_PTHREAD_MUTEX_UNLOCK,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });
    add("libc.so", "pthread_mutex_destroy", HVC_PTHREAD_MUTEX_DESTROY,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });
    add("libc.so", "pthread_rwlock_init", HVC_PTHREAD_RWLOCK_INIT,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });
    add("libc.so", "pthread_rwlock_rdlock", HVC_PTHREAD_RWLOCK_RDLOCK,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });
    add("libc.so", "pthread_rwlock_wrlock", HVC_PTHREAD_RWLOCK_WRLOCK,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });
    add("libc.so", "pthread_rwlock_unlock", HVC_PTHREAD_RWLOCK_UNLOCK,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });
    add("libc.so", "pthread_rwlock_destroy", HVC_PTHREAD_RWLOCK_DESTROY,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });

    add("libc.so", "pthread_once", HVC_PTHREAD_ONCE,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            if (!guest_read_u64(g, a[0]))
                guest_write_u64(g, a[0], 1);
            return 0;
        });

    add("libc.so", "pthread_key_create", HVC_PTHREAD_KEY_CREATE,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            guest_write_u64(g, a[0], 1);
            return 0;
        });
    add("libc.so", "pthread_getspecific", HVC_PTHREAD_GETSPECIFIC,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });
    add("libc.so", "pthread_setspecific", HVC_PTHREAD_SETSPECIFIC,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });

    add("libc.so", "clock_gettime", HVC_CLOCK_GETTIME,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            struct timespec ts;
            ::clock_gettime((clockid_t) a[0], &ts);
            uint64_t sec = ts.tv_sec, nsec = ts.tv_nsec;
            if (a[1]) {
                guest_write(g, a[1], &sec, 8);
                guest_write(g, a[1] + 8, &nsec, 8);
            }
            return 0;
        });

    add("libc.so", "gettimeofday", HVC_GETTIMEOFDAY,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            struct timeval tv;
            ::gettimeofday(&tv, nullptr);
            uint64_t sec = tv.tv_sec, usec = tv.tv_usec;
            if (a[0]) {
                guest_write(g, a[0], &sec, 8);
                guest_write(g, a[0] + 8, &usec, 8);
            }
            return 0;
        });

    add("libc.so", "usleep", HVC_USLEEP,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            ::usleep((useconds_t) a[0]);
            return 0;
        });
    add("libc.so", "nanosleep", HVC_NANOSLEEP,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });
    add("libc.so", "atoi", HVC_ATOI,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            auto s = guest_read_string(g, a[0]);
            return s.empty() ? 0 : (uint64_t) std::stol(s);
        });
    add("libc.so", "atof", HVC_ATOF,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });
    add("libc.so", "rand", HVC_RAND,
        [](guest_t *, const uint64_t[8]) -> uint64_t {
            return static_cast<uint64_t>(::rand());
        });
    add("libc.so", "srand", HVC_SRAND,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            ::srand((unsigned) a[0]);
            return 0;
        });

    auto create_pipe = [this](guest_t *g, uint64_t fds_gpa) -> uint64_t {
        if (!fds_gpa)
            return guest_neg_errno(14);  // EFAULT

        HostPipe pipe;
        pipe.read_fd = next_pipe_fd_++;
        pipe.write_fd = next_pipe_fd_++;
        pipes_.push_back(pipe);

        guest_write_u32(g, fds_gpa + 0, static_cast<uint32_t>(pipe.read_fd));
        guest_write_u32(g, fds_gpa + 4, static_cast<uint32_t>(pipe.write_fd));
        std::fprintf(stderr, "[ART] pipe -> [%d,%d]\n", pipe.read_fd,
                     pipe.write_fd);
        return 0;
    };

    add("libc.so", "pipe", HVC_PIPE,
        [create_pipe](guest_t *g, const uint64_t a[8]) -> uint64_t {
            return create_pipe(g, a[0]);
        });
    add("libc.so", "pipe2", HVC_PIPE2,
        [create_pipe](guest_t *g, const uint64_t a[8]) -> uint64_t {
            (void) a[1];
            return create_pipe(g, a[0]);
        });
    add("libc.so", "read", HVC_READ,
        [this](guest_t *g, const uint64_t a[8]) -> uint64_t {
            int32_t fd = static_cast<int32_t>(a[0]);
            if (fd == 0) {
                if (!a[1] && a[2])
                    return guest_neg_errno(14);  // EFAULT

                size_t requested =
                    a[2] > SIZE_MAX ? SIZE_MAX : static_cast<size_t>(a[2]);
                std::vector<uint8_t> bytes(requested);
                while (true) {
                    ssize_t n =
                        ::read(STDIN_FILENO, bytes.data(), bytes.size());
                    if (n < 0 && errno == EINTR)
                        continue;
                    if (n < 0)
                        return guest_neg_errno(errno);
                    if (n > 0 && guest_write(g, a[1], bytes.data(),
                                             static_cast<size_t>(n)) != 0)
                        return guest_neg_errno(14);
                    return static_cast<uint64_t>(n);
                }
            }

            HostPipe *pipe = pipe_for_fd(fd);
            if (!pipe || fd != pipe->read_fd || !pipe->read_open)
                return guest_neg_errno(9);  // EBADF
            if (!a[1] && a[2])
                return guest_neg_errno(14);  // EFAULT

            size_t requested =
                a[2] > SIZE_MAX ? SIZE_MAX : static_cast<size_t>(a[2]);
            size_t to_copy = std::min(requested, pipe->buffer.size());
            if (to_copy == 0)
                return pipe->write_open ? guest_neg_errno(11)
                                        : 0;  // EAGAIN or EOF

            if (guest_write(g, a[1], pipe->buffer.data(), to_copy) != 0)
                return guest_neg_errno(14);
            pipe->buffer.erase(pipe->buffer.begin(),
                               pipe->buffer.begin() + to_copy);
            if (!pipe->buffer.empty())
                rearm_looper_fd(pipe->read_fd);

            std::fprintf(stderr,
                         "[ART] read pipe fd=%d bytes=%zu remaining=%zu\n", fd,
                         to_copy, pipe->buffer.size());
            return static_cast<uint64_t>(to_copy);
        });
    add("libc.so", "write", HVC_WRITE,
        [this](guest_t *g, const uint64_t a[8]) -> uint64_t {
            int32_t fd = static_cast<int32_t>(a[0]);
            if (!a[1] && a[2])
                return guest_neg_errno(14);  // EFAULT

            size_t count =
                a[2] > SIZE_MAX ? SIZE_MAX : static_cast<size_t>(a[2]);
            std::vector<uint8_t> bytes(count);
            if (count && guest_read(g, a[1], bytes.data(), count) != 0)
                return guest_neg_errno(14);

            if (fd == 1 || fd == 2) {
                if (!bytes.empty()) {
                    FILE *out = (fd == 1) ? stdout : stderr;
                    std::fwrite(bytes.data(), 1, bytes.size(), out);
                    std::fflush(out);
                }
                return static_cast<uint64_t>(count);
            }

            HostPipe *pipe = pipe_for_fd(fd);
            if (!pipe || fd != pipe->write_fd || !pipe->write_open)
                return guest_neg_errno(9);  // EBADF

            pipe->buffer.insert(pipe->buffer.end(), bytes.begin(), bytes.end());
            rearm_looper_fd(pipe->read_fd);
            std::fprintf(stderr,
                         "[ART] write pipe fd=%d bytes=%zu queued=%zu\n", fd,
                         count, pipe->buffer.size());
            return static_cast<uint64_t>(count);
        });
    add("libc.so", "close", HVC_CLOSE,
        [this](guest_t *, const uint64_t a[8]) -> uint64_t {
            int32_t fd = static_cast<int32_t>(a[0]);
            HostPipe *pipe = pipe_for_fd(fd);
            if (!pipe)
                return guest_neg_errno(9);  // EBADF
            if (fd == pipe->read_fd)
                pipe->read_open = false;
            if (fd == pipe->write_fd) {
                pipe->write_open = false;
                rearm_looper_fd(pipe->read_fd);
            }
            std::fprintf(stderr, "[ART] close fd=%d\n", fd);
            return 0;
        });

    uint64_t stdio_gpa = arena_gpa_ + 0x080000;
    std::vector<uint8_t> stdio_zero(0x600, 0);
    guest_write(guest_, stdio_gpa, stdio_zero.data(), stdio_zero.size());
    sym_tables_["libc.so"]["__sF"] = stdio_gpa;

    sym_tables_["libm.so"] = sym_tables_["libc.so"];
    // Import coverage for libm-heavy render stacks. Scalar FP arguments and
    // return values are not carried through the current integer-only HVC shim,
    // so these are conservative compatibility stubs.
    auto libm_ret0 = [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; };
    for (const char *symbol :
         {"acosf", "asinf", "atan", "atanf", "atan2f", "cosf", "exp2", "exp2f",
          "expf", "fmod", "fmodf", "ilogbf", "log2f", "log10f", "logf", "powf",
          "sinf", "tan", "nextafterf"}) {
        add("libm.so", symbol, HVC_LIBM_RET0, libm_ret0);
    }
    add("libm.so", "sincos", HVC_LIBM_SINCOS,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            uint64_t zero = 0;
            if (a[0])
                guest_write(g, a[0], &zero, sizeof(zero));
            if (a[1])
                guest_write(g, a[1], &zero, sizeof(zero));
            return 0;
        });
    add("libm.so", "sincosf", HVC_LIBM_SINCOSF,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            uint32_t zero = 0;
            if (a[0])
                guest_write_u32(g, a[0], zero);
            if (a[1])
                guest_write_u32(g, a[1], zero);
            return 0;
        });

    sym_tables_["libstdc++.so"] = sym_tables_["libc.so"];
    // libc++_shared.so and NDK wrapper libs re-export most libc symbols
    sym_tables_["libc++_shared.so"] = sym_tables_["libc.so"];
    sym_tables_["libandroid_support.so"] = sym_tables_["libc.so"];
    sym_tables_["libc++abi.so"] = sym_tables_["libc.so"];
}

// ── liblog stubs
// ──────────────────────────────────────────────────────────────

void AndroidRuntime::register_liblog_stubs()
{
    add("liblog.so", "__android_log_print", HVC_LOG_PRINT,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            auto tag = guest_read_string(g, a[1]);
            auto fmt = guest_read_string(g, a[2]);
            auto msg = format_guest_log(g, fmt, a + 3, 5);
            std::fprintf(stderr, "[logcat/%s] %s\n", tag.c_str(), msg.c_str());
            return (uint64_t) msg.size();
        });

    add("liblog.so", "__android_log_vprint", HVC_LOG_VPRINT,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            auto tag = guest_read_string(g, a[1]);
            auto fmt = guest_read_string(g, a[2]);
            std::fprintf(stderr, "[logcat/%s] %s\n", tag.c_str(), fmt.c_str());
            return static_cast<uint64_t>(fmt.size());
        });

    add("liblog.so", "__android_log_write", HVC_LOG_WRITE,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            auto tag = guest_read_string(g, a[1]);
            auto msg = guest_read_string(g, a[2]);
            std::fprintf(stderr, "[logcat/%s] %s\n", tag.c_str(), msg.c_str());
            return 0;
        });

    add("liblog.so", "__android_log_buf_write", HVC_LOG_BUF_WRITE,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            auto tag = guest_read_string(g, a[2]);
            auto msg = guest_read_string(g, a[3]);
            std::fprintf(stderr, "[logcat/%s] %s\n", tag.c_str(), msg.c_str());
            return 0;
        });
}

// ── libandroid stubs
// ──────────────────────────────────────────────────────────

void AndroidRuntime::register_libandroid_stubs()
{
    auto input_event_pending = [this]() -> bool {
        collect_host_input_events();
        return next_input_event_ < input_events_.size();
    };
    auto find_input_event = [this](uint64_t handle) -> InputEventState * {
        auto it = std::find_if(input_events_.begin(), input_events_.end(),
                               [handle](const InputEventState &event) {
                                   return event.handle == handle;
                               });
        return it == input_events_.end() ? nullptr : &*it;
    };

    add("libandroid.so", "ALooper_prepare", HVC_ALOOPER_PREPARE,
        [this](guest_t *, const uint64_t[8]) -> uint64_t {
            return arena_gpa_ + 0x100;
        });
    add("libandroid.so", "ALooper_acquire", HVC_ALOOPER_ACQUIRE,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });
    add("libandroid.so", "ALooper_release", HVC_ALOOPER_RELEASE,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });

    auto poll_looper = [this, input_event_pending](
                           guest_t *g, const uint64_t a[8]) -> uint64_t {
        constexpr uint64_t ALOOPER_POLL_TIMEOUT =
            static_cast<uint64_t>(static_cast<int64_t>(-3));
        constexpr uint64_t ALOOPER_POLL_CALLBACK =
            static_cast<uint64_t>(static_cast<int64_t>(-2));

        for (auto &reg : looper_regs_) {
            if (reg.delivered)
                continue;
            if (reg.fd == INPUT_QUEUE_FD &&
                (!input_queue_.attached || !input_event_pending())) {
                continue;
            }
            if (!looper_fd_ready(reg.fd))
                continue;
            reg.delivered = true;

            if (a[1])
                guest_write_u32(g, a[1], static_cast<uint32_t>(reg.fd));
            if (a[2])
                guest_write_u32(g, a[2], static_cast<uint32_t>(reg.events));
            if (a[3])
                guest_write_u64(g, a[3], reg.data);

            std::fprintf(stderr,
                         "[ALooper] poll fd=%d ident=%d events=0x%x "
                         "callback=0x%llx data=0x%llx\n",
                         reg.fd, reg.ident, reg.events,
                         (unsigned long long) reg.callback,
                         (unsigned long long) reg.data);

            if (reg.callback) {
                pending_looper_callbacks_.push_back(
                    {reg.callback, reg.fd, reg.events, reg.data, reg.ident});
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
    add("libandroid.so", "ALooper_pollAll", HVC_ALOOPER_POLL_ALL, poll_looper);
    add("libandroid.so", "ALooper_addFd", HVC_ALOOPER_ADD_FD,
        [this](guest_t *, const uint64_t a[8]) -> uint64_t {
            int32_t fd = static_cast<int32_t>(a[1]);
            int32_t ident = static_cast<int32_t>(a[2]);
            int32_t events = static_cast<int32_t>(a[3]);
            uint64_t callback = a[4];
            uint64_t data = a[5];

            if (fd < 0 || (!callback && ident < 0))
                return 0;

            auto it = std::find_if(
                looper_regs_.begin(), looper_regs_.end(),
                [fd](const LooperRegistration &reg) { return reg.fd == fd; });
            if (it == looper_regs_.end()) {
                looper_regs_.push_back(
                    {fd, ident, events, callback, data, false});
            } else {
                *it = {fd, ident, events, callback, data, false};
            }

            std::fprintf(stderr,
                         "[ALooper] addFd fd=%d ident=%d events=0x%x "
                         "callback=0x%llx data=0x%llx\n",
                         fd, ident, events, (unsigned long long) callback,
                         (unsigned long long) data);
            return 1;
        });
    add("libandroid.so", "ALooper_removeFd", HVC_ALOOPER_REMOVE_FD,
        [this](guest_t *, const uint64_t a[8]) -> uint64_t {
            int32_t fd = static_cast<int32_t>(a[1]);
            auto old_size = looper_regs_.size();
            looper_regs_.erase(
                std::remove_if(looper_regs_.begin(), looper_regs_.end(),
                               [fd](const LooperRegistration &reg) {
                                   return reg.fd == fd;
                               }),
                looper_regs_.end());
            return looper_regs_.size() != old_size ? 1 : 0;
        });
    add("libandroid.so", "ALooper_wake", HVC_ALOOPER_WAKE,
        [this](guest_t *, const uint64_t[8]) -> uint64_t {
            for (auto &reg : looper_regs_)
                reg.delivered = false;
            return 0;
        });

    // AAssetManager/AAsset — host-backed reads from extracted APK assets/.
    add("libandroid.so", "AAssetManager_fromJava", HVC_ASSET_MGR_FROM_JAVA,
        [](guest_t *, const uint64_t[8]) -> uint64_t {
            return GUEST_ASSET_MANAGER;
        });

    add("libandroid.so", "AAssetManager_open", HVC_ASSET_OPEN,
        [this](guest_t *g, const uint64_t a[8]) -> uint64_t {
            if (a[0] != GUEST_ASSET_MANAGER || asset_root_.empty())
                return 0;

            std::string name = guest_read_string(g, a[1]);
            if (!safe_asset_name(name))
                return 0;

            std::filesystem::path path = std::filesystem::path(asset_root_) /
                                         std::filesystem::path(name);
            std::error_code ec;
            if (!std::filesystem::is_regular_file(path, ec))
                return 0;

            std::vector<uint8_t> bytes = read_asset_file(path);
            if (bytes.empty() && std::filesystem::file_size(path, ec) != 0)
                return 0;

            uint64_t handle = next_asset_handle_++;
            assets_[handle] = {name, std::move(bytes), 0};
            std::fprintf(
                stderr, "[AssetManager] open %s size=%zu mode=%lld -> 0x%llx\n",
                name.c_str(), assets_[handle].bytes.size(),
                (long long) static_cast<int64_t>(a[2]),
                (unsigned long long) handle);
            return handle;
        });

    add("libandroid.so", "AAsset_read", HVC_ASSET_READ,
        [this](guest_t *g, const uint64_t a[8]) -> uint64_t {
            auto it = assets_.find(a[0]);
            if (it == assets_.end() || !a[1])
                return guest_negative_one();

            AssetState &asset = it->second;
            size_t remaining = asset.offset < asset.bytes.size()
                                   ? asset.bytes.size() - asset.offset
                                   : 0;
            size_t requested =
                static_cast<size_t>(std::min<uint64_t>(a[2], 0x7fffffffULL));
            size_t n = std::min(remaining, requested);
            if (n > 0 && guest_write(g, a[1], asset.bytes.data() + asset.offset,
                                     n) != 0) {
                return guest_negative_one();
            }
            asset.offset += n;
            return n;
        });

    add("libandroid.so", "AAsset_close", HVC_ASSET_CLOSE,
        [this](guest_t *, const uint64_t a[8]) -> uint64_t {
            assets_.erase(a[0]);
            return 0;
        });

    auto asset_length = [this](guest_t *, const uint64_t a[8]) -> uint64_t {
        auto it = assets_.find(a[0]);
        return it == assets_.end() ? 0 : it->second.bytes.size();
    };
    add("libandroid.so", "AAsset_getLength", HVC_ASSET_LENGTH, asset_length);
    add("libandroid.so", "AAsset_getLength64", HVC_ASSET_LENGTH, asset_length);

    auto asset_remaining = [this](guest_t *, const uint64_t a[8]) -> uint64_t {
        auto it = assets_.find(a[0]);
        if (it == assets_.end())
            return 0;
        const AssetState &asset = it->second;
        return asset.offset < asset.bytes.size()
                   ? asset.bytes.size() - asset.offset
                   : 0;
    };
    add("libandroid.so", "AAsset_getRemainingLength", HVC_ASSET_REMAINING,
        asset_remaining);
    add("libandroid.so", "AAsset_getRemainingLength64", HVC_ASSET_REMAINING,
        asset_remaining);

    auto asset_seek = [this](guest_t *, const uint64_t a[8]) -> uint64_t {
        auto it = assets_.find(a[0]);
        if (it == assets_.end())
            return guest_negative_one();

        AssetState &asset = it->second;
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
        [this](guest_t *, const uint64_t a[8]) -> uint64_t {
            if (a[0] != GUEST_INPUT_QUEUE)
                return 0;

            input_queue_.attached = true;
            input_queue_.looper = a[1];
            input_queue_.ident = static_cast<int32_t>(a[2]);
            input_queue_.callback = a[3];
            input_queue_.data = a[4];

            auto it = std::find_if(looper_regs_.begin(), looper_regs_.end(),
                                   [](const LooperRegistration &reg) {
                                       return reg.fd == INPUT_QUEUE_FD;
                                   });
            LooperRegistration reg{
                INPUT_QUEUE_FD,        input_queue_.ident, 1,
                input_queue_.callback, input_queue_.data,  false};
            if (it == looper_regs_.end())
                looper_regs_.push_back(reg);
            else
                *it = reg;

            std::fprintf(stderr,
                         "[InputQueue] attach looper=0x%llx ident=%d "
                         "callback=0x%llx data=0x%llx pending=%zu\n",
                         (unsigned long long) input_queue_.looper,
                         input_queue_.ident,
                         (unsigned long long) input_queue_.callback,
                         (unsigned long long) input_queue_.data,
                         input_events_.size() - next_input_event_);
            return 0;
        });

    add("libandroid.so", "AInputQueue_detachLooper", HVC_INPUT_QUEUE_DETACH,
        [this](guest_t *, const uint64_t a[8]) -> uint64_t {
            if (a[0] == GUEST_INPUT_QUEUE) {
                input_queue_ = {};
                looper_regs_.erase(
                    std::remove_if(looper_regs_.begin(), looper_regs_.end(),
                                   [](const LooperRegistration &reg) {
                                       return reg.fd == INPUT_QUEUE_FD;
                                   }),
                    looper_regs_.end());
                std::fprintf(stderr, "[InputQueue] detach\n");
            }
            return 0;
        });

    add("libandroid.so", "AInputQueue_hasEvents", HVC_INPUT_QUEUE_HAS_EVENTS,
        [input_event_pending](guest_t *, const uint64_t[8]) -> uint64_t {
            return input_event_pending() ? 1 : 0;
        });

    add("libandroid.so", "AInputQueue_getEvent", HVC_INPUT_QUEUE_GET_EVENT,
        [this, input_event_pending](guest_t *g,
                                    const uint64_t a[8]) -> uint64_t {
            constexpr uint64_t INPUT_QUEUE_EMPTY =
                static_cast<uint64_t>(static_cast<int64_t>(-1));
            if (a[0] != GUEST_INPUT_QUEUE || !a[1] || !input_event_pending())
                return INPUT_QUEUE_EMPTY;

            InputEventState &event = input_events_[next_input_event_++];
            event.offered = true;
            current_input_event_ = event.handle;
            guest_write_u64(g, a[1], event.handle);

            std::fprintf(
                stderr,
                "[InputQueue] getEvent handle=0x%llx type=%d action=%d\n",
                (unsigned long long) event.handle, event.type, event.action);
            return 0;
        });

    add("libandroid.so", "AInputQueue_preDispatchEvent",
        HVC_INPUT_QUEUE_PRE_DISPATCH,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });

    add("libandroid.so", "AInputQueue_finishEvent", HVC_INPUT_QUEUE_FINISH,
        [this, input_event_pending, find_input_event](
            guest_t *, const uint64_t a[8]) -> uint64_t {
            if (a[0] != GUEST_INPUT_QUEUE)
                return 0;

            InputEventState *event = find_input_event(a[1]);
            if (event && !event->finished) {
                event->finished = true;
                current_input_event_ = 0;
                std::fprintf(stderr,
                             "[InputQueue] finishEvent handle=0x%llx "
                             "handled=%lld remaining=%zu\n",
                             (unsigned long long) a[1],
                             (long long) static_cast<int64_t>(a[2]),
                             input_events_.size() - next_input_event_);
            }

            if (next_input_event_ >= input_events_.size() &&
                input_events_.size() > 100) {
                input_events_.clear();
                next_input_event_ = 0;
            }

            if (input_queue_.attached && input_event_pending())
                rearm_looper_fd(INPUT_QUEUE_FD);
            return 0;
        });

    add("libandroid.so", "AInputEvent_getType", HVC_INPUT_EVENT_GET_TYPE,
        [find_input_event](guest_t *, const uint64_t a[8]) -> uint64_t {
            auto *event = find_input_event(a[0]);
            return event ? static_cast<uint64_t>(
                               static_cast<int64_t>(event->type))
                         : 0;
        });
    add("libandroid.so", "AInputEvent_getDeviceId",
        HVC_INPUT_EVENT_GET_DEVICE_ID,
        [find_input_event](guest_t *, const uint64_t a[8]) -> uint64_t {
            auto *event = find_input_event(a[0]);
            return event ? static_cast<uint64_t>(
                               static_cast<int64_t>(event->device_id))
                         : 0;
        });
    add("libandroid.so", "AInputEvent_getSource", HVC_INPUT_EVENT_GET_SOURCE,
        [find_input_event](guest_t *, const uint64_t a[8]) -> uint64_t {
            auto *event = find_input_event(a[0]);
            return event ? static_cast<uint64_t>(
                               static_cast<int64_t>(event->source))
                         : 0;
        });
    auto float_to_u64 = [](float f) -> uint64_t {
        uint32_t bits = 0;
        std::memcpy(&bits, &f, sizeof(f));
        return static_cast<uint64_t>(bits);
    };

    add("libandroid.so", "AMotionEvent_getAction", HVC_MOTION_EVENT_GET_ACTION,
        [find_input_event](guest_t *, const uint64_t a[8]) -> uint64_t {
            auto *event = find_input_event(a[0]);
            return event ? static_cast<uint64_t>(
                               static_cast<int64_t>(event->action))
                         : static_cast<uint64_t>(static_cast<int64_t>(-1));
        });
    add("libandroid.so", "AMotionEvent_getX", HVC_MOTION_EVENT_GET_X,
        [find_input_event, float_to_u64](guest_t *,
                                         const uint64_t a[8]) -> uint64_t {
            auto *event = find_input_event(a[0]);
            return event ? float_to_u64(event->x) : float_to_u64(0.0f);
        });
    add("libandroid.so", "AMotionEvent_getY", HVC_MOTION_EVENT_GET_Y,
        [find_input_event, float_to_u64](guest_t *,
                                         const uint64_t a[8]) -> uint64_t {
            auto *event = find_input_event(a[0]);
            return event ? float_to_u64(event->y) : float_to_u64(0.0f);
        });
    add("libandroid.so", "AMotionEvent_getRawX", HVC_MOTION_EVENT_GET_RAW_X,
        [find_input_event, float_to_u64](guest_t *,
                                         const uint64_t a[8]) -> uint64_t {
            auto *event = find_input_event(a[0]);
            return event ? float_to_u64(event->x) : float_to_u64(0.0f);
        });
    add("libandroid.so", "AMotionEvent_getRawY", HVC_MOTION_EVENT_GET_RAW_Y,
        [find_input_event, float_to_u64](guest_t *,
                                         const uint64_t a[8]) -> uint64_t {
            auto *event = find_input_event(a[0]);
            return event ? float_to_u64(event->y) : float_to_u64(0.0f);
        });
    add("libandroid.so", "AMotionEvent_getPointerCount",
        HVC_MOTION_EVENT_GET_POINTER_COUNT,
        [find_input_event](guest_t *, const uint64_t a[8]) -> uint64_t {
            auto *event = find_input_event(a[0]);
            return event ? 1 : 0;
        });
    add("libandroid.so", "AMotionEvent_getPointerId",
        HVC_MOTION_EVENT_GET_POINTER_ID,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });
    add("libandroid.so", "AMotionEvent_getEventTime",
        HVC_MOTION_EVENT_GET_EVENT_TIME,
        [this](guest_t *, const uint64_t[8]) -> uint64_t {
            return next_frame_time_nanos_;
        });
    add("libandroid.so", "AMotionEvent_getDownTime",
        HVC_MOTION_EVENT_GET_DOWN_TIME,
        [this](guest_t *, const uint64_t[8]) -> uint64_t {
            return next_frame_time_nanos_;
        });
    add("libandroid.so", "AMotionEvent_getPressure",
        HVC_MOTION_EVENT_GET_PRESSURE,
        [float_to_u64](guest_t *, const uint64_t[8]) -> uint64_t {
            return float_to_u64(1.0f);
        });
    add("libandroid.so", "AMotionEvent_getHistorySize",
        HVC_MOTION_EVENT_GET_HISTORY_SIZE,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });
    add("libandroid.so", "AMotionEvent_getHistoricalEventTime",
        HVC_MOTION_EVENT_GET_HISTORICAL_EVENT_TIME,
        [this](guest_t *, const uint64_t[8]) -> uint64_t {
            return next_frame_time_nanos_;
        });
    add("libandroid.so", "AMotionEvent_getHistoricalX",
        HVC_MOTION_EVENT_GET_HISTORICAL_X,
        [find_input_event, float_to_u64](guest_t *,
                                         const uint64_t a[8]) -> uint64_t {
            auto *event = find_input_event(a[0]);
            return event ? float_to_u64(event->x) : float_to_u64(0.0f);
        });
    add("libandroid.so", "AMotionEvent_getHistoricalY",
        HVC_MOTION_EVENT_GET_HISTORICAL_Y,
        [find_input_event, float_to_u64](guest_t *,
                                         const uint64_t a[8]) -> uint64_t {
            auto *event = find_input_event(a[0]);
            return event ? float_to_u64(event->y) : float_to_u64(0.0f);
        });
    add("libandroid.so", "AMotionEvent_getHistoricalRawX",
        HVC_MOTION_EVENT_GET_HISTORICAL_RAW_X,
        [find_input_event, float_to_u64](guest_t *,
                                         const uint64_t a[8]) -> uint64_t {
            auto *event = find_input_event(a[0]);
            return event ? float_to_u64(event->x) : float_to_u64(0.0f);
        });
    add("libandroid.so", "AMotionEvent_getHistoricalRawY",
        HVC_MOTION_EVENT_GET_HISTORICAL_RAW_Y,
        [find_input_event, float_to_u64](guest_t *,
                                         const uint64_t a[8]) -> uint64_t {
            auto *event = find_input_event(a[0]);
            return event ? float_to_u64(event->y) : float_to_u64(0.0f);
        });
    add("libandroid.so", "AMotionEvent_getHistoricalPressure",
        HVC_MOTION_EVENT_GET_HISTORICAL_PRESSURE,
        [float_to_u64](guest_t *, const uint64_t[8]) -> uint64_t {
            return float_to_u64(1.0f);
        });
    add("libandroid.so", "AMotionEvent_getAxisValue",
        HVC_MOTION_EVENT_GET_AXIS_VALUE,
        [find_input_event, float_to_u64](guest_t *,
                                         const uint64_t a[8]) -> uint64_t {
            auto *event = find_input_event(a[0]);
            if (!event)
                return float_to_u64(0.0f);
            int32_t axis = static_cast<int32_t>(a[1]);
            if (axis == 0 /* AMOTION_EVENT_AXIS_X */) {
                return float_to_u64(event->x);
            } else if (axis == 1 /* AMOTION_EVENT_AXIS_Y */) {
                return float_to_u64(event->y);
            } else if (axis == 2 /* PRESSURE */ || axis == 3 /* SIZE */ ||
                       axis == 4 /* TOUCH_MAJOR */ ||
                       axis == 5 /* TOUCH_MINOR */) {
                return float_to_u64(1.0f);
            }
            return float_to_u64(0.0f);
        });
    add("libandroid.so", "AMotionEvent_getFlags", HVC_MOTION_EVENT_GET_FLAGS,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });
    add("libandroid.so", "AMotionEvent_getMetaState",
        HVC_MOTION_EVENT_GET_META_STATE,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });
    add("libandroid.so", "AMotionEvent_getButtonState",
        HVC_MOTION_EVENT_GET_BUTTON_STATE,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });
    add("libandroid.so", "AMotionEvent_getEdgeFlags",
        HVC_MOTION_EVENT_GET_EDGE_FLAGS,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });
    add("libandroid.so", "AKeyEvent_getAction", HVC_KEY_EVENT_GET_ACTION,
        [find_input_event](guest_t *, const uint64_t a[8]) -> uint64_t {
            auto *event = find_input_event(a[0]);
            return event ? static_cast<uint64_t>(
                               static_cast<int64_t>(event->action))
                         : 0;
        });
    add("libandroid.so", "AKeyEvent_getKeyCode", HVC_KEY_EVENT_GET_KEYCODE,
        [find_input_event](guest_t *, const uint64_t a[8]) -> uint64_t {
            auto *event = find_input_event(a[0]);
            return event ? static_cast<uint64_t>(
                               static_cast<int64_t>(event->key_code))
                         : 0;
        });

    // ANativeWindow — stable opaque handle plus a guest-visible software
    // buffer.
    add("libandroid.so", "ANativeWindow_fromSurface",
        HVC_NATIVE_WINDOW_FROM_SURFACE,
        [this](guest_t *, const uint64_t[8]) -> uint64_t {
            native_window_.ref_count++;
            return GUEST_NATIVE_WINDOW;
        });

    add("libandroid.so", "ANativeWindow_acquire", HVC_NATIVE_WINDOW_ACQUIRE,
        [this](guest_t *, const uint64_t a[8]) -> uint64_t {
            if (a[0] == GUEST_NATIVE_WINDOW)
                native_window_.ref_count++;
            return 0;
        });

    add("libandroid.so", "ANativeWindow_release", HVC_NATIVE_WINDOW_RELEASE,
        [this](guest_t *, const uint64_t a[8]) -> uint64_t {
            if (a[0] == GUEST_NATIVE_WINDOW && native_window_.ref_count > 0)
                native_window_.ref_count--;
            return 0;
        });

    add("libandroid.so", "ANativeWindow_getWidth", HVC_NATIVE_WINDOW_WIDTH,
        [this](guest_t *, const uint64_t a[8]) -> uint64_t {
            return a[0] == GUEST_NATIVE_WINDOW
                       ? static_cast<uint64_t>(
                             static_cast<uint32_t>(native_window_.width))
                       : static_cast<uint64_t>(static_cast<uint32_t>(-22));
        });

    add("libandroid.so", "ANativeWindow_getHeight", HVC_NATIVE_WINDOW_HEIGHT,
        [this](guest_t *, const uint64_t a[8]) -> uint64_t {
            return a[0] == GUEST_NATIVE_WINDOW
                       ? static_cast<uint64_t>(
                             static_cast<uint32_t>(native_window_.height))
                       : static_cast<uint64_t>(static_cast<uint32_t>(-22));
        });

    add("libandroid.so", "ANativeWindow_getFormat", HVC_NATIVE_WINDOW_FORMAT,
        [this](guest_t *, const uint64_t a[8]) -> uint64_t {
            return a[0] == GUEST_NATIVE_WINDOW
                       ? static_cast<uint64_t>(
                             static_cast<uint32_t>(native_window_.format))
                       : static_cast<uint64_t>(static_cast<uint32_t>(-22));
        });

    add("libandroid.so", "ANativeWindow_setBuffersGeometry",
        HVC_NATIVE_WINDOW_SET_BUF,
        [this](guest_t *, const uint64_t a[8]) -> uint64_t {
            if (a[0] != GUEST_NATIVE_WINDOW)
                return static_cast<uint32_t>(-22);

            int32_t width = static_cast<int32_t>(a[1]);
            int32_t height = static_cast<int32_t>(a[2]);
            int32_t format = static_cast<int32_t>(a[3]);

            if (width > 0)
                native_window_.width = width;
            if (height > 0)
                native_window_.height = height;
            if (format > 0)
                native_window_.format = format;

            if (native_window_.width > MAX_NATIVE_WINDOW_WIDTH)
                native_window_.width = MAX_NATIVE_WINDOW_WIDTH;
            if (native_window_.height > MAX_NATIVE_WINDOW_HEIGHT)
                native_window_.height = MAX_NATIVE_WINDOW_HEIGHT;
            native_window_.stride = native_window_.width;

            std::fprintf(stderr,
                         "[NativeWindow] setBuffersGeometry %dx%d fmt=%d\n",
                         native_window_.width, native_window_.height,
                         native_window_.format);
            return 0;
        });

    add("libandroid.so", "ANativeWindow_lock", HVC_NATIVE_WINDOW_LOCK,
        [this](guest_t *g, const uint64_t a[8]) -> uint64_t {
            if (a[0] != GUEST_NATIVE_WINDOW || !a[1])
                return static_cast<uint32_t>(-22);

            // ANativeWindow_Buffer: width, height, stride, format, bits,
            // reserved[6].
            guest_write_u32(g, a[1] + 0x00,
                            static_cast<uint32_t>(native_window_.width));
            guest_write_u32(g, a[1] + 0x04,
                            static_cast<uint32_t>(native_window_.height));
            guest_write_u32(g, a[1] + 0x08,
                            static_cast<uint32_t>(native_window_.stride));
            guest_write_u32(g, a[1] + 0x0C,
                            static_cast<uint32_t>(native_window_.format));
            guest_write_u64(g, a[1] + 0x10, native_window_.bits_gpa);

            if (a[2]) {
                guest_write_u32(g, a[2] + 0x00, 0);
                guest_write_u32(g, a[2] + 0x04, 0);
                guest_write_u32(g, a[2] + 0x08,
                                static_cast<uint32_t>(native_window_.width));
                guest_write_u32(g, a[2] + 0x0C,
                                static_cast<uint32_t>(native_window_.height));
            }

            native_window_.locked = true;
            std::fprintf(stderr,
                         "[NativeWindow] lock buffer=%dx%d bits=0x%llx\n",
                         native_window_.width, native_window_.height,
                         (unsigned long long) native_window_.bits_gpa);
            return 0;
        });

    add("libandroid.so", "ANativeWindow_unlockAndPost",
        HVC_NATIVE_WINDOW_UNLOCK,
        [this](guest_t *, const uint64_t a[8]) -> uint64_t {
            if (a[0] != GUEST_NATIVE_WINDOW)
                return static_cast<uint32_t>(-22);
            native_window_.locked = false;
            std::fprintf(stderr, "[NativeWindow] unlockAndPost\n");
            present_native_window_buffer();
            return 0;
        });

    add("libandroid.so", "AChoreographer_getInstance", HVC_CHOREOGRAPHER_GET,
        [this](guest_t *, const uint64_t[8]) -> uint64_t {
            return arena_gpa_ + 0x200;
        });

    auto post_frame_callback = [this](const char *symbol, const uint64_t a[8],
                                      uint64_t delay_ms) -> uint64_t {
        (void) a[0];
        uint64_t callback = a[1];
        uint64_t data = a[2];
        if (!callback)
            return 0;

        uint64_t frame_time = next_frame_time_nanos_ + delay_ms * 1'000'000ULL;
        next_frame_time_nanos_ = frame_time + 16'666'666ULL;
        pending_frame_callbacks_.push_back({callback, frame_time, data});
        std::fprintf(stderr,
                     "[Choreographer] %s callback=0x%llx data=0x%llx "
                     "frame=%llu delay_ms=%llu\n",
                     symbol, (unsigned long long) callback,
                     (unsigned long long) data, (unsigned long long) frame_time,
                     (unsigned long long) delay_ms);
        return 0;
    };

    add("libandroid.so", "AChoreographer_postFrameCallback",
        HVC_CHOREOGRAPHER_CB,
        [post_frame_callback](guest_t *, const uint64_t a[8]) -> uint64_t {
            return post_frame_callback("postFrameCallback", a, 0);
        });
    add("libandroid.so", "AChoreographer_postFrameCallbackDelayed",
        HVC_CHOREOGRAPHER_CB_DELAYED,
        [post_frame_callback](guest_t *, const uint64_t a[8]) -> uint64_t {
            return post_frame_callback("postFrameCallbackDelayed", a, a[3]);
        });
    add("libandroid.so", "AChoreographer_postFrameCallback64",
        HVC_CHOREOGRAPHER_CB64,
        [post_frame_callback](guest_t *, const uint64_t a[8]) -> uint64_t {
            return post_frame_callback("postFrameCallback64", a, 0);
        });
    add("libandroid.so", "AChoreographer_postFrameCallbackDelayed64",
        HVC_CHOREOGRAPHER_CB64_DELAYED,
        [post_frame_callback](guest_t *, const uint64_t a[8]) -> uint64_t {
            return post_frame_callback("postFrameCallbackDelayed64", a, a[3]);
        });

    auto system_property_get = [](guest_t *g, const uint64_t a[8]) -> uint64_t {
        auto name = guest_read_string(g, a[0]);
        const char *property = default_android_property(name);
        std::string_view value =
            property ? std::string_view(property) : std::string_view();
        if (a[1]) {
            guest_write(g, a[1], value.data(), value.size());
            uint8_t nul = 0;
            guest_write(g, a[1] + value.size(), &nul, 1);
        }
        std::fprintf(stderr, "[ART] __system_property_get(%s) -> %.*s\n",
                     name.c_str(), static_cast<int>(value.size()),
                     value.data());
        return value.size();
    };
    add("libc.so", "__system_property_get", HVC_PROP_GET, system_property_get);
    add("libandroid.so", "__system_property_get", HVC_PROP_GET,
        system_property_get);
    add("libcutils.so", "property_get", HVC_CUTILS_PROP_GET,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            auto name = guest_read_string(g, a[0]);
            const char *property = default_android_property(name);
            std::string value;
            if (property) {
                value = property;
            } else if (a[2]) {
                value = guest_read_string(g, a[2]);
            }
            if (a[1]) {
                guest_write(g, a[1], value.data(), value.size());
                uint8_t nul = 0;
                guest_write(g, a[1] + value.size(), &nul, 1);
            }
            std::fprintf(stderr, "[ART] property_get(%s) -> %s\n", name.c_str(),
                         value.c_str());
            return value.size();
        });
}

// ── libjnigraphics stubs
// ──────────────────────────────────────────────────────

void AndroidRuntime::register_libjnigraphics_stubs()
{
    constexpr uint64_t RESULT_SUCCESS = 0;
    constexpr uint64_t RESULT_BAD_PARAMETER =
        static_cast<uint64_t>(static_cast<uint32_t>(-1));
    constexpr uint64_t RESULT_ALLOCATION_FAILED =
        static_cast<uint64_t>(static_cast<uint32_t>(-3));
    constexpr uint32_t BITMAP_FORMAT_RGBA_8888 = 1;
    constexpr uint32_t BITMAP_WIDTH = 1;
    constexpr uint32_t BITMAP_HEIGHT = 1;
    constexpr uint32_t BITMAP_STRIDE = BITMAP_WIDTH * 4;
    constexpr size_t BITMAP_BYTES = BITMAP_STRIDE * BITMAP_HEIGHT;

    auto alloc_pixels = [this](guest_t *g) -> uint64_t {
        uint64_t ptr = (heap_bump_ + 15) & ~15ULL;
        if (ptr + BITMAP_BYTES > heap_base_ + HEAP_SIZE)
            return 0;

        heap_bump_ = ptr + BITMAP_BYTES;
        uint32_t pixel = 0;
        guest_write(g, ptr, &pixel, sizeof(pixel));
        return ptr;
    };

    add("libjnigraphics.so", "AndroidBitmap_getInfo", HVC_BITMAP_GET_INFO,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            uint64_t info = a[2];
            if (!info)
                return RESULT_BAD_PARAMETER;

            // AndroidBitmapInfo: width, height, stride, format, flags.
            guest_write_u32(g, info + 0x00, BITMAP_WIDTH);
            guest_write_u32(g, info + 0x04, BITMAP_HEIGHT);
            guest_write_u32(g, info + 0x08, BITMAP_STRIDE);
            guest_write_u32(g, info + 0x0C, BITMAP_FORMAT_RGBA_8888);
            guest_write_u32(g, info + 0x10, 0);
            return RESULT_SUCCESS;
        });

    add("libjnigraphics.so", "AndroidBitmap_lockPixels", HVC_BITMAP_LOCK_PIXELS,
        [alloc_pixels](guest_t *g, const uint64_t a[8]) -> uint64_t {
            uint64_t out_pixels = a[2];
            if (!out_pixels)
                return RESULT_BAD_PARAMETER;

            uint64_t pixels = alloc_pixels(g);
            if (!pixels)
                return RESULT_ALLOCATION_FAILED;

            guest_write_u64(g, out_pixels, pixels);
            return RESULT_SUCCESS;
        });

    add("libjnigraphics.so", "AndroidBitmap_unlockPixels",
        HVC_BITMAP_UNLOCK_PIXELS, [](guest_t *, const uint64_t[8]) -> uint64_t {
            return RESULT_SUCCESS;
        });
}

// ── libdl stubs
// ───────────────────────────────────────────────────────────────

void AndroidRuntime::register_libdl_stubs()
{
    add("libdl.so", "dlopen", HVC_DLOPEN,
        [this](guest_t *g, const uint64_t a[8]) -> uint64_t {
            auto path = guest_read_string(g, a[0]);
            std::fprintf(stderr, "[ART] dlopen(%s)\n", path.c_str());
            uint64_t h = next_dl_handle_++;
            dl_handles_[h] = {path, 0};
            return h;
        });

    add("libdl.so", "dlsym", HVC_DLSYM,
        [this](guest_t *g, const uint64_t a[8]) -> uint64_t {
            uint64_t handle = a[0];
            auto sym = guest_read_string(g, a[1]);
            if (sym.empty())
                return 0;

            // RTLD_DEFAULT (0) or RTLD_NEXT (−1): search all tables.
            // Otherwise narrow to the soname that was dlopen'd.
            std::string scoped_soname;
            if (handle != 0 && handle != static_cast<uint64_t>(-1LL)) {
                auto hit = dl_handles_.find(handle);
                if (hit != dl_handles_.end())
                    scoped_soname = hit->second.path;
            }

            // Helper: search one soname table
            auto search_table = [&](const std::string &soname) -> uint64_t {
                auto tit = sym_tables_.find(soname);
                if (tit == sym_tables_.end())
                    return 0;
                auto sit = tit->second.find(sym);
                return (sit != tit->second.end()) ? sit->second : 0;
            };

            // 1. Try scoped soname first (exact handle match)
            if (!scoped_soname.empty()) {
                uint64_t gpa = search_table(scoped_soname);
                if (gpa)
                    return gpa;
                // Also try just the basename (dlopen path may be full path)
                auto slash = scoped_soname.rfind('/');
                std::string base = (slash == std::string::npos)
                                       ? scoped_soname
                                       : scoped_soname.substr(slash + 1);
                if (base != scoped_soname) {
                    gpa = search_table(base);
                    if (gpa)
                        return gpa;
                }
            }

            // 2. Search all tables (covers RTLD_DEFAULT and cross-soname refs)
            for (auto &[soname, syms] : sym_tables_) {
                auto it = syms.find(sym);
                if (it != syms.end()) {
                    return it->second;
                }
            }

            // 3. Try ANGLE for GL symbols
            void *fn = angle_sym(sym.c_str());
            if (fn) {
                uint32_t nr = next_proc_hvc_++;
                uint64_t gpa = write_stub(nr);
                void *captured = fn;
                std::string captured_name = sym;
                handlers_[nr] = [captured, captured_name](
                                    guest_t *, const uint64_t[8]) -> uint64_t {
                    std::fprintf(stderr, "[GL] dlsym-resolved %s() called\n",
                                 captured_name.c_str());
                    (void) captured;
                    return 0;
                };
                std::fprintf(stderr, "[ART] dlsym(%s) → ANGLE stub 0x%llx\n",
                             sym.c_str(), (unsigned long long) gpa);
                return gpa;
            }

            std::fprintf(stderr, "[ART] dlsym(%s) → NOT FOUND\n", sym.c_str());
            return 0;
        });

    add("libdl.so", "dlclose", HVC_DLCLOSE,
        [this](guest_t *, const uint64_t a[8]) -> uint64_t {
            dl_handles_.erase(a[0]);
            return 0;
        });
    add("libdl.so", "dlerror", HVC_DLERROR,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });
    add("libdl.so", "dl_iterate_phdr", HVC_DL_ITERATE_PHDR,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            std::fprintf(
                stderr, "[ART] dl_iterate_phdr(callback=0x%llx) -> 0 entries\n",
                (unsigned long long) a[0]);
            return 0;
        });

    sym_tables_["libdl_android.so"] = sym_tables_["libdl.so"];
    add("libdl_android.so", "android_get_exported_namespace",
        HVC_DL_ANDROID_GET_NS,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0x1001; });
    add("libdl_android.so", "android_create_namespace",
        HVC_DL_ANDROID_CREATE_NS,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0x1002; });
    add("libdl_android.so", "android_link_namespaces", HVC_DL_ANDROID_LINK_NS,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 1; });

    sym_tables_["libnativebridge.so"] = BuiltinSymbols{};
    add("libnativebridge.so", "NativeBridgeIsPathSupported",
        HVC_NATIVE_BRIDGE_IS_PATH_SUPPORTED,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });
    add("libnativebridge.so", "NativeBridgeGetError",
        HVC_NATIVE_BRIDGE_GET_ERROR,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });
    add("libnativebridge.so", "NativeBridgeGetExportedNamespace",
        HVC_NATIVE_BRIDGE_GET_NS,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0x1001; });
    add("libnativebridge.so", "NativeBridgeCreateNamespace",
        HVC_NATIVE_BRIDGE_CREATE_NS,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0x1002; });
    add("libnativebridge.so", "NativeBridgeLinkNamespaces",
        HVC_NATIVE_BRIDGE_LINK_NS,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 1; });
    add("libnativebridge.so", "NativeBridgeLoadLibraryExt",
        HVC_NATIVE_BRIDGE_LOAD_LIBRARY_EXT,
        [this](guest_t *g, const uint64_t a[8]) -> uint64_t {
            auto path = guest_read_string(g, a[0]);
            if (!path.empty())
                std::fprintf(stderr, "[ART] NativeBridgeLoadLibraryExt(%s)\n",
                             path.c_str());
            uint64_t h = next_dl_handle_++;
            dl_handles_[h] = {path, 0};
            return h;
        });
}

// ── libbinder_ndk stubs ──────────────────────────────────────────────────────

void AndroidRuntime::register_libbinder_stubs()
{
    constexpr uint64_t STATUS_OK = 0;
    constexpr uint64_t STATUS_BAD_VALUE =
        static_cast<uint64_t>(static_cast<int64_t>(-22));
    constexpr uint64_t STATUS_BAD_TYPE =
        static_cast<uint64_t>(static_cast<int64_t>(-2147483647LL));
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
    constexpr uint64_t STATUS_UNEXPECTED_NULL =
        static_cast<uint64_t>(static_cast<int64_t>(-2147483640LL));

    auto find_service = [this](uint64_t handle) -> BinderService * {
        auto it = binder_services_.find(handle);
        return it == binder_services_.end() ? nullptr : &it->second;
    };
    auto find_weak = [this](uint64_t handle) -> BinderWeak * {
        auto it = binder_weaks_.find(handle);
        return it == binder_weaks_.end() ? nullptr : &it->second;
    };
    auto make_binder_service =
        [](std::string name, uint32_t ref_count, bool alive, bool remote,
           uint64_t class_handle, uint64_t user_data) -> BinderService {
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
        [this](uint64_t handle) -> BinderDeathRecipient * {
        auto it = binder_death_recipients_.find(handle);
        return it == binder_death_recipients_.end() ? nullptr : &it->second;
    };
    auto notify_death_unlinked = [this](const BinderService::DeathLink &link) {
        if (link.on_unlinked && guest_function_invoker_)
            guest_function_invoker_(link.on_unlinked, {link.cookie});
    };
    auto notify_binder_died = [this](const BinderService::DeathLink &link) {
        if (link.on_binder_died && guest_function_invoker_)
            guest_function_invoker_(link.on_binder_died, {link.cookie});
    };
    auto cleanup_service_names_for_handle = [this](uint64_t handle,
                                                   bool mark_removed) {
        for (auto it = binder_service_by_name_.begin();
             it != binder_service_by_name_.end();) {
            if (it->second == handle) {
                if (mark_removed)
                    binder_removed_service_names_.insert(it->first);
                it = binder_service_by_name_.erase(it);
            } else {
                ++it;
            }
        }
    };
    auto retire_service = [cleanup_service_names_for_handle, notify_binder_died,
                           notify_death_unlinked](uint64_t handle,
                                                  BinderService *service,
                                                  const char *reason) -> bool {
        if (!service || !service->alive)
            return false;
        service->alive = false;
        cleanup_service_names_for_handle(handle, false);
        for (const BinderService::DeathLink &link : service->death_links) {
            notify_binder_died(link);
            notify_death_unlinked(link);
        }
        service->death_links.clear();
        std::fprintf(stderr, "[Binder] service 0x%llx marked dead (%s)\n",
                     (unsigned long long) handle, reason ? reason : "unknown");
        return true;
    };

    auto service_handle_for_name = [this, make_binder_service](
                                       const std::string &name,
                                       bool create_missing) -> uint64_t {
        if (name.empty())
            return 0;

        auto existing = binder_service_by_name_.find(name);
        if (existing != binder_service_by_name_.end()) {
            auto service_it = binder_services_.find(existing->second);
            if (service_it != binder_services_.end() &&
                service_it->second.alive) {
                return existing->second;
            }
            binder_service_by_name_.erase(existing);
        }

        if (binder_removed_service_names_.find(name) !=
            binder_removed_service_names_.end()) {
            return 0;
        }
        if (!create_missing)
            return 0;

        uint64_t handle = next_binder_handle_++;
        binder_services_[handle] =
            make_binder_service(name, 1, true, true, 0, 0);
        if (!service_socket_.empty()) {
            services::MuplardClient client(service_socket_);
            binder_services_[handle].daemon_owned =
                client.check_service(name) == services::ServiceState::Owned;
        }
        binder_service_by_name_[name] = handle;
        return handle;
    };

    auto service_lookup = [service_handle_for_name](
                              const char *op, guest_t *g, const uint64_t a[8],
                              bool create_missing) -> uint64_t {
        std::string name = guest_read_string(g, a[0]);
        uint64_t handle = service_handle_for_name(name, create_missing);
        std::fprintf(stderr, "[Binder] %s(%s) -> 0x%llx\n", op,
                     name.empty() ? "<empty>" : name.c_str(),
                     (unsigned long long) handle);
        return handle;
    };

    add("libbinder_ndk.so", "AServiceManager_checkService", HVC_SERVICE_CHECK,
        [service_lookup](guest_t *g, const uint64_t a[8]) -> uint64_t {
            return service_lookup("checkService", g, a, true);
        });
    add("libbinder_ndk.so", "AServiceManager_getService", HVC_SERVICE_GET,
        [service_lookup](guest_t *g, const uint64_t a[8]) -> uint64_t {
            return service_lookup("getService", g, a, true);
        });
    add("libbinder_ndk.so", "AServiceManager_waitForService", HVC_SERVICE_WAIT,
        [service_lookup](guest_t *g, const uint64_t a[8]) -> uint64_t {
            return service_lookup("waitForService", g, a, true);
        });
    add("libbinder_ndk.so", "AServiceManager_isDeclared",
        HVC_SERVICE_IS_DECLARED,
        [this](guest_t *g, const uint64_t a[8]) -> uint64_t {
            std::string name = guest_read_string(g, a[0]);
            uint64_t declared =
                name.empty() || binder_removed_service_names_.find(name) !=
                                    binder_removed_service_names_.end()
                    ? 0
                    : 1;
            std::fprintf(stderr, "[Binder] isDeclared(%s) -> %llu\n",
                         name.empty() ? "<empty>" : name.c_str(),
                         (unsigned long long) declared);
            return declared;
        });
    add("libbinder_ndk.so", "AServiceManager_addService", HVC_SERVICE_ADD,
        [this, find_service](guest_t *g, const uint64_t a[8]) -> uint64_t {
            std::string name = guest_read_string(g, a[1]);
            uint64_t handle = a[0];
            BinderService *service = find_service(handle);
            if (name.empty() || !service || !service->alive || service->remote)
                return STATUS_BAD_VALUE;

            auto existing = binder_service_by_name_.find(name);
            if (existing != binder_service_by_name_.end() &&
                existing->second != handle) {
                BinderService *existing_service =
                    find_service(existing->second);
                if (existing_service && existing_service->alive)
                    return STATUS_ALREADY_EXISTS;
            }

            binder_service_by_name_[name] = handle;
            binder_removed_service_names_.erase(name);
            service->name = name;
            std::fprintf(stderr, "[Binder] addService(%s, 0x%llx) -> OK\n",
                         name.c_str(), (unsigned long long) handle);
            return STATUS_OK;
        });

    add("libbinder_ndk.so", "AIBinder_isRemote", HVC_BINDER_IS_REMOTE,
        [find_service](guest_t *, const uint64_t a[8]) -> uint64_t {
            BinderService *service = find_service(a[0]);
            return service && service->remote ? 1 : 0;
        });
    add("libbinder_ndk.so", "AIBinder_isAlive", HVC_BINDER_IS_ALIVE,
        [find_service](guest_t *, const uint64_t a[8]) -> uint64_t {
            BinderService *service = find_service(a[0]);
            return service && service->alive ? 1 : 0;
        });
    add("libbinder_ndk.so", "AIBinder_ping", HVC_BINDER_PING,
        [find_service](guest_t *, const uint64_t a[8]) -> uint64_t {
            BinderService *service = find_service(a[0]);
            if (!service)
                return STATUS_BAD_VALUE;
            return service->alive ? STATUS_OK : STATUS_DEAD_OBJECT;
        });
    add("libbinder_ndk.so", "AIBinder_incStrong", HVC_BINDER_INC_STRONG,
        [find_service](guest_t *, const uint64_t a[8]) -> uint64_t {
            if (BinderService *service = find_service(a[0]))
                service->ref_count++;
            return 0;
        });
    add("libbinder_ndk.so", "AIBinder_decStrong", HVC_BINDER_DEC_STRONG,
        [this](guest_t *, const uint64_t a[8]) -> uint64_t {
            release_binder_strong(a[0]);
            return 0;
        });
    add("libbinder_ndk.so", "AIBinder_debugGetRefCount", HVC_BINDER_REF_COUNT,
        [find_service](guest_t *, const uint64_t a[8]) -> uint64_t {
            BinderService *service = find_service(a[0]);
            if (!a[0])
                return static_cast<uint64_t>(static_cast<int64_t>(-1));
            return service ? service->ref_count : 0;
        });
    add("libbinder_ndk.so", "AIBinder_getCallingUid", HVC_BINDER_CALLING_UID,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 10000; });
    add("libbinder_ndk.so", "AIBinder_getCallingPid", HVC_BINDER_CALLING_PID,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 1; });
    add("libbinder_ndk.so", "AIBinder_isHandlingTransaction",
        HVC_BINDER_HANDLING_TX,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });
    add("libbinder_ndk.so", "AIBinder_dump", HVC_BINDER_DUMP,
        [find_service](guest_t *, const uint64_t a[8]) -> uint64_t {
            BinderService *service = find_service(a[0]);
            if (!service)
                return STATUS_BAD_VALUE;
            return service->alive ? STATUS_OK : STATUS_DEAD_OBJECT;
        });

    constexpr int32_t EX_NONE = 0;
    constexpr int32_t EX_SERVICE_SPECIFIC = -8;
    constexpr int32_t EX_TRANSACTION_FAILED = -129;
    constexpr uint32_t FLAG_ONEWAY = 0x01;
    constexpr int32_t MAX_PARCEL_ARRAY_LENGTH = 1024;
    constexpr int32_t MAX_PARCEL_STRING_LENGTH = 4096;

    auto find_parcel = [this](uint64_t handle) -> BinderParcel * {
        auto it = binder_parcels_.find(handle);
        return it == binder_parcels_.end() ? nullptr : &it->second;
    };
    auto create_parcel = [this](uint64_t target_binder, uint32_t code,
                                bool reply) -> uint64_t {
        uint64_t handle = next_binder_parcel_handle_++;
        binder_parcels_[handle] = {target_binder, code, reply, 0, {}};
        return handle;
    };
    auto create_status = [this](int32_t exception, int32_t service_error,
                                int32_t status,
                                std::string message) -> uint64_t {
        uint64_t handle = next_binder_status_handle_++;
        binder_statuses_[handle] = {exception, service_error, status,
                                    std::move(message)};
        return handle;
    };
    auto find_status = [this](uint64_t handle) -> BinderStatus * {
        auto it = binder_statuses_.find(handle);
        return it == binder_statuses_.end() ? nullptr : &it->second;
    };
    auto find_class = [this](uint64_t handle) -> BinderClass * {
        auto it = binder_classes_.find(handle);
        return it == binder_classes_.end() ? nullptr : &it->second;
    };
    auto append_bytes = [](std::string &output, const void *data, size_t size) {
        output.append(static_cast<const char *>(data), size);
    };
    auto encode_parcel = [append_bytes](const BinderParcel *parcel,
                                        uint32_t code, uint32_t flags) {
        std::string output;
        const uint32_t magic = 0x4d425031;  // MBP1
        uint32_t count =
            parcel ? static_cast<uint32_t>(parcel->values.size()) : 0;
        append_bytes(output, &magic, sizeof(magic));
        append_bytes(output, &code, sizeof(code));
        append_bytes(output, &flags, sizeof(flags));
        append_bytes(output, &count, sizeof(count));
        if (!parcel)
            return output;
        for (const BinderParcelValue &value : parcel->values) {
            uint32_t kind = static_cast<uint32_t>(value.kind);
            uint32_t text_size = static_cast<uint32_t>(value.text.size());
            uint32_t element_count =
                static_cast<uint32_t>(value.elements.size());
            uint32_t string_count = static_cast<uint32_t>(value.strings.size());
            append_bytes(output, &kind, sizeof(kind));
            append_bytes(output, &value.value, sizeof(value.value));
            append_bytes(output, &text_size, sizeof(text_size));
            append_bytes(output, value.text.data(), value.text.size());
            append_bytes(output, &element_count, sizeof(element_count));
            for (uint64_t element : value.elements)
                append_bytes(output, &element, sizeof(element));
            append_bytes(output, &string_count, sizeof(string_count));
            for (const std::string &item : value.strings) {
                uint32_t size = static_cast<uint32_t>(item.size());
                append_bytes(output, &size, sizeof(size));
                append_bytes(output, item.data(), item.size());
            }
        }
        return output;
    };
    auto decode_parcel = [](const std::string &input,
                            std::vector<BinderParcelValue> &values) {
        size_t offset = 0;
        auto read = [&input, &offset](void *output, size_t size) {
            if (size > input.size() - std::min(offset, input.size()))
                return false;
            std::memcpy(output, input.data() + offset, size);
            offset += size;
            return true;
        };
        uint32_t magic = 0, code = 0, flags = 0, count = 0;
        if (!read(&magic, sizeof(magic)) || magic != 0x4d425031 ||
            !read(&code, sizeof(code)) || !read(&flags, sizeof(flags)) ||
            !read(&count, sizeof(count)) || count > 4096)
            return false;
        (void) code;
        (void) flags;
        values.clear();
        for (uint32_t index = 0; index < count; ++index) {
            BinderParcelValue value;
            uint32_t kind = 0, text_size = 0, element_count = 0;
            uint32_t string_count = 0;
            if (!read(&kind, sizeof(kind)) ||
                kind > static_cast<uint32_t>(
                           BinderParcelKind::ParcelFileDescriptor) ||
                !read(&value.value, sizeof(value.value)) ||
                !read(&text_size, sizeof(text_size)) ||
                text_size > input.size() - std::min(offset, input.size()))
                return false;
            value.kind = static_cast<BinderParcelKind>(kind);
            value.text.assign(input.data() + offset, text_size);
            offset += text_size;
            if (!read(&element_count, sizeof(element_count)) ||
                element_count > 65536)
                return false;
            value.elements.resize(element_count);
            for (uint64_t &element : value.elements)
                if (!read(&element, sizeof(element)))
                    return false;
            if (!read(&string_count, sizeof(string_count)) ||
                string_count > 65536)
                return false;
            for (uint32_t string_index = 0; string_index < string_count;
                 ++string_index) {
                uint32_t size = 0;
                if (!read(&size, sizeof(size)) ||
                    size > input.size() - std::min(offset, input.size()))
                    return false;
                value.strings.emplace_back(input.data() + offset, size);
                offset += size;
            }
            values.push_back(std::move(value));
        }
        return offset == input.size();
    };
    auto remove_death_link = [notify_death_unlinked](BinderService *service,
                                                     uint64_t recipient_handle,
                                                     uint64_t cookie) -> bool {
        if (!service)
            return false;
        for (auto it = service->death_links.begin();
             it != service->death_links.end(); ++it) {
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
            for (auto &entry : binder_services_) {
                auto &links = entry.second.death_links;
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
        size_t aligned_size =
            (size + static_cast<size_t>(mask)) & ~static_cast<size_t>(mask);
        if (ptr + aligned_size > heap_base_ + HEAP_SIZE)
            return 0;
        heap_bump_ = ptr + aligned_size;
        return ptr;
    };
    auto alloc_guest_string =
        [alloc_guest](guest_t *g, const std::string &text) -> uint64_t {
        size_t size = text.size() + 1;
        uint64_t ptr = alloc_guest(size);
        if (!ptr)
            return 0;
        guest_write(g, ptr, text.c_str(), size);
        return ptr;
    };
    auto append_value = [find_parcel](uint64_t parcel_handle,
                                      BinderParcelKind kind, uint64_t value,
                                      std::string text = {}) -> uint64_t {
        BinderParcel *parcel = find_parcel(parcel_handle);
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
    auto append_parcel_value = [find_parcel](
                                   uint64_t parcel_handle,
                                   BinderParcelValue value) -> uint64_t {
        BinderParcel *parcel = find_parcel(parcel_handle);
        if (!parcel)
            return STATUS_BAD_VALUE;
        parcel->values.push_back(std::move(value));
        parcel->cursor = parcel->values.size();
        return STATUS_OK;
    };
    auto read_value = [find_parcel](uint64_t parcel_handle,
                                    BinderParcelKind expected,
                                    BinderParcelValue *out) -> uint64_t {
        BinderParcel *parcel = find_parcel(parcel_handle);
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

    add("libbinder_ndk.so", "AIBinder_DeathRecipient_new", HVC_BINDER_DEATH_NEW,
        [this](guest_t *, const uint64_t a[8]) -> uint64_t {
            if (!a[0])
                return 0;
            uint64_t handle = next_binder_death_handle_++;
            binder_death_recipients_[handle] = {a[0], 0};
            std::fprintf(
                stderr, "[Binder] DeathRecipient_new -> 0x%llx onDied=0x%llx\n",
                (unsigned long long) handle, (unsigned long long) a[0]);
            return handle;
        });
    add("libbinder_ndk.so", "AIBinder_DeathRecipient_setOnUnlinked",
        HVC_BINDER_DEATH_SET_UNLINK,
        [find_death_recipient](guest_t *, const uint64_t a[8]) -> uint64_t {
            if (BinderDeathRecipient *recipient = find_death_recipient(a[0]))
                recipient->on_unlinked = a[1];
            return 0;
        });
    add("libbinder_ndk.so", "AIBinder_DeathRecipient_delete",
        HVC_BINDER_DEATH_DELETE,
        [this, find_death_recipient, remove_recipient_links](
            guest_t *, const uint64_t a[8]) -> uint64_t {
            if (!find_death_recipient(a[0]))
                return 0;
            remove_recipient_links(a[0]);
            binder_death_recipients_.erase(a[0]);
            std::fprintf(stderr, "[Binder] DeathRecipient_delete 0x%llx\n",
                         (unsigned long long) a[0]);
            return 0;
        });
    add("libbinder_ndk.so", "AIBinder_linkToDeath", HVC_BINDER_LINK_DEATH,
        [find_service, find_death_recipient, notify_death_unlinked](
            guest_t *, const uint64_t a[8]) -> uint64_t {
            BinderService *service = find_service(a[0]);
            BinderDeathRecipient *recipient = find_death_recipient(a[1]);
            if (!service || !recipient)
                return STATUS_BAD_VALUE;

            BinderService::DeathLink link = {
                a[1],
                a[2],
                recipient->on_binder_died,
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
            for (const BinderService::DeathLink &existing :
                 service->death_links) {
                if (existing.recipient_handle == a[1] &&
                    existing.cookie == a[2]) {
                    notify_death_unlinked(link);
                    return STATUS_ALREADY_EXISTS;
                }
            }

            service->death_links.push_back(link);
            std::fprintf(stderr,
                         "[Binder] linkToDeath binder=0x%llx recipient=0x%llx "
                         "cookie=0x%llx\n",
                         (unsigned long long) a[0], (unsigned long long) a[1],
                         (unsigned long long) a[2]);
            return STATUS_OK;
        });
    add("libbinder_ndk.so", "AIBinder_unlinkToDeath", HVC_BINDER_UNLINK_DEATH,
        [find_service, find_death_recipient, remove_death_link](
            guest_t *, const uint64_t a[8]) -> uint64_t {
            BinderService *service = find_service(a[0]);
            if (!service || !find_death_recipient(a[1]))
                return STATUS_BAD_VALUE;
            if (remove_death_link(service, a[1], a[2])) {
                std::fprintf(stderr,
                             "[Binder] unlinkToDeath binder=0x%llx "
                             "recipient=0x%llx cookie=0x%llx\n",
                             (unsigned long long) a[0],
                             (unsigned long long) a[1],
                             (unsigned long long) a[2]);
                return STATUS_OK;
            }
            return STATUS_NAME_NOT_FOUND;
        });
    add("libbinder_ndk.so", "__muplar_binder_kill", HVC_BINDER_MUPLAR_KILL,
        [find_service, retire_service](guest_t *,
                                       const uint64_t a[8]) -> uint64_t {
            BinderService *service = find_service(a[0]);
            if (!service)
                return STATUS_BAD_VALUE;
            return retire_service(a[0], service, "test hook")
                       ? STATUS_OK
                       : STATUS_DEAD_OBJECT;
        });

    add("libbinder_ndk.so", "AIBinder_Class_define", HVC_BINDER_CLASS_DEFINE,
        [this](guest_t *g, const uint64_t a[8]) -> uint64_t {
            std::string descriptor = guest_read_string(g, a[0]);
            if (descriptor.empty())
                return 0;

            uint64_t handle = next_binder_class_handle_++;
            BinderClass clazz;
            clazz.descriptor = descriptor;
            clazz.on_create = a[1];
            clazz.on_destroy = a[2];
            clazz.on_transact = a[3];
            binder_classes_[handle] = std::move(clazz);
            std::fprintf(
                stderr,
                "[Binder] Class_define(%s) -> 0x%llx onTransact=0x%llx\n",
                descriptor.c_str(), (unsigned long long) handle,
                (unsigned long long) a[3]);
            return handle;
        });
    add("libbinder_ndk.so", "AIBinder_Class_setOnDump",
        HVC_BINDER_CLASS_SET_DUMP,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });
    add("libbinder_ndk.so",
        "AIBinder_Class_setTransactionCodeToFunctionNameMap",
        HVC_BINDER_CLASS_SET_NAMES,
        [find_class](guest_t *g, const uint64_t a[8]) -> uint64_t {
            BinderClass *clazz = find_class(a[0]);
            size_t length = static_cast<size_t>(a[2]);
            if (!clazz || !a[1] || length > MAX_PARCEL_ARRAY_LENGTH)
                return 0;

            clazz->transaction_names.clear();
            clazz->transaction_name_gpas.clear();
            clazz->transaction_names.reserve(length);
            clazz->transaction_name_gpas.resize(length);
            for (size_t i = 0; i < length; ++i) {
                uint64_t name_gpa =
                    guest_read_u64(g, a[1] + static_cast<uint64_t>(i) * 8);
                clazz->transaction_names.push_back(
                    name_gpa ? guest_read_string(g, name_gpa) : std::string());
            }
            return 0;
        });
    add("libbinder_ndk.so", "AIBinder_Class_getDescriptor",
        HVC_BINDER_CLASS_GET_NAME,
        [find_class, alloc_guest_string](guest_t *g,
                                         const uint64_t a[8]) -> uint64_t {
            BinderClass *clazz = find_class(a[0]);
            if (!clazz)
                return 0;
            if (!clazz->descriptor_gpa)
                clazz->descriptor_gpa =
                    alloc_guest_string(g, clazz->descriptor);
            return clazz->descriptor_gpa;
        });
    add("libbinder_ndk.so", "AIBinder_Class_getFunctionName",
        HVC_BINDER_CLASS_GET_FN_NAME,
        [find_class, alloc_guest_string](guest_t *g,
                                         const uint64_t a[8]) -> uint64_t {
            BinderClass *clazz = find_class(a[0]);
            uint32_t code = static_cast<uint32_t>(a[1]);
            if (!clazz || code == 0)
                return 0;
            size_t index = static_cast<size_t>(code - 1);
            if (index >= clazz->transaction_names.size() ||
                clazz->transaction_names[index].empty()) {
                return 0;
            }
            if (index >= clazz->transaction_name_gpas.size())
                clazz->transaction_name_gpas.resize(
                    clazz->transaction_names.size());
            if (!clazz->transaction_name_gpas[index]) {
                clazz->transaction_name_gpas[index] =
                    alloc_guest_string(g, clazz->transaction_names[index]);
            }
            return clazz->transaction_name_gpas[index];
        });
    add("libbinder_ndk.so", "AIBinder_Class_disableInterfaceTokenHeader",
        HVC_BINDER_CLASS_DISABLE_TOKEN,
        [find_class](guest_t *, const uint64_t a[8]) -> uint64_t {
            if (BinderClass *clazz = find_class(a[0]))
                clazz->interface_token_header = false;
            return 0;
        });

    add("libbinder_ndk.so", "AIBinder_new", HVC_BINDER_NEW,
        [this, find_class, make_binder_service](
            guest_t *, const uint64_t a[8]) -> uint64_t {
            BinderClass *clazz = find_class(a[0]);
            if (!clazz)
                return 0;

            uint64_t user_data = a[1];
            if (clazz->on_create && guest_function_invoker_)
                user_data = static_cast<uint64_t>(
                    guest_function_invoker_(clazz->on_create, {a[1]}));

            uint64_t handle = next_binder_handle_++;
            binder_services_[handle] = make_binder_service(
                clazz->descriptor, 1, true, false, a[0], user_data);
            std::fprintf(stderr,
                         "[Binder] AIBinder_new class=0x%llx -> binder=0x%llx "
                         "user=0x%llx\n",
                         (unsigned long long) a[0], (unsigned long long) handle,
                         (unsigned long long) user_data);
            return handle;
        });
    add("libbinder_ndk.so", "AIBinder_associateClass", HVC_BINDER_ASSOC_CLASS,
        [find_service, find_class](guest_t *, const uint64_t a[8]) -> uint64_t {
            BinderService *service = find_service(a[0]);
            if (!service || !find_class(a[1]))
                return 0;
            service->class_handle = a[1];
            return 1;
        });
    add("libbinder_ndk.so", "AIBinder_getClass", HVC_BINDER_GET_CLASS,
        [find_service](guest_t *, const uint64_t a[8]) -> uint64_t {
            BinderService *service = find_service(a[0]);
            return service ? service->class_handle : 0;
        });
    add("libbinder_ndk.so", "AIBinder_getUserData", HVC_BINDER_GET_USER_DATA,
        [find_service](guest_t *, const uint64_t a[8]) -> uint64_t {
            BinderService *service = find_service(a[0]);
            return service ? service->user_data : 0;
        });

    add("libbinder_ndk.so", "AIBinder_Weak_new", HVC_BINDER_WEAK_NEW,
        [this, find_service](guest_t *, const uint64_t a[8]) -> uint64_t {
            BinderService *service = find_service(a[0]);
            if (!service)
                return 0;
            uint64_t handle = next_binder_weak_handle_++;
            binder_weaks_[handle] = {a[0]};
            std::fprintf(
                stderr, "[Binder] Weak_new binder=0x%llx -> weak=0x%llx\n",
                (unsigned long long) a[0], (unsigned long long) handle);
            return handle;
        });
    add("libbinder_ndk.so", "AIBinder_Weak_delete", HVC_BINDER_WEAK_DELETE,
        [this](guest_t *, const uint64_t a[8]) -> uint64_t {
            binder_weaks_.erase(a[0]);
            return 0;
        });
    add("libbinder_ndk.so", "AIBinder_Weak_promote", HVC_BINDER_WEAK_PROMOTE,
        [find_weak, find_service](guest_t *, const uint64_t a[8]) -> uint64_t {
            BinderWeak *weak = find_weak(a[0]);
            if (!weak)
                return 0;
            BinderService *service = find_service(weak->binder_handle);
            if (!service || !service->alive)
                return 0;
            service->ref_count++;
            return weak->binder_handle;
        });
    add("libbinder_ndk.so", "AIBinder_Weak_clone", HVC_BINDER_WEAK_CLONE,
        [this, find_weak](guest_t *, const uint64_t a[8]) -> uint64_t {
            BinderWeak *weak = find_weak(a[0]);
            if (!weak)
                return 0;
            uint64_t handle = next_binder_weak_handle_++;
            binder_weaks_[handle] = *weak;
            return handle;
        });
    add("libbinder_ndk.so", "AIBinder_Weak_lt", HVC_BINDER_WEAK_LT,
        [find_weak](guest_t *, const uint64_t a[8]) -> uint64_t {
            BinderWeak *lhs = find_weak(a[0]);
            BinderWeak *rhs = find_weak(a[1]);
            uint64_t lhs_binder = lhs ? lhs->binder_handle : 0;
            uint64_t rhs_binder = rhs ? rhs->binder_handle : 0;
            return lhs_binder < rhs_binder ? 1 : 0;
        });
    add("libbinder_ndk.so", "AIBinder_lt", HVC_BINDER_LT,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            return a[0] < a[1] ? 1 : 0;
        });
    add("libbinder_ndk.so", "AIBinder_setExtension", HVC_BINDER_SET_EXTENSION,
        [this, find_service](guest_t *, const uint64_t a[8]) -> uint64_t {
            BinderService *service = find_service(a[0]);
            BinderService *extension = find_service(a[1]);
            if (!a[0] || !a[1])
                return STATUS_UNEXPECTED_NULL;
            if (!service || !extension)
                return STATUS_BAD_VALUE;
            if (!service->alive || !extension->alive)
                return STATUS_DEAD_OBJECT;
            if (service->remote)
                return STATUS_INVALID_OPERATION;
            if (service->extension_handle == a[1])
                return STATUS_OK;

            extension->ref_count++;
            uint64_t old_extension = service->extension_handle;
            service->extension_handle = a[1];
            if (old_extension)
                release_binder_strong(old_extension);
            std::fprintf(stderr,
                         "[Binder] setExtension binder=0x%llx ext=0x%llx\n",
                         (unsigned long long) a[0], (unsigned long long) a[1]);
            return STATUS_OK;
        });
    add("libbinder_ndk.so", "AIBinder_getExtension", HVC_BINDER_GET_EXTENSION,
        [find_service](guest_t *g, const uint64_t a[8]) -> uint64_t {
            if (!a[0])
                return STATUS_UNEXPECTED_NULL;
            if (!a[1])
                return STATUS_BAD_VALUE;
            BinderService *service = find_service(a[0]);
            if (!service)
                return STATUS_BAD_VALUE;
            uint64_t extension_handle = service->extension_handle;
            if (!extension_handle) {
                guest_write_u64(g, a[1], 0);
                return STATUS_OK;
            }
            BinderService *extension = find_service(extension_handle);
            if (!extension || !extension->alive) {
                guest_write_u64(g, a[1], 0);
                return STATUS_DEAD_OBJECT;
            }
            extension->ref_count++;
            guest_write_u64(g, a[1], extension_handle);
            return STATUS_OK;
        });

    add("libbinder_ndk.so", "AIBinder_prepareTransaction",
        HVC_BINDER_PREPARE_TX,
        [this, find_service, find_class, create_parcel, append_value](
            guest_t *g, const uint64_t a[8]) -> uint64_t {
            BinderService *service = find_service(a[0]);
            if (!service || !a[1])
                return STATUS_BAD_VALUE;
            if (!service->alive)
                return STATUS_DEAD_OBJECT;
            BinderClass *clazz = service->class_handle
                                     ? find_class(service->class_handle)
                                     : nullptr;
            if (service->class_handle && !clazz)
                return STATUS_INVALID_OPERATION;
            uint64_t parcel = create_parcel(a[0], 0, false);
            if (clazz && clazz->interface_token_header) {
                uint64_t rc =
                    append_value(parcel, BinderParcelKind::InterfaceToken, 0,
                                 clazz->descriptor);
                if (rc != STATUS_OK) {
                    binder_parcels_.erase(parcel);
                    return rc;
                }
            }
            guest_write_u64(g, a[1], parcel);
            std::fprintf(
                stderr,
                "[Binder] prepareTransaction binder=0x%llx parcel=0x%llx\n",
                (unsigned long long) a[0], (unsigned long long) parcel);
            return STATUS_OK;
        });

    add("libbinder_ndk.so", "AIBinder_transact", HVC_BINDER_TRANSACT,
        [this, find_service, find_parcel, create_parcel, encode_parcel,
         decode_parcel](guest_t *g, const uint64_t a[8]) -> uint64_t {
            uint64_t binder = a[0];
            uint32_t code = static_cast<uint32_t>(a[1]);
            uint64_t in_handle = a[2] ? guest_read_u64(g, a[2]) : 0;
            uint64_t out_gpa = a[3];
            uint32_t flags = static_cast<uint32_t>(a[4]);

            BinderService *service = find_service(binder);
            if (!service)
                return STATUS_BAD_VALUE;
            BinderParcel *in = in_handle ? find_parcel(in_handle) : nullptr;
            if (in_handle && !in)
                return STATUS_BAD_VALUE;

            uint64_t out_handle = 0;
            uint64_t status = STATUS_OK;
            auto consume_input = [&]() {
                if (in_handle) {
                    binder_parcels_.erase(in_handle);
                    if (a[2])
                        guest_write_u64(g, a[2], 0);
                }
            };
            if (!service->alive) {
                consume_input();
                if (out_gpa)
                    guest_write_u64(g, out_gpa, 0);
                std::fprintf(stderr,
                             "[Binder] transact dead binder=0x%llx code=%u "
                             "in=0x%llx flags=0x%x\n",
                             (unsigned long long) binder, code,
                             (unsigned long long) in_handle, flags);
                return STATUS_DEAD_OBJECT;
            }

            if (!service->remote && service->class_handle &&
                guest_function_invoker_) {
                auto class_it = binder_classes_.find(service->class_handle);
                if (class_it == binder_classes_.end() ||
                    !class_it->second.on_transact) {
                    return STATUS_INVALID_OPERATION;
                }

                if (in)
                    in->cursor = 0;
                if (class_it->second.interface_token_header) {
                    if (!in || in->cursor >= in->values.size() ||
                        in->values[in->cursor].kind !=
                            BinderParcelKind::InterfaceToken ||
                        in->values[in->cursor].text !=
                            class_it->second.descriptor) {
                        consume_input();
                        if (out_gpa)
                            guest_write_u64(g, out_gpa, 0);
                        std::fprintf(stderr,
                                     "[Binder] local transact bad interface "
                                     "token binder=0x%llx code=%u\n",
                                     (unsigned long long) binder, code);
                        return STATUS_BAD_TYPE;
                    }
                    in->cursor++;
                }

                if (!(flags & FLAG_ONEWAY) && out_gpa) {
                    out_handle = create_parcel(binder, code, true);
                    guest_write_u64(g, out_gpa, out_handle);
                } else if (out_gpa) {
                    guest_write_u64(g, out_gpa, 0);
                }

                status = static_cast<uint64_t>(guest_function_invoker_(
                    class_it->second.on_transact,
                    {binder, code, in_handle, out_handle}));

                if (out_handle)
                    binder_parcels_[out_handle].cursor = 0;

                consume_input();

                std::fprintf(stderr,
                             "[Binder] local transact binder=0x%llx code=%u "
                             "in=0x%llx out=0x%llx status=%lld\n",
                             (unsigned long long) binder, code,
                             (unsigned long long) in_handle,
                             (unsigned long long) out_handle,
                             (long long) static_cast<int64_t>(status));
                return status;
            }

            if (service->daemon_owned) {
                services::MuplardClient client(service_socket_);
                std::string reply_payload;
                bool delivered = client.transact(service->name,
                                                 encode_parcel(in, code, flags),
                                                 reply_payload);
                if (!delivered) {
                    service->alive = false;
                    consume_input();
                    if (out_gpa)
                        guest_write_u64(g, out_gpa, 0);
                    return STATUS_DEAD_OBJECT;
                }
                if (!(flags & FLAG_ONEWAY) && out_gpa) {
                    std::vector<BinderParcelValue> reply_values;
                    if (!decode_parcel(reply_payload, reply_values)) {
                        consume_input();
                        guest_write_u64(g, out_gpa, 0);
                        return STATUS_BAD_VALUE;
                    }
                    out_handle = create_parcel(binder, code, true);
                    BinderParcel &out = binder_parcels_[out_handle];
                    BinderParcelValue status_value;
                    status_value.kind = BinderParcelKind::Status;
                    out.values.push_back(std::move(status_value));
                    for (BinderParcelValue &value : reply_values) {
                        if (value.kind != BinderParcelKind::InterfaceToken)
                            out.values.push_back(std::move(value));
                    }
                    guest_write_u64(g, out_gpa, out_handle);
                } else if (out_gpa) {
                    guest_write_u64(g, out_gpa, 0);
                }
                consume_input();
                std::fprintf(
                    stderr,
                    "[Binder] muplard transact service=%s code=%u out=0x%llx\n",
                    service->name.c_str(), code,
                    (unsigned long long) out_handle);
                return STATUS_OK;
            }

            if (!(flags & FLAG_ONEWAY) && out_gpa) {
                out_handle = create_parcel(binder, code, true);
                BinderParcel &out = binder_parcels_[out_handle];
                BinderParcelValue status_value;
                status_value.kind = BinderParcelKind::Status;
                out.values.push_back(std::move(status_value));

                bool have_i32 = false;
                int32_t first_i32 = 0;
                if (in) {
                    for (const BinderParcelValue &value : in->values) {
                        if (value.kind == BinderParcelKind::Int32 ||
                            value.kind == BinderParcelKind::Uint32) {
                            first_i32 = static_cast<int32_t>(value.value);
                            have_i32 = true;
                            break;
                        }
                    }
                }

                int32_t reply_value =
                    have_i32 ? first_i32 + static_cast<int32_t>(code)
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

            consume_input();

            std::fprintf(stderr,
                         "[Binder] transact binder=0x%llx code=%u in=0x%llx "
                         "out=0x%llx flags=0x%x\n",
                         (unsigned long long) binder, code,
                         (unsigned long long) in_handle,
                         (unsigned long long) out_handle, flags);
            return STATUS_OK;
        });

    add("libbinder_ndk.so", "AParcel_delete", HVC_PARCEL_DELETE,
        [this](guest_t *, const uint64_t a[8]) -> uint64_t {
            binder_parcels_.erase(a[0]);
            return 0;
        });
    add("libbinder_ndk.so", "AParcel_setDataPosition", HVC_PARCEL_SET_POS,
        [find_parcel](guest_t *, const uint64_t a[8]) -> uint64_t {
            BinderParcel *parcel = find_parcel(a[0]);
            int32_t position = static_cast<int32_t>(a[1]);
            if (!parcel || position < 0 ||
                static_cast<size_t>(position) > parcel->values.size()) {
                return STATUS_BAD_VALUE;
            }
            parcel->cursor = static_cast<size_t>(position);
            return STATUS_OK;
        });
    add("libbinder_ndk.so", "AParcel_getDataPosition", HVC_PARCEL_GET_POS,
        [find_parcel](guest_t *, const uint64_t a[8]) -> uint64_t {
            BinderParcel *parcel = find_parcel(a[0]);
            return parcel ? parcel->cursor : 0;
        });

    add("libbinder_ndk.so", "AParcel_writeStrongBinder",
        HVC_PARCEL_WRITE_BINDER,
        [append_value](guest_t *, const uint64_t a[8]) -> uint64_t {
            return append_value(a[0], BinderParcelKind::StrongBinder, a[1]);
        });
    add("libbinder_ndk.so", "AParcel_readStrongBinder", HVC_PARCEL_READ_BINDER,
        [read_value, find_service](guest_t *g,
                                   const uint64_t a[8]) -> uint64_t {
            BinderParcelValue value;
            uint64_t rc =
                read_value(a[0], BinderParcelKind::StrongBinder, &value);
            if (rc != STATUS_OK)
                return rc;
            if (BinderService *service = find_service(value.value))
                service->ref_count++;
            if (a[1])
                guest_write_u64(g, a[1], value.value);
            return STATUS_OK;
        });
    add("libbinder_ndk.so", "AParcel_writeParcelFileDescriptor",
        HVC_PARCEL_WRITE_FD,
        [append_value](guest_t *, const uint64_t a[8]) -> uint64_t {
            int32_t fd = static_cast<int32_t>(a[1]);
            return append_value(
                a[0], BinderParcelKind::ParcelFileDescriptor,
                static_cast<uint64_t>(static_cast<int64_t>(fd)));
        });
    add("libbinder_ndk.so", "AParcel_readParcelFileDescriptor",
        HVC_PARCEL_READ_FD,
        [read_value](guest_t *g, const uint64_t a[8]) -> uint64_t {
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
    add("libbinder_ndk.so", "AParcel_writeStatusHeader",
        HVC_PARCEL_WRITE_STATUS,
        [append_parcel_value, find_status](guest_t *,
                                           const uint64_t a[8]) -> uint64_t {
            BinderStatus *status = find_status(a[1]);
            BinderParcelValue value;
            value.kind = BinderParcelKind::Status;
            if (status) {
                value.value =
                    static_cast<uint64_t>(static_cast<int64_t>(status->status));
                value.elements.push_back(static_cast<uint64_t>(
                    static_cast<int64_t>(status->exception)));
                value.elements.push_back(static_cast<uint64_t>(
                    static_cast<int64_t>(status->service_error)));
                value.text = status->message;
            }
            return append_parcel_value(a[0], std::move(value));
        });
    add("libbinder_ndk.so", "AParcel_readStatusHeader", HVC_PARCEL_READ_STATUS,
        [find_parcel, create_status](guest_t *g,
                                     const uint64_t a[8]) -> uint64_t {
            BinderParcel *parcel = find_parcel(a[0]);
            if (!parcel || !a[1])
                return STATUS_BAD_VALUE;
            int32_t low_status = 0;
            int32_t exception = EX_NONE;
            int32_t service_error = 0;
            std::string message;
            if (parcel->cursor < parcel->values.size() &&
                parcel->values[parcel->cursor].kind ==
                    BinderParcelKind::Status) {
                const BinderParcelValue &value = parcel->values[parcel->cursor];
                low_status =
                    static_cast<int32_t>(static_cast<int64_t>(value.value));
                if (value.elements.size() > 0) {
                    exception = static_cast<int32_t>(
                        static_cast<int64_t>(value.elements[0]));
                } else if (low_status != 0) {
                    exception = EX_TRANSACTION_FAILED;
                }
                if (value.elements.size() > 1) {
                    service_error = static_cast<int32_t>(
                        static_cast<int64_t>(value.elements[1]));
                }
                message = value.text;
                parcel->cursor++;
            }
            uint64_t status_handle = create_status(
                exception, service_error, low_status, std::move(message));
            guest_write_u64(g, a[1], status_handle);
            return STATUS_OK;
        });
    add("libbinder_ndk.so", "AParcel_writeString", HVC_PARCEL_WRITE_STRING,
        [append_value](guest_t *g, const uint64_t a[8]) -> uint64_t {
            int32_t length = static_cast<int32_t>(a[2]);
            if (length < 0)
                return append_value(a[0], BinderParcelKind::String, 1, {});
            if (!a[1])
                return STATUS_BAD_VALUE;
            size_t size = static_cast<size_t>(std::min<int32_t>(length, 4096));
            std::string text(size, '\0');
            if (size > 0 && guest_read(g, a[1], text.data(), size) != 0)
                return STATUS_BAD_VALUE;
            return append_value(a[0], BinderParcelKind::String, 0,
                                std::move(text));
        });
    add("libbinder_ndk.so", "AParcel_readString", HVC_PARCEL_READ_STRING,
        [read_value, alloc_guest, this](guest_t *g,
                                        const uint64_t a[8]) -> uint64_t {
            if (!a[2] || !guest_function_invoker_)
                return STATUS_BAD_VALUE;

            BinderParcelValue value;
            uint64_t rc = read_value(a[0], BinderParcelKind::String, &value);
            if (rc != STATUS_OK)
                return rc;

            bool is_null = value.value != 0;
            int32_t length = is_null ? -1
                                     : static_cast<int32_t>(std::min<size_t>(
                                           value.text.size() + 1,
                                           static_cast<size_t>(INT32_MAX)));
            uint64_t out_buffer_gpa =
                alloc_guest(sizeof(uint64_t), alignof(uint64_t));
            if (!out_buffer_gpa)
                return STATUS_NO_MEMORY;
            guest_write_u64(g, out_buffer_gpa, 0);

            int64_t ok = guest_function_invoker_(
                a[2], {
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
                if (guest_write(g, buffer_gpa, value.text.c_str(),
                                value.text.size()) != 0)
                    return STATUS_BAD_VALUE;
                char nul = '\0';
                if (guest_write(g, buffer_gpa + value.text.size(), &nul,
                                sizeof(nul)) != 0)
                    return STATUS_BAD_VALUE;
            }

            std::fprintf(
                stderr,
                "[Binder] readString parcel=0x%llx len=%d buffer=0x%llx\n",
                (unsigned long long) a[0], length,
                (unsigned long long) buffer_gpa);
            return STATUS_OK;
        });
    add("libbinder_ndk.so", "AParcel_writeStringArray",
        HVC_PARCEL_WRITE_STRING_ARRAY,
        [append_parcel_value, alloc_guest, this](
            guest_t *g, const uint64_t a[8]) -> uint64_t {
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

            uint64_t out_length_gpa =
                alloc_guest(sizeof(uint32_t), alignof(uint32_t));
            if (!out_length_gpa)
                return STATUS_NO_MEMORY;

            value.elements.reserve(static_cast<size_t>(length));
            value.strings.reserve(static_cast<size_t>(length));
            for (int32_t i = 0; i < length; ++i) {
                guest_write_u32(g, out_length_gpa, 0);
                int64_t buffer_gpa = guest_function_invoker_(
                    a[3], {
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
                if (element_length < 0 ||
                    element_length > MAX_PARCEL_STRING_LENGTH)
                    return STATUS_BAD_VALUE;
                if (element_length > 0 && buffer_gpa == 0)
                    return STATUS_BAD_VALUE;

                std::string text(static_cast<size_t>(element_length), '\0');
                if (element_length > 0 &&
                    guest_read(g, static_cast<uint64_t>(buffer_gpa),
                               text.data(), text.size()) != 0) {
                    return STATUS_BAD_VALUE;
                }
                value.elements.push_back(0);
                value.strings.push_back(std::move(text));
            }

            return append_parcel_value(a[0], std::move(value));
        });
    add("libbinder_ndk.so", "AParcel_readStringArray",
        HVC_PARCEL_READ_STRING_ARRAY,
        [read_value, alloc_guest, this](guest_t *g,
                                        const uint64_t a[8]) -> uint64_t {
            if (!a[2] || !a[3] || !guest_function_invoker_)
                return STATUS_BAD_VALUE;

            BinderParcelValue value;
            uint64_t rc =
                read_value(a[0], BinderParcelKind::StringArray, &value);
            if (rc != STATUS_OK)
                return rc;

            bool is_null = value.value != 0;
            int32_t length = is_null ? -1
                                     : static_cast<int32_t>(std::min<size_t>(
                                           value.strings.size(),
                                           static_cast<size_t>(INT32_MAX)));
            int64_t array_ok = guest_function_invoker_(
                a[2], {
                          a[1],
                          static_cast<uint64_t>(static_cast<int64_t>(length)),
                      });
            if (!array_ok)
                return STATUS_NO_MEMORY;
            if (is_null)
                return STATUS_OK;

            uint64_t out_buffer_gpa =
                alloc_guest(sizeof(uint64_t), alignof(uint64_t));
            if (!out_buffer_gpa)
                return STATUS_NO_MEMORY;

            for (int32_t i = 0; i < length; ++i) {
                bool element_null =
                    i < static_cast<int32_t>(value.elements.size()) &&
                    value.elements[static_cast<size_t>(i)] != 0;
                int32_t element_length =
                    element_null
                        ? -1
                        : static_cast<int32_t>(std::min<size_t>(
                              value.strings[static_cast<size_t>(i)].size() + 1,
                              static_cast<size_t>(INT32_MAX)));
                guest_write_u64(g, out_buffer_gpa, 0);
                int64_t element_ok = guest_function_invoker_(
                    a[3], {
                              a[1],
                              static_cast<uint64_t>(static_cast<size_t>(i)),
                              static_cast<uint64_t>(
                                  static_cast<int64_t>(element_length)),
                              out_buffer_gpa,
                          });
                if (!element_ok)
                    return STATUS_NO_MEMORY;
                if (element_null)
                    continue;

                uint64_t buffer_gpa = guest_read_u64(g, out_buffer_gpa);
                if (!buffer_gpa)
                    return STATUS_NO_MEMORY;
                const std::string &text = value.strings[static_cast<size_t>(i)];
                if (!text.empty() &&
                    guest_write(g, buffer_gpa, text.data(), text.size()) != 0) {
                    return STATUS_BAD_VALUE;
                }
                char nul = '\0';
                if (guest_write(g, buffer_gpa + text.size(), &nul,
                                sizeof(nul)) != 0)
                    return STATUS_BAD_VALUE;
            }

            std::fprintf(stderr,
                         "[Binder] readStringArray parcel=0x%llx len=%d\n",
                         (unsigned long long) a[0], length);
            return STATUS_OK;
        });
    add("libbinder_ndk.so", "AParcel_writeParcelableArray",
        HVC_PARCEL_WRITE_PARCELABLE_ARRAY,
        [append_parcel_value, this](guest_t *,
                                    const uint64_t a[8]) -> uint64_t {
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
                    a[3], {
                              a[0],
                              a[1],
                              static_cast<uint64_t>(static_cast<size_t>(i)),
                          });
                if (element_status != static_cast<int64_t>(STATUS_OK))
                    return static_cast<uint64_t>(element_status);
            }

            std::fprintf(stderr,
                         "[Binder] writeParcelableArray parcel=0x%llx len=%d\n",
                         (unsigned long long) a[0], length);
            return STATUS_OK;
        });
    add("libbinder_ndk.so", "AParcel_readParcelableArray",
        HVC_PARCEL_READ_PARCELABLE_ARRAY,
        [read_value, this](guest_t *, const uint64_t a[8]) -> uint64_t {
            if (!a[2] || !a[3] || !guest_function_invoker_)
                return STATUS_BAD_VALUE;

            BinderParcelValue marker;
            uint64_t rc =
                read_value(a[0], BinderParcelKind::ParcelableArray, &marker);
            if (rc != STATUS_OK)
                return rc;

            bool is_null = marker.value != 0;
            int32_t length = is_null ? -1
                                     : static_cast<int32_t>(std::min<size_t>(
                                           marker.elements.size(),
                                           static_cast<size_t>(INT32_MAX)));
            int64_t array_ok = guest_function_invoker_(
                a[2], {
                          a[1],
                          static_cast<uint64_t>(static_cast<int64_t>(length)),
                      });
            if (!array_ok)
                return STATUS_NO_MEMORY;
            if (is_null)
                return STATUS_OK;

            for (int32_t i = 0; i < length; ++i) {
                int64_t element_status = guest_function_invoker_(
                    a[3], {
                              a[0],
                              a[1],
                              static_cast<uint64_t>(static_cast<size_t>(i)),
                          });
                if (element_status != static_cast<int64_t>(STATUS_OK))
                    return static_cast<uint64_t>(element_status);
            }

            std::fprintf(stderr,
                         "[Binder] readParcelableArray parcel=0x%llx len=%d\n",
                         (unsigned long long) a[0], length);
            return STATUS_OK;
        });

    add("libbinder_ndk.so", "AParcel_writeInt32", HVC_PARCEL_WRITE_I32,
        [append_value](guest_t *, const uint64_t a[8]) -> uint64_t {
            return append_value(a[0], BinderParcelKind::Int32,
                                static_cast<uint64_t>(static_cast<int64_t>(
                                    static_cast<int32_t>(a[1]))));
        });
    add("libbinder_ndk.so", "AParcel_writeUint32", HVC_PARCEL_WRITE_U32,
        [append_value](guest_t *, const uint64_t a[8]) -> uint64_t {
            return append_value(a[0], BinderParcelKind::Uint32,
                                static_cast<uint32_t>(a[1]));
        });
    add("libbinder_ndk.so", "AParcel_writeInt64", HVC_PARCEL_WRITE_I64,
        [append_value](guest_t *, const uint64_t a[8]) -> uint64_t {
            return append_value(a[0], BinderParcelKind::Int64, a[1]);
        });
    add("libbinder_ndk.so", "AParcel_writeUint64", HVC_PARCEL_WRITE_U64,
        [append_value](guest_t *, const uint64_t a[8]) -> uint64_t {
            return append_value(a[0], BinderParcelKind::Uint64, a[1]);
        });
    add("libbinder_ndk.so", "AParcel_writeFloat", HVC_PARCEL_WRITE_FLOAT,
        [append_value](guest_t *, const uint64_t a[8]) -> uint64_t {
            return append_value(a[0], BinderParcelKind::Float,
                                static_cast<uint32_t>(a[1]));
        });
    add("libbinder_ndk.so", "AParcel_writeDouble", HVC_PARCEL_WRITE_DOUBLE,
        [append_value](guest_t *, const uint64_t a[8]) -> uint64_t {
            return append_value(a[0], BinderParcelKind::Double, a[1]);
        });
    add("libbinder_ndk.so", "AParcel_writeBool", HVC_PARCEL_WRITE_BOOL,
        [append_value](guest_t *, const uint64_t a[8]) -> uint64_t {
            return append_value(a[0], BinderParcelKind::Bool, a[1] ? 1 : 0);
        });
    add("libbinder_ndk.so", "AParcel_writeChar", HVC_PARCEL_WRITE_CHAR,
        [append_value](guest_t *, const uint64_t a[8]) -> uint64_t {
            return append_value(a[0], BinderParcelKind::Char,
                                static_cast<uint16_t>(a[1]));
        });
    add("libbinder_ndk.so", "AParcel_writeByte", HVC_PARCEL_WRITE_BYTE,
        [append_value](guest_t *, const uint64_t a[8]) -> uint64_t {
            return append_value(a[0], BinderParcelKind::Byte,
                                static_cast<uint8_t>(a[1]));
        });
    add("libbinder_ndk.so", "AParcel_writeInt32Array",
        HVC_PARCEL_WRITE_I32_ARRAY,
        [append_parcel_value](guest_t *g, const uint64_t a[8]) -> uint64_t {
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
                int32_t item = static_cast<int32_t>(guest_read_u32(
                    g, a[1] + static_cast<uint64_t>(i) * sizeof(uint32_t)));
                value.elements.push_back(
                    static_cast<uint64_t>(static_cast<int64_t>(item)));
            }
            return append_parcel_value(a[0], std::move(value));
        });

    add("libbinder_ndk.so", "AParcel_readInt32", HVC_PARCEL_READ_I32,
        [read_value](guest_t *g, const uint64_t a[8]) -> uint64_t {
            BinderParcelValue value;
            uint64_t rc = read_value(a[0], BinderParcelKind::Int32, &value);
            if (rc == STATUS_OK && a[1])
                guest_write_u32(g, a[1], static_cast<uint32_t>(value.value));
            return rc;
        });
    add("libbinder_ndk.so", "AParcel_readUint32", HVC_PARCEL_READ_U32,
        [read_value](guest_t *g, const uint64_t a[8]) -> uint64_t {
            BinderParcelValue value;
            uint64_t rc = read_value(a[0], BinderParcelKind::Uint32, &value);
            if (rc == STATUS_OK && a[1])
                guest_write_u32(g, a[1], static_cast<uint32_t>(value.value));
            return rc;
        });
    add("libbinder_ndk.so", "AParcel_readInt64", HVC_PARCEL_READ_I64,
        [read_value](guest_t *g, const uint64_t a[8]) -> uint64_t {
            BinderParcelValue value;
            uint64_t rc = read_value(a[0], BinderParcelKind::Int64, &value);
            if (rc == STATUS_OK && a[1])
                guest_write_u64(g, a[1], value.value);
            return rc;
        });
    add("libbinder_ndk.so", "AParcel_readUint64", HVC_PARCEL_READ_U64,
        [read_value](guest_t *g, const uint64_t a[8]) -> uint64_t {
            BinderParcelValue value;
            uint64_t rc = read_value(a[0], BinderParcelKind::Uint64, &value);
            if (rc == STATUS_OK && a[1])
                guest_write_u64(g, a[1], value.value);
            return rc;
        });
    add("libbinder_ndk.so", "AParcel_readFloat", HVC_PARCEL_READ_FLOAT,
        [read_value](guest_t *g, const uint64_t a[8]) -> uint64_t {
            BinderParcelValue value;
            uint64_t rc = read_value(a[0], BinderParcelKind::Float, &value);
            if (rc == STATUS_OK && a[1]) {
                uint32_t bits = static_cast<uint32_t>(value.value);
                guest_write(g, a[1], &bits, sizeof(bits));
            }
            return rc;
        });
    add("libbinder_ndk.so", "AParcel_readDouble", HVC_PARCEL_READ_DOUBLE,
        [read_value](guest_t *g, const uint64_t a[8]) -> uint64_t {
            BinderParcelValue value;
            uint64_t rc = read_value(a[0], BinderParcelKind::Double, &value);
            if (rc == STATUS_OK && a[1])
                guest_write_u64(g, a[1], value.value);
            return rc;
        });
    add("libbinder_ndk.so", "AParcel_readBool", HVC_PARCEL_READ_BOOL,
        [read_value](guest_t *g, const uint64_t a[8]) -> uint64_t {
            BinderParcelValue value;
            uint64_t rc = read_value(a[0], BinderParcelKind::Bool, &value);
            if (rc == STATUS_OK && a[1]) {
                uint8_t b = value.value ? 1 : 0;
                guest_write(g, a[1], &b, sizeof(b));
            }
            return rc;
        });
    add("libbinder_ndk.so", "AParcel_readChar", HVC_PARCEL_READ_CHAR,
        [read_value](guest_t *g, const uint64_t a[8]) -> uint64_t {
            BinderParcelValue value;
            uint64_t rc = read_value(a[0], BinderParcelKind::Char, &value);
            if (rc == STATUS_OK && a[1]) {
                uint16_t ch = static_cast<uint16_t>(value.value);
                guest_write(g, a[1], &ch, sizeof(ch));
            }
            return rc;
        });
    add("libbinder_ndk.so", "AParcel_readByte", HVC_PARCEL_READ_BYTE,
        [read_value](guest_t *g, const uint64_t a[8]) -> uint64_t {
            BinderParcelValue value;
            uint64_t rc = read_value(a[0], BinderParcelKind::Byte, &value);
            if (rc == STATUS_OK && a[1]) {
                uint8_t b = static_cast<uint8_t>(value.value);
                guest_write(g, a[1], &b, sizeof(b));
            }
            return rc;
        });
    add("libbinder_ndk.so", "AParcel_readInt32Array", HVC_PARCEL_READ_I32_ARRAY,
        [read_value, alloc_guest, this](guest_t *g,
                                        const uint64_t a[8]) -> uint64_t {
            if (!a[2] || !guest_function_invoker_)
                return STATUS_BAD_VALUE;

            BinderParcelValue value;
            uint64_t rc =
                read_value(a[0], BinderParcelKind::Int32Array, &value);
            if (rc != STATUS_OK)
                return rc;

            bool is_null = value.value != 0;
            int32_t length = is_null ? -1
                                     : static_cast<int32_t>(std::min<size_t>(
                                           value.elements.size(),
                                           static_cast<size_t>(INT32_MAX)));
            uint64_t out_buffer_gpa =
                alloc_guest(sizeof(uint64_t), alignof(uint64_t));
            if (!out_buffer_gpa)
                return STATUS_NO_MEMORY;
            guest_write_u64(g, out_buffer_gpa, 0);

            int64_t ok = guest_function_invoker_(
                a[2], {
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
                    static_cast<int32_t>(static_cast<int64_t>(
                        value.elements[static_cast<size_t>(i)])));
                guest_write_u32(
                    g, buffer_gpa + static_cast<uint64_t>(i) * sizeof(uint32_t),
                    item);
            }

            std::fprintf(stderr,
                         "[Binder] readInt32Array parcel=0x%llx len=%d\n",
                         (unsigned long long) a[0], length);
            return STATUS_OK;
        });

    add("libbinder_ndk.so", "AStatus_newOk", HVC_STATUS_NEW_OK,
        [create_status](guest_t *, const uint64_t[8]) -> uint64_t {
            return create_status(EX_NONE, 0, 0, {});
        });
    add("libbinder_ndk.so", "AStatus_fromExceptionCode",
        HVC_STATUS_FROM_EXCEPTION,
        [create_status](guest_t *, const uint64_t a[8]) -> uint64_t {
            int32_t exception = static_cast<int32_t>(a[0]);
            return create_status(exception, 0, 0, {});
        });
    add("libbinder_ndk.so", "AStatus_fromExceptionCodeWithMessage",
        HVC_STATUS_FROM_EXCEPTION_MSG,
        [create_status](guest_t *g, const uint64_t a[8]) -> uint64_t {
            int32_t exception = static_cast<int32_t>(a[0]);
            return create_status(
                exception, 0, 0,
                a[1] ? guest_read_string(g, a[1]) : std::string());
        });
    add("libbinder_ndk.so", "AStatus_fromServiceSpecificError",
        HVC_STATUS_FROM_SERVICE,
        [create_status](guest_t *, const uint64_t a[8]) -> uint64_t {
            int32_t service_error = static_cast<int32_t>(a[0]);
            return create_status(EX_SERVICE_SPECIFIC, service_error, 0, {});
        });
    add("libbinder_ndk.so", "AStatus_fromServiceSpecificErrorWithMessage",
        HVC_STATUS_FROM_SERVICE_MSG,
        [create_status](guest_t *g, const uint64_t a[8]) -> uint64_t {
            int32_t service_error = static_cast<int32_t>(a[0]);
            return create_status(
                EX_SERVICE_SPECIFIC, service_error, 0,
                a[1] ? guest_read_string(g, a[1]) : std::string());
        });
    add("libbinder_ndk.so", "AStatus_fromStatus", HVC_STATUS_FROM_STATUS,
        [create_status](guest_t *, const uint64_t a[8]) -> uint64_t {
            int32_t status = static_cast<int32_t>(a[0]);
            int32_t exception = status == 0 ? EX_NONE : EX_TRANSACTION_FAILED;
            return create_status(exception, 0, status, {});
        });
    add("libbinder_ndk.so", "AStatus_isOk", HVC_STATUS_IS_OK,
        [find_status](guest_t *, const uint64_t a[8]) -> uint64_t {
            BinderStatus *status = find_status(a[0]);
            return status && status->exception == 0 && status->status == 0 ? 1
                                                                           : 0;
        });
    add("libbinder_ndk.so", "AStatus_getExceptionCode", HVC_STATUS_EXCEPTION,
        [find_status](guest_t *, const uint64_t a[8]) -> uint64_t {
            BinderStatus *status = find_status(a[0]);
            return status ? static_cast<uint64_t>(
                                static_cast<int64_t>(status->exception))
                          : static_cast<uint64_t>(
                                static_cast<int64_t>(EX_TRANSACTION_FAILED));
        });
    add("libbinder_ndk.so", "AStatus_getServiceSpecificError",
        HVC_STATUS_SERVICE_ERR,
        [find_status](guest_t *, const uint64_t a[8]) -> uint64_t {
            BinderStatus *status = find_status(a[0]);
            return status ? static_cast<uint64_t>(
                                static_cast<int64_t>(status->service_error))
                          : 0;
        });
    add("libbinder_ndk.so", "AStatus_getStatus", HVC_STATUS_STATUS,
        [find_status](guest_t *, const uint64_t a[8]) -> uint64_t {
            BinderStatus *status = find_status(a[0]);
            return status ? static_cast<uint64_t>(
                                static_cast<int64_t>(status->status))
                          : STATUS_BAD_VALUE;
        });
    add("libbinder_ndk.so", "AStatus_getMessage", HVC_STATUS_MESSAGE,
        [this, find_status](guest_t *g, const uint64_t a[8]) -> uint64_t {
            BinderStatus *status = find_status(a[0]);
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
    add("libbinder_ndk.so", "AStatus_getDescription", HVC_STATUS_DESCRIPTION,
        [alloc_guest_string, find_status](guest_t *g,
                                          const uint64_t a[8]) -> uint64_t {
            BinderStatus *status = find_status(a[0]);
            if (!status)
                return 0;
            char buffer[96];
            std::snprintf(
                buffer, sizeof(buffer), "exception=%d service=%d status=%d",
                status->exception, status->service_error, status->status);
            std::string description(buffer);
            if (!status->message.empty()) {
                description += " message=";
                description += status->message;
            }
            return alloc_guest_string(g, description);
        });
    add("libbinder_ndk.so", "AStatus_deleteDescription", HVC_STATUS_DELETE_DESC,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });
    add("libbinder_ndk.so", "AStatus_delete", HVC_STATUS_DELETE,
        [this](guest_t *, const uint64_t a[8]) -> uint64_t {
            binder_statuses_.erase(a[0]);
            return 0;
        });
}

// ── libEGL stubs
// ──────────────────────────────────────────────────────────────
//
// Strategy: map Android guest EGLDisplay/EGLContext/EGLSurface opaque handles
// to real ANGLE host objects.  The guest never dereferences these — it just
// passes them back to subsequent EGL calls — so we can use any stable integer.
//
// GUEST_EGL_DISPLAY / GUEST_EGL_CONTEXT / GUEST_EGL_SURFACE are the constants
// returned to the guest.  Internally we keep the real ANGLE objects in members.



// ── libGLESv2 stubs
// ───────────────────────────────────────────────────────────
//
// These call ANGLE directly on the host — no guest memory translation needed
// for most GL calls since the params are just integers / float bits.
// Buffer data (glBufferData, glTexImage2D) copies from guest memory first.



// ── C++ runtime stubs (libc++_shared.so, libandroid_support.so) ──────────────
//
// Phase 5.26 — C++ runtime dependency closure.
//
// Generated AIDL output and NDK wrapper libs pull in symbols from libc++_shared
// and libandroid_support.  These stubs are enough to satisfy the dynamic linker
// symbol resolution pass so the guest .so loads without aborting on PLT holes.
//
// Implementation philosophy:
//   __cxa_atexit / __cxa_finalize  — register/run destructors at exit.
//     We keep a host-side atexit list keyed by guest function pointer.
//   __cxa_guard_*   — once-initialisation guards; trivial boolean in guest mem.
//   operator new/delete  — delegate to our existing bump allocator / free stub.
//   __cxa_throw / personality / unwind  — log + terminate (no real unwind).
//   std::terminate, std::bad_alloc  — log + abort.

void AndroidRuntime::register_libcxx_stubs()
{
    constexpr uint64_t kSharedOwnersOffset = 8;
    constexpr uint64_t kWeakOwnersOffset = 16;

    auto refcount_add = [](guest_t *g, uint64_t ptr, uint64_t offset) {
        int64_t value = 0;
        guest_read(g, ptr + offset, &value, sizeof(value));
        ++value;
        guest_write(g, ptr + offset, &value, sizeof(value));
    };

    auto refcount_release = [](guest_t *g, uint64_t ptr, uint64_t offset) {
        int64_t value = 0;
        guest_read(g, ptr + offset, &value, sizeof(value));
        --value;
        guest_write(g, ptr + offset, &value, sizeof(value));
        return value;
    };

    // ── atexit destructor registration ─────────────────────────────────────
    // __cxa_atexit(void(*func)(void*), void* arg, void* dso_handle)
    add("libc++_shared.so", "__cxa_atexit", HVC_CXA_ATEXIT,
        [this](guest_t *, const uint64_t a[8]) -> uint64_t {
            // Record the destructor for later; for now just acknowledge.
            // a[0]=func a[1]=arg a[2]=dso_handle
            (void) this;
            if (a[0])
                std::fprintf(
                    stderr,
                    "[C++] __cxa_atexit fn=0x%llx arg=0x%llx (registered)\n",
                    (unsigned long long) a[0], (unsigned long long) a[1]);
            return 0;  // success
        });

    // __cxa_finalize(void* dso_handle) — run registered destructors
    add("libc++_shared.so", "__cxa_finalize", HVC_CXA_FINALIZE,
        [](guest_t *, const uint64_t[8]) -> uint64_t {
            std::fprintf(
                stderr, "[C++] __cxa_finalize (stub — skipping destructors)\n");
            return 0;
        });

    // ── exception handling ─────────────────────────────────────────────────
    // Real C++ exception unwinding needs an unwinder; we stub it to abort.
    add("libc++_shared.so", "__cxa_throw", HVC_CXA_THROW,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            std::fprintf(stderr, "[C++] __cxa_throw obj=0x%llx — aborting\n",
                         (unsigned long long) a[0]);
            ::abort();
            return 0;
        });

    add("libc++_shared.so", "__cxa_begin_catch", HVC_CXA_BEGIN_CATCH,
        [](guest_t *, const uint64_t a[8]) -> uint64_t {
            // Returns a pointer to the thrown object.
            return a[0];
        });

    add("libc++_shared.so", "__cxa_end_catch", HVC_CXA_END_CATCH,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });

    add("libc++_shared.so", "__cxa_rethrow", HVC_CXA_RETHROW,
        [](guest_t *, const uint64_t[8]) -> uint64_t {
            std::fprintf(stderr, "[C++] __cxa_rethrow — aborting\n");
            ::abort();
            return 0;
        });

    add("libc++_shared.so", "__cxa_current_exception_type",
        HVC_CXA_CURRENT_EXCEPTION,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });

    // __gxx_personality_v0 — DWARF-based stack unwinder entry; stub aborts.
    add("libc++_shared.so", "__gxx_personality_v0", HVC_GXX_PERSONALITY,
        [](guest_t *, const uint64_t[8]) -> uint64_t {
            std::fprintf(stderr,
                         "[C++] __gxx_personality_v0 called — aborting\n");
            ::abort();
            return 0;
        });

    add("libc++_shared.so", "_Unwind_Resume", HVC_UNWIND_RESUME,
        [](guest_t *, const uint64_t[8]) -> uint64_t {
            std::fprintf(stderr, "[C++] _Unwind_Resume — aborting\n");
            ::abort();
            return 0;
        });

    // ── once-initialisation guards ─────────────────────────────────────────
    // Guard layout (AArch64 ABI): byte 0 = initialised flag.
    // __cxa_guard_acquire returns 1 if init needed (byte==0), 0 if already
    // done.
    add("libc++_shared.so", "__cxa_guard_acquire", HVC_CXA_GUARD_ACQUIRE,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            if (!a[0])
                return 0;
            uint8_t flag = 0;
            guest_read(g, a[0], &flag, 1);
            if (flag)
                return 0;  // already initialised
            flag = 1;
            guest_write(g, a[0], &flag, 1);  // mark in-progress
            return 1;                        // caller must run initialiser
        });

    add("libc++_shared.so", "__cxa_guard_release", HVC_CXA_GUARD_RELEASE,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            if (!a[0])
                return 0;
            uint8_t flag = 1;
            guest_write(g, a[0], &flag, 1);
            return 0;
        });

    add("libc++_shared.so", "__cxa_guard_abort", HVC_CXA_GUARD_ABORT,
        [](guest_t *g, const uint64_t a[8]) -> uint64_t {
            if (!a[0])
                return 0;
            uint8_t flag = 0;
            guest_write(g, a[0], &flag, 1);
            return 0;
        });

    // ── pure/deleted virtual ───────────────────────────────────────────────
    add("libc++_shared.so", "__cxa_pure_virtual", HVC_CXA_PURE_VIRTUAL,
        [](guest_t *, const uint64_t[8]) -> uint64_t {
            std::fprintf(stderr,
                         "[C++] __cxa_pure_virtual called — aborting\n");
            ::abort();
            return 0;
        });

    add("libc++_shared.so", "__cxa_deleted_virtual", HVC_CXA_DELETED_VIRTUAL,
        [](guest_t *, const uint64_t[8]) -> uint64_t {
            std::fprintf(stderr,
                         "[C++] __cxa_deleted_virtual called — aborting\n");
            ::abort();
            return 0;
        });

    // ── operator new / delete ─────────────────────────────────────────────
    // Forward to our bump allocator (same arena as malloc).
    add("libc++_shared.so", "_Znwm", HVC_OP_NEW,  // operator new(size_t)
        [this](guest_t *g, const uint64_t a[8]) -> uint64_t {
            uint64_t sz = (a[0] + 15) & ~15ULL;
            if (!a[0] || heap_bump_ + sz > heap_base_ + HEAP_SIZE) {
                std::fprintf(stderr, "[C++] operator new(%llu) — heap full\n",
                             (unsigned long long) a[0]);
                ::abort();
            }
            uint64_t ptr = heap_bump_;
            heap_bump_ += sz;
            std::vector<uint8_t> z(sz, 0);
            guest_write(g, ptr, z.data(), sz);
            return ptr;
        });

    add("libc++_shared.so", "_ZnwmRKSt9nothrow_t",
        HVC_OP_NEW_NOTHROW,  // op new nothrow
        [this](guest_t *g, const uint64_t a[8]) -> uint64_t {
            uint64_t sz = (a[0] + 15) & ~15ULL;
            if (!a[0] || heap_bump_ + sz > heap_base_ + HEAP_SIZE)
                return 0;
            uint64_t ptr = heap_bump_;
            heap_bump_ += sz;
            std::vector<uint8_t> z(sz, 0);
            guest_write(g, ptr, z.data(), sz);
            return ptr;
        });

    add("libc++_shared.so", "_Znam",
        HVC_OP_NEW_ARRAY,  // operator new[](size_t)
        [this](guest_t *g, const uint64_t a[8]) -> uint64_t {
            uint64_t sz = (a[0] + 15) & ~15ULL;
            if (!a[0] || heap_bump_ + sz > heap_base_ + HEAP_SIZE)
                ::abort();
            uint64_t ptr = heap_bump_;
            heap_bump_ += sz;
            std::vector<uint8_t> z(sz, 0);
            guest_write(g, ptr, z.data(), sz);
            return ptr;
        });

    // operator delete — bump allocator doesn't reclaim, just accept the call.
    add("libc++_shared.so", "_ZdlPv", HVC_OP_DELETE,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });
    add("libc++_shared.so", "_ZdlPvm", HVC_OP_DELETE_SIZED,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });
    add("libc++_shared.so", "_ZdaPv", HVC_OP_DELETE_ARRAY,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });

    // ── std::terminate / std::bad_alloc ───────────────────────────────────
    add("libc++_shared.so", "_ZSt9terminatev", HVC_STD_TERMINATE,
        [](guest_t *, const uint64_t[8]) -> uint64_t {
            std::fprintf(stderr, "[C++] std::terminate()\n");
            ::abort();
            return 0;
        });

    add("libc++_shared.so", "_ZSt15set_terminatePFvvE", HVC_STD_UNEXPECTED,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });

    // std::bad_alloc::what() — return a static host string GPA
    static const char kBadAlloc[] = "std::bad_alloc";
    add("libc++_shared.so", "_ZNSt9bad_allocD1Ev", HVC_STD_BAD_ALLOC,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });

    add("libc++_shared.so", "_ZNKSt9bad_alloc4whatEv", HVC_STD_BAD_CAST,
        [](guest_t *, const uint64_t[8]) -> uint64_t {
            return reinterpret_cast<uint64_t>(kBadAlloc);
        });

    // ── RTTI / typeinfo ───────────────────────────────────────────────────
    // __dynamic_cast — we can't do real RTTI without type tables.
    // Return null (cast fails) so callers handle it gracefully.
    add("libc++_shared.so", "__dynamic_cast", HVC_DYNAMIC_CAST,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });

    // ── libc++ shared_ptr refcounts ────────────────────────────────────────
    // These normally use AArch64 atomic RMW helpers inside libc++_shared.so.
    // Muplar's host-driven Android path is single-threaded here, so simple
    // guest-memory refcount updates avoid guest LSE/LLSC faults.
    add("libc++_shared.so", "_ZNSt6__ndk114__shared_count12__add_sharedEv",
        HVC_SHARED_COUNT_ADD,
        [refcount_add](guest_t *g, const uint64_t a[8]) -> uint64_t {
            if (a[0])
                refcount_add(g, a[0], kSharedOwnersOffset);
            return 0;
        });
    add("libc++_shared.so", "_ZNSt6__ndk114__shared_count16__release_sharedEv",
        HVC_SHARED_COUNT_RELEASE,
        [refcount_release](guest_t *g, const uint64_t a[8]) -> uint64_t {
            if (!a[0])
                return 0;
            return refcount_release(g, a[0], kSharedOwnersOffset) == -1;
        });
    add("libc++_shared.so", "_ZNSt6__ndk119__shared_weak_count12__add_sharedEv",
        HVC_SHARED_COUNT_ADD,
        [refcount_add](guest_t *g, const uint64_t a[8]) -> uint64_t {
            if (a[0])
                refcount_add(g, a[0], kSharedOwnersOffset);
            return 0;
        });
    add("libc++_shared.so", "_ZNSt6__ndk119__shared_weak_count10__add_weakEv",
        HVC_SHARED_WEAK_ADD,
        [refcount_add](guest_t *g, const uint64_t a[8]) -> uint64_t {
            if (a[0])
                refcount_add(g, a[0], kWeakOwnersOffset);
            return 0;
        });
    add("libc++_shared.so",
        "_ZNSt6__ndk119__shared_weak_count16__release_sharedEv",
        HVC_SHARED_WEAK_RELEASE_SHARED,
        [refcount_release](guest_t *g, const uint64_t a[8]) -> uint64_t {
            if (!a[0])
                return 0;
            if (refcount_release(g, a[0], kSharedOwnersOffset) == -1)
                refcount_release(g, a[0], kWeakOwnersOffset);
            return 0;
        });
    add("libc++_shared.so",
        "_ZNSt6__ndk119__shared_weak_count14__release_weakEv",
        HVC_SHARED_WEAK_RELEASE_WEAK,
        [refcount_release](guest_t *g, const uint64_t a[8]) -> uint64_t {
            if (a[0])
                refcount_release(g, a[0], kWeakOwnersOffset);
            return 0;
        });
    add("libc++_shared.so", "_ZNSt6__ndk119__shared_weak_count4lockEv",
        HVC_SHARED_WEAK_LOCK,
        [refcount_add](guest_t *g, const uint64_t a[8]) -> uint64_t {
            if (!a[0])
                return 0;
            int64_t owners = -1;
            guest_read(g, a[0] + kSharedOwnersOffset, &owners, sizeof(owners));
            if (owners < 0)
                return 0;
            refcount_add(g, a[0], kSharedOwnersOffset);
            return a[0];
        });
    add("libc++_shared.so",
        "_ZNKSt6__ndk119__shared_weak_count13__get_deleterERKSt9type_info",
        HVC_SHARED_GET_DELETER,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 0; });

    // __cxa_bad_cast / __cxa_bad_typeid — just abort
    add("libc++_shared.so", "__cxa_bad_cast", HVC_TYPEINFO_NAME,
        [](guest_t *, const uint64_t[8]) -> uint64_t {
            std::fprintf(stderr, "[C++] __cxa_bad_cast\n");
            ::abort();
            return 0;
        });

    // ── android_getCpuFamily / android_getCpuFeatures / android_getCpuCount ──
    // Used by NDK cpu-features helper; games check this for NEON/AES presence.
    add("libandroid_support.so", "android_getCpuFamily", HVC_ANDROID_CPUFAMILY,
        [](guest_t *, const uint64_t[8]) -> uint64_t {
            return 4;  // ANDROID_CPU_FAMILY_ARM64
        });

    add("libandroid_support.so", "android_getCpuFeatures",
        HVC_ANDROID_CPUFEATURES, [](guest_t *, const uint64_t[8]) -> uint64_t {
            // ANDROID_CPU_ARM64_FEATURE_FP | SIMD | AES | PMULL | SHA1 | SHA2 |
            // CRC32
            return 0x7FULL;
        });

    add("libandroid_support.so", "android_getCpuCount", HVC_ANDROID_CPUCOUNT,
        [](guest_t *, const uint64_t[8]) -> uint64_t { return 8; });

    // ── Mirror all C++ symbols under the NDK wrapper sonames ──────────────
    // libandroid_support, libc++abi, libunwind all re-export the same symbols.
    for (const std::string &alias :
         {std::string("libc++abi.so"), std::string("libunwind.so"),
          std::string("libandroid_support.so")}) {
        auto &dst = sym_tables_[alias];
        for (auto &[sym, gpa] : sym_tables_["libc++_shared.so"])
            dst.emplace(sym, gpa);
    }

    // Also add into the libc table so RTLD_DEFAULT dlsym finds them
    for (auto &[sym, gpa] : sym_tables_["libc++_shared.so"])
        sym_tables_["libc.so"].emplace(sym, gpa);

    std::fprintf(stderr, "[C++] registered %zu C++ runtime stubs\n",
                 sym_tables_["libc++_shared.so"].size());
}

}  // namespace muplar::runtime::android
