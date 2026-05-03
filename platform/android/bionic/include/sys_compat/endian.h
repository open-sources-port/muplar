// sys_compat/endian.h
#pragma once

#if defined(__APPLE__)
  #include <machine/endian.h>
  #include <libkern/OSByteOrder.h>
  #include <arpa/inet.h>

  // Define GNU-style aliases if needed
  #define __BYTE_ORDER    BYTE_ORDER
  #define __BIG_ENDIAN    BIG_ENDIAN
  #define __LITTLE_ENDIAN LITTLE_ENDIAN
  #define __PDP_ENDIAN    PDP_ENDIAN

  // Byte-swap helpers (map to macOS functions)
  #define bswap_16(x) OSSwapInt16(x)
  #define bswap_32(x) OSSwapInt32(x)
  #define bswap_64(x) OSSwapInt64(x)
  
  #define be32toh(x) OSSwapBigToHostInt32(x)
  // #define be32toh(x) ntohl(x)
  #define htobe32(x) htonl(x)

  #define be16toh(x) OSSwapBigToHostInt16(x)
  #define be32toh(x) OSSwapBigToHostInt32(x)
  #define be64toh(x) OSSwapBigToHostInt64(x)

  #define le16toh(x) OSSwapLittleToHostInt16(x)
  #define le32toh(x) OSSwapLittleToHostInt32(x)
  #define le64toh(x) OSSwapLittleToHostInt64(x)

  #define htole16(x) OSSwapHostToLittleInt16(x)
  #define htole32(x) OSSwapHostToLittleInt32(x)
  #define htole64(x) OSSwapHostToLittleInt64(x)

#else
  #error "Unsupported platform: need endian definitions"
#endif
