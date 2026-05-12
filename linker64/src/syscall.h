/* syscall.h — raw AArch64 Linux syscall wrappers (no libc)
 *
 * All functions are inline so the linker binary stays single-file and
 * requires no additional linkage.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

/* AArch64 Linux syscall numbers */
#define SYS_read        63
#define SYS_write       64
#define SYS_openat      56
#define SYS_close       57
#define SYS_fstat       80
#define SYS_lseek       62
#define SYS_mmap       222
#define SYS_mprotect   226
#define SYS_munmap     215
#define SYS_exit_group  94

/* mmap / mprotect flags */
#define PROT_READ    1
#define PROT_WRITE   2
#define PROT_EXEC    4
#define PROT_NONE    0
#define MAP_SHARED   1
#define MAP_PRIVATE  2
#define MAP_FIXED    0x10
#define MAP_ANON     0x20
#define MAP_ANONYMOUS 0x20

/* open flags */
#define O_RDONLY     0
#define AT_FDCWD    -100

/* ELF segment flags */
#define PF_X 1
#define PF_W 2
#define PF_R 4

/* Inline syscall stubs */

static inline long __syscall0(long nr) {
    register long x8 __asm__("x8") = nr;
    register long x0 __asm__("x0");
    __asm__ volatile("svc #0" : "=r"(x0) : "r"(x8) : "memory", "cc");
    return x0;
}

static inline long __syscall1(long nr, long a) {
    register long x8 __asm__("x8") = nr;
    register long x0 __asm__("x0") = a;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8) : "memory", "cc");
    return x0;
}

static inline long __syscall2(long nr, long a, long b) {
    register long x8 __asm__("x8") = nr;
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1) : "memory", "cc");
    return x0;
}

static inline long __syscall3(long nr, long a, long b, long c) {
    register long x8 __asm__("x8") = nr;
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory", "cc");
    return x0;
}

static inline long __syscall4(long nr, long a, long b, long c, long d) {
    register long x8 __asm__("x8") = nr;
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    register long x3 __asm__("x3") = d;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3)
                     : "memory", "cc");
    return x0;
}

static inline long __syscall6(long nr, long a, long b, long c,
                               long d, long e, long f) {
    register long x8 __asm__("x8") = nr;
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    register long x3 __asm__("x3") = d;
    register long x4 __asm__("x4") = e;
    register long x5 __asm__("x5") = f;
    __asm__ volatile("svc #0" : "+r"(x0)
                     : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5)
                     : "memory", "cc");
    return x0;
}

static inline void sys_exit(int code) {
    __syscall1(SYS_exit_group, code);
    __builtin_unreachable();
}

static inline long sys_write(int fd, const void *buf, size_t n) {
    return __syscall3(SYS_write, fd, (long)buf, (long)n);
}

static inline long sys_openat(int dirfd, const char *path, int flags) {
    return __syscall4(SYS_openat, dirfd, (long)path, flags, 0);
}

static inline long sys_close(int fd) {
    return __syscall1(SYS_close, fd);
}

static inline long sys_read(int fd, void *buf, size_t n) {
    return __syscall3(SYS_read, fd, (long)buf, (long)n);
}

static inline long sys_lseek(int fd, long off, int whence) {
    return __syscall3(SYS_lseek, fd, off, whence);
}

static inline void *sys_mmap(void *addr, size_t len, int prot,
                              int flags, int fd, long off) {
    return (void *)__syscall6(SYS_mmap, (long)addr, (long)len,
                              prot, flags, fd, off);
}

static inline long sys_mprotect(void *addr, size_t len, int prot) {
    return __syscall3(SYS_mprotect, (long)addr, (long)len, prot);
}

static inline long sys_munmap(void *addr, size_t len) {
    return __syscall2(SYS_munmap, (long)addr, (long)len);
}
