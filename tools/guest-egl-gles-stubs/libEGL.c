#include <stdint.h>

#if defined(__x86_64__)
#include <unistd.h>
#include <sys/syscall.h>

static inline uint64_t bridge_call(uint32_t nr, uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t arg6, uint64_t arg7) {
    uint64_t args[8] = {arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7};
    return syscall(999, (uint64_t)nr, (uint64_t)args);
}
#elif defined(__aarch64__)
static inline uint64_t bridge_call(uint32_t nr, uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t arg6, uint64_t arg7) {
    register uint64_t r0 __asm__("x0") = arg0;
    register uint64_t r1 __asm__("x1") = arg1;
    register uint64_t r2 __asm__("x2") = arg2;
    register uint64_t r3 __asm__("x3") = arg3;
    register uint64_t r4 __asm__("x4") = arg4;
    register uint64_t r5 __asm__("x5") = arg5;
    register uint64_t r6 __asm__("x6") = arg6;
    register uint64_t r7 __asm__("x7") = arg7;
    register uint64_t r8 __asm__("x8") = nr;
    __asm__ volatile("hvc #6"
                     : "+r"(r0)
                     : "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5), "r"(r6), "r"(r7), "r"(r8)
                     : "memory");
    return r0;
}
#endif

uint64_t eglBindAPI(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2402, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t eglChooseConfig(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2403, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t eglCreateContext(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2404, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t eglCreatePbufferSurface(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2406, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t eglCreateWindowSurface(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2405, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t eglDestroyContext(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2409, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t eglDestroySurface(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x240A, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t eglGetDisplay(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2400, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t eglGetError(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x240B, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t eglGetProcAddress(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2410, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t eglInitialize(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2401, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t eglMakeCurrent(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2407, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t eglQueryString(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x240C, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t eglReleaseThread(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x240F, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t eglSurfaceAttrib(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2411, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t eglSwapBuffers(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2408, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t eglSwapInterval(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x240D, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t eglTerminate(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x240E, a0, a1, a2, a3, a4, a5, a6, a7);
}
