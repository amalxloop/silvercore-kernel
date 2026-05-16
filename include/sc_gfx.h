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
 * Blend / depth / pipeline state
 * ---------------------------------------------------------------------- */
typedef enum SCBlendFactor {
    SC_BLEND_ZERO               = 0,
    SC_BLEND_ONE                = 1,
    SC_BLEND_SRC_ALPHA          = 2,
    SC_BLEND_ONE_MINUS_SRC_ALPHA= 3,
    SC_BLEND_DST_ALPHA          = 4,
} SCBlendFactor;

typedef enum SCCompareFunc {
    SC_COMPARE_NEVER    = 0,
    SC_COMPARE_LESS     = 1,
    SC_COMPARE_EQUAL    = 2,
    SC_COMPARE_LEQUAL   = 3,
    SC_COMPARE_GREATER  = 4,
    SC_COMPARE_NOTEQUAL = 5,
    SC_COMPARE_GEQUAL   = 6,
    SC_COMPARE_ALWAYS   = 7,
} SCCompareFunc;

typedef enum SCStencilOp {
    SC_STENCIL_KEEP       = 0,
    SC_STENCIL_ZERO       = 1,
    SC_STENCIL_REPLACE    = 2,
    SC_STENCIL_INCR_CLAMP = 3,
    SC_STENCIL_DECR_CLAMP = 4,
    SC_STENCIL_INVERT     = 5,
    SC_STENCIL_INCR_WRAP  = 6,
    SC_STENCIL_DECR_WRAP  = 7,
} SCStencilOp;

typedef struct SCGfxStencilState {
    SCStencilOp   fail_op;
    SCStencilOp   pass_op;
    SCStencilOp   depth_fail_op;
    SCCompareFunc compare;
    u32           read_mask;
    u32           write_mask;
    u32           reference;
} SCGfxStencilState;

typedef struct SCGfxDepthState {
    bool              depth_test;
    bool              depth_write;
    SCCompareFunc     depth_compare;
    bool              stencil_test;
    SCGfxStencilState stencil_front;
    SCGfxStencilState stencil_back;
} SCGfxDepthState;

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
    SCGfxDepthState depth;
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
    f32           depth;        /* uniform depth for all vertices (0 = near, 1 = far) */
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
    bool         thread_safe;    /* enable internal mutex (default: false) */
} SCGfxDesc;

/* -------------------------------------------------------------------------
 * Context object (opaque to callers)
 * ---------------------------------------------------------------------- */
typedef struct SCGfxContext SCGfxContext;

/* -------------------------------------------------------------------------
 * API
 * ---------------------------------------------------------------------- */

/** @brief  Initialise the graphics system and select a backend.
 *  @param desc   Initialisation parameters (backend, resolution, vsync, …).
 *  @param out_ctx  Receives the opaque context pointer.
 *  @return SC_OK on success, or an error code (e.g. SC_ERR_OOM, SC_ERR_GFX). */
SCResult       sc_gfx_init        (const SCGfxDesc *desc, SCGfxContext **out_ctx);

/** @brief  Shut down the graphics system and free all resources.
 *  Safe to call with NULL ctx. */
void           sc_gfx_shutdown    (SCGfxContext *ctx);

/** @brief  Create a GPU buffer (vertex, index, or uniform).
 *  @return A valid handle, or {0} on failure (check with sc_gfx_buf_valid). */
SCGfxBuffer    sc_gfx_make_buffer  (SCGfxContext *ctx, const SCGfxBufferDesc *desc);

/** @brief  Update (overwrite) the contents of an existing buffer. */
void           sc_gfx_update_buffer(SCGfxContext *ctx, SCGfxBuffer buf, const void *data, usize size);

/** @brief  Destroy a buffer and free its GPU memory. */
void           sc_gfx_destroy_buffer(SCGfxContext *ctx, SCGfxBuffer buf);

/** @brief  Create a 2-D texture from CPU data (or allocate uninitialised).
 *  @return A valid handle, or {0} on failure (check with sc_gfx_tex_valid). */
SCGfxTexture   sc_gfx_make_texture (SCGfxContext *ctx, const SCGfxTextureDesc *desc);

/** @brief  Update a sub-rectangle of an existing texture (software backend only). */
void           sc_gfx_update_texture(SCGfxContext *ctx, SCGfxTexture tex,
                                     u32 x, u32 y, u32 w, u32 h,
                                     const void *data, usize size);

/** @brief  Destroy a texture and free its GPU memory. */
void           sc_gfx_destroy_texture(SCGfxContext *ctx, SCGfxTexture tex);

/** @brief  Compile a vertex+fragment shader pair.
 *  @return A valid handle, or {0} on failure (check with sc_gfx_shd_valid). */
SCGfxShader    sc_gfx_make_shader  (SCGfxContext *ctx, const SCGfxShaderDesc *desc);

/** @brief  Destroy a shader and release its GPU resources. */
void           sc_gfx_destroy_shader(SCGfxContext *ctx, SCGfxShader shd);

/** @brief  Create a pipeline (shader + blend + depth + primitive type).
 *  @return A valid handle, or {0} on failure (check with sc_gfx_pip_valid). */
SCGfxPipeline  sc_gfx_make_pipeline (SCGfxContext *ctx, const SCGfxPipelineDesc *desc);

/** @brief  Destroy a pipeline state object. */
void           sc_gfx_destroy_pipeline(SCGfxContext *ctx, SCGfxPipeline pip);

/** @brief  Begin a new frame: clear the target and reset internal state. */
void           sc_gfx_begin_frame  (SCGfxContext *ctx, SCColor clear_color);

/** @brief  Submit a batch of retained draw commands for the current frame. */
void           sc_gfx_submit       (SCGfxContext *ctx, const SCGfxDrawCmd *cmds, u32 count);

/** @brief  End the current frame: execute all retained commands and present. */
void           sc_gfx_end_frame    (SCGfxContext *ctx);

/** @brief  Return the backend currently in use. */
SCGfxBackend   sc_gfx_active_backend(const SCGfxContext *ctx);

/** @brief  Return statistics for the last completed frame. */
SCGfxFrameStats sc_gfx_frame_stats  (const SCGfxContext *ctx);

/** @brief  Enable or disable software framebuffer rasterisation (default: on). */
void           sc_gfx_set_rasterize(SCGfxContext *ctx, bool enable);

/** @brief  Handle a window resize: recreate swapchain / framebuffer.
 *  @return SC_OK on success. */
SCResult       sc_gfx_resize(SCGfxContext *ctx, u32 width, u32 height);

/** @brief  Lock the internal mutex (only needed when thread_safe was set in
 *          SCGfxDesc).  Nested locks from the same thread are undefined. */
void           sc_gfx_lock  (SCGfxContext *ctx);

/** @brief  Unlock the internal mutex. */
void           sc_gfx_unlock(SCGfxContext *ctx);

/** @brief  Draw a filled rectangle (immediate-mode 2-D helper). */
void sc_gfx_draw_rect  (SCGfxContext *ctx, SCRect2f rect, SCColor color);

/** @brief  Draw a textured sprite (immediate-mode 2-D helper). */
void sc_gfx_draw_sprite(SCGfxContext *ctx, SCRect2f dest, SCGfxTexture tex, SCColor tint);

/** @brief  Draw a line segment with a given width (immediate-mode 2-D helper). */
void sc_gfx_draw_line  (SCGfxContext *ctx, SCVec2 a, SCVec2 b, f32 width, SCColor color);

/* ---- Slot / texture / batch types (used by both impl and declaration) --- */
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
SCGfxTexture sc_vulkan_make_texture (SCGfxContext *ctx,
                                     const SCGfxTextureDesc *desc);
void         sc_vulkan_destroy_texture(SCGfxContext *ctx, SCGfxTexture tex);
SCResult sc_vulkan_resize      (SCGfxContext *ctx, u32 width, u32 height);
SCGfxBuffer sc_vulkan_make_buffer   (SCGfxContext *ctx, const SCGfxBufferDesc *desc);
void        sc_vulkan_destroy_buffer(SCGfxContext *ctx, SCGfxBuffer buf);
void        sc_vulkan_update_buffer (SCGfxContext *ctx, SCGfxBuffer buf,
                                     const void *data, usize size);
SCGfxShader  sc_vulkan_make_shader  (SCGfxContext *ctx, const SCGfxShaderDesc *desc);
void         sc_vulkan_destroy_shader(SCGfxContext *ctx, SCGfxShader shd);
SCGfxPipeline sc_vulkan_make_pipeline (SCGfxContext *ctx, const SCGfxPipelineDesc *desc);
void          sc_vulkan_destroy_pipeline(SCGfxContext *ctx, SCGfxPipeline pip);
#endif
#ifdef SC_GFX_BACKEND_METAL
struct SCMetalDesc;
SCResult sc_metal_init         (SCGfxContext *ctx, const SCGfxDesc *desc,
                                 const struct SCMetalDesc *mtl_desc);
void     sc_metal_shutdown     (SCGfxContext *ctx);
void     sc_metal_begin_frame  (SCGfxContext *ctx, SCColor clear);
void     sc_metal_submit       (SCGfxContext *ctx,
                                 const SCGfxDrawCmd *cmds, u32 count);
void     sc_metal_end_frame    (SCGfxContext *ctx);
SCGfxBuffer  sc_metal_make_buffer   (SCGfxContext *ctx, const SCGfxBufferDesc *desc);
void         sc_metal_destroy_buffer(SCGfxContext *ctx, SCGfxBuffer buf);
void         sc_metal_update_buffer (SCGfxContext *ctx, SCGfxBuffer buf,
                                     const void *data, usize size);
SCGfxTexture sc_metal_make_texture  (SCGfxContext *ctx, const SCGfxTextureDesc *desc);
void         sc_metal_destroy_texture(SCGfxContext *ctx, SCGfxTexture tex);
SCGfxShader  sc_metal_make_shader   (SCGfxContext *ctx, const SCGfxShaderDesc *desc);
void         sc_metal_destroy_shader(SCGfxContext *ctx, SCGfxShader shd);
SCGfxPipeline sc_metal_make_pipeline(SCGfxContext *ctx, const SCGfxPipelineDesc *desc);
void          sc_metal_destroy_pipeline(SCGfxContext *ctx, SCGfxPipeline pip);
SCResult      sc_metal_resize          (SCGfxContext *ctx, u32 width, u32 height);
#endif
#ifdef SC_GFX_BACKEND_D3D12
struct SCD3D12Desc;
SCResult sc_d3d12_init         (SCGfxContext *ctx, const SCGfxDesc *desc,
                                 const struct SCD3D12Desc *d3d_desc);
void     sc_d3d12_shutdown     (SCGfxContext *ctx);
void     sc_d3d12_begin_frame  (SCGfxContext *ctx, SCColor clear);
void     sc_d3d12_submit       (SCGfxContext *ctx,
                                 const SCGfxDrawCmd *cmds, u32 count);
void     sc_d3d12_end_frame    (SCGfxContext *ctx);
SCGfxBuffer  sc_d3d12_make_buffer   (SCGfxContext *ctx, const SCGfxBufferDesc *desc);
void         sc_d3d12_destroy_buffer(SCGfxContext *ctx, SCGfxBuffer buf);
void         sc_d3d12_update_buffer (SCGfxContext *ctx, SCGfxBuffer buf,
                                      const void *data, usize size);
SCGfxTexture sc_d3d12_make_texture  (SCGfxContext *ctx, const SCGfxTextureDesc *desc);
void         sc_d3d12_destroy_texture(SCGfxContext *ctx, SCGfxTexture tex);
SCGfxShader  sc_d3d12_make_shader   (SCGfxContext *ctx, const SCGfxShaderDesc *desc);
void         sc_d3d12_destroy_shader(SCGfxContext *ctx, SCGfxShader shd);
SCGfxPipeline sc_d3d12_make_pipeline(SCGfxContext *ctx, const SCGfxPipelineDesc *desc);
void          sc_d3d12_destroy_pipeline(SCGfxContext *ctx, SCGfxPipeline pip);
SCResult      sc_d3d12_resize        (SCGfxContext *ctx, u32 width, u32 height);
#endif
#ifdef SC_GFX_BACKEND_WGPU
struct SCWGPUDesc;
SCResult sc_wgpu_init          (SCGfxContext *ctx, const SCGfxDesc *desc,
                                 const struct SCWGPUDesc *wgpu_desc);
void     sc_wgpu_shutdown      (SCGfxContext *ctx);
void     sc_wgpu_begin_frame   (SCGfxContext *ctx, SCColor clear);
void     sc_wgpu_submit        (SCGfxContext *ctx,
                                 const SCGfxDrawCmd *cmds, u32 count);
void     sc_wgpu_end_frame     (SCGfxContext *ctx);
SCGfxBuffer  sc_wgpu_make_buffer   (SCGfxContext *ctx, const SCGfxBufferDesc *desc);
void         sc_wgpu_destroy_buffer(SCGfxContext *ctx, SCGfxBuffer buf);
void         sc_wgpu_update_buffer (SCGfxContext *ctx, SCGfxBuffer buf,
                                     const void *data, usize size);
SCGfxTexture sc_wgpu_make_texture  (SCGfxContext *ctx, const SCGfxTextureDesc *desc);
void         sc_wgpu_destroy_texture(SCGfxContext *ctx, SCGfxTexture tex);
SCGfxShader  sc_wgpu_make_shader   (SCGfxContext *ctx, const SCGfxShaderDesc *desc);
void         sc_wgpu_destroy_shader(SCGfxContext *ctx, SCGfxShader shd);
SCGfxPipeline sc_wgpu_make_pipeline(SCGfxContext *ctx, const SCGfxPipelineDesc *desc);
void          sc_wgpu_destroy_pipeline(SCGfxContext *ctx, SCGfxPipeline pip);
SCResult      sc_wgpu_resize         (SCGfxContext *ctx, u32 width, u32 height);
#endif

/* ---- Software backend: buffer data store -------------------------------- */
typedef struct {
    u8    *data;
    usize  size;
    SCBufferType type;
} _SCBufData;

/* -------------------------------------------------------------------------
 * Implementation (software rasteriser only – other backends link separately)
 * ---------------------------------------------------------------------- */
#ifdef SC_GFX_IMPLEMENTATION

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* ---- Platform mutex abstraction (used when thread_safe is true) ------- */
#if defined(SC_PLATFORM_LINUX) || defined(SC_PLATFORM_MACOS)
#include <pthread.h>
#define _SC_MTX_T          pthread_mutex_t
#define _SC_MTX_INIT(m)    pthread_mutex_init(m, NULL)
#define _SC_MTX_LOCK(m)    pthread_mutex_lock(m)
#define _SC_MTX_UNLOCK(m)  pthread_mutex_unlock(m)
#define _SC_MTX_DESTROY(m) pthread_mutex_destroy(m)
#elif defined(SC_PLATFORM_WINDOWS)
#define _SC_MTX_T          CRITICAL_SECTION
#define _SC_MTX_INIT(m)    InitializeCriticalSection(m)
#define _SC_MTX_LOCK(m)    EnterCriticalSection(m)
#define _SC_MTX_UNLOCK(m)  LeaveCriticalSection(m)
#define _SC_MTX_DESTROY(m) DeleteCriticalSection(m)
#else
#define _SC_MTX_T          int
#define _SC_MTX_INIT(m)    (*(m) = 0)
#define _SC_MTX_LOCK(m)    ((void)0)
#define _SC_MTX_UNLOCK(m)  ((void)0)
#define _SC_MTX_DESTROY(m) ((void)0)
#endif

/* ---- Internal context ------------------------------------------------- */
#define _SC_MAX_2D_VERTS  65536
#define _SC_MAX_2D_CMDS   8192

struct SCGfxContext {
    SCGfxBackend   backend;
    u32            width, height;
    u32            sample_count;
    bool           vsync;
    SCArena       *frame_arena;

    /* Resource tables */
    _SCSlot        buf_slots  [SC_GFX_MAX_BUFFERS];
    _SCSlot        tex_slots  [SC_GFX_MAX_TEXTURES];
    _SCSlot        shd_slots  [SC_GFX_MAX_SHADERS];
    _SCSlot        pip_slots  [SC_GFX_MAX_PIPELINES];

    /* Thread-safety: non-NULL when thread_safe mode is active */
    void          *lock_mutex;

    /* Backend-specific data (e.g. _SCVkState for Vulkan) */
    void          *backend_data;

    /* Software backend: buffer data storage */
    _SCBufData     buf_data[SC_GFX_MAX_BUFFERS];

    /* Software backend: shader desc storage */
    SCGfxShaderDesc shd_desc[SC_GFX_MAX_SHADERS];

    /* Software backend: pipeline desc storage */
    SCGfxPipelineDesc pip_desc[SC_GFX_MAX_PIPELINES];

    /* Software rasteriser: framebuffer + depth buffer */
    u8            *framebuffer;  /* RGBA8, width*height*4 bytes */
    f32           *depth_buffer; /* f32 depth, width*height entries, 1.0 = far */
    _SCTexData     tex_data[SC_GFX_MAX_TEXTURES];
    bool           rasterize;    /* if true, software rasterizes quads into framebuffer */

    /* Free-list heads for O(1) slot allocation */
    u32            buf_free_head;
    u32            tex_free_head;
    u32            shd_free_head;
    u32            pip_free_head;

    /* Submitted draw commands (retained for end_frame) */
    SCGfxDrawCmd   submit_cmds[SC_GFX_MAX_CMDS];
    u32            submit_ccount;

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

/* ---- Free-list helpers (O(1) slot alloc/free) ------------------------- */

/* Thread a free-list through the gen field of inactive slots.
   gen[slot] = next-free-index, 0 = end-of-list. */
static void _sc_gfx_init_freelist(_SCSlot *slots, u32 max, u32 *head) {
    *head = 1;
    for (u32 i = 1; i < max; i++) slots[i].gen = i + 1;
    slots[max - 1].gen = 0;
}

static u32 _sc_gfx_alloc_slot(_SCSlot *slots, u32 max, u32 *head) {
    (void)max;
    u32 id = *head;
    if (id == 0) return 0;
    *head = slots[id].gen;
    slots[id].live = true;
    return id;
}

static void _sc_gfx_free_slot(_SCSlot *slots, u32 id, u32 *head) {
    if (id == 0) return;
    slots[id].live = false;
    slots[id].gen = *head;
    *head = id;
}

/* ---- Core API --------------------------------------------------------- */

SCResult sc_gfx_init(const SCGfxDesc *desc, SCGfxContext **out_ctx) {
    SCGfxContext *ctx = (SCGfxContext*)calloc(1, sizeof(SCGfxContext));
    if (!ctx) return SC_ERR_OOM;
    ctx->width        = desc->width  ? desc->width  : 1280;
    ctx->height       = desc->height ? desc->height : 720;
    ctx->sample_count = desc->sample_count < 2 ? 1 : desc->sample_count;
    ctx->vsync        = desc->vsync;
    ctx->frame_arena  = desc->frame_arena;
    ctx->backend      = desc->backend != SC_BACKEND_AUTO
                        ? desc->backend : SC_BACKEND_SOFTWARE;

    /* Initialize optional thread-safety mutex */
    ctx->lock_mutex = NULL;
    if (desc->thread_safe) {
        ctx->lock_mutex = malloc(sizeof(_SC_MTX_T));
        if (ctx->lock_mutex) {
            _SC_MTX_INIT((_SC_MTX_T*)ctx->lock_mutex);
        }
    }

    /* Initialize free-list slot allocators */
    _sc_gfx_init_freelist(ctx->buf_slots, SC_GFX_MAX_BUFFERS, &ctx->buf_free_head);
    _sc_gfx_init_freelist(ctx->tex_slots, SC_GFX_MAX_TEXTURES, &ctx->tex_free_head);
    _sc_gfx_init_freelist(ctx->shd_slots, SC_GFX_MAX_SHADERS,  &ctx->shd_free_head);
    _sc_gfx_init_freelist(ctx->pip_slots, SC_GFX_MAX_PIPELINES, &ctx->pip_free_head);

#ifdef SC_GFX_BACKEND_VULKAN
    if (ctx->backend == SC_BACKEND_VULKAN) {
        SCResult r = sc_vulkan_init(ctx, desc, NULL);
        if (r != SC_OK) { free(ctx); return r; }
        *out_ctx = ctx;
        fprintf(stderr, "[sc_gfx] vulkan backend %ux%u\n", ctx->width, ctx->height);
        return SC_OK;
    }
#endif
#ifdef SC_GFX_BACKEND_METAL
    if (ctx->backend == SC_BACKEND_METAL) {
        SCResult r = sc_metal_init(ctx, desc, NULL);
        if (r != SC_OK) { free(ctx); return r; }
        *out_ctx = ctx;
        fprintf(stderr, "[sc_gfx] metal backend %ux%u\n", ctx->width, ctx->height);
        return SC_OK;
    }
#endif
#ifdef SC_GFX_BACKEND_D3D12
    if (ctx->backend == SC_BACKEND_D3D12) {
        SCResult r = sc_d3d12_init(ctx, desc, NULL);
        if (r != SC_OK) { free(ctx); return r; }
        *out_ctx = ctx;
        fprintf(stderr, "[sc_gfx] d3d12 backend %ux%u\n", ctx->width, ctx->height);
        return SC_OK;
    }
#endif
#ifdef SC_GFX_BACKEND_WGPU
    if (ctx->backend == SC_BACKEND_WGPU) {
        SCResult r = sc_wgpu_init(ctx, desc, NULL);
        if (r != SC_OK) { free(ctx); return r; }
        *out_ctx = ctx;
        fprintf(stderr, "[sc_gfx] wgpu backend %ux%u\n", ctx->width, ctx->height);
        return SC_OK;
    }
#endif

    /* Software: allocate framebuffer + depth buffer */
    if (ctx->backend == SC_BACKEND_SOFTWARE) {
        ctx->rasterize = true;
        usize fb_size = (usize)ctx->width * ctx->height;
        if (ctx->width > 0 && fb_size > (SIZE_MAX / 4)) {
            free(ctx); return SC_ERR_INVALID_ARG;
        }
        ctx->framebuffer = (u8*)calloc(fb_size * 4, 1);
        if (!ctx->framebuffer) { free(ctx); return SC_ERR_OOM; }
        ctx->depth_buffer = (f32*)malloc(fb_size * sizeof(f32));
        if (!ctx->depth_buffer) { free(ctx->framebuffer); free(ctx); return SC_ERR_OOM; }
        for (usize i = 0; i < fb_size; i++) ctx->depth_buffer[i] = 1.0f;
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
#ifdef SC_GFX_BACKEND_METAL
    if (ctx->backend == SC_BACKEND_METAL) {
        sc_metal_shutdown(ctx);
        free(ctx);
        return;
    }
#endif
#ifdef SC_GFX_BACKEND_D3D12
    if (ctx->backend == SC_BACKEND_D3D12) {
        sc_d3d12_shutdown(ctx);
        free(ctx);
        return;
    }
#endif
#ifdef SC_GFX_BACKEND_WGPU
    if (ctx->backend == SC_BACKEND_WGPU) {
        sc_wgpu_shutdown(ctx);
        free(ctx);
        return;
    }
#endif
    if (ctx->lock_mutex) {
        _SC_MTX_DESTROY((_SC_MTX_T*)ctx->lock_mutex);
        free(ctx->lock_mutex);
        ctx->lock_mutex = NULL;
    }
    if (ctx->backend == SC_BACKEND_SOFTWARE) {
        for (u32 i = 0; i < SC_GFX_MAX_TEXTURES; i++) {
            free(ctx->tex_data[i].pixels);
        }
        for (u32 i = 0; i < SC_GFX_MAX_BUFFERS; i++) {
            free(ctx->buf_data[i].data);
        }
    }
    free(ctx->depth_buffer);
    free(ctx->framebuffer);
    free(ctx);
}

SCGfxBuffer sc_gfx_make_buffer(SCGfxContext *ctx, const SCGfxBufferDesc *desc) {
    u32 id = _sc_gfx_alloc_slot(ctx->buf_slots, SC_GFX_MAX_BUFFERS, &ctx->buf_free_head);
    SCGfxBuffer h = {id};
    if (id == 0) return h;
#ifdef SC_GFX_BACKEND_VULKAN
    if (ctx->backend == SC_BACKEND_VULKAN) {
        return sc_vulkan_make_buffer(ctx, desc);
    }
#endif
#ifdef SC_GFX_BACKEND_METAL
    if (ctx->backend == SC_BACKEND_METAL) {
        return sc_metal_make_buffer(ctx, desc);
    }
#endif
#ifdef SC_GFX_BACKEND_D3D12
    if (ctx->backend == SC_BACKEND_D3D12) {
        return sc_d3d12_make_buffer(ctx, desc);
    }
#endif
#ifdef SC_GFX_BACKEND_WGPU
    if (ctx->backend == SC_BACKEND_WGPU) {
        return sc_wgpu_make_buffer(ctx, desc);
    }
#endif
    if (ctx->backend == SC_BACKEND_SOFTWARE && desc) {
        _SCBufData *bd = &ctx->buf_data[id];
        bd->type = desc->type;
        bd->size = desc->size;
        bd->data = NULL;
        if (desc->data && desc->size > 0) {
            bd->data = (u8*)malloc(desc->size);
            if (!bd->data) {
                _sc_gfx_free_slot(ctx->buf_slots, id, &ctx->buf_free_head);
                h.id = 0;
                return h;
            }
            memcpy(bd->data, desc->data, desc->size);
        }
    }
    return h;
}
void sc_gfx_update_buffer(SCGfxContext *ctx, SCGfxBuffer buf,
                           const void *data, usize size) {
    if (buf.id == 0 || !data) return;
#ifdef SC_GFX_BACKEND_VULKAN
    if (ctx->backend == SC_BACKEND_VULKAN) {
        sc_vulkan_update_buffer(ctx, buf, data, size);
        return;
    }
#endif
#ifdef SC_GFX_BACKEND_METAL
    if (ctx->backend == SC_BACKEND_METAL) {
        sc_metal_update_buffer(ctx, buf, data, size);
        return;
    }
#endif
#ifdef SC_GFX_BACKEND_D3D12
    if (ctx->backend == SC_BACKEND_D3D12) {
        sc_d3d12_update_buffer(ctx, buf, data, size);
        return;
    }
#endif
#ifdef SC_GFX_BACKEND_WGPU
    if (ctx->backend == SC_BACKEND_WGPU) {
        sc_wgpu_update_buffer(ctx, buf, data, size);
        return;
    }
#endif
    if (ctx->backend == SC_BACKEND_SOFTWARE) {
        _SCBufData *bd = &ctx->buf_data[buf.id];
        free(bd->data);
        bd->data = NULL;
        bd->size = 0;
        if (size > 0) {
            bd->data = (u8*)malloc(size);
            if (bd->data) {
                memcpy(bd->data, data, size);
                bd->size = size;
            }
        }
    }
}
void sc_gfx_destroy_buffer(SCGfxContext *ctx, SCGfxBuffer buf) {
    if (buf.id == 0) return;
#ifdef SC_GFX_BACKEND_VULKAN
    if (ctx->backend == SC_BACKEND_VULKAN) {
        sc_vulkan_destroy_buffer(ctx, buf);
        return;
    }
#endif
#ifdef SC_GFX_BACKEND_METAL
    if (ctx->backend == SC_BACKEND_METAL) {
        sc_metal_destroy_buffer(ctx, buf);
        return;
    }
#endif
#ifdef SC_GFX_BACKEND_D3D12
    if (ctx->backend == SC_BACKEND_D3D12) {
        sc_d3d12_destroy_buffer(ctx, buf);
        return;
    }
#endif
#ifdef SC_GFX_BACKEND_WGPU
    if (ctx->backend == SC_BACKEND_WGPU) {
        sc_wgpu_destroy_buffer(ctx, buf);
        return;
    }
#endif
    if (ctx->backend == SC_BACKEND_SOFTWARE) {
        free(ctx->buf_data[buf.id].data);
        ctx->buf_data[buf.id].data = NULL;
    }
    _sc_gfx_free_slot(ctx->buf_slots, buf.id, &ctx->buf_free_head);
}

SCGfxTexture sc_gfx_make_texture(SCGfxContext *ctx, const SCGfxTextureDesc *desc) {
#ifdef SC_GFX_BACKEND_VULKAN
    if (ctx->backend == SC_BACKEND_VULKAN) {
        return sc_vulkan_make_texture(ctx, desc);
    }
#endif
#ifdef SC_GFX_BACKEND_METAL
    if (ctx->backend == SC_BACKEND_METAL) {
        return sc_metal_make_texture(ctx, desc);
    }
#endif
#ifdef SC_GFX_BACKEND_D3D12
    if (ctx->backend == SC_BACKEND_D3D12) {
        return sc_d3d12_make_texture(ctx, desc);
    }
#endif
#ifdef SC_GFX_BACKEND_WGPU
    if (ctx->backend == SC_BACKEND_WGPU) {
        return sc_wgpu_make_texture(ctx, desc);
    }
#endif
    u32 id = _sc_gfx_alloc_slot(ctx->tex_slots, SC_GFX_MAX_TEXTURES, &ctx->tex_free_head);
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
            _sc_gfx_free_slot(ctx->tex_slots, id, &ctx->tex_free_head);
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
#ifdef SC_GFX_BACKEND_VULKAN
    if (ctx->backend == SC_BACKEND_VULKAN) {
        sc_vulkan_destroy_texture(ctx, tex);
        _sc_gfx_free_slot(ctx->tex_slots, tex.id, &ctx->tex_free_head);
        return;
    }
#endif
#ifdef SC_GFX_BACKEND_METAL
    if (ctx->backend == SC_BACKEND_METAL) {
        sc_metal_destroy_texture(ctx, tex);
        _sc_gfx_free_slot(ctx->tex_slots, tex.id, &ctx->tex_free_head);
        return;
    }
#endif
#ifdef SC_GFX_BACKEND_D3D12
    if (ctx->backend == SC_BACKEND_D3D12) {
        sc_d3d12_destroy_texture(ctx, tex);
        _sc_gfx_free_slot(ctx->tex_slots, tex.id, &ctx->tex_free_head);
        return;
    }
#endif
#ifdef SC_GFX_BACKEND_WGPU
    if (ctx->backend == SC_BACKEND_WGPU) {
        sc_wgpu_destroy_texture(ctx, tex);
        _sc_gfx_free_slot(ctx->tex_slots, tex.id, &ctx->tex_free_head);
        return;
    }
#endif
    if (ctx->backend == SC_BACKEND_SOFTWARE) {
        free(ctx->tex_data[tex.id].pixels);
        ctx->tex_data[tex.id].pixels = NULL;
    }
    _sc_gfx_free_slot(ctx->tex_slots, tex.id, &ctx->tex_free_head);
}

SCGfxShader sc_gfx_make_shader(SCGfxContext *ctx, const SCGfxShaderDesc *desc) {
    u32 id = _sc_gfx_alloc_slot(ctx->shd_slots, SC_GFX_MAX_SHADERS, &ctx->shd_free_head);
    SCGfxShader h = {id};
    if (id == 0) return h;
#ifdef SC_GFX_BACKEND_VULKAN
    if (ctx->backend == SC_BACKEND_VULKAN) {
        return sc_vulkan_make_shader(ctx, desc);
    }
#endif
#ifdef SC_GFX_BACKEND_METAL
    if (ctx->backend == SC_BACKEND_METAL) {
        return sc_metal_make_shader(ctx, desc);
    }
#endif
#ifdef SC_GFX_BACKEND_D3D12
    if (ctx->backend == SC_BACKEND_D3D12) {
        return sc_d3d12_make_shader(ctx, desc);
    }
#endif
#ifdef SC_GFX_BACKEND_WGPU
    if (ctx->backend == SC_BACKEND_WGPU) {
        return sc_wgpu_make_shader(ctx, desc);
    }
#endif
    if (ctx->backend == SC_BACKEND_SOFTWARE && desc) {
        ctx->shd_desc[id] = *desc;
    }
    return h;
}
void sc_gfx_destroy_shader(SCGfxContext *ctx, SCGfxShader shd) {
    if (shd.id == 0) return;
#ifdef SC_GFX_BACKEND_VULKAN
    if (ctx->backend == SC_BACKEND_VULKAN) {
        sc_vulkan_destroy_shader(ctx, shd);
        return;
    }
#endif
#ifdef SC_GFX_BACKEND_METAL
    if (ctx->backend == SC_BACKEND_METAL) {
        sc_metal_destroy_shader(ctx, shd);
        return;
    }
#endif
#ifdef SC_GFX_BACKEND_D3D12
    if (ctx->backend == SC_BACKEND_D3D12) {
        sc_d3d12_destroy_shader(ctx, shd);
        return;
    }
#endif
#ifdef SC_GFX_BACKEND_WGPU
    if (ctx->backend == SC_BACKEND_WGPU) {
        sc_wgpu_destroy_shader(ctx, shd);
        return;
    }
#endif
    _sc_gfx_free_slot(ctx->shd_slots, shd.id, &ctx->shd_free_head);
}

SCGfxPipeline sc_gfx_make_pipeline(SCGfxContext *ctx, const SCGfxPipelineDesc *desc) {
    u32 id = _sc_gfx_alloc_slot(ctx->pip_slots, SC_GFX_MAX_PIPELINES, &ctx->pip_free_head);
    SCGfxPipeline h = {id};
    if (id == 0) return h;
#ifdef SC_GFX_BACKEND_VULKAN
    if (ctx->backend == SC_BACKEND_VULKAN) {
        return sc_vulkan_make_pipeline(ctx, desc);
    }
#endif
#ifdef SC_GFX_BACKEND_METAL
    if (ctx->backend == SC_BACKEND_METAL) {
        return sc_metal_make_pipeline(ctx, desc);
    }
#endif
#ifdef SC_GFX_BACKEND_D3D12
    if (ctx->backend == SC_BACKEND_D3D12) {
        return sc_d3d12_make_pipeline(ctx, desc);
    }
#endif
#ifdef SC_GFX_BACKEND_WGPU
    if (ctx->backend == SC_BACKEND_WGPU) {
        return sc_wgpu_make_pipeline(ctx, desc);
    }
#endif
    if (ctx->backend == SC_BACKEND_SOFTWARE && desc) {
        ctx->pip_desc[id] = *desc;
    }
    return h;
}
void sc_gfx_destroy_pipeline(SCGfxContext *ctx, SCGfxPipeline pip) {
    if (pip.id == 0) return;
#ifdef SC_GFX_BACKEND_VULKAN
    if (ctx->backend == SC_BACKEND_VULKAN) {
        sc_vulkan_destroy_pipeline(ctx, pip);
        return;
    }
#endif
#ifdef SC_GFX_BACKEND_METAL
    if (ctx->backend == SC_BACKEND_METAL) {
        sc_metal_destroy_pipeline(ctx, pip);
        return;
    }
#endif
#ifdef SC_GFX_BACKEND_D3D12
    if (ctx->backend == SC_BACKEND_D3D12) {
        sc_d3d12_destroy_pipeline(ctx, pip);
        return;
    }
#endif
#ifdef SC_GFX_BACKEND_WGPU
    if (ctx->backend == SC_BACKEND_WGPU) {
        sc_wgpu_destroy_pipeline(ctx, pip);
        return;
    }
#endif
    _sc_gfx_free_slot(ctx->pip_slots, pip.id, &ctx->pip_free_head);
}

void sc_gfx_begin_frame(SCGfxContext *ctx, SCColor c) {
    memset(&ctx->frame_stats, 0, sizeof(ctx->frame_stats));
    ctx->batch_vcount = 0;
    ctx->batch_ccount = 0;
    ctx->submit_ccount = 0;
#ifdef SC_GFX_BACKEND_VULKAN
    if (ctx->backend == SC_BACKEND_VULKAN) {
        sc_vulkan_begin_frame(ctx, c);
        return;
    }
#endif
#ifdef SC_GFX_BACKEND_METAL
    if (ctx->backend == SC_BACKEND_METAL) {
        sc_metal_begin_frame(ctx, c);
        return;
    }
#endif
#ifdef SC_GFX_BACKEND_D3D12
    if (ctx->backend == SC_BACKEND_D3D12) {
        sc_d3d12_begin_frame(ctx, c);
        return;
    }
#endif
#ifdef SC_GFX_BACKEND_WGPU
    if (ctx->backend == SC_BACKEND_WGPU) {
        sc_wgpu_begin_frame(ctx, c);
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
        if (ctx->depth_buffer) {
            for (usize i = 0; i < n; i++) ctx->depth_buffer[i] = 1.0f;
        }
    }
}

void sc_gfx_submit(SCGfxContext *ctx, const SCGfxDrawCmd *cmds, u32 count) {
    ctx->frame_stats.draw_calls += count;
    for (u32 i = 0; i < count; i++) {
        ctx->frame_stats.vertex_count += cmds[i].vertex_count;
        ctx->frame_stats.index_count  += cmds[i].index_count;
    }
    u32 n = count < SC_GFX_MAX_CMDS - ctx->submit_ccount
            ? count : SC_GFX_MAX_CMDS - ctx->submit_ccount;
    if (n > 0) {
        memcpy(&ctx->submit_cmds[ctx->submit_ccount], cmds, n * sizeof(SCGfxDrawCmd));
        ctx->submit_ccount += n;
    }
}

/* ---- Software rasteriser: edge function --------------------------------- */
static f32 _sc_edge(SCVec2 a, SCVec2 b, SCVec2 c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

/* ---- MSAA sample positions (N-sample offsets from pixel centre) -------- */
/* Indexed linearly; 1x uses offset[0], 2x uses offset[0..1], 4x uses offset[0..3] */
#define _SC_MSAA_MAX_SAMPLES 4
static const f32 _sc_msaa_offsets[_SC_MSAA_MAX_SAMPLES][2] = {
    { 0.0f, 0.0f },       /* sample 0 - always used            */
    {-0.25f, 0.25f},      /* sample 1 - used in 2x and 4x      */
    { 0.25f,-0.25f},      /* sample 2 - used in 4x             */
    {-0.25f,-0.25f},      /* sample 3 - used in 4x             */
};

/* Return the number of sample positions for a given sample_count (1/2/4) */
SC_INLINE u32 _sc_msaa_sample_count(u32 sc) {
    if (sc >= 4) return 4;
    if (sc == 2) return 2;
    return 1;
}

/* ---- Draw-call sort comparator (pipeline primary, texture secondary) ---- */
static int _sc_gfx_cmd_sort_cmp(const void *a, const void *b) {
    const SCGfxDrawCmd *ca = (const SCGfxDrawCmd*)a;
    const SCGfxDrawCmd *cb = (const SCGfxDrawCmd*)b;
    u32 pa = ca->pipeline.id, pb = cb->pipeline.id;
    if (pa != pb) return pa < pb ? -1 : 1;
    u32 ta = ca->texture.id, tb = cb->texture.id;
    if (ta != tb) return ta < tb ? -1 : 1;
    return 0;
}

static bool _sc_depth_test(SCCompareFunc cmp, f32 frag_z, f32 ref_z) {
    switch (cmp) {
        case SC_COMPARE_NEVER:    return false;
        case SC_COMPARE_LESS:     return frag_z < ref_z;
        case SC_COMPARE_EQUAL:    return fabsf(frag_z - ref_z) < 0.000001f;
        case SC_COMPARE_LEQUAL:   return frag_z <= ref_z + 0.000001f;
        case SC_COMPARE_GREATER:  return frag_z > ref_z;
        case SC_COMPARE_NOTEQUAL: return fabsf(frag_z - ref_z) >= 0.000001f;
        case SC_COMPARE_GEQUAL:   return frag_z >= ref_z - 0.000001f;
        case SC_COMPARE_ALWAYS:
        default:                  return true;
    }
}

/* Rasterise a single triangle into the framebuffer.
   depth_val is the uniform z for all vertices (0=near, 1=far).
   if !depth_test depth checking is skipped.
   scissor of {0,0,0,0} means no scissor. */
static void _sc_raster_tri(SCGfxContext *ctx,
    SCVec2 v0, SCVec2 v1, SCVec2 v2,
    SCVec2 t0, SCVec2 t1, SCVec2 t2,
    u8 r0, u8 g0, u8 b0, u8 a0,
    u8 r1, u8 g1, u8 b1, u8 a1,
    u8 r2, u8 g2, u8 b2, u8 a2,
    _SCTexData *tex,
    f32 depth_val, bool depth_test, bool depth_write,
    SCCompareFunc depth_compare,
    SCRect2i scissor)
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

    /* Apply scissor rect if valid */
    if (scissor.w > 0 && scissor.h > 0) {
        i32 sx0 = scissor.x;
        i32 sy0 = scissor.y;
        i32 sx1 = scissor.x + scissor.w - 1;
        i32 sy1 = scissor.y + scissor.h - 1;
        if (ix0 > sx1 || iy0 > sy1 || ix1 < sx0 || iy1 < sy0) return;
        if (ix0 < sx0) ix0 = sx0;
        if (iy0 < sy0) iy0 = sy0;
        if (ix1 > sx1) ix1 = sx1;
        if (iy1 > sy1) iy1 = sy1;
    }

    u32 nsamples = _sc_msaa_sample_count(ctx->sample_count);

    for (int y = iy0; y <= iy1; y++) {
        for (int x = ix0; x <= ix1; x++) {
            /* --- MSAA: evaluate all sub-pixel samples --- */
            u32 coverage = 0;
            u32 acc_r = 0, acc_g = 0, acc_b = 0, acc_a = 0;
            f32 sum_a = 0, sum_b = 0, sum_c = 0;

            for (u32 s = 0; s < nsamples; s++) {
                SCVec2 sp = {(f32)x + 0.5f + _sc_msaa_offsets[s][0],
                             (f32)y + 0.5f + _sc_msaa_offsets[s][1]};
                f32 swa = _sc_edge(v1, v2, sp);
                f32 swb = _sc_edge(v2, v0, sp);
                f32 swc = _sc_edge(v0, v1, sp);

                if (swa < -0.01f || swb < -0.01f || swc < -0.01f) continue;

                f32 sa = swa * inv_area;
                f32 sb = swb * inv_area;
                f32 sc = swc * inv_area;

                acc_r += (u32)(sa * (f32)r0 + sb * (f32)r1 + sc * (f32)r2);
                acc_g += (u32)(sa * (f32)g0 + sb * (f32)g1 + sc * (f32)g2);
                acc_b += (u32)(sa * (f32)b0 + sb * (f32)b1 + sc * (f32)b2);
                acc_a += (u32)(sa * (f32)a0 + sb * (f32)a1 + sc * (f32)a2);
                sum_a += sa; sum_b += sb; sum_c += sc;
                coverage++;
            }

            if (coverage == 0) continue;

            usize idx = (usize)y * (usize)ctx->width + (usize)x;

            /* Depth test (evaluated at pixel centroid when MSAA active) */
            f32 centroid_a = sum_a / (f32)coverage;
            f32 centroid_b = sum_b / (f32)coverage;
            f32 centroid_c = sum_c / (f32)coverage;

            if (depth_test) {
                f32 depth_at_sample = centroid_a * depth_val + centroid_b * depth_val + centroid_c * depth_val;
                if (!_sc_depth_test(depth_compare, depth_at_sample, ctx->depth_buffer[idx]))
                    continue;
            }
            if (depth_write)
                ctx->depth_buffer[idx] = depth_val;

            /* Resolve: average color across covered samples */
            u8 sr = (u8)SC_MIN(acc_r / coverage, 255u);
            u8 sg = (u8)SC_MIN(acc_g / coverage, 255u);
            u8 sb = (u8)SC_MIN(acc_b / coverage, 255u);
            u8 sa = (u8)SC_MIN(acc_a / coverage, 255u);

            /* Interpolate texcoord at centroid */
            f32 u = centroid_a * t0.x + centroid_b * t1.x + centroid_c * t2.x;
            f32 v = centroid_a * t0.y + centroid_b * t1.y + centroid_c * t2.y;

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
            usize fb_off = idx * 4;
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
    /* Sort retained draw calls by (pipeline, texture) to reduce state changes */
    if (ctx->submit_ccount > 1) {
        qsort(ctx->submit_cmds, ctx->submit_ccount, sizeof(SCGfxDrawCmd),
              _sc_gfx_cmd_sort_cmp);
    }

#ifdef SC_GFX_BACKEND_VULKAN
    if (ctx->backend == SC_BACKEND_VULKAN) {
        sc_vulkan_end_frame(ctx);
        return;
    }
#endif
#ifdef SC_GFX_BACKEND_METAL
    if (ctx->backend == SC_BACKEND_METAL) {
        sc_metal_end_frame(ctx);
        return;
    }
#endif
#ifdef SC_GFX_BACKEND_D3D12
    if (ctx->backend == SC_BACKEND_D3D12) {
        sc_d3d12_end_frame(ctx);
        return;
    }
#endif
#ifdef SC_GFX_BACKEND_WGPU
    if (ctx->backend == SC_BACKEND_WGPU) {
        sc_wgpu_end_frame(ctx);
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
        for (u32 j = 0; j + 2 < cmd->vertex_count; j += 3) {
            SCGfxVertex2D *v = &ctx->batch_verts[vo + j];
            SCVec2 p0 = {v[0].x, v[0].y}; SCVec2 t0 = {v[0].u, v[0].v};
            SCVec2 p1 = {v[1].x, v[1].y}; SCVec2 t1 = {v[1].u, v[1].v};
            SCVec2 p2 = {v[2].x, v[2].y}; SCVec2 t2 = {v[2].u, v[2].v};

            SCRect2i no_scissor = {0,0,0,0};
            _sc_raster_tri(ctx,
                p0, p1, p2, t0, t1, t2,
                v[0].r, v[0].g, v[0].b, v[0].a,
                v[1].r, v[1].g, v[1].b, v[1].a,
                v[2].r, v[2].g, v[2].b, v[2].a,
                tex, 0.0f, false, false, SC_COMPARE_ALWAYS, no_scissor);
        }
    }

    /* Process user-submitted draw commands */
    for (u32 i = 0; i < ctx->submit_ccount; i++) {
        SCGfxDrawCmd *cmd = &ctx->submit_cmds[i];
        if (cmd->vertex_buf.id == 0) continue;
        if (cmd->vertex_buf.id >= SC_GFX_MAX_BUFFERS) continue;
        _SCBufData *vb = &ctx->buf_data[cmd->vertex_buf.id];
        if (!vb->data) continue;

        _SCTexData *tex = NULL;
        if (cmd->texture.id > 0 && cmd->texture.id < SC_GFX_MAX_TEXTURES) {
            tex = &ctx->tex_data[cmd->texture.id];
        }

        SCGfxVertex2D *verts = (SCGfxVertex2D*)vb->data;
        u32 base_v = cmd->base_vertex;
        u32 vcount = cmd->vertex_count;

        /* Resolve depth state from pipeline */
        SCGfxDepthState ds = {0};
        ds.depth_test  = false;
        ds.depth_write = false;
        ds.depth_compare = SC_COMPARE_ALWAYS;
        if (cmd->pipeline.id > 0 && cmd->pipeline.id < SC_GFX_MAX_PIPELINES) {
            ds = ctx->pip_desc[cmd->pipeline.id].depth;
        }
        f32 depth_val = cmd->depth;

        if (cmd->index_buf.id > 0 && cmd->index_buf.id < SC_GFX_MAX_BUFFERS) {
            _SCBufData *ib = &ctx->buf_data[cmd->index_buf.id];
            if (ib->data && ib->type == SC_BUFFER_INDEX) {
                u32 *indices = (u32*)ib->data;
                u32 base_i = cmd->base_index;
                u32 icount = cmd->index_count > 0 ? cmd->index_count : ib->size / sizeof(u32);
                for (u32 j = 0; j + 2 < icount; j += 3) {
                    u32 i0 = indices[base_i + j];
                    u32 i1 = indices[base_i + j + 1];
                    u32 i2 = indices[base_i + j + 2];
                    usize max_v = vb->size / sizeof(SCGfxVertex2D);
                    if (i0 >= max_v || i1 >= max_v || i2 >= max_v) continue;
                    SCGfxVertex2D *v0 = &verts[i0];
                    SCGfxVertex2D *v1 = &verts[i1];
                    SCGfxVertex2D *v2 = &verts[i2];
                    SCVec2 p0 = {v0->x, v0->y}; SCVec2 t0 = {v0->u, v0->v};
                    SCVec2 p1 = {v1->x, v1->y}; SCVec2 t1 = {v1->u, v1->v};
                    SCVec2 p2 = {v2->x, v2->y}; SCVec2 t2 = {v2->u, v2->v};
                    _sc_raster_tri(ctx, p0,p1,p2, t0,t1,t2,
                        v0->r,v0->g,v0->b,v0->a,
                        v1->r,v1->g,v1->b,v1->a,
                        v2->r,v2->g,v2->b,v2->a, tex,
                        depth_val, ds.depth_test, ds.depth_write, ds.depth_compare,
                        cmd->scissor);
                }
                continue;
            }
        }

        /* Non-indexed draw */
        u32 end = base_v + vcount;
        if (end > vb->size / sizeof(SCGfxVertex2D)) {
            end = (u32)(vb->size / sizeof(SCGfxVertex2D));
        }
        for (u32 j = base_v; j + 2 < end; j += 3) {
            SCGfxVertex2D *v = &verts[j];
            SCVec2 p0 = {v[0].x, v[0].y}; SCVec2 t0 = {v[0].u, v[0].v};
            SCVec2 p1 = {v[1].x, v[1].y}; SCVec2 t1 = {v[1].u, v[1].v};
            SCVec2 p2 = {v[2].x, v[2].y}; SCVec2 t2 = {v[2].u, v[2].v};
            _sc_raster_tri(ctx, p0,p1,p2, t0,t1,t2,
                v[0].r,v[0].g,v[0].b,v[0].a,
                v[1].r,v[1].g,v[1].b,v[1].a,
                v[2].r,v[2].g,v[2].b,v[2].a, tex,
                depth_val, ds.depth_test, ds.depth_write, ds.depth_compare,
                cmd->scissor);
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

void sc_gfx_lock(SCGfxContext *ctx) {
    if (ctx && ctx->lock_mutex) {
        _SC_MTX_LOCK((_SC_MTX_T*)ctx->lock_mutex);
    }
}
void sc_gfx_unlock(SCGfxContext *ctx) {
    if (ctx && ctx->lock_mutex) {
        _SC_MTX_UNLOCK((_SC_MTX_T*)ctx->lock_mutex);
    }
}

SCResult sc_gfx_resize(SCGfxContext *ctx, u32 width, u32 height) {
    if (!ctx || width == 0 || height == 0) return SC_ERR_INVALID_ARG;
    if (height > (SIZE_MAX / 4) / width) return SC_ERR_INVALID_ARG;
#ifdef SC_GFX_BACKEND_VULKAN
    if (ctx->backend == SC_BACKEND_VULKAN) {
        return sc_vulkan_resize(ctx, width, height);
    }
#endif
#ifdef SC_GFX_BACKEND_METAL
    if (ctx->backend == SC_BACKEND_METAL) {
        return sc_metal_resize(ctx, width, height);
    }
#endif
#ifdef SC_GFX_BACKEND_D3D12
    if (ctx->backend == SC_BACKEND_D3D12) {
        return sc_d3d12_resize(ctx, width, height);
    }
#endif
#ifdef SC_GFX_BACKEND_WGPU
    if (ctx->backend == SC_BACKEND_WGPU) {
        return sc_wgpu_resize(ctx, width, height);
    }
#endif
    if (ctx->backend == SC_BACKEND_SOFTWARE) {
        usize fb_size = (usize)width * height;
        u8 *fb = (u8*)realloc(ctx->framebuffer, fb_size * 4);
        if (!fb) return SC_ERR_OOM;
        ctx->framebuffer = fb;
        f32 *db = (f32*)realloc(ctx->depth_buffer, fb_size * sizeof(f32));
        if (!db) return SC_ERR_OOM;
        ctx->depth_buffer = db;
        ctx->width  = width;
        ctx->height = height;
        return SC_OK;
    }
    return SC_ERR_NOT_SUPPORTED;
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
