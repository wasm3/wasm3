//
//  m3_config_platforms.h
//
//  Created by Volodymyr Shymanskyy on 11/20/19.
//  Copyright © 2019 Volodymyr Shymanskyy. All rights reserved.
//

#ifndef m3_config_platforms_h
#define m3_config_platforms_h

/*
 * Helpers
 */

#define M3_STR__(x) #x
#define M3_STR(x)   M3_STR__(x)

#define M3_CONCAT__(a,b) a##b
#define M3_CONCAT(a,b)   M3_CONCAT__(a,b)

// Post-increment by a specific number
#define M3_INC(x,n) (((x)+=(n))-(n))

# if !defined(__cplusplus)
#   define not      !
#   define and      &&
#   define or       ||
# endif

/*
 * Detect platform
 */

# if defined(__clang__)
#  define M3_COMPILER_CLANG 1
# elif defined(__INTEL_COMPILER)
#  define M3_COMPILER_ICC 1
# elif defined(__GNUC__) || defined(__GNUG__)
#  define M3_COMPILER_GCC 1
# elif defined(_MSC_VER)
#  define M3_COMPILER_MSVC 1
# else
#  warning "Compiler not detected"
# endif

# if defined(M3_COMPILER_CLANG) || defined(M3_COMPILER_GCC)
#  if defined(__wasm__)
#   define M3_ARCH "wasm"
#  elif defined(__x86_64__)
#   define M3_ARCH "x86_64"
#  elif defined(__i386__)
#   define M3_ARCH "x86"
#  elif defined(__aarch64__)
#   define M3_ARCH "arm64-v8a"
#  elif defined(__arm__)
#   if defined(__ARM_ARCH_7A__)
#    if defined(__ARM_NEON__)
#     if defined(__ARM_PCS_VFP)
#      define M3_ARCH "armeabi-v7a/NEON (hard-float)"
#     else
#      define M3_ARCH "armeabi-v7a/NEON"
#     endif
#    else
#     if defined(__ARM_PCS_VFP)
#      define M3_ARCH "armeabi-v7a (hard-float)"
#     else
#      define M3_ARCH "armeabi-v7a"
#     endif
#    endif
#   else
#    define M3_ARCH "armeabi"
#   endif
#  elif defined(__mips64)
#   define M3_ARCH "mips64"
#  elif defined(__mips__)
#   define M3_ARCH "mips"
#  elif defined(__xtensa__)
#   define M3_ARCH "xtensa"
#  elif defined(__riscv)
#   if defined(__riscv_32e)
#    define _M3_ARCH_RV "rv32e"
#   elif __riscv_xlen == 128
#    define _M3_ARCH_RV "rv128i"
#   elif __riscv_xlen == 64
#    define _M3_ARCH_RV "rv64i"
#   elif __riscv_xlen == 32
#    define _M3_ARCH_RV "rv32i"
#   endif
#   if defined(__riscv_muldiv)
#    define _M3_ARCH_RV_M _M3_ARCH_RV "m"
#   else
#    define _M3_ARCH_RV_M _M3_ARCH_RV
#   endif
#   if defined(__riscv_atomic)
#    define _M3_ARCH_RV_A _M3_ARCH_RV_M "a"
#   else
#    define _M3_ARCH_RV_A _M3_ARCH_RV_M
#   endif
#   if defined(__riscv_flen)
#    define _M3_ARCH_RV_F _M3_ARCH_RV_A "f"
#   else
#    define _M3_ARCH_RV_F _M3_ARCH_RV_A
#   endif
#   if defined(__riscv_flen) && __riscv_flen >= 64
#    define _M3_ARCH_RV_D _M3_ARCH_RV_F "d"
#   else
#    define _M3_ARCH_RV_D _M3_ARCH_RV_F
#   endif
#   if defined(__riscv_compressed)
#    define _M3_ARCH_RV_C _M3_ARCH_RV_D "c"
#   else
#    define _M3_ARCH_RV_C _M3_ARCH_RV_D
#   endif
#   define M3_ARCH _M3_ARCH_RV_C
#  elif defined(__AVR__)
#   define M3_ARCH "avr"
#  endif
# endif

#if defined(M3_COMPILER_MSVC)
#  if defined(_M_X64)
#   define M3_ARCH "x64"
#  elif defined(_M_IX86)
#   define M3_ARCH "x86"
#  elif defined(_M_ARM64)
#   define M3_ARCH "arm64"
#  elif defined(_M_ARM)
#   define M3_ARCH "arm"
#  endif
# endif

# if !defined(M3_ARCH)
#  warning "Architecture not detected"
#  define M3_ARCH "unknown"
# endif

# if defined(M3_COMPILER_CLANG)
#  define M3_COMPILER_VER __VERSION__
# elif defined(M3_COMPILER_GCC)
#  define M3_COMPILER_VER "GCC " __VERSION__
# elif defined(M3_COMPILER_MSVC)
#  define M3_COMPILER_VER "MSVC " M3_STR(_MSC_VER)
# else
#  define M3_COMPILER_VER "unknown"
# endif

/*
 * Detect/define features
 */

# if defined(M3_COMPILER_MSVC)
#  include <stdint.h>
#  if UINTPTR_MAX == 0xFFFFFFFF
#   define M3_SIZEOF_PTR 4
#  elif UINTPTR_MAX == 0xFFFFFFFFFFFFFFFFu
#   define M3_SIZEOF_PTR 8
#  else
#   error Pointer size not supported
#  endif
# else
#  define M3_SIZEOF_PTR __SIZEOF_POINTER__
# endif

# if defined(M3_COMPILER_MSVC)
#  define UNLIKELY(x) (x)
#  define LIKELY(x)   (x)
# else
#  define UNLIKELY(x) __builtin_expect(!!(x), 0)
#  define LIKELY(x)   __builtin_expect(!!(x), 1)
# endif


# if defined(M3_COMPILER_MSVC)
#  define M3_WEAK
# else
#  define M3_WEAK __attribute__((weak))
# endif

# ifndef min
#  define min(A,B) (((A) < (B)) ? (A) : (B))
# endif
# ifndef max
#  define max(A,B) (((A) > (B)) ? (A) : (B))
# endif

#define M3_INIT(field) memset(&field, 0, sizeof(field))

#define M3_COUNT_OF(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

#if defined(__AVR__)

# define PRIu64         "llu"
# define PRIi64         "lli"

# define d_m3ShortTypesDefined
typedef double          f64;
typedef float           f32;
typedef uint64_t        u64;
typedef int64_t         i64;
typedef uint32_t        u32;
typedef int32_t         i32;
typedef short unsigned  u16;
typedef short           i16;
typedef uint8_t         u8;
typedef int8_t          i8;

#endif

/*
 * Apply settings
 */

# if defined (M3_COMPILER_MSVC)
#   define  vectorcall
# elif defined(WIN32)
#   define  vectorcall __vectorcall
# elif defined (ESP8266)
#   include <c_types.h>
#   define vectorcall //ICACHE_FLASH_ATTR
# elif defined (ESP32)
#   include "esp_system.h"
#   define vectorcall IRAM_ATTR
# elif defined (FOMU)
#   define vectorcall __attribute__((section(".ramtext")))
# elif defined(HIFIVE1)
#   define vectorcall
# else
#   define vectorcall
# endif


/*
 * Platform-specific defaults
 */

# if defined(ARDUINO) || defined(PARTICLE) || defined(PLATFORMIO) || defined(__MBED__) || \
     defined(ESP8266) || defined(ESP32) || defined(BLUE_PILL) || defined(WM_W600) || defined(FOMU)
#  ifndef d_m3LogOutput
#    define d_m3LogOutput                       false
#  endif
#  ifndef d_m3VerboseLogs
#    define d_m3VerboseLogs                     false
#  endif
#  ifndef d_m3MaxFunctionStackHeight
#    define d_m3MaxFunctionStackHeight          64
#  endif
# endif

# if defined(ESP8266) || defined(BLUE_PILL) || defined(FOMU)
#  ifndef d_m3FixedHeap
#    define d_m3FixedHeap                       (8*1024)
#  endif
# endif

#endif // m3_config_platforms_h
