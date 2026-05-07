/* Extended attribute syscall handlers
 *
 * Copyright 2026 elfuse contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/xattr.h>

#include "core/guest.h"

#include "syscall/abi.h"
#include "syscall/fs.h"
#include "syscall/internal.h"

/* macOS xattr API has extra position and options parameters vs Linux.
 * Linux: getxattr(path, name, value, size)
 * macOS: getxattr(path, name, value, size, position, options)
 * The xattr shim passes position=0, options=0 for normal operation, and
 * options=XATTR_NOFOLLOW for lgetxattr/lsetxattr/etc.
 */

#define LINUX_XATTR_NAME_MAX 255
#define LINUX_XATTR_SIZE_MAX 65536
#define LINUX_XATTR_CREATE 1
#define LINUX_XATTR_REPLACE 2

static int xattr_translate_flags(int flags, int *opts)
{
    if ((flags & ~(LINUX_XATTR_CREATE | LINUX_XATTR_REPLACE)) != 0 ||
        flags == (LINUX_XATTR_CREATE | LINUX_XATTR_REPLACE))
        return -LINUX_EINVAL;

    if (flags & LINUX_XATTR_CREATE)
        *opts |= XATTR_CREATE;
    if (flags & LINUX_XATTR_REPLACE)
        *opts |= XATTR_REPLACE;
    return 0;
}

static int64_t xattr_alloc_buf(uint64_t size, void **buf)
{
    if (size > LINUX_XATTR_SIZE_MAX)
        return -LINUX_E2BIG;
    if (size == 0) {
        *buf = NULL;
        return 0;
    }

    *buf = malloc((size_t) size);
    return *buf ? 0 : -LINUX_ENOMEM;
}

static int64_t xattr_copy_out_result(guest_t *g,
                                     uint64_t dst_gva,
                                     void *buf,
                                     ssize_t ret)
{
    if (ret < 0)
        return linux_errno();
    if (guest_write(g, dst_gva, buf, (size_t) ret) < 0)
        return -LINUX_EFAULT;
    return ret;
}

int64_t sys_getxattr(guest_t *g,
                     uint64_t path_gva,
                     uint64_t name_gva,
                     uint64_t value_gva,
                     uint64_t size,
                     int nofollow)
{
    char path[LINUX_PATH_MAX], name[LINUX_XATTR_NAME_MAX + 1];
    if (guest_read_str(g, path_gva, path, sizeof(path)) < 0)
        return -LINUX_EFAULT;
    if (guest_read_str(g, name_gva, name, sizeof(name)) < 0)
        return -LINUX_EFAULT;

    int opts = nofollow ? XATTR_NOFOLLOW : 0;

    if (size == 0) {
        ssize_t ret = getxattr(path, name, NULL, 0, 0, opts);
        return ret < 0 ? linux_errno() : ret;
    }

    void *buf;
    int64_t err = xattr_alloc_buf(size, &buf);
    if (err < 0)
        return err;

    ssize_t ret = getxattr(path, name, buf, (size_t) size, 0, opts);
    int64_t result = xattr_copy_out_result(g, value_gva, buf, ret);
    free(buf);
    return result;
}

int64_t sys_setxattr(guest_t *g,
                     uint64_t path_gva,
                     uint64_t name_gva,
                     uint64_t value_gva,
                     uint64_t size,
                     int flags,
                     int nofollow)
{
    char path[LINUX_PATH_MAX], name[LINUX_XATTR_NAME_MAX + 1];
    if (guest_read_str(g, path_gva, path, sizeof(path)) < 0)
        return -LINUX_EFAULT;
    if (guest_read_str(g, name_gva, name, sizeof(name)) < 0)
        return -LINUX_EFAULT;

    void *buf;
    int64_t err = xattr_alloc_buf(size, &buf);
    if (err < 0)
        return err;
    if (size > 0 && guest_read(g, value_gva, buf, (size_t) size) < 0) {
        free(buf);
        return -LINUX_EFAULT;
    }

    int opts = nofollow ? XATTR_NOFOLLOW : 0;
    err = xattr_translate_flags(flags, &opts);
    if (err < 0) {
        free(buf);
        return err;
    }

    int ret = setxattr(path, name, buf, (size_t) size, 0, opts);
    free(buf);
    return ret < 0 ? linux_errno() : 0;
}

int64_t sys_listxattr(guest_t *g,
                      uint64_t path_gva,
                      uint64_t list_gva,
                      uint64_t size,
                      int nofollow)
{
    char path[LINUX_PATH_MAX];
    if (guest_read_str(g, path_gva, path, sizeof(path)) < 0)
        return -LINUX_EFAULT;

    int opts = nofollow ? XATTR_NOFOLLOW : 0;

    if (size == 0) {
        ssize_t ret = listxattr(path, NULL, 0, opts);
        return ret < 0 ? linux_errno() : ret;
    }

    void *buf;
    int64_t err = xattr_alloc_buf(size, &buf);
    if (err < 0)
        return err;

    ssize_t ret = listxattr(path, buf, (size_t) size, opts);
    int64_t result = xattr_copy_out_result(g, list_gva, buf, ret);
    free(buf);
    return result;
}

int64_t sys_removexattr(guest_t *g,
                        uint64_t path_gva,
                        uint64_t name_gva,
                        int nofollow)
{
    char path[LINUX_PATH_MAX], name[LINUX_XATTR_NAME_MAX + 1];
    if (guest_read_str(g, path_gva, path, sizeof(path)) < 0)
        return -LINUX_EFAULT;
    if (guest_read_str(g, name_gva, name, sizeof(name)) < 0)
        return -LINUX_EFAULT;

    int opts = nofollow ? XATTR_NOFOLLOW : 0;
    int ret = removexattr(path, name, opts);
    return ret < 0 ? linux_errno() : 0;
}

int64_t sys_fgetxattr(guest_t *g,
                      int fd,
                      uint64_t name_gva,
                      uint64_t value_gva,
                      uint64_t size)
{
    host_fd_ref_t host_ref;
    if (host_fd_ref_open(fd, &host_ref) < 0)
        return -LINUX_EBADF;

    char name[LINUX_XATTR_NAME_MAX + 1];
    if (guest_read_str(g, name_gva, name, sizeof(name)) < 0) {
        host_fd_ref_close(&host_ref);
        return -LINUX_EFAULT;
    }

    if (size == 0) {
        ssize_t ret = fgetxattr(host_ref.fd, name, NULL, 0, 0, 0);
        host_fd_ref_close(&host_ref);
        return ret < 0 ? linux_errno() : ret;
    }

    void *buf;
    int64_t err = xattr_alloc_buf(size, &buf);
    if (err < 0) {
        host_fd_ref_close(&host_ref);
        return err;
    }

    ssize_t ret = fgetxattr(host_ref.fd, name, buf, (size_t) size, 0, 0);
    int64_t result = xattr_copy_out_result(g, value_gva, buf, ret);
    free(buf);
    host_fd_ref_close(&host_ref);
    return result;
}

int64_t sys_fsetxattr(guest_t *g,
                      int fd,
                      uint64_t name_gva,
                      uint64_t value_gva,
                      uint64_t size,
                      int flags)
{
    host_fd_ref_t host_ref;
    /* Linux: fsetxattr on an O_PATH fd returns EBADF (the descriptor lacks the
     * write reference required by mnt_want_write_file).
     */
    int64_t err = host_fd_ref_open_io(fd, &host_ref);
    if (err < 0)
        return err;

    char name[LINUX_XATTR_NAME_MAX + 1];
    if (guest_read_str(g, name_gva, name, sizeof(name)) < 0) {
        host_fd_ref_close(&host_ref);
        return -LINUX_EFAULT;
    }

    void *buf;
    err = xattr_alloc_buf(size, &buf);
    if (err < 0) {
        host_fd_ref_close(&host_ref);
        return err;
    }
    if (size > 0 && guest_read(g, value_gva, buf, (size_t) size) < 0) {
        free(buf);
        host_fd_ref_close(&host_ref);
        return -LINUX_EFAULT;
    }

    int opts = 0;
    err = xattr_translate_flags(flags, &opts);
    if (err < 0) {
        free(buf);
        host_fd_ref_close(&host_ref);
        return err;
    }

    int ret = fsetxattr(host_ref.fd, name, buf, (size_t) size, 0, opts);
    int64_t result = ret < 0 ? linux_errno() : 0;
    free(buf);
    host_fd_ref_close(&host_ref);
    return result;
}

int64_t sys_flistxattr(guest_t *g, int fd, uint64_t list_gva, uint64_t size)
{
    host_fd_ref_t host_ref;
    if (host_fd_ref_open(fd, &host_ref) < 0)
        return -LINUX_EBADF;

    if (size == 0) {
        ssize_t ret = flistxattr(host_ref.fd, NULL, 0, 0);
        host_fd_ref_close(&host_ref);
        return ret < 0 ? linux_errno() : ret;
    }

    void *buf;
    int64_t err = xattr_alloc_buf(size, &buf);
    if (err < 0) {
        host_fd_ref_close(&host_ref);
        return err;
    }

    ssize_t ret = flistxattr(host_ref.fd, buf, (size_t) size, 0);
    int64_t result = xattr_copy_out_result(g, list_gva, buf, ret);
    free(buf);
    host_fd_ref_close(&host_ref);
    return result;
}

int64_t sys_fremovexattr(guest_t *g, int fd, uint64_t name_gva)
{
    host_fd_ref_t host_ref;
    /* Linux: fremovexattr on an O_PATH fd returns EBADF, same reason as
     * fsetxattr above.
     */
    int64_t err = host_fd_ref_open_io(fd, &host_ref);
    if (err < 0)
        return err;

    char name[LINUX_XATTR_NAME_MAX + 1];
    if (guest_read_str(g, name_gva, name, sizeof(name)) < 0) {
        host_fd_ref_close(&host_ref);
        return -LINUX_EFAULT;
    }

    int ret = fremovexattr(host_ref.fd, name, 0);
    host_fd_ref_close(&host_ref);
    return ret < 0 ? linux_errno() : 0;
}
