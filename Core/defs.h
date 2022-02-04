#ifndef defs_h
#define defs_h

#ifdef GB_INTERNAL

// "Keyword" definitions
#define likely(x)   __builtin_expect((bool)(x), 1)
#define unlikely(x) __builtin_expect((bool)(x), 0)

#define internal __attribute__((visibility("internal")))

#if __clang__
#define unrolled _Pragma("unroll")
#elif __GNUC__ >= 8
#define unrolled _Pragma("GCC unroll 8")
#else
#define unrolled
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

#if __GNUC__ < 4 || (__GNUC__ == 4 && __GNUC_MINOR__ < 8)
#define __builtin_bswap16(x) ({ typeof(x) _x = (x); _x >> 8 | _x << 8; })
#endif

struct GB_gameboy_s;
typedef struct GB_gameboy_s GB_gameboy_t;
#endif 
