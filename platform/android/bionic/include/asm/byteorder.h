#ifndef _ASM_BYTEORDER_H
#define _ASM_BYTEORDER_H

#include <machine/endian.h>
#include <libkern/OSByteOrder.h>

/* Linux compatibility layer */
#define __LITTLE_ENDIAN LITTLE_ENDIAN
#define __BIG_ENDIAN    BIG_ENDIAN
#define __BYTE_ORDER    BYTE_ORDER

#endif
