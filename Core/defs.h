#pragma once

#define GB_likely(x)   __builtin_expect((bool)(x), 1)
#define GB_unlikely(x) __builtin_expect((bool)(x), 0)
#define GB_inline_const(type, ...) (*({static const typeof(type) _= __VA_ARGS__; &_;}))

#if !defined(typeof)
#if defined(__cplusplus) || __STDC_VERSION__ < 202311
#define typeof __typeof__
#endif
#endif

#ifdef GB_INTERNAL

// "Keyword" definitions
#define likely(x)   GB_likely(x)
#define unlikely(x) GB_unlikely(x)
#define inline_const GB_inline_const

#if !defined(MIN)
#define MIN(A, B)    ({ __typeof__(A) __a = (A); __typeof__(B) __b = (B); __a < __b ? __a : __b; })
#endif

#if !defined(MAX)
#define MAX(A, B)    ({ __typeof__(A) __a = (A); __typeof__(B) __b = (B); __a < __b ? __b : __a; })
#endif

#if __GNUC__ < 4 || (__GNUC__ == 4 && __GNUC_MINOR__ < 8)
#define __builtin_bswap16(x) ({ typeof(x) _x = (x); _x >> 8 | _x << 8; })
#endif

#define internal __attribute__((visibility("hidden")))
#define noinline __attribute__((noinline))

#if __clang__
#define unrolled _Pragma("unroll")
#define nounroll _Pragma("clang loop unroll(disable)")
#elif __GNUC__ >= 8
#define unrolled _Pragma("GCC unroll 8")
#define nounroll _Pragma("GCC unroll 0")
#else
#define unrolled
#define nounroll
#endif

#define unreachable() __builtin_unreachable();
#define nodefault default: unreachable()

#ifdef GB_BIG_ENDIAN
#define LE16(x) __builtin_bswap16(x)
#define LE32(x) __builtin_bswap32(x)
#define LE64(x) __builtin_bswap64(x)
#define BE16(x) (x)
#define BE32(x) (x)
#define BE64(x) (x)
#else
#define LE16(x) (x)
#define LE32(x) (x)
#define LE64(x) (x)
#define BE16(x) __builtin_bswap16(x)
#define BE32(x) __builtin_bswap32(x)
#define BE64(x) __builtin_bswap64(x)
#endif
#endif

struct GB_gameboy_s;
typedef struct GB_gameboy_s GB_gameboy_t;
