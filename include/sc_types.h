/*
 * sc_types.h  --  SilverCore fundamental types
 *
 * Provides a single include that gives every translation unit:
 *   - fixed-width integers  (no <stdint.h> scattered everywhere)
 *   - bool                  (C99 _Bool wrapped nicely)
 *   - SC_INLINE / SC_FORCE_INLINE / SC_NORETURN / SC_PACKED attribute macros
 *   - SC_ASSERT / SC_STATIC_ASSERT
 *   - SC_UNUSED / SC_ARRAY_LEN helpers
 *   - SC_ALIGN(n) / SC_CACHE_LINE_SIZE
 *   - Result / status codes
 */
#ifndef SC_TYPES_H
#define SC_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <assert.h>

/* -------------------------------------------------------------------------
 * Compiler detection
 * ---------------------------------------------------------------------- */
#if defined(__clang__)
#  define SC_COMPILER_CLANG 1
#elif defined(__GNUC__)
#  define SC_COMPILER_GCC 1
#elif defined(_MSC_VER)
#  define SC_COMPILER_MSVC 1
#endif

/* -------------------------------------------------------------------------
 * Platform detection
 * ---------------------------------------------------------------------- */
#if defined(_WIN32) || defined(_WIN64)
#  define SC_PLATFORM_WINDOWS 1
#elif defined(__APPLE__)
#  include <TargetConditionals.h>
#  if TARGET_OS_IPHONE
#    define SC_PLATFORM_IOS 1
#  else
#    define SC_PLATFORM_MACOS 1
#  endif
#elif defined(__EMSCRIPTEN__)
#  define SC_PLATFORM_WASM 1
#elif defined(__linux__)
#  define SC_PLATFORM_LINUX 1
#elif defined(__ANDROID__)
#  define SC_PLATFORM_ANDROID 1
#endif

/* -------------------------------------------------------------------------
 * Attribute macros
 * ---------------------------------------------------------------------- */
#if defined(SC_COMPILER_CLANG) || defined(SC_COMPILER_GCC)
#  define SC_INLINE          static inline __attribute__((always_inline))
#  define SC_FORCE_INLINE    __attribute__((always_inline)) inline
#  define SC_NOINLINE        __attribute__((noinline))
#  define SC_NORETURN        __attribute__((noreturn))
#  define SC_PACKED          __attribute__((packed))
#  define SC_LIKELY(x)       __builtin_expect(!!(x), 1)
#  define SC_UNLIKELY(x)     __builtin_expect(!!(x), 0)
#  define SC_PREFETCH(p)     __builtin_prefetch((p), 0, 1)
#  define SC_ALIGN(n)        __attribute__((aligned(n)))
#  define SC_RESTRICT        __restrict__
#elif defined(SC_COMPILER_MSVC)
#  define SC_INLINE          static __forceinline
#  define SC_FORCE_INLINE    __forceinline
#  define SC_NOINLINE        __declspec(noinline)
#  define SC_NORETURN        __declspec(noreturn)
#  define SC_PACKED          /* MSVC: use #pragma pack */
#  define SC_LIKELY(x)       (x)
#  define SC_UNLIKELY(x)     (x)
#  define SC_PREFETCH(p)     (void)(p)
#  define SC_ALIGN(n)        __declspec(align(n))
#  define SC_RESTRICT        __restrict
#else
#  define SC_INLINE          static inline
#  define SC_FORCE_INLINE    inline
#  define SC_NOINLINE
#  define SC_NORETURN
#  define SC_PACKED
#  define SC_LIKELY(x)       (x)
#  define SC_UNLIKELY(x)     (x)
#  define SC_PREFETCH(p)     (void)(p)
#  define SC_ALIGN(n)
#  define SC_RESTRICT
#endif

/* -------------------------------------------------------------------------
 * Basic integer aliases
 * ---------------------------------------------------------------------- */
typedef uint8_t   u8;
typedef uint16_t  u16;
typedef uint32_t  u32;
typedef uint64_t  u64;
typedef int8_t    i8;
typedef int16_t   i16;
typedef int32_t   i32;
typedef int64_t   i64;
typedef float     f32;
typedef double    f64;
typedef size_t    usize;
typedef ptrdiff_t isize;
typedef uintptr_t uptr;

/* -------------------------------------------------------------------------
 * Cache / alignment constants
 * ---------------------------------------------------------------------- */
#define SC_CACHE_LINE_SIZE  64
#define SC_PAGE_SIZE        4096

/* -------------------------------------------------------------------------
 * Version
 * ---------------------------------------------------------------------- */
#define SC_VERSION_MAJOR 0
#define SC_VERSION_MINOR 1
#define SC_VERSION_PATCH 0

#define SC_VERSION_CHECK(major,minor,patch) \
    ((major) * 10000 + (minor) * 100 + (patch))

#define SC_CURRENT_VERSION SC_VERSION_CHECK(SC_VERSION_MAJOR, SC_VERSION_MINOR, SC_VERSION_PATCH)

/* -------------------------------------------------------------------------
 * Utility macros
 * ---------------------------------------------------------------------- */
#define SC_ARRAY_LEN(a)     (sizeof(a) / sizeof((a)[0]))
#define SC_UNUSED(x)        ((void)(x))
#define SC_MIN(a, b)        ((a) < (b) ? (a) : (b))
#define SC_MAX(a, b)        ((a) > (b) ? (a) : (b))
#define SC_CLAMP(v, lo, hi) SC_MIN(SC_MAX((v),(lo)),(hi))
#define SC_KB(n)            ((usize)(n) * 1024ULL)
#define SC_MB(n)            ((usize)(n) * 1024ULL * 1024ULL)
#define SC_GB(n)            ((usize)(n) * 1024ULL * 1024ULL * 1024ULL)

#define SC_ALIGN_UP(x, a)   (((x) + (a) - 1) & ~((a) - 1))
#define SC_ALIGN_DOWN(x, a) ((x) & ~((a) - 1))
#define SC_IS_POW2(x)       (((x) != 0) && (((x) & ((x)-1)) == 0))

/* -------------------------------------------------------------------------
 * Assertions
 * ---------------------------------------------------------------------- */
#define SC_ASSERT(expr)       assert(expr)
#define SC_STATIC_ASSERT(c)   _Static_assert((c), #c)

/* -------------------------------------------------------------------------
 * Status / result codes
 * ---------------------------------------------------------------------- */
typedef enum SCResult {
    SC_OK                   =  0,
    SC_ERR_GENERIC          = -1,
    SC_ERR_OOM              = -2,   /* out of memory          */
    SC_ERR_INVALID_ARG      = -3,
    SC_ERR_NOT_FOUND        = -4,
    SC_ERR_NOT_SUPPORTED    = -5,
    SC_ERR_BACKEND          = -6,   /* graphics backend error  */
    SC_ERR_GFX              = SC_ERR_BACKEND, /* alias for SC_ERR_BACKEND */
    SC_ERR_IO               = -7,
    SC_ERR_OVERFLOW         = -8,
    SC_ERR_TIMEOUT          = -9,
} SCResult;

SC_INLINE bool sc_ok(SCResult r) { return r == SC_OK; }

/* -------------------------------------------------------------------------
 * RGBA color (packed u32 + float4)
 * ---------------------------------------------------------------------- */
typedef struct SCColor { f32 r, g, b, a; } SCColor;

SC_INLINE SCColor sc_rgba(f32 r, f32 g, f32 b, f32 a) {
    SCColor c = {r, g, b, a}; return c;
}
SC_INLINE SCColor sc_rgba8(u8 r, u8 g, u8 b, u8 a) {
    return sc_rgba(r/255.0f, g/255.0f, b/255.0f, a/255.0f);
}
#define SC_BLACK   sc_rgba(0,0,0,1)
#define SC_WHITE   sc_rgba(1,1,1,1)
#define SC_TRANSPARENT sc_rgba(0,0,0,0)

/* -------------------------------------------------------------------------
 * 2-D integer point / rect
 * ---------------------------------------------------------------------- */
typedef struct SCPoint2i { i32 x, y; } SCPoint2i;
typedef struct SCRect2i  { i32 x, y, w, h; } SCRect2i;
typedef struct SCPoint2f { f32 x, y; } SCPoint2f;
typedef struct SCSize2f  { f32 w, h; } SCSize2f;
typedef struct SCRect2f  { f32 x, y, w, h; } SCRect2f;
typedef struct SCEdgeInsets { f32 top, right, bottom, left; } SCEdgeInsets;

SC_INLINE bool sc_rect2f_contains(SCRect2f r, f32 px, f32 py) {
    return px >= r.x && px <= (r.x + r.w) && py >= r.y && py <= (r.y + r.h);
}

#endif /* SC_TYPES_H */
