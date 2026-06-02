/* SPDX-License-Identifier: GPL-2.0 */
#ifndef CROSS_PLATFORM_LINUX_TYPES_H
#define CROSS_PLATFORM_LINUX_TYPES_H

#include <linux/config.h>
#include <stdint.h>
#include <time.h>

// typedef struct {
//     int fd[2];        // pipe file descriptors: fd[0] read, fd[1] write
//     char name[64];    // queue name
//     size_t msgsize;   // max message size
// } mqd_t;
typedef int mqd_t;

/* File descriptor set constants */
#undef __NFDBITS
#define __NFDBITS  (8 * sizeof(unsigned long))

#undef __FD_SETSIZE
#define __FD_SETSIZE 1024

#undef __FDSET_LONGS
#define __FDSET_LONGS (__FD_SETSIZE / __NFDBITS)

#undef __FDELT
#define __FDELT(d) ((d) / __NFDBITS)

#undef __FDMASK
#define __FDMASK(d) (1UL << ((d) % __NFDBITS))

/* fd_set-like struct */
typedef struct {
    unsigned long fds_bits[__FDSET_LONGS];
} __kernel_fd_set;

/* Manipulation macros/functions for __kernel_fd_set */

static inline void __kernel_FD_ZERO(__kernel_fd_set *set) {
    for (unsigned long i = 0; i < __FDSET_LONGS; i++) {
        set->fds_bits[i] = 0UL;
    }
}

static inline void __kernel_FD_SET(int fd, __kernel_fd_set *set) {
    set->fds_bits[__FDELT(fd)] |= __FDMASK(fd);
}

static inline void __kernel_FD_CLR(int fd, __kernel_fd_set *set) {
    set->fds_bits[__FDELT(fd)] &= ~__FDMASK(fd);
}

static inline int __kernel_FD_ISSET(int fd, const __kernel_fd_set *set) {
    return (set->fds_bits[__FDELT(fd)] & __FDMASK(fd)) != 0;
}

/* Signal handler type */
typedef void (*__kernel_sighandler_t)(int);

/* IPC key and message queue descriptor types */
typedef int __kernel_key_t;
typedef int __kernel_mqd_t;

/* Note:
 * - On Windows/macOS, use native signal and IPC APIs.
 * - This header provides Linux-kernel style types/macros only.
 */

typedef int8_t  __s8;
typedef uint8_t __u8;

typedef int16_t  __s16;
typedef uint16_t __u16;

typedef int32_t  __s32;
typedef uint32_t __u32;

#ifndef __s8
typedef int8_t __s8;
#endif

#ifndef __u8
typedef uint8_t __u8;
#endif

#ifndef __s16
typedef int16_t __s16;
#endif

#ifndef __u16
typedef uint16_t __u16;
#endif

#ifndef __s32
typedef int32_t __s32;
#endif

#ifndef __u32
typedef uint32_t __u32;
#endif

#ifndef __s64
typedef int64_t __s64;
#endif

#ifndef __u64
typedef uint64_t __u64;
#endif

/* Bitwise attribute for sparse checker compatibility (noop if unsupported) */
#ifndef __CHECKER__
#define __bitwise
#else
#define __bitwise __attribute__((bitwise))
#endif

/* Basic fixed-width integer types */
typedef int8_t    __s8;
typedef uint8_t   __u8;

typedef int16_t   __s16;
typedef uint16_t  __u16;

typedef int32_t   __s32;
typedef uint32_t  __u32;

typedef int64_t   __s64;
typedef uint64_t  __u64;

/* Bitwise endianness-marked types */

typedef __u16 __bitwise __le16;
typedef __u16 __bitwise __be16;

typedef __u32 __bitwise __le32;
typedef __u32 __bitwise __be32;

typedef __u64 __bitwise __le64;
typedef __u64 __bitwise __be64;

#if defined(__SIZEOF_INT128__)
    typedef __int128  __s128;
    typedef unsigned __int128 __u128;
#define HAS_INT128 1
#else
    // Fallback definition or error
    #define HAS_INT128 0
#endif

/* Bitwise checksum types */
typedef __u16 __bitwise __sum16;
typedef __u32 __bitwise __wsum;

/* Aligned 64-bit types (8-byte aligned) */
#if defined(__GNUC__) || defined(__clang__)
#define __aligned_u64 __u64 __attribute__((aligned(8)))
#define __aligned_s64 __s64 __attribute__((aligned(8)))
#define __aligned_be64 __be64 __attribute__((aligned(8)))
#define __aligned_le64 __le64 __attribute__((aligned(8)))
#else
/* fallback without attribute */
#define __aligned_u64 __u64
#define __aligned_s64 __s64
#define __aligned_be64 __be64
#define __aligned_le64 __le64
#endif

/* Bitwise poll type */
typedef unsigned __bitwise __poll_t;

#define __FD_SETSIZE 1024

/* Long types (platform-dependent) */
#if defined(_WIN64) || defined(__x86_64__) || defined(__aarch64__) || defined(__LP64__)
typedef uint64_t  __kernel_ulong_t;
typedef int64_t   __kernel_long_t;
#else
typedef uint32_t  __kernel_ulong_t;
typedef int32_t   __kernel_long_t;
#endif

/* Core kernel-like typedefs */
typedef __kernel_ulong_t __kernel_size_t;
typedef __kernel_long_t  __kernel_ssize_t;
typedef __kernel_long_t  __kernel_ptrdiff_t;
typedef __kernel_long_t  __kernel_clock_t;
typedef void*            __kernel_caddr_t;

typedef __u32 __kernel_uid32_t;
typedef __u32 __kernel_gid32_t;
typedef __u16 __kernel_uid16_t;
typedef __u16 __kernel_gid16_t;

typedef __u16 __kernel_old_uid_t;
typedef __u16 __kernel_old_gid_t;

//typedef __kernel_long_t __kernel_pid_t;
typedef int __kernel_pid_t;
typedef int __kernel_uid_t;
typedef int __kernel_gid_t;

typedef __kernel_long_t __kernel_off_t;
typedef __u64           __kernel_loff_t;
typedef __kernel_ulong_t __kernel_ino_t;
typedef __u32           __kernel_mode_t;
typedef __s32           __kernel_daddr_t;
typedef __s32           __kernel_key_t;
typedef __s32           __kernel_suseconds_t;
typedef __s32           __kernel_timer_t;
// typedef __s32           __kernel_clockid_t;
typedef clockid_t __kernel_clockid_t;
typedef __s32           __kernel_mqd_t;

#define DECLARE_BITMAP(name,bits) \
	unsigned long name[BITS_TO_LONGS(bits)]

typedef unsigned char u8;
typedef signed char   s8;
typedef unsigned short u16;
typedef signed short   s16;
typedef unsigned int   u32;
typedef signed int     s32;
typedef unsigned long long u64;
typedef signed long long   s64;

#ifdef __SIZEOF_INT128__
typedef __s128 s128;
typedef __u128 u128;
#endif

/* Kernel-like typedefs mapped to cross-platform aliases */
typedef u32 __kernel_dev_t;
// typedef __kernel_fd_set  fd_set;
// typedef __kernel_dev_t   dev_t;
// typedef __kernel_ulong_t ino_t;
// typedef __kernel_mode_t  mode_t;
typedef unsigned short   umode_t;
// typedef u32              nlink_t;
typedef __kernel_off_t   off_t;
typedef __kernel_pid_t   pid_t;
typedef __kernel_daddr_t daddr_t;
typedef __kernel_key_t   key_t;
typedef __kernel_suseconds_t suseconds_t;
typedef __kernel_timer_t     timer_t;
typedef __kernel_clockid_t   clockid_t;
// typedef __kernel_mqd_t       mqd_t;

// typedef _Bool bool;

typedef __kernel_uid32_t uid_t;
typedef __kernel_gid32_t gid_t;
typedef __kernel_uid16_t uid16_t;
typedef __kernel_gid16_t gid16_t;

#ifndef _MSC_VER
  typedef unsigned long uintptr_t;
  typedef long           intptr_t;
#else
  #include <stdint.h>
#endif

#ifdef CONFIG_HAVE_UID16
typedef __kernel_old_uid_t old_uid_t;
typedef __kernel_old_gid_t old_gid_t;
#endif

#if defined(__APPLE__)
typedef off_t loff_t;
#elif defined(__GNUC__)
typedef __kernel_loff_t loff_t;
#endif

#ifndef _SIZE_T
typedef __kernel_size_t size_t;
#define _SIZE_T
#endif

#ifndef _SSIZE_T
typedef __kernel_ssize_t ssize_t;
#define _SSIZE_T
#endif

#ifndef _PTRDIFF_T
typedef __kernel_ptrdiff_t ptrdiff_t;
#define _PTRDIFF_T
#endif

#ifndef _CLOCK_T
typedef __kernel_clock_t clock_t;
#define _CLOCK_T
#endif

#ifndef _CADDR_T
typedef __kernel_caddr_t caddr_t;
#define _CADDR_T
#endif

/* BSD + SYSV aliases */
typedef unsigned char  u_char, unchar;
typedef unsigned short u_short, ushort;
typedef unsigned int   u_int,   uint;
typedef unsigned long  u_long,  ulong;

#ifndef __BIT_TYPES_DEFINED__
#define __BIT_TYPES_DEFINED__
typedef u8  u_int8_t;
typedef s8  int8_t;
typedef u16 u_int16_t;
typedef s16 int16_t;
typedef u32 u_int32_t;
typedef s32 int32_t;
#endif

typedef u8  uint8_t;
typedef u16 uint16_t;
typedef u32 uint32_t;
#if defined(__GNUC__)
typedef u64 uint64_t, u_int64_t;
typedef s64 int64_t;
#endif

#define aligned_u64  __aligned_u64
#define aligned_be64 __aligned_be64
#define aligned_le64 __aligned_le64

typedef s64 ktime_t;
typedef u64 sector_t;
//typedef u64 blkcnt_t;
typedef unsigned long long u64_blkcnt_t;
#define blkcnt_t __darwin_blkcnt_t
#define pgoff_t unsigned long

#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
typedef u64 dma_addr_t;
#else
typedef u32 dma_addr_t;
#endif

typedef unsigned int __bitwise gfp_t;
typedef unsigned int __bitwise slab_flags_t;
typedef unsigned int __bitwise fmode_t;

#ifdef CONFIG_PHYS_ADDR_T_64BIT
typedef u64 phys_addr_t;
#else
typedef u32 phys_addr_t;
#endif

typedef phys_addr_t resource_size_t;
typedef unsigned long irq_hw_number_t;

typedef struct { int counter; } atomic_t;
#define ATOMIC_INIT(i) { (i) }

#ifdef CONFIG_64BIT
typedef struct { s64 counter; } atomic64_t;
#endif

typedef struct { atomic_t refcnt; } rcuref_t;
#define RCUREF_INIT(i) { .refcnt = ATOMIC_INIT(i - 1) }

struct list_head {
	struct list_head *next, *prev;
};

struct hlist_node {
	struct hlist_node *next, **pprev;
};

struct hlist_head {
	struct hlist_node *first;
};

struct ustat {
	__kernel_daddr_t f_tfree;
#ifdef CONFIG_ARCH_32BIT_USTAT_F_TINODE
	unsigned int f_tinode;
#else
	unsigned long f_tinode;
#endif
	char f_fname[6];
	char f_fpack[6];
};

#if defined(_MSC_VER)
  #if defined(_WIN64)
    #define PTR_ALIGN 8
  #else
    #define PTR_ALIGN 4
  #endif
  #define ALIGNED_PTR __declspec(align(PTR_ALIGN))
  ALIGNED_PTR struct callback_head {
	struct callback_head *next;
	void (*func)(struct callback_head *head);
  };
#else
  #define ALIGNED_PTR __attribute__((aligned(sizeof(void *))))
  struct callback_head {
	struct callback_head *next;
	void (*func)(struct callback_head *head);
  } ALIGNED(sizeof(void *));
#endif

#define rcu_head callback_head

typedef void (*rcu_callback_t)(struct rcu_head *head);
typedef void (*call_rcu_func_t)(struct rcu_head *head, rcu_callback_t func);

typedef void (*swap_r_func_t)(void *a, void *b, int size, const void *priv);
typedef void (*swap_func_t)(void *a, void *b, int size);
typedef int  (*cmp_r_func_t)(const void *a, const void *b, const void *priv);
typedef int  (*cmp_func_t)(const void *a, const void *b);

#endif /* CROSS_PLATFORM_LINUX_TYPES_H */
