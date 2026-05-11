/*
 * sc_gfx.h  --  SilverCore Graphics Abstraction Layer
 *
 * A thin, backend-agnostic 2-D/3-D rendering API inspired by sokol_gfx.
 * The kernel compiles one backend (Vulkan / Metal / D3D12 / Software) at a
 * time; the app sees only this header.
 *
 * Concepts
 *   SCGfxBuffer  – vertex / index / uniform data on the GPU (or SW memory)
 *   SCGfxTexture – 2-D RGBA texture
 *   SCGfxShader  – compiled vertex + fragment shader pair
 *   SCGfxPipeline– shader + blend/depth/raster state bundle
 *   SCGfxPass    – render-pass (clear + draw into a target)
 *   SCGfxCmd     – retained draw command (to be batched and issued)
 *
 * Lifecycle
 *   sc_gfx_init()   once at startup
 *   sc_gfx_begin_frame() / sc_gfx_end_frame()  per rendered frame
 *   sc_gfx_shutdown() at exit
 *
 * Thread safety: NOT thread-safe.  Wrap in your own command queue if needed.
 *
 * Implementation
 *   #define SC_GFX_IMPLEMENTATION
 *   // pick exactly one:
 *   #define SC_GFX_BACKEND_VULKAN
 *   #define SC_GFX_BACKEND_METAL
 *   #define SC_GFX_BACKEND_D3D12
 *   #define SC_GFX_BACKEND_SOFTWARE
 *   #include "sc_gfx.h"
 */
#ifndef SC_GFX_H
#define SC_GFX_H

#include "sc_types.h"
#include "sc_math.h"
#include "sc_arena.h"

/* -------------------------------------------------------------------------
 * Limits / caps
 * ---------------------------------------------------------------------- */
#define SC_GFX_MAX_BUFFERS   512
#define SC_GFX_MAX_TEXTURES  512
#define SC_GFX_MAX_SHADERS   64
#define SC_GFX_MAX_PIPELINES 64
#define SC_GFX_MAX_CMDS      16384   /* draw calls per frame              */
#define SC_GFX_MAX_VERTS     (1 << 20)
#define SC_GFX_MAX_INDICES   (1 << 21)

/* -------------------------------------------------------------------------
 * Handle types  (index-based, generation-validated)
 * ---------------------------------------------------------------------- */
typedef struct { u32 id; } SCGfxBuffer;
typedef struct { u32 id; } SCGfxTexture;
typedef struct { u32 id; } SCGfxShader;
typedef struct { u32 id; } SCGfxPipeline;

#define SC_GFX_INVALID_ID 0

SC_INLINE bool sc_gfx_buf_valid (SCGfxBuffer   h) { return h.id != 0; }
SC_INLINE bool sc_gfx_tex_valid (SCGfxTexture  h) { return h.id != 0; }
SC_INLINE bool sc_gfx_shd_valid (SCGfxShader   h) { return h.id != 0; }
SC_INLINE bool sc_gfx_pip_valid (SCGfxPipeline h) { return h.id != 0; }

/* -------------------------------------------------------------------------
 * Pixel formats
 * ---------------------------------------------------------------------- */
typedef enum SCPixelFormat {
    SC_PIXFMT_RGBA8    = 0,
    SC_PIXFMT_BGRA8    = 1,
    SC_PIXFMT_R8       = 2,
    SC_PIXFMT_RG8      = 3,
    SC_PIXFMT_RGBA16F  = 4,
    SC_PIXFMT_DEPTH24  = 5,
} SCPixelFormat;

/* -------------------------------------------------------------------------
 * Buffer usage
 * ---------------------------------------------------------------------- */
typedef enum SCBufferUsage {
    SC_BUFFER_STATIC  = 0,
    SC_BUFFER_DYNAMIC = 1,
    SC_BUFFER_STREAM  = 2,
} SCBufferUsage;

typedef enum SCBufferType {
    SC_BUFFER_VERTEX  = 0,
    SC_BUFFER_INDEX   = 1,
    SC_BUFFER_UNIFORM = 2,
} SCBufferType;

/* -------------------------------------------------------------------------
 * Blend / pipeline state
 * ---------------------------------------------------------------------- */
typedef enum SCBlendFactor {
    SC_BLEND_ZERO               = 0,
    SC_BLEND_ONE                = 1,
    SC_BLEND_SRC_ALPHA          = 2,
    SC_BLEND_ONE_MINUS_SRC_ALPHA= 3,
    SC_BLEND_DST_ALPHA          = 4,
} SCBlendFactor;

typedef enum SCPrimType {
    SC_PRIM_TRIANGLES     = 0,
    SC_PRIM_TRIANGLE_STRIP= 1,
    SC_PRIM_LINES         = 2,
    SC_PRIM_LINE_STRIP    = 3,
    SC_PRIM_POINTS        = 4,
} SCPrimType;

/* -------------------------------------------------------------------------
 * Descriptor structs
 * ---------------------------------------------------------------------- */
typedef struct SCGfxBufferDesc {
    SCBufferType  type;
    SCBufferUsage usage;
    const void   *data;
    usize         size;
    const char   *label;
} SCGfxBufferDesc;

typedef struct SCGfxTextureDesc {
    u32           width, height;
    SCPixelFormat fmt;
    const void   *data;
    usize         data_size;
    bool          gen_mips;
    const char   *label;
} SCGfxTextureDesc;

typedef struct SCGfxShaderDesc {
    const char *vs_source;   /* GLSL / HLSL / MSL source         */
    const char *fs_source;
    const void *vs_bytecode; /* pre-compiled path (priority)     */
    usize       vs_bytecode_size;
    const void *fs_bytecode;
    usize       fs_bytecode_size;
    const char *label;
} SCGfxShaderDesc;

typedef struct SCGfxBlendState {
    bool          enabled;
    SCBlendFactor src_factor;
    SCBlendFactor dst_factor;
} SCGfxBlendState;

typedef struct SCGfxPipelineDesc {
    SCGfxShader    shader;
    SCPrimType     prim_type;
    SCGfxBlendState blend;
    bool            depth_write;
    bool            depth_test;
    const char     *label;
} SCGfxPipelineDesc;

/* -------------------------------------------------------------------------
 * Per-frame vertex layout (2-D UI)
 *
 * SCGfxVertex2D is the interleaved vertex for the built-in 2-D pipeline.
 * ---------------------------------------------------------------------- */
typedef struct SC_PACKED SCGfxVertex2D {
    f32 x, y;         /* position  */
    f32 u, v;         /* texcoord  */
    u8  r, g, b, a;   /* color     */
} SCGfxVertex2D;

/* -------------------------------------------------------------------------
 * Draw command (retained)
 * ---------------------------------------------------------------------- */
typedef struct SCGfxDrawCmd {
    SCGfxPipeline pipeline;
    SCGfxBuffer   vertex_buf;
    SCGfxBuffer   index_buf;
    SCGfxTexture  texture;
    u32           base_vertex;
    u32           vertex_count;
    u32           base_index;
    u32           index_count;
    SCRect2i      scissor;    /* {0,0,0,0} = no clip */
    /* Uniform block (up to 256 bytes) */
    u8            uniforms[256];
    u32           uniform_size;
} SCGfxDrawCmd;

/* -------------------------------------------------------------------------
 * Frame stats
 * ---------------------------------------------------------------------- */
typedef struct SCGfxFrameStats {
    u32 draw_calls;
    u32 vertex_count;
    u32 index_count;
    u32 texture_switches;
    f64 gpu_time_ms;
} SCGfxFrameStats;

/* -------------------------------------------------------------------------
 * Backend descriptor
 * ---------------------------------------------------------------------- */
typedef enum SCGfxBackend {
    SC_BACKEND_AUTO     = 0,
    SC_BACKEND_VULKAN   = 1,
    SC_BACKEND_METAL    = 2,
    SC_BACKEND_D3D12    = 3,
    SC_BACKEND_SOFTWARE = 4,
    SC_BACKEND_WGPU     = 5,   /* WebGPU / Emscripten              */
} SCGfxBackend;

typedef struct SCGfxDesc {
    SCGfxBackend backend;
    u32          width, height;
    u32          sample_count;   /* MSAA, 1 = disabled               */
    bool         vsync;
    const char  *app_name;
    void        *native_window;  /* HWND / NSWindow* / xcb_window_t  */
    void        *native_display; /* HDC / Display* / NULL            */
    SCArena     *frame_arena;    /* scratch memory for frame allocs  */
} SCGfxDesc;

/* -------------------------------------------------------------------------
 * Context object (opaque to callers)
 * ---------------------------------------------------------------------- */
typedef struct SCGfxContext SCGfxContext;

/* -------------------------------------------------------------------------
 * API
 * ---------------------------------------------------------------------- */

SCResult       sc_gfx_init        (const SCGfxDesc *desc, SCGfxContext **out_ctx);
void           sc_gfx_shutdown    (SCGfxContext *ctx);

SCGfxBuffer    sc_gfx_make_buffer  (SCGfxContext *ctx, const SCGfxBufferDesc *desc);
void           sc_gfx_update_buffer(SCGfxContext *ctx, SCGfxBuffer buf, const void *data, usize size);
void           sc_gfx_destroy_buffer(SCGfxContext *ctx, SCGfxBuffer buf);

SCGfxTexture   sc_gfx_make_texture (SCGfxContext *ctx, const SCGfxTextureDesc *desc);
void           sc_gfx_update_texture(SCGfxContext *ctx, SCGfxTexture tex,
                                     u32 x, u32 y, u32 w, u32 h,
                                     const void *data, usize size);
void           sc_gfx_destroy_texture(SCGfxContext *ctx, SCGfxTexture tex);

SCGfxShader    sc_gfx_make_shader  (SCGfxContext *ctx, const SCGfxShaderDesc *desc);
void           sc_gfx_destroy_shader(SCGfxContext *ctx, SCGfxShader shd);

SCGfxPipeline  sc_gfx_make_pipeline (SCGfxContext *ctx, const SCGfxPipelineDesc *desc);
void           sc_gfx_destroy_pipeline(SCGfxContext *ctx, SCGfxPipeline pip);

/* Frame lifecycle */
void           sc_gfx_begin_frame  (SCGfxContext *ctx, SCColor clear_color);
void           sc_gfx_submit       (SCGfxContext *ctx, const SCGfxDrawCmd *cmds, u32 count);
void           sc_gfx_end_frame    (SCGfxContext *ctx);

/* Query */
SCGfxBackend   sc_gfx_active_backend(const SCGfxContext *ctx);
SCGfxFrameStats sc_gfx_frame_stats  (const SCGfxContext *ctx);

/* Enable or disable software framebuffer rasterization (default: on) */
void           sc_gfx_set_rasterize(SCGfxContext *ctx, bool enable);

/* Built-in 2-D helpers (sprite / rect / text batch) */
void sc_gfx_draw_rect  (SCGfxContext *ctx, SCRect2f rect, SCColor color);
void sc_gfx_draw_sprite(SCGfxContext *ctx, SCRect2f dest, SCGfxTexture tex, SCColor tint);
void sc_gfx_draw_line  (SCGfxContext *ctx, SCVec2 a, SCVec2 b, f32 width, SCColor color);

/* -------------------------------------------------------------------------
 * Implementation (software rasteriser only – other backends link separately)
 * ---------------------------------------------------------------------- */
#ifdef SC_GFX_IMPLEMENTATION

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* ---- slot pool macros ------------------------------------------------- */
#define _SC_SLOT_GENERATIONS 1

typedef struct { u32 gen; bool live; } _SCSlot;

/* ---- Software texture data -------------------------------------------- */
typedef struct {
    u8    *pixels;   /* RGBA8 or R8 pixel data */
    u32    w, h;
    SCPixelFormat fmt;
} _SCTexData;

/* ---- Batch command for software rasterizer ---------------------------- */
typedef struct {
    SCGfxTexture texture;
    u32          vertex_offset;
    u32          vertex_count;
} _SC2DCmd;

/* ---- Forward declarations for GPU backends ---------------------------- */
#ifdef SC_GFX_BACKEND_VULKAN
struct SCVulkanDesc;
SCResult sc_vulkan_init        (SCGfxContext *ctx, const SCGfxDesc *desc,
                                 const struct SCVulkanDesc *vk_desc);
void     sc_vulkan_shutdown    (SCGfxContext *ctx);
void     sc_vulkan_begin_frame (SCGfxContext *ctx, SCColor clear);
void     sc_vulkan_submit      (SCGfxContext *ctx,
                                 const SCGfxDrawCmd *cmds, u32 count);
void     sc_vulkan_end_frame   (SCGfxContext *ctx);
#endif

/* ---- Internal context ------------------------------------------------- */
#define _SC_MAX_2D_VERTS  65536
#define _SC_MAX_2D_CMDS   8192

struct SCGfxContext {
    SCGfxBackend   backend;
    u32            width, height;
    bool           vsync;
    SCArena       *frame_arena;

    /* Resource tables */
    _SCSlot        buf_slots  [SC_GFX_MAX_BUFFERS];
    _SCSlot        tex_slots  [SC_GFX_MAX_TEXTURES];
    _SCSlot        shd_slots  [SC_GFX_MAX_SHADERS];
    _SCSlot        pip_slots  [SC_GFX_MAX_PIPELINES];

    /* Backend-specific data (e.g. _SCVkState for Vulkan) */
    void          *backend_data;

    /* Software rasteriser: framebuffer */
    u8            *framebuffer;  /* RGBA8, width*height*4 bytes */
    _SCTexData     tex_data[SC_GFX_MAX_TEXTURES];
    bool           rasterize;    /* if true, software rasterizes quads into framebuffer */

    /* 2-D batch */
    SCGfxVertex2D  batch_verts[_SC_MAX_2D_VERTS];
    u32            batch_vcount;
    _SC2DCmd       batch_cmds [_SC_MAX_2D_CMDS];
    u32            batch_ccount;

    SCGfxFrameStats frame_stats;
};

/* ---- Safe float→u8 color conversion (bug #4: clamp before truncation) - */
SC_INLINE u8 _sc_f32_to_u8(f32 v) {
    if (v <= 0.0f) return 0;
    if (v >= 1.0f) return 255;
    return (u8)(v * 255.0f);
}

/* ---- ID helpers -------------------------------------------------------- */
static u32 _sc_gfx_alloc_slot(_SCSlot *slots, u32 max) {
    for (u32 i = 1; i < max; i++) {
        if (!slots[i].live) { slots[i].live = true; return i; }
    }
    return 0;
}
static void _sc_gfx_free_slot(_SCSlot *slots, u32 id) {
    if (id > 0) { slots[id].live = false; slots[id].gen++; }
}

/* ---- Core API --------------------------------------------------------- */

SCResult sc_gfx_init(const SCGfxDesc *desc, SCGfxContext **out_ctx) {
    SCGfxContext *ctx = (SCGfxContext*)calloc(1, sizeof(SCGfxContext));
    if (!ctx) return SC_ERR_OOM;
    ctx->width        = desc->width  ? desc->width  : 1280;
    ctx->height       = desc->height ? desc->height : 720;
    ctx->vsync        = desc->vsync;
    ctx->frame_arena  = desc->frame_arena;
    ctx->backend      = desc->backend != SC_BACKEND_AUTO
                        ? desc->backend : SC_BACKEND_SOFTWARE;

#ifdef SC_GFX_BACKEND_VULKAN
    if (ctx->backend == SC_BACKEND_VULKAN) {
        SCResult r = sc_vulkan_init(ctx, desc, NULL);
        if (r != SC_OK) { free(ctx); return r; }
        *out_ctx = ctx;
        fprintf(stderr, "[sc_gfx] vulkan backend %ux%u\n", ctx->width, ctx->height);
        return SC_OK;
    }
#endif

    /* Software: allocate framebuffer */
    if (ctx->backend == SC_BACKEND_SOFTWARE) {
        ctx->rasterize = true;
        if (ctx->width > 0 && ctx->height > (SIZE_MAX / 4) / ctx->width) {
            free(ctx); return SC_ERR_INVALID_ARG;
        }
        ctx->framebuffer = (u8*)calloc((usize)ctx->width * ctx->height * 4, 1);
        if (!ctx->framebuffer) { free(ctx); return SC_ERR_OOM; }
    }

    fprintf(stderr, "[sc_gfx] backend=%d  %ux%u\n",
            ctx->backend, ctx->width, ctx->height);
    *out_ctx = ctx;
    return SC_OK;
}

void sc_gfx_shutdown(SCGfxContext *ctx) {
    if (!ctx) return;
#ifdef SC_GFX_BACKEND_VULKAN
    if (ctx->backend == SC_BACKEND_VULKAN) {
        sc_vulkan_shutdown(ctx);
        free(ctx);
        return;
    }
#endif
    if (ctx->backend == SC_BACKEND_SOFTWARE) {
        for (u32 i = 0; i < SC_GFX_MAX_TEXTURES; i++) {
            free(ctx->tex_data[i].pixels);
        }
    }
    free(ctx->framebuffer);
    free(ctx);
}

SCGfxBuffer sc_gfx_make_buffer(SCGfxContext *ctx, const SCGfxBufferDesc *desc) {
    SC_UNUSED(desc);
    u32 id = _sc_gfx_alloc_slot(ctx->buf_slots, SC_GFX_MAX_BUFFERS);
    SCGfxBuffer h = {id};
    return h;
}
void sc_gfx_update_buffer(SCGfxContext *ctx, SCGfxBuffer buf,
                           const void *data, usize size) {
    SC_UNUSED(ctx); SC_UNUSED(buf); SC_UNUSED(data); SC_UNUSED(size);
}
void sc_gfx_destroy_buffer(SCGfxContext *ctx, SCGfxBuffer buf) {
    _sc_gfx_free_slot(ctx->buf_slots, buf.id);
}

SCGfxTexture sc_gfx_make_texture(SCGfxContext *ctx, const SCGfxTextureDesc *desc) {
    u32 id = _sc_gfx_alloc_slot(ctx->tex_slots, SC_GFX_MAX_TEXTURES);
    SCGfxTexture h = {id};
    if (id == 0) return h;
    if (ctx->backend == SC_BACKEND_SOFTWARE && desc) {
        _SCTexData *td = &ctx->tex_data[id];
        td->w   = desc->width;
        td->h   = desc->height;
        td->fmt = desc->fmt;
        usize bpp = (desc->fmt == SC_PIXFMT_R8) ? 1 : 4;
        usize sz  = (usize)desc->width * (usize)desc->height * bpp;
        td->pixels = (u8*)malloc(sz);
        if (!td->pixels) {
            _sc_gfx_free_slot(ctx->tex_slots, id);
            h.id = 0;
            return h;
        }
        if (desc->data) {
            usize copy_sz = desc->data_size < sz ? desc->data_size : sz;
            memcpy(td->pixels, desc->data, copy_sz);
        } else {
            memset(td->pixels, 0, sz);
        }
    }
    return h;
}
void sc_gfx_update_texture(SCGfxContext *ctx, SCGfxTexture tex,
                            u32 x, u32 y, u32 w, u32 h,
                            const void *data, usize size) {
    (void)size;
    if (tex.id == 0 || !data) return;
    if (ctx->backend == SC_BACKEND_SOFTWARE) {
        _SCTexData *td = &ctx->tex_data[tex.id];
        if (!td->pixels) return;
        usize bpp = (td->fmt == SC_PIXFMT_R8) ? 1 : 4;
        usize src_stride = (usize)w * bpp;
        usize dst_stride = (usize)td->w * bpp;
        if (x + w > td->w) w = td->w - x;
        if (y + h > td->h) h = td->h - y;
        for (u32 row = 0; row < h; row++) {
            usize src_off = (usize)row * src_stride;
            usize dst_off = ((usize)(y + row)) * dst_stride + (usize)x * bpp;
            memcpy(&td->pixels[dst_off], &((const u8*)data)[src_off], (usize)w * bpp);
        }
    }
}
void sc_gfx_destroy_texture(SCGfxContext *ctx, SCGfxTexture tex) {
    if (tex.id == 0) return;
    if (ctx->backend == SC_BACKEND_SOFTWARE) {
        free(ctx->tex_data[tex.id].pixels);
        ctx->tex_data[tex.id].pixels = NULL;
    }
    _sc_gfx_free_slot(ctx->tex_slots, tex.id);
}

SCGfxShader sc_gfx_make_shader(SCGfxContext *ctx, const SCGfxShaderDesc *desc) {
    SC_UNUSED(desc);
    u32 id = _sc_gfx_alloc_slot(ctx->shd_slots, SC_GFX_MAX_SHADERS);
    SCGfxShader h = {id};
    return h;
}
void sc_gfx_destroy_shader(SCGfxContext *ctx, SCGfxShader shd) {
    _sc_gfx_free_slot(ctx->shd_slots, shd.id);
}

SCGfxPipeline sc_gfx_make_pipeline(SCGfxContext *ctx, const SCGfxPipelineDesc *desc) {
    SC_UNUSED(desc);
    u32 id = _sc_gfx_alloc_slot(ctx->pip_slots, SC_GFX_MAX_PIPELINES);
    SCGfxPipeline h = {id};
    return h;
}
void sc_gfx_destroy_pipeline(SCGfxContext *ctx, SCGfxPipeline pip) {
    _sc_gfx_free_slot(ctx->pip_slots, pip.id);
}

void sc_gfx_begin_frame(SCGfxContext *ctx, SCColor c) {
    memset(&ctx->frame_stats, 0, sizeof(ctx->frame_stats));
    ctx->batch_vcount = 0;
    ctx->batch_ccount = 0;
#ifdef SC_GFX_BACKEND_VULKAN
    if (ctx->backend == SC_BACKEND_VULKAN) {
        sc_vulkan_begin_frame(ctx, c);
        return;
    }
#endif
    if (ctx->backend == SC_BACKEND_SOFTWARE && ctx->framebuffer) {
        u8 cr = _sc_f32_to_u8(c.r), cg = _sc_f32_to_u8(c.g),
           cb = _sc_f32_to_u8(c.b), ca = _sc_f32_to_u8(c.a);
        usize n = (usize)ctx->width * ctx->height;
        for (usize i = 0; i < n; i++) {
            ctx->framebuffer[i*4+0] = cr;
            ctx->framebuffer[i*4+1] = cg;
            ctx->framebuffer[i*4+2] = cb;
            ctx->framebuffer[i*4+3] = ca;
        }
    }
}

void sc_gfx_submit(SCGfxContext *ctx, const SCGfxDrawCmd *cmds, u32 count) {
    ctx->frame_stats.draw_calls += count;
    for (u32 i = 0; i < count; i++) {
        ctx->frame_stats.vertex_count += cmds[i].vertex_count;
        ctx->frame_stats.index_count  += cmds[i].index_count;
    }
}

/* ---- Software rasteriser: edge function --------------------------------- */
static f32 _sc_edge(SCVec2 a, SCVec2 b, SCVec2 c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

/* Rasterise a single triangle into the framebuffer. */
static void _sc_raster_tri(SCGfxContext *ctx,
    SCVec2 v0, SCVec2 v1, SCVec2 v2,
    SCVec2 t0, SCVec2 t1, SCVec2 t2,
    u8 r0, u8 g0, u8 b0, u8 a0,
    u8 r1, u8 g1, u8 b1, u8 a1,
    u8 r2, u8 g2, u8 b2, u8 a2,
    _SCTexData *tex)
{
    f32 min_x = SC_MAX(0.0f,  SC_MIN(SC_MIN(v0.x, v1.x), v2.x));
    f32 min_y = SC_MAX(0.0f,  SC_MIN(SC_MIN(v0.y, v1.y), v2.y));
    f32 max_x = SC_MIN((f32)ctx->width  - 1.0f, SC_MAX(SC_MAX(v0.x, v1.x), v2.x));
    f32 max_y = SC_MIN((f32)ctx->height - 1.0f, SC_MAX(SC_MAX(v0.y, v1.y), v2.y));

    if (min_x > max_x || min_y > max_y) return;

    f32 area = _sc_edge(v0, v1, v2);
    if (fabsf(area) < 0.0001f) return;
    f32 inv_area = 1.0f / area;

    int ix0 = (int)min_x, iy0 = (int)min_y;
    int ix1 = (int)max_x, iy1 = (int)max_y;

    for (int y = iy0; y <= iy1; y++) {
        for (int x = ix0; x <= ix1; x++) {
            SCVec2 p = {(f32)x + 0.5f, (f32)y + 0.5f};
            f32 wa = _sc_edge(v1, v2, p);
            f32 wb = _sc_edge(v2, v0, p);
            f32 wc = _sc_edge(v0, v1, p);

            /* Check if point is inside triangle (with small epsilon for edge pixels) */
            if (wa < -0.01f || wb < -0.01f || wc < -0.01f) continue;

            f32 a = wa * inv_area;
            f32 b = wb * inv_area;
            f32 c = wc * inv_area;

            /* Interpolate attributes */
            f32 u = a * t0.x + b * t1.x + c * t2.x;
            f32 v = a * t0.y + b * t1.y + c * t2.y;
            u32 ur = (u32)(a * (f32)r0 + b * (f32)r1 + c * (f32)r2);
            u32 ug = (u32)(a * (f32)g0 + b * (f32)g1 + c * (f32)g2);
            u32 ub = (u32)(a * (f32)b0 + b * (f32)b1 + c * (f32)b2);
            u32 ua = (u32)(a * (f32)a0 + b * (f32)a1 + c * (f32)a2);

            u8 sr = (u8)SC_MIN(ur, 255u);
            u8 sg = (u8)SC_MIN(ug, 255u);
            u8 sb = (u8)SC_MIN(ub, 255u);
            u8 sa = (u8)SC_MIN(ua, 255u);

            /* Sample texture if available */
            u8 tr = 255, tg = 255, tb = 255, ta = 255;
            if (tex && tex->pixels && tex->w > 0 && tex->h > 0) {
                u32 tx = (u32)(u * (f32)(tex->w - 1));
                u32 ty = (u32)(v * (f32)(tex->h - 1));
                if (tx < tex->w && ty < tex->h) {
                    if (tex->fmt == SC_PIXFMT_R8) {
                        u8 alpha = tex->pixels[ty * tex->w + tx];
                        ta = (u8)((u32)alpha * (u32)sa / 255u);
                        tr = tg = tb = 255;
                    } else {
                        usize off = ((usize)ty * tex->w + (usize)tx) * 4;
                        tr = tex->pixels[off + 0];
                        tg = tex->pixels[off + 1];
                        tb = tex->pixels[off + 2];
                        ta = (u8)((u32)tex->pixels[off + 3] * (u32)sa / 255u);
                    }
                }
            } else {
                tr = sr; tg = sg; tb = sb; ta = sa;
            }

            /* Alpha blend into framebuffer */
            usize fb_off = ((usize)y * (usize)ctx->width + (usize)x) * 4;
            if (ta >= 255 || (tex && tex->pixels)) {
                ctx->framebuffer[fb_off + 0] = tr;
                ctx->framebuffer[fb_off + 1] = tg;
                ctx->framebuffer[fb_off + 2] = tb;
                ctx->framebuffer[fb_off + 3] = 255;
            } else if (ta > 0) {
                u32 inv_a = 255 - ta;
                ctx->framebuffer[fb_off + 0] = (u8)(((u32)tr * ta + (u32)ctx->framebuffer[fb_off + 0] * inv_a) / 255);
                ctx->framebuffer[fb_off + 1] = (u8)(((u32)tg * ta + (u32)ctx->framebuffer[fb_off + 1] * inv_a) / 255);
                ctx->framebuffer[fb_off + 2] = (u8)(((u32)tb * ta + (u32)ctx->framebuffer[fb_off + 2] * inv_a) / 255);
                ctx->framebuffer[fb_off + 3] = 255;
            }
        }
    }
}

void sc_gfx_end_frame(SCGfxContext *ctx) {
#ifdef SC_GFX_BACKEND_VULKAN
    if (ctx->backend == SC_BACKEND_VULKAN) {
        sc_vulkan_end_frame(ctx);
        return;
    }
#endif
    if (ctx->backend != SC_BACKEND_SOFTWARE || !ctx->framebuffer || !ctx->rasterize) {
        return;
    }

    for (u32 i = 0; i < ctx->batch_ccount; i++) {
        _SC2DCmd *cmd = &ctx->batch_cmds[i];
        _SCTexData *tex = NULL;
        if (cmd->texture.id > 0 && cmd->texture.id < SC_GFX_MAX_TEXTURES) {
            tex = &ctx->tex_data[cmd->texture.id];
        }

        u32 vo = cmd->vertex_offset;
        /* Process triangles (groups of 3 vertices) */
        for (u32 j = 0; j + 2 < cmd->vertex_count; j += 3) {
            SCGfxVertex2D *v = &ctx->batch_verts[vo + j];
            SCVec2 p0 = {v[0].x, v[0].y}; SCVec2 t0 = {v[0].u, v[0].v};
            SCVec2 p1 = {v[1].x, v[1].y}; SCVec2 t1 = {v[1].u, v[1].v};
            SCVec2 p2 = {v[2].x, v[2].y}; SCVec2 t2 = {v[2].u, v[2].v};

            _sc_raster_tri(ctx,
                p0, p1, p2, t0, t1, t2,
                v[0].r, v[0].g, v[0].b, v[0].a,
                v[1].r, v[1].g, v[1].b, v[1].a,
                v[2].r, v[2].g, v[2].b, v[2].a,
                tex);
        }
    }
}

SCGfxBackend sc_gfx_active_backend(const SCGfxContext *ctx) {
    return ctx->backend;
}
SCGfxFrameStats sc_gfx_frame_stats(const SCGfxContext *ctx) {
    return ctx->frame_stats;
}

void sc_gfx_set_rasterize(SCGfxContext *ctx, bool enable) {
    if (ctx) ctx->rasterize = enable;
}

/* ---- 2-D batch helpers ------------------------------------------------ */
static void _sc_gfx_push_quad(SCGfxContext *ctx, SCGfxTexture tex,
    f32 x0, f32 y0, f32 x1, f32 y1,
    f32 u0, f32 v0, f32 u1, f32 v1,
    u8 r, u8 g, u8 b, u8 a)
{
    if (ctx->batch_vcount + 6 > _SC_MAX_2D_VERTS) return;
    if (ctx->batch_ccount >= _SC_MAX_2D_CMDS) return;
    SCGfxVertex2D *v = &ctx->batch_verts[ctx->batch_vcount];
    v[0] = (SCGfxVertex2D){x0,y0, u0,v0, r,g,b,a};
    v[1] = (SCGfxVertex2D){x1,y0, u1,v0, r,g,b,a};
    v[2] = (SCGfxVertex2D){x1,y1, u1,v1, r,g,b,a};
    v[3] = (SCGfxVertex2D){x0,y0, u0,v0, r,g,b,a};
    v[4] = (SCGfxVertex2D){x1,y1, u1,v1, r,g,b,a};
    v[5] = (SCGfxVertex2D){x0,y1, u0,v1, r,g,b,a};
    ctx->batch_cmds[ctx->batch_ccount].texture       = tex;
    ctx->batch_cmds[ctx->batch_ccount].vertex_offset = ctx->batch_vcount;
    ctx->batch_cmds[ctx->batch_ccount].vertex_count  = 6;
    ctx->batch_ccount++;
    ctx->batch_vcount += 6;
    ctx->frame_stats.draw_calls++;
    ctx->frame_stats.vertex_count += 6;
}

void sc_gfx_draw_rect(SCGfxContext *ctx, SCRect2f rect, SCColor color) {
    SCGfxTexture null_tex = {0};
    _sc_gfx_push_quad(ctx, null_tex,
        rect.x, rect.y, rect.x + rect.w, rect.y + rect.h,
        0,0,1,1,
        _sc_f32_to_u8(color.r), _sc_f32_to_u8(color.g),
        _sc_f32_to_u8(color.b), _sc_f32_to_u8(color.a));
}

void sc_gfx_draw_sprite(SCGfxContext *ctx, SCRect2f dest,
                         SCGfxTexture tex, SCColor tint) {
    _sc_gfx_push_quad(ctx, tex,
        dest.x, dest.y, dest.x + dest.w, dest.y + dest.h,
        0,0,1,1,
        _sc_f32_to_u8(tint.r), _sc_f32_to_u8(tint.g),
        _sc_f32_to_u8(tint.b), _sc_f32_to_u8(tint.a));
}

void sc_gfx_draw_line(SCGfxContext *ctx, SCVec2 a, SCVec2 b, f32 w, SCColor c) {
    SCVec2 dir  = sc_v2_norm(sc_v2_sub(b, a));
    SCVec2 perp = sc_v2(-dir.y * w * 0.5f, dir.x * w * 0.5f);
    SCGfxTexture null_tex = {0};
    _sc_gfx_push_quad(ctx, null_tex,
        a.x + perp.x, a.y + perp.y,
        b.x - perp.x, b.y - perp.y,
        0,0,1,1,
        _sc_f32_to_u8(c.r), _sc_f32_to_u8(c.g),
        _sc_f32_to_u8(c.b), _sc_f32_to_u8(c.a));
}

#endif /* SC_GFX_IMPLEMENTATION */
#endif /* SC_GFX_H */
