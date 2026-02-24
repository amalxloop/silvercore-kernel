/*
 * sc_math.h  --  SilverCore SIMD-friendly math primitives
 *
 * vec2 / vec3 / vec4 / mat4 with scalar fallback.
 * When __SSE2__ or __ARM_NEON is available the hot paths use intrinsics.
 *
 * All types are plain structs so they are C-compatible and trivially
 * copyable / serialisable.
 */
#ifndef SC_MATH_H
#define SC_MATH_H

#include "sc_types.h"
#include <math.h>
#include <string.h>   /* memcpy */

/* -----------------------------------------------------------------------
 * SIMD detection
 * -------------------------------------------------------------------- */
#if defined(__SSE2__) && !defined(SC_NO_SSE)
#  include <immintrin.h>
#  define SC_SIMD_SSE2 1
#elif defined(__ARM_NEON) && !defined(SC_NO_NEON)
#  include <arm_neon.h>
#  define SC_SIMD_NEON 1
#endif

/* -----------------------------------------------------------------------
 * Constants
 * -------------------------------------------------------------------- */
#define SC_PI        3.14159265358979323846f
#define SC_TAU       6.28318530717958647692f
#define SC_DEG2RAD   (SC_PI / 180.0f)
#define SC_RAD2DEG   (180.0f / SC_PI)
#define SC_EPSILON   1e-6f

SC_INLINE f32 sc_sqrtf(f32 x) { return sqrtf(x); }
SC_INLINE f32 sc_fabsf(f32 x) { return fabsf(x); }
SC_INLINE f32 sc_floorf(f32 x) { return floorf(x); }
SC_INLINE f32 sc_ceilf(f32 x) { return ceilf(x); }
SC_INLINE f32 sc_roundf(f32 x) { return roundf(x); }
SC_INLINE f32 sc_sinf(f32 x) { return sinf(x); }
SC_INLINE f32 sc_cosf(f32 x) { return cosf(x); }
SC_INLINE f32 sc_tanf(f32 x) { return tanf(x); }
SC_INLINE f32 sc_atan2f(f32 y, f32 x) { return atan2f(y, x); }
SC_INLINE f32 sc_lerpf(f32 a, f32 b, f32 t) { return a + (b - a) * t; }

/* -----------------------------------------------------------------------
 * vec2
 * -------------------------------------------------------------------- */
typedef struct SCVec2 { f32 x, y; } SCVec2;

SC_INLINE SCVec2 sc_v2(f32 x, f32 y)           { SCVec2 v = {x, y}; return v; }
SC_INLINE SCVec2 sc_v2s(f32 s)                  { return sc_v2(s, s); }
SC_INLINE SCVec2 sc_v2_add(SCVec2 a, SCVec2 b)  { return sc_v2(a.x+b.x, a.y+b.y); }
SC_INLINE SCVec2 sc_v2_sub(SCVec2 a, SCVec2 b)  { return sc_v2(a.x-b.x, a.y-b.y); }
SC_INLINE SCVec2 sc_v2_mul(SCVec2 a, f32 s)     { return sc_v2(a.x*s, a.y*s); }
SC_INLINE f32    sc_v2_dot(SCVec2 a, SCVec2 b)  { return a.x*b.x + a.y*b.y; }
SC_INLINE f32    sc_v2_len(SCVec2 v)            { return sc_sqrtf(sc_v2_dot(v,v)); }
SC_INLINE SCVec2 sc_v2_norm(SCVec2 v) {
    f32 l = sc_v2_len(v);
    return l > SC_EPSILON ? sc_v2_mul(v, 1.0f/l) : sc_v2(0,0);
}
SC_INLINE SCVec2 sc_v2_lerp(SCVec2 a, SCVec2 b, f32 t) {
    return sc_v2(sc_lerpf(a.x,b.x,t), sc_lerpf(a.y,b.y,t));
}

/* -----------------------------------------------------------------------
 * vec3
 * -------------------------------------------------------------------- */
typedef struct SCVec3 { f32 x, y, z; } SCVec3;

SC_INLINE SCVec3 sc_v3(f32 x, f32 y, f32 z)         { SCVec3 v = {x,y,z}; return v; }
SC_INLINE SCVec3 sc_v3s(f32 s)                        { return sc_v3(s,s,s); }
SC_INLINE SCVec3 sc_v3_add(SCVec3 a, SCVec3 b)        { return sc_v3(a.x+b.x,a.y+b.y,a.z+b.z); }
SC_INLINE SCVec3 sc_v3_sub(SCVec3 a, SCVec3 b)        { return sc_v3(a.x-b.x,a.y-b.y,a.z-b.z); }
SC_INLINE SCVec3 sc_v3_mul(SCVec3 a, f32 s)           { return sc_v3(a.x*s,a.y*s,a.z*s); }
SC_INLINE f32    sc_v3_dot(SCVec3 a, SCVec3 b)        { return a.x*b.x+a.y*b.y+a.z*b.z; }
SC_INLINE f32    sc_v3_len(SCVec3 v)                  { return sc_sqrtf(sc_v3_dot(v,v)); }
SC_INLINE SCVec3 sc_v3_norm(SCVec3 v) {
    f32 l = sc_v3_len(v);
    return l > SC_EPSILON ? sc_v3_mul(v, 1.0f/l) : sc_v3(0,0,0);
}
SC_INLINE SCVec3 sc_v3_cross(SCVec3 a, SCVec3 b) {
    return sc_v3(a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x);
}
SC_INLINE SCVec3 sc_v3_lerp(SCVec3 a, SCVec3 b, f32 t) {
    return sc_v3(sc_lerpf(a.x,b.x,t), sc_lerpf(a.y,b.y,t), sc_lerpf(a.z,b.z,t));
}

/* -----------------------------------------------------------------------
 * vec4  (also used for quaternions)
 * -------------------------------------------------------------------- */
typedef SC_ALIGN(16) struct SCVec4 { f32 x, y, z, w; } SCVec4;

SC_INLINE SCVec4 sc_v4(f32 x, f32 y, f32 z, f32 w) { SCVec4 v = {x,y,z,w}; return v; }
SC_INLINE SCVec4 sc_v4s(f32 s)                       { return sc_v4(s,s,s,s); }

#ifdef SC_SIMD_SSE2
SC_INLINE SCVec4 sc_v4_add(SCVec4 a, SCVec4 b) {
    SCVec4 r;
    _mm_store_ps(&r.x, _mm_add_ps(_mm_load_ps(&a.x), _mm_load_ps(&b.x)));
    return r;
}
SC_INLINE SCVec4 sc_v4_mul(SCVec4 a, f32 s) {
    SCVec4 r;
    _mm_store_ps(&r.x, _mm_mul_ps(_mm_load_ps(&a.x), _mm_set1_ps(s)));
    return r;
}
SC_INLINE f32 sc_v4_dot(SCVec4 a, SCVec4 b) {
    __m128 m = _mm_mul_ps(_mm_load_ps(&a.x), _mm_load_ps(&b.x));
    __m128 s = _mm_hadd_ps(m, m);
    s = _mm_hadd_ps(s, s);
    return _mm_cvtss_f32(s);
}
#else
SC_INLINE SCVec4 sc_v4_add(SCVec4 a, SCVec4 b) { return sc_v4(a.x+b.x,a.y+b.y,a.z+b.z,a.w+b.w); }
SC_INLINE SCVec4 sc_v4_mul(SCVec4 a, f32 s)     { return sc_v4(a.x*s,a.y*s,a.z*s,a.w*s); }
SC_INLINE f32    sc_v4_dot(SCVec4 a, SCVec4 b)  { return a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w; }
#endif

/* -----------------------------------------------------------------------
 * mat4 – column-major, SSE2 path for mul
 * -------------------------------------------------------------------- */
typedef SC_ALIGN(64) struct SCMat4 { f32 m[16]; } SCMat4;

SC_INLINE SCMat4 sc_mat4_identity(void) {
    SCMat4 r;
    memset(r.m, 0, sizeof(r.m));
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

/* column-major: m[col*4 + row] */
SC_INLINE f32* sc_mat4_col(SCMat4 *m, int c) { return &m->m[c*4]; }

SC_INLINE SCMat4 sc_mat4_mul(const SCMat4 *a, const SCMat4 *b) {
    SCMat4 r;
#ifdef SC_SIMD_SSE2
    __m128 row0 = _mm_load_ps(&a->m[0]);
    __m128 row1 = _mm_load_ps(&a->m[4]);
    __m128 row2 = _mm_load_ps(&a->m[8]);
    __m128 row3 = _mm_load_ps(&a->m[12]);
    for (int c = 0; c < 4; c++) {
        __m128 brow = _mm_load_ps(&b->m[c*4]);
        __m128 res = _mm_add_ps(
            _mm_add_ps(_mm_mul_ps(_mm_shuffle_ps(brow,brow,0x00), row0),
                       _mm_mul_ps(_mm_shuffle_ps(brow,brow,0x55), row1)),
            _mm_add_ps(_mm_mul_ps(_mm_shuffle_ps(brow,brow,0xAA), row2),
                       _mm_mul_ps(_mm_shuffle_ps(brow,brow,0xFF), row3)));
        _mm_store_ps(&r.m[c*4], res);
    }
#else
    for (int c = 0; c < 4; c++) {
        for (int row = 0; row < 4; row++) {
            r.m[c*4+row] = a->m[0*4+row]*b->m[c*4+0]
                          + a->m[1*4+row]*b->m[c*4+1]
                          + a->m[2*4+row]*b->m[c*4+2]
                          + a->m[3*4+row]*b->m[c*4+3];
        }
    }
#endif
    return r;
}

SC_INLINE SCMat4 sc_mat4_ortho(f32 l, f32 r, f32 b, f32 t, f32 n, f32 f) {
    SCMat4 m; memset(m.m, 0, sizeof(m.m));
    m.m[0]  =  2.0f/(r-l);
    m.m[5]  =  2.0f/(t-b);
    m.m[10] = -2.0f/(f-n);
    m.m[12] = -(r+l)/(r-l);
    m.m[13] = -(t+b)/(t-b);
    m.m[14] = -(f+n)/(f-n);
    m.m[15] =  1.0f;
    return m;
}

SC_INLINE SCMat4 sc_mat4_translate(f32 tx, f32 ty, f32 tz) {
    SCMat4 m = sc_mat4_identity();
    m.m[12] = tx; m.m[13] = ty; m.m[14] = tz;
    return m;
}

SC_INLINE SCMat4 sc_mat4_scale(f32 sx, f32 sy, f32 sz) {
    SCMat4 m = sc_mat4_identity();
    m.m[0] = sx; m.m[5] = sy; m.m[10] = sz;
    return m;
}

SC_INLINE SCVec4 sc_mat4_mul_vec4(const SCMat4 *m, SCVec4 v) {
    return sc_v4(
        m->m[0]*v.x + m->m[4]*v.y + m->m[8]*v.z  + m->m[12]*v.w,
        m->m[1]*v.x + m->m[5]*v.y + m->m[9]*v.z  + m->m[13]*v.w,
        m->m[2]*v.x + m->m[6]*v.y + m->m[10]*v.z + m->m[14]*v.w,
        m->m[3]*v.x + m->m[7]*v.y + m->m[11]*v.z + m->m[15]*v.w
    );
}

#endif /* SC_MATH_H */
