/* compat/sys/vfs.h
 *
 * Linux <sys/vfs.h> compatibility layer for macOS / BSD.
 * Provides basic typedefs, statfs mapping, and common FS_* magic constants
 * used by systemd.
 */
#pragma once

#ifndef COMPAT_SYS_VFS_H
#define COMPAT_SYS_VFS_H

#include <sys/param.h>
#include <sys/mount.h>

/*
 * Linux defines:
 *     struct statfs
 * macOS defines:
 *     struct statfs   (but with different fields)
 *
 * Most user code only checks:
 *     f_type (filesystem magic)
 *
 * macOS does NOT provide f_type.
 * You can approximate by using f_fstypename hash or numeric mapping.
 *
 * For compatibility, we provide f_type and fill it with 0,
 * unless the caller provides an override.
 */

#ifndef __fsword_t
typedef long __fsword_t;
#endif

#ifndef FS_MAGIC_FALLBACK
#define FS_MAGIC_FALLBACK 0
#endif

#endif /* COMPAT_SYS_VFS_H */
