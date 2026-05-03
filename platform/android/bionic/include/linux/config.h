#ifndef CROSS_PLATFORM_CONFIG_H
#define CROSS_PLATFORM_CONFIG_H

/* Detect 64-bit platforms */
#ifndef CONFIG_64BIT
#if defined(_WIN64) || defined(__x86_64__) || defined(__aarch64__)
#define CONFIG_64BIT 1
#define CONFIG_PHYS_ADDR_T_64BIT 1
#define CONFIG_ARCH_DMA_ADDR_T_64BIT 1
#else
#define CONFIG_64BIT 0
#define CONFIG_PHYS_ADDR_T_64BIT 0
#define CONFIG_ARCH_DMA_ADDR_T_64BIT 0
#endif
#endif

/* Convert bits to how many unsigned longs are needed */
#ifndef BITS_TO_LONGS
#define BITS_TO_LONGS(bits) (((bits) + (sizeof(unsigned long)*8 - 1)) / (sizeof(unsigned long)*8))
#endif

/* Compiler-agnostic alignment macro */
#if defined(_MSC_VER)
  #define ALIGNED(x) __declspec(align(x))
#elif defined(__GNUC__) || defined(__clang__)
  #define ALIGNED(x) __attribute__((aligned(x)))
#else
  #define ALIGNED(x)
#endif

#endif /* CROSS_PLATFORM_CONFIG_H */
