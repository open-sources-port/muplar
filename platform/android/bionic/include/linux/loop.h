#pragma once

#ifdef __APPLE__
// Simulate linux/loop.h on macOS

#include <stdint.h>
#include <sys/ioctl.h>

#ifndef LO_NAME_SIZE
#define LO_NAME_SIZE 64
#endif

#ifndef LO_KEY_SIZE
#define LO_KEY_SIZE 32
#endif

// Loop device info struct (simplified)
struct loop_info64 {
    uint64_t lo_device;
    uint64_t lo_inode;
    uint64_t lo_rdevice;
    uint64_t lo_offset;
    uint64_t lo_sizelimit;
    uint32_t lo_number;
    uint32_t lo_encrypt_type;
    uint32_t lo_encrypt_key_size;
    uint32_t lo_flags;
    char lo_file_name[LO_NAME_SIZE];
    uint8_t lo_crypt_name[LO_NAME_SIZE];
    uint8_t lo_encrypt_key[LO_KEY_SIZE];
    uint64_t lo_init[2]; // just a placeholder
};

// Simulated ioctl commands (fake values)
#define LOOP_SET_FD        _IOW(0x4C, 0, int)
#define LOOP_CLR_FD        _IO(0x4C, 1)
#define LOOP_GET_STATUS64  _IOR(0x4C, 0x05, struct loop_info64)
#define LOOP_SET_STATUS64  _IOW(0x4C, 0x04, struct loop_info64)

#endif // __APPLE__
