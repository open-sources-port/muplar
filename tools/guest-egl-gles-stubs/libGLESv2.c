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

uint64_t glActiveTexture(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2536, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glAttachShader(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2517, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glBeginQuery(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x255E, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glBindBuffer(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2511, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glBindBufferRange(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2563, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glBindFramebuffer(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x252B, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glBindRenderbuffer(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x252E, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glBindSampler(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2564, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glBindTexture(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x250D, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glBindVertexArray(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x254B, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glBlendEquationSeparate(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x254C, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glBlendFunc(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2507, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glBlendFuncSeparate(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x253F, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glBlitFramebuffer(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2567, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glBufferData(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2512, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glBufferSubData(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x253B, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glClear(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2502, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glClearBufferfi(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2551, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glClearBufferfv(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x254D, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glClearBufferiv(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2552, a0, a1, a2, a3, a4, a5, a6, a7);
}

void glClearColor(float r, float g, float b, float a) {
    union { float f; uint64_t u; } ur, ug, ub, ua;
    ur.f = r; ug.f = g; ub.f = b; ua.f = a;
    bridge_call(0x2503, ur.u, ug.u, ub.u, ua.u, 0, 0, 0, 0);
}

void glClearDepthf(float depth) {
    union { float f; uint64_t u; } ud;
    ud.f = depth;
    bridge_call(0x2504, ud.u, 0, 0, 0, 0, 0, 0, 0);
}

uint64_t glClientWaitSync(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2540, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glColorMask(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2534, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glCompileShader(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2515, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glCompressedTexSubImage2D(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2558, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glCompressedTexSubImage3D(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2547, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glCreateProgram(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2516, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glCreateShader(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2513, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glCullFace(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x250A, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glDeleteBuffers(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2527, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glDeleteFramebuffers(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2568, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glDeleteProgram(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2529, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glDeleteQueries(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x255F, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glDeleteRenderbuffers(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2569, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glDeleteSamplers(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2559, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glDeleteShader(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2528, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glDeleteSync(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x256A, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glDeleteTextures(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2526, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glDeleteVertexArrays(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2553, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glDepthFunc(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2508, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glDepthMask(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2509, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glDetachShader(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2565, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glDisable(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2506, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glDisableVertexAttribArray(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2541, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glDrawArrays(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x251E, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glDrawBuffers(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x255A, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glDrawElements(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x251F, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glDrawRangeElements(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x253C, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glEnable(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2505, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glEnableVertexAttribArray(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x251C, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glEndQuery(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2542, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glFenceSync(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2548, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glFinish(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2531, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glFlush(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2532, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glFramebufferRenderbuffer(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2530, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glFramebufferTexture2D(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x252C, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glFramebufferTextureLayer(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x256B, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glFrontFace(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x250B, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glGenBuffers(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2510, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glGenFramebuffers(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x252A, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glGenQueries(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x254E, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glGenRenderbuffers(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x252D, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glGenSamplers(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x256C, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glGenTextures(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x250C, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glGenVertexArrays(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x253D, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glGenerateMipmap(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2549, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glGetAttribLocation(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x251A, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glGetError(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2500, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glGetFloatv(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x256D, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glGetIntegerv(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2524, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glGetProgramInfoLog(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x253A, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glGetProgramiv(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2538, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glGetQueryObjectuiv(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2560, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glGetShaderInfoLog(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2539, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glGetShaderiv(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2537, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glGetString(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2525, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glGetStringi(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2543, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glGetUniformBlockIndex(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x256E, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glGetUniformLocation(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x251B, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glGetVertexAttribiv(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2544, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glHint(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2561, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glInvalidateFramebuffer(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2554, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glIsEnabled(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x255B, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glLinkProgram(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2518, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glMapBufferRange(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2545, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glPixelStorei(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2535, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glPolygonOffset(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x254A, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glReadPixels(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x255C, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glRenderbufferStorage(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x252F, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glRenderbufferStorageMultisample(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2562, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glSamplerParameterf(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2555, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glSamplerParameteri(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x256F, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glScissor(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2533, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glShaderSource(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2514, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glTexImage2D(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x250E, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glTexParameteri(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x250F, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glTexStorage2D(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x253E, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glTexStorage3D(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2556, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glTexSubImage2D(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2566, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glTexSubImage3D(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x254F, a0, a1, a2, a3, a4, a5, a6, a7);
}

void glUniform1f(int location, float v) {
    union { float f; uint64_t u; } uv;
    uv.f = v;
    bridge_call(0x2521, location, uv.u, 0, 0, 0, 0, 0, 0);
}

uint64_t glUniform1i(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2520, a0, a1, a2, a3, a4, a5, a6, a7);
}

void glUniform4f(int location, float x, float y, float z, float w) {
    union { float f; uint64_t u; } ux, uy, uz, uw;
    ux.f = x; uy.f = y; uz.f = z; uw.f = w;
    bridge_call(0x2522, location, ux.u, uy.u, uz.u, uw.u, 0, 0, 0);
}

uint64_t glUniformBlockBinding(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2546, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glUniformMatrix4fv(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2523, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glUnmapBuffer(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2557, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glUseProgram(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2519, a0, a1, a2, a3, a4, a5, a6, a7);
}

void glVertexAttrib4f(unsigned int index, float x, float y, float z, float w) {
    union { float f; uint64_t u; } ux, uy, uz, uw;
    ux.f = x; uy.f = y; uz.f = z; uw.f = w;
    bridge_call(0x2570, index, ux.u, uy.u, uz.u, uw.u, 0, 0, 0);
}

uint64_t glVertexAttribI4ui(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2550, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glVertexAttribIPointer(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2571, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glVertexAttribPointer(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x251D, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glViewport(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x2501, a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t glWaitSync(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    return bridge_call(0x255D, a0, a1, a2, a3, a4, a5, a6, a7);
}
