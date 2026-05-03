#pragma once

#include <stdint.h>
#include <string.h>
#include <sys_compat/muplar_errno.h>

/* blkpg constants */
#define BLKPG_ADD_PARTITION     1
#define BLKPG_DEL_PARTITION     2
#define BLKPG_RESIZE_PARTITION  3
#define BLKPG                   0x1260
#define BLKRRPART               0x125f

/* blkpg partition struct */
struct blkpg_partition {
    int pno;
    char devname[64];
    int start;
    int length;
    int pvolid;
    int fstype;
};

/* blkpg ioctl argument struct */
struct blkpg_ioctl_arg {
    int op;         /* operation: add, del, resize */
    void *data;     /* pointer to struct blkpg_partition */
    unsigned int datalen;
};

/* simulate blkpg_ioctl — just return success */
/* stub blkpg_ioctl for macOS — ignore all arguments */
#define blkpg_ioctl(...) 0
