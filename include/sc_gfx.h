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

/* ---- slot pool macros ------------------------------------------------- */
#define _SC_SLOT_GENERATIONS 1

typedef struct { u32 gen; bool live; } _SCSlot;

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

    /* Software rasteriser: framebuffer */
    u8            *framebuffer;  /* RGBA8, width*height*4 bytes */

    /* 2-D batch */
    SCGfxVertex2D  batch_verts[_SC_MAX_2D_VERTS];
    u32            batch_vcount;
    SCGfxDrawCmd   batch_cmds [_SC_MAX_2D_CMDS];
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

    /* Software: allocate framebuffer */
    if (ctx->backend == SC_BACKEND_SOFTWARE) {
        /* Bug #6: guard against overflow in width * height * 4 */
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
    SC_UNUSED(desc);
    u32 id = _sc_gfx_alloc_slot(ctx->tex_slots, SC_GFX_MAX_TEXTURES);
    SCGfxTexture h = {id};
    return h;
}
void sc_gfx_update_texture(SCGfxContext *ctx, SCGfxTexture tex,
                            u32 x, u32 y, u32 w, u32 h,
                            const void *data, usize size) {
    SC_UNUSED(ctx); SC_UNUSED(tex);
    SC_UNUSED(x); SC_UNUSED(y); SC_UNUSED(w); SC_UNUSED(h);
    SC_UNUSED(data); SC_UNUSED(size);
}
void sc_gfx_destroy_texture(SCGfxContext *ctx, SCGfxTexture tex) {
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
    /* Software: clear framebuffer */
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

void sc_gfx_end_frame(SCGfxContext *ctx) {
    SC_UNUSED(ctx);
    /* Real backends: present swap-chain here */
}

SCGfxBackend sc_gfx_active_backend(const SCGfxContext *ctx) {
    return ctx->backend;
}
SCGfxFrameStats sc_gfx_frame_stats(const SCGfxContext *ctx) {
    return ctx->frame_stats;
}

/* ---- 2-D batch helpers ------------------------------------------------ */
static void _sc_gfx_push_quad(SCGfxContext *ctx,
    f32 x0, f32 y0, f32 x1, f32 y1,
    f32 u0, f32 v0, f32 u1, f32 v1,
    u8 r, u8 g, u8 b, u8 a)
{
    if (ctx->batch_vcount + 6 > _SC_MAX_2D_VERTS) return;
    SCGfxVertex2D *v = &ctx->batch_verts[ctx->batch_vcount];
    /* triangle 0 */
    v[0] = (SCGfxVertex2D){x0,y0, u0,v0, r,g,b,a};
    v[1] = (SCGfxVertex2D){x1,y0, u1,v0, r,g,b,a};
    v[2] = (SCGfxVertex2D){x1,y1, u1,v1, r,g,b,a};
    /* triangle 1 */
    v[3] = (SCGfxVertex2D){x0,y0, u0,v0, r,g,b,a};
    v[4] = (SCGfxVertex2D){x1,y1, u1,v1, r,g,b,a};
    v[5] = (SCGfxVertex2D){x0,y1, u0,v1, r,g,b,a};
    ctx->batch_vcount += 6;
    ctx->frame_stats.draw_calls++;
}

void sc_gfx_draw_rect(SCGfxContext *ctx, SCRect2f rect, SCColor color) {
    _sc_gfx_push_quad(ctx,
        rect.x, rect.y, rect.x + rect.w, rect.y + rect.h,
        0,0,1,1,
        _sc_f32_to_u8(color.r), _sc_f32_to_u8(color.g),
        _sc_f32_to_u8(color.b), _sc_f32_to_u8(color.a));
}

void sc_gfx_draw_sprite(SCGfxContext *ctx, SCRect2f dest,
                         SCGfxTexture tex, SCColor tint) {
    SC_UNUSED(tex);
    _sc_gfx_push_quad(ctx,
        dest.x, dest.y, dest.x + dest.w, dest.y + dest.h,
        0,0,1,1,
        _sc_f32_to_u8(tint.r), _sc_f32_to_u8(tint.g),
        _sc_f32_to_u8(tint.b), _sc_f32_to_u8(tint.a));
}

void sc_gfx_draw_line(SCGfxContext *ctx, SCVec2 a, SCVec2 b, f32 w, SCColor c) {
    /* Expand line to quad */
    SCVec2 dir = sc_v2_norm(sc_v2_sub(b, a));
    SCVec2 perp = sc_v2(-dir.y * w * 0.5f, dir.x * w * 0.5f);
    _sc_gfx_push_quad(ctx,
        a.x + perp.x, a.y + perp.y,
        b.x - perp.x, b.y - perp.y,
        0,0,1,1,
        _sc_f32_to_u8(c.r), _sc_f32_to_u8(c.g),
        _sc_f32_to_u8(c.b), _sc_f32_to_u8(c.a));
}

#endif /* SC_GFX_IMPLEMENTATION */
#endif /* SC_GFX_H */
