/*
 * sc_backend_metal.h  --  SilverCore Metal GPU Backend
 *
 * Full Metal backend for hardware-accelerated 2-D rendering on
 * macOS 10.13+ / iOS 11+.  Compile as Objective-C (.mm) and link:
 *   -framework Metal -framework MetalKit -framework QuartzCore
 *
 * To activate:
 *   #define SC_GFX_BACKEND_METAL
 *   #define SC_BACKEND_METAL_IMPLEMENTATION
 *
 * Architecture:
 *   MTLDevice -> MTLCommandQueue -> per-frame MTLCommandBuffer +
 *   MTLRenderPipelineState + MTLBuffer (vertex) + MTLTexture (swapchain)
 */
#ifndef SC_BACKEND_METAL_H
#define SC_BACKEND_METAL_H

#include "../include/sc_types.h"
#include "../include/sc_gfx.h"

typedef struct SCMetalDesc {
    bool         headless;
} SCMetalDesc;

SCResult sc_metal_init        (SCGfxContext *ctx, const SCGfxDesc *desc,
                                const SCMetalDesc *mtl_desc);
void     sc_metal_shutdown    (SCGfxContext *ctx);
void     sc_metal_begin_frame (SCGfxContext *ctx, SCColor clear);
void     sc_metal_submit      (SCGfxContext *ctx,
                               const SCGfxDrawCmd *cmds, u32 count);
void     sc_metal_end_frame   (SCGfxContext *ctx);
SCGfxBuffer   sc_metal_make_buffer   (SCGfxContext *ctx, const SCGfxBufferDesc *desc);
void          sc_metal_destroy_buffer(SCGfxContext *ctx, SCGfxBuffer buf);
void          sc_metal_update_buffer (SCGfxContext *ctx, SCGfxBuffer buf,
                                       const void *data, usize size);
SCGfxTexture  sc_metal_make_texture  (SCGfxContext *ctx, const SCGfxTextureDesc *desc);
void          sc_metal_destroy_texture(SCGfxContext *ctx, SCGfxTexture tex);
SCGfxShader   sc_metal_make_shader   (SCGfxContext *ctx, const SCGfxShaderDesc *desc);
void          sc_metal_destroy_shader(SCGfxContext *ctx, SCGfxShader shd);
SCGfxPipeline sc_metal_make_pipeline (SCGfxContext *ctx, const SCGfxPipelineDesc *desc);
void          sc_metal_destroy_pipeline(SCGfxContext *ctx, SCGfxPipeline pip);
SCResult      sc_metal_resize         (SCGfxContext *ctx, u32 width, u32 height);

#ifdef SC_BACKEND_METAL_IMPLEMENTATION

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <objc/runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Constants --------------------------------------------------------- */
#define SC_MTL_FRAME_OVERLAP 3
#define SC_MTL_MAX_VERTS     (1 << 20)

/* ---- Per-frame data ---------------------------------------------------- */
typedef struct {
    id<MTLCommandBuffer> cmd_buf;
    id<MTLBuffer>        vert_buf;
    usize                vert_capacity;
    dispatch_semaphore_t frame_sem;
} _SCMtlFrame;

/* ---- Metal backend state ----------------------------------------------- */
typedef struct {
    id<MTLDevice>         device;
    id<MTLCommandQueue>   queue;
    id<MTLLibrary>        library;
    id<MTLRenderPipelineState> pipeline;
    id<MTLDepthStencilState>   depth_state;

    /* Surface / swapchain */
    CAMetalLayer         *layer;
    id<CAMetalDrawable>   drawable;
    id<MTLTexture>        swap_tex;
    MTLPixelFormat        pix_fmt;

    /* Offscreen (headless) */
    id<MTLTexture>        os_tex;

    /* Default white 1x1 texture (always bound so the built-in fragment
       shader never samples an unbound texture) */
    id<MTLTexture>        white_tex;

    /* Extent */
    u32                   width, height;
    bool                  headless;
    bool                  in_frame;

    /* Frame overlap */
    _SCMtlFrame           frames[SC_MTL_FRAME_OVERLAP];
    u32                   frame_idx;

    /* User resource storage */
    id<MTLBuffer>         buf_buffers[SC_GFX_MAX_BUFFERS];
    id<MTLTexture>        tex_textures[SC_GFX_MAX_TEXTURES];
    id<MTLFunction>       shd_vs[SC_GFX_MAX_SHADERS];
    id<MTLFunction>       shd_fs[SC_GFX_MAX_SHADERS];
    id<MTLRenderPipelineState> user_pipelines[SC_GFX_MAX_PIPELINES];
    id<MTLDepthStencilState>   user_depth_states[SC_GFX_MAX_PIPELINES];

    /* Built-in vertex/fragment functions — used as a fallback when a user
       pipeline references a missing or failed-to-compile shader, since
       Metal asserts (SIGABRT) on a nil vertexFunction in
       newRenderPipelineStateWithDescriptor:. */
    id<MTLFunction>       builtin_vs;
    id<MTLFunction>       builtin_fs;
} _SCMtlState;

/* -------------------------------------------------------------------------
 * Helper: compile a Metal shader from source
 * ---------------------------------------------------------------------- */
static id<MTLFunction> _sc_mtl_make_shader(id<MTLDevice> dev,
                                            const char *src,
                                            const char *func_name) {
    if (!src || !func_name) return nil;
    NSString *ns_src = [NSString stringWithUTF8String:src];
    NSString *ns_name = [NSString stringWithUTF8String:func_name];
    NSError *err = nil;
    id<MTLLibrary> lib = [dev newLibraryWithSource:ns_src options:nil error:&err];
    if (err) {
        fprintf(stderr, "[sc_metal] shader compile error: %s\n",
                [[err localizedDescription] UTF8String]);
        return nil;
    }
    id<MTLFunction> fn = [lib newFunctionWithName:ns_name];
    return fn;
}

/* =========================================================================
 * Public API
 * ========================================================================= */

SCResult sc_metal_init(SCGfxContext *ctx, const SCGfxDesc *desc,
                        const SCMetalDesc *mtl_desc) {
    _SCMtlState *s = (_SCMtlState*)calloc(1, sizeof(_SCMtlState));
    if (!s) return SC_ERR_OOM;
    ctx->backend_data = s;

    s->width    = desc->width  ? desc->width  : 1280;
    s->height   = desc->height ? desc->height : 720;
    s->headless = mtl_desc ? mtl_desc->headless : true;
    s->pix_fmt  = MTLPixelFormatBGRA8Unorm;

    s->device = MTLCreateSystemDefaultDevice();
    if (!s->device) { free(s); ctx->backend_data = NULL; return SC_ERR_GFX; }
    s->queue = [s->device newCommandQueue];
    if (!s->queue) { sc_metal_shutdown(ctx); return SC_ERR_GFX; }

    /* Build default library from embedded source or default library */
    NSError *err = nil;
    NSString *lib_src =
        @"#include <metal_stdlib>\n"
         "using namespace metal;\n"
         "struct VIn { packed_float2 pos; packed_float2 uv; uchar4 col; };\n"
         "struct VOut { float4 pos [[position]]; float2 uv; float4 col; };\n"
         "vertex VOut vs_main(uint vid [[vertex_id]],\n"
         "  constant VIn *v [[buffer(0)]]) {\n"
         "  VOut o; o.pos = float4(v[vid].pos, 0, 1);\n"
         "  o.uv = v[vid].uv; o.col = float4(v[vid].col) / 255.0;\n"
         "  return o;\n"
         "}\n"
         "fragment float4 fs_main(VOut in [[stage_in]],\n"
         "  texture2d<float> tex [[texture(0)]],\n"
         "  constant float &has_tex [[buffer(0)]]) {\n"
         "  if (has_tex > 0.5) {\n"
         "    constexpr sampler s(coord::normalized, filter::linear);\n"
         "    return tex.sample(s, in.uv) * in.col;\n"
         "  }\n"
         "  return in.col;\n"
         "}\n";
    s->library = [s->device newLibraryWithSource:lib_src options:nil error:&err];
    if (!s->library) {
        fprintf(stderr, "[sc_metal] library error: %s\n",
                [[err localizedDescription] UTF8String]);
        sc_metal_shutdown(ctx);
        return SC_ERR_GFX;
    }

    id<MTLFunction> vs = [s->library newFunctionWithName:@"vs_main"];
    id<MTLFunction> fs = [s->library newFunctionWithName:@"fs_main"];
    if (!vs || !fs) { sc_metal_shutdown(ctx); return SC_ERR_GFX; }
    s->builtin_vs = vs;
    s->builtin_fs = fs;

    /* Build render pipeline state */
    MTLRenderPipelineDescriptor *rpd = [MTLRenderPipelineDescriptor new];
    rpd.vertexFunction   = vs;
    rpd.fragmentFunction = fs;
    rpd.colorAttachments[0].pixelFormat = s->pix_fmt;
    rpd.colorAttachments[0].blendingEnabled = YES;
    rpd.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    rpd.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    rpd.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
    rpd.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorZero;
    rpd.depthAttachmentPixelFormat = MTLPixelFormatInvalid;

    id<MTLRenderPipelineState> pip = [s->device newRenderPipelineStateWithDescriptor:rpd
                                                                               error:&err];
    if (!pip) {
        fprintf(stderr, "[sc_metal] pipeline error: %s\n",
                [[err localizedDescription] UTF8String]);
        sc_metal_shutdown(ctx);
        return SC_ERR_GFX;
    }
    s->pipeline = pip;

    /* Default depth state (no test, no write) */
    MTLDepthStencilDescriptor *dsd = [MTLDepthStencilDescriptor new];
    dsd.depthCompareFunction = MTLCompareFunctionAlways;
    dsd.depthWriteEnabled = NO;
    s->depth_state = [s->device newDepthStencilStateWithDescriptor:dsd];

    if (!s->headless) {
        CAMetalLayer *layer = (__bridge CAMetalLayer*)desc->native_window;
        if (!layer) { sc_metal_shutdown(ctx); return SC_ERR_NOT_SUPPORTED; }
        s->layer = layer;
        layer.pixelFormat = s->pix_fmt;
        layer.drawableSize = CGSizeMake(s->width, s->height);
    } else {
        MTLTextureDescriptor *td = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:s->pix_fmt
            width:s->width height:s->height mipmapped:NO];
        td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        s->os_tex = [s->device newTextureWithDescriptor:td];
        if (!s->os_tex) { sc_metal_shutdown(ctx); return SC_ERR_GFX; }
    }

    /* Default white 1x1 texture — the built-in fragment shader samples
       texture(0), so a valid texture must be bound on every draw. */
    {
        MTLTextureDescriptor *wtd = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
            width:1 height:1 mipmapped:NO];
        wtd.usage = MTLTextureUsageShaderRead;
        s->white_tex = [s->device newTextureWithDescriptor:wtd];
        if (s->white_tex) {
            static const u8 px[4] = {255,255,255,255};
            MTLRegion wr = {{0,0,0},{1,1,1}};
            [s->white_tex replaceRegion:wr mipmapLevel:0 withBytes:px bytesPerRow:4];
        }
    }

    /* Per-frame semaphores */
    for (u32 i = 0; i < SC_MTL_FRAME_OVERLAP; i++) {
        s->frames[i].frame_sem = dispatch_semaphore_create(1);
    }

    s->frame_idx = 0;
    s->in_frame  = false;

    fprintf(stderr, "[sc_metal] initialized %s %ux%u\n",
            s->headless ? "headless" : "windowed", s->width, s->height);
    return SC_OK;
}

void sc_metal_shutdown(SCGfxContext *ctx) {
    if (!ctx || !ctx->backend_data) return;
    _SCMtlState *s = (_SCMtlState*)ctx->backend_data;

    for (u32 i = 0; i < SC_MTL_FRAME_OVERLAP; i++) {
        if (s->frames[i].frame_sem) dispatch_semaphore_wait(s->frames[i].frame_sem, DISPATCH_TIME_FOREVER);
    }

    for (u32 i = 0; i < SC_GFX_MAX_PIPELINES; i++) {
        s->user_pipelines[i] = nil;
        s->user_depth_states[i] = nil;
    }
    for (u32 i = 0; i < SC_GFX_MAX_SHADERS; i++) {
        s->shd_vs[i] = nil;
        s->shd_fs[i] = nil;
    }
    for (u32 i = 0; i < SC_GFX_MAX_TEXTURES; i++) s->tex_textures[i] = nil;
    for (u32 i = 0; i < SC_GFX_MAX_BUFFERS; i++) s->buf_buffers[i] = nil;

    s->os_tex    = nil;
    s->white_tex = nil;
    s->builtin_vs = nil;
    s->builtin_fs = nil;
    s->pipeline  = nil;
    s->depth_state = nil;
    s->library   = nil;
    s->queue     = nil;
    s->device    = nil;

    free(s);
    ctx->backend_data = NULL;
}

void sc_metal_begin_frame(SCGfxContext *ctx, SCColor clear) {
    if (!ctx || !ctx->backend_data) return;
    _SCMtlState *s = (_SCMtlState*)ctx->backend_data;

    _SCMtlFrame *f = &s->frames[s->frame_idx];
    dispatch_semaphore_wait(f->frame_sem, DISPATCH_TIME_FOREVER);

    @autoreleasepool {
        if (!s->headless) {
            s->drawable = [s->layer nextDrawable];
            if (!s->drawable) { s->in_frame = false; return; }
            s->swap_tex = s->drawable.texture;
        }

        f->cmd_buf = [s->queue commandBuffer];

        MTLRenderPassDescriptor *rpd = [MTLRenderPassDescriptor renderPassDescriptor];
        id<MTLTexture> target = s->headless ? s->os_tex : s->swap_tex;
        rpd.colorAttachments[0].texture = target;
        rpd.colorAttachments[0].loadAction = MTLLoadActionClear;
        rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
        rpd.colorAttachments[0].clearColor =
            MTLClearColorMake(clear.r, clear.g, clear.b, clear.a);

        id<MTLRenderCommandEncoder> enc =
            [f->cmd_buf renderCommandEncoderWithDescriptor:rpd];
        if (!enc) { s->in_frame = false; return; }

        MTLViewport vp = {0, 0, (double)s->width, (double)s->height, 0, 1};
        [enc setViewport:vp];
        [enc setRenderPipelineState:s->pipeline];
        [enc setDepthStencilState:s->depth_state];

        objc_setAssociatedObject(f->cmd_buf, "encoder", enc,
                                 OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    }

    s->in_frame = true;
}

void sc_metal_submit(SCGfxContext *ctx, const SCGfxDrawCmd *cmds, u32 count) {
    /* Stats are counted in common sc_gfx_submit */
    SC_UNUSED(ctx); SC_UNUSED(cmds); SC_UNUSED(count);
}

void sc_metal_end_frame(SCGfxContext *ctx) {
    if (!ctx || !ctx->backend_data || !((_SCMtlState*)ctx->backend_data)->in_frame) return;
    _SCMtlState *s = (_SCMtlState*)ctx->backend_data;
    _SCMtlFrame *f = &s->frames[s->frame_idx];

    @autoreleasepool {
        id<MTLRenderCommandEncoder> enc =
            (id<MTLRenderCommandEncoder>)objc_getAssociatedObject(f->cmd_buf, "encoder");
        if (!enc) return;

        /* Upload batch vertices */
        if (ctx->batch_vcount > 0) {
            usize needed = (usize)ctx->batch_vcount * sizeof(SCGfxVertex2D);
            if (needed > f->vert_capacity || !f->vert_buf) {
                f->vert_buf = [s->device newBufferWithLength:needed
                                                     options:MTLResourceStorageModeShared];
                f->vert_capacity = needed;
            }
            memcpy([f->vert_buf contents], ctx->batch_verts, needed);

            [enc setVertexBuffer:f->vert_buf offset:0 atIndex:0];

            for (u32 i = 0; i < ctx->batch_ccount; i++) {
                u32 tex_id = ctx->batch_cmds[i].texture.id;
                bool has_tex = tex_id > 0 && tex_id < SC_GFX_MAX_TEXTURES &&
                               s->tex_textures[tex_id];
                f32 ht = has_tex ? 1.0f : 0.0f;
                [enc setFragmentBytes:&ht length:sizeof(f32) atIndex:0];
                /* The fragment shader always samples texture(0); bind the
                   white texture when the draw has none so validation passes. */
                [enc setFragmentTexture:has_tex ? s->tex_textures[tex_id] : s->white_tex
                               atIndex:0];
                [enc drawPrimitives:MTLPrimitiveTypeTriangle
                       vertexStart:ctx->batch_cmds[i].vertex_offset
                       vertexCount:ctx->batch_cmds[i].vertex_count];
            }
        }

        /* Process user-submitted draw commands */
        for (u32 i = 0; i < ctx->submit_ccount; i++) {
            SCGfxDrawCmd *cmd = &ctx->submit_cmds[i];
            u32 vb_id = cmd->vertex_buf.id;
            if (vb_id == 0 || vb_id >= SC_GFX_MAX_BUFFERS) continue;
            if (!s->buf_buffers[vb_id]) continue;

            id<MTLRenderPipelineState> pip = s->pipeline;
            id<MTLDepthStencilState> ds = s->depth_state;
            u32 pip_id = cmd->pipeline.id;
            if (pip_id > 0 && pip_id < SC_GFX_MAX_PIPELINES && s->user_pipelines[pip_id]) {
                pip = s->user_pipelines[pip_id];
                ds = s->user_depth_states[pip_id] ?: s->depth_state;
            }

            [enc setRenderPipelineState:pip];
            [enc setDepthStencilState:ds];
            [enc setVertexBuffer:s->buf_buffers[vb_id] offset:0 atIndex:0];

            u32 tex_id = cmd->texture.id;
            bool has_tex = tex_id > 0 && tex_id < SC_GFX_MAX_TEXTURES &&
                           s->tex_textures[tex_id];
            f32 ht = has_tex ? 1.0f : 0.0f;
            [enc setFragmentBytes:&ht length:sizeof(f32) atIndex:0];
            [enc setFragmentTexture:has_tex ? s->tex_textures[tex_id] : s->white_tex
                           atIndex:0];

            u32 ib_id = cmd->index_buf.id;
            if (ib_id > 0 && ib_id < SC_GFX_MAX_BUFFERS && s->buf_buffers[ib_id]) {
                [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                               indexCount:cmd->index_count > 0 ? cmd->index_count
                                        : (u32)([s->buf_buffers[ib_id] length] / sizeof(u32))
                                indexType:MTLIndexTypeUInt32
                              indexBuffer:s->buf_buffers[ib_id]
                        indexBufferOffset:0];
            } else {
                [enc drawPrimitives:MTLPrimitiveTypeTriangle
                       vertexStart:cmd->base_vertex
                       vertexCount:cmd->vertex_count];
            }
        }

        [enc endEncoding];
        if (s->drawable) [f->cmd_buf presentDrawable:s->drawable];
        [f->cmd_buf addCompletedHandler:^(id<MTLCommandBuffer>) {
            dispatch_semaphore_signal(f->frame_sem);
        }];
        [f->cmd_buf commit];
    }

    s->frame_idx = (s->frame_idx + 1) % SC_MTL_FRAME_OVERLAP;
    s->in_frame  = false;
}

/* -------------------------------------------------------------------------
 * Buffer management
 * ---------------------------------------------------------------------- */
SCGfxBuffer sc_metal_make_buffer(SCGfxContext *ctx, const SCGfxBufferDesc *desc) {
    SCGfxBuffer h = {0};
    if (!ctx || !ctx->backend_data) return h;
    _SCMtlState *s = (_SCMtlState*)ctx->backend_data;

    /* NULL desc === valid empty buffer slot */
    u32 sid = _sc_gfx_alloc_slot(ctx->buf_slots, SC_GFX_MAX_BUFFERS, &ctx->buf_free_head);
    if (sid == 0) return h;
    h.id = sid;
    if (!desc) return h;

    if (desc->data && desc->size > 0) {
        MTLResourceOptions opts = MTLResourceStorageModeShared;
        s->buf_buffers[sid] = [s->device newBufferWithBytes:desc->data
                                                      length:desc->size
                                                     options:opts];
    } else {
        s->buf_buffers[sid] = [s->device newBufferWithLength:desc->size ? desc->size : 1
                                                      options:MTLResourceStorageModeShared];
    }
    if (!s->buf_buffers[sid]) {
        _sc_gfx_free_slot(ctx->buf_slots, sid, &ctx->buf_free_head);
        h.id = 0;
    }
    return h;
}

void sc_metal_destroy_buffer(SCGfxContext *ctx, SCGfxBuffer buf) {
    if (!ctx || !ctx->backend_data || buf.id == 0) return;
    _SCMtlState *s = (_SCMtlState*)ctx->backend_data;
    if (buf.id >= SC_GFX_MAX_BUFFERS) return;
    s->buf_buffers[buf.id] = nil;
    _sc_gfx_free_slot(ctx->buf_slots, buf.id, &ctx->buf_free_head);
}

void sc_metal_update_buffer(SCGfxContext *ctx, SCGfxBuffer buf,
                             const void *data, usize size) {
    if (!ctx || !ctx->backend_data || buf.id == 0 || !data) return;
    _SCMtlState *s = (_SCMtlState*)ctx->backend_data;
    if (buf.id >= SC_GFX_MAX_BUFFERS) return;
    s->buf_buffers[buf.id] = nil;
    if (size > 0) {
        s->buf_buffers[buf.id] = [s->device newBufferWithBytes:data
                                                         length:size
                                                        options:MTLResourceStorageModeShared];
    }
}

/* -------------------------------------------------------------------------
 * Texture management
 * ---------------------------------------------------------------------- */
SCGfxTexture sc_metal_make_texture(SCGfxContext *ctx, const SCGfxTextureDesc *desc) {
    SCGfxTexture h = {0};
    if (!ctx || !ctx->backend_data || !desc) return h;
    _SCMtlState *s = (_SCMtlState*)ctx->backend_data;

    u32 sid = 0;
    for (u32 i = 1; i < SC_GFX_MAX_TEXTURES; i++) {
        if (!s->tex_textures[i]) { sid = i; break; }
    }
    if (sid == 0) return h;
    h.id = sid;

    MTLPixelFormat pf = MTLPixelFormatRGBA8Unorm;
    if (desc->fmt == SC_PIXFMT_R8) pf = MTLPixelFormatR8Unorm;

    MTLTextureDescriptor *td = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:pf
        width:desc->width height:desc->height mipmapped:NO];
    td.usage = MTLTextureUsageShaderRead;

    if (desc->data) {
        MTLRegion region = {{0,0,0}, {(u32)desc->width, (u32)desc->height, 1}};
        id<MTLTexture> tex = [s->device newTextureWithDescriptor:td];
        [tex replaceRegion:region mipmapLevel:0 withBytes:desc->data
               bytesPerRow:desc->width * 4];
        s->tex_textures[sid] = tex;
    } else {
        s->tex_textures[sid] = [s->device newTextureWithDescriptor:td];
    }

    if (!s->tex_textures[sid]) h.id = 0;
    return h;
}

void sc_metal_destroy_texture(SCGfxContext *ctx, SCGfxTexture tex) {
    if (!ctx || !ctx->backend_data || tex.id == 0) return;
    _SCMtlState *s = (_SCMtlState*)ctx->backend_data;
    if (tex.id >= SC_GFX_MAX_TEXTURES) return;
    s->tex_textures[tex.id] = nil;
}

/* -------------------------------------------------------------------------
 * Shader management
 * ---------------------------------------------------------------------- */
SCGfxShader sc_metal_make_shader(SCGfxContext *ctx, const SCGfxShaderDesc *desc) {
    SCGfxShader h = {0};
    if (!ctx || !ctx->backend_data) return h;
    _SCMtlState *s = (_SCMtlState*)ctx->backend_data;

    /* NULL desc === valid empty shader slot */
    u32 sid = _sc_gfx_alloc_slot(ctx->shd_slots, SC_GFX_MAX_SHADERS, &ctx->shd_free_head);
    if (sid == 0) return h;
    h.id = sid;
    if (!desc) return h;

    if (desc->vs_source)
        s->shd_vs[sid] = _sc_mtl_make_shader(s->device, desc->vs_source, "vs_main");
    if (desc->fs_source)
        s->shd_fs[sid] = _sc_mtl_make_shader(s->device, desc->fs_source, "fs_main");
    return h;
}

void sc_metal_destroy_shader(SCGfxContext *ctx, SCGfxShader shd) {
    if (!ctx || !ctx->backend_data || shd.id == 0) return;
    _SCMtlState *s = (_SCMtlState*)ctx->backend_data;
    if (shd.id >= SC_GFX_MAX_SHADERS) return;
    s->shd_vs[shd.id] = nil;
    s->shd_fs[shd.id] = nil;
    _sc_gfx_free_slot(ctx->shd_slots, shd.id, &ctx->shd_free_head);
}

/* -------------------------------------------------------------------------
 * Pipeline management
 * ---------------------------------------------------------------------- */
SCGfxPipeline sc_metal_make_pipeline(SCGfxContext *ctx, const SCGfxPipelineDesc *desc) {
    SCGfxPipeline h = {0};
    if (!ctx || !ctx->backend_data) return h;
    _SCMtlState *s = (_SCMtlState*)ctx->backend_data;

    /* NULL desc === valid empty pipeline slot */
    u32 sid = _sc_gfx_alloc_slot(ctx->pip_slots, SC_GFX_MAX_PIPELINES, &ctx->pip_free_head);
    if (sid == 0) return h;
    h.id = sid;
    if (!desc) return h;

    u32 shd_id = desc->shader.id;
    id<MTLFunction> vs = (shd_id > 0 && shd_id < SC_GFX_MAX_SHADERS)
                          ? s->shd_vs[shd_id] : nil;
    id<MTLFunction> fs = (shd_id > 0 && shd_id < SC_GFX_MAX_SHADERS)
                          ? s->shd_fs[shd_id] : nil;
    /* Metal asserts when a pipeline descriptor has a nil vertexFunction
       ("vertexFunction must not be nil"). Fall back to the built-in
       shaders when the user shader is missing or failed to compile. */
    if (!vs) vs = s->builtin_vs;
    if (!fs) fs = s->builtin_fs;

    MTLRenderPipelineDescriptor *rpd = [MTLRenderPipelineDescriptor new];
    rpd.vertexFunction   = vs;
    rpd.fragmentFunction = fs;
    rpd.colorAttachments[0].pixelFormat = s->pix_fmt;
    rpd.colorAttachments[0].blendingEnabled = desc->blend.enabled ? YES : NO;
    rpd.colorAttachments[0].sourceRGBBlendFactor =
        (MTLBlendFactor)(desc->blend.src_factor + 1);
    rpd.colorAttachments[0].destinationRGBBlendFactor =
        (MTLBlendFactor)(desc->blend.dst_factor + 1);
    rpd.depthAttachmentPixelFormat = MTLPixelFormatInvalid;

    NSError *err = nil;
    id<MTLRenderPipelineState> pip =
        [s->device newRenderPipelineStateWithDescriptor:rpd error:&err];
    if (pip) s->user_pipelines[sid] = pip;

    /* Depth/stencil state */
    MTLDepthStencilDescriptor *dsd = [MTLDepthStencilDescriptor new];
    dsd.depthCompareFunction = desc->depth.depth_test
        ? (MTLCompareFunction)desc->depth.depth_compare : MTLCompareFunctionAlways;
    dsd.depthWriteEnabled = desc->depth.depth_write ? YES : NO;
    s->user_depth_states[sid] = [s->device newDepthStencilStateWithDescriptor:dsd];

    return h;
}

void sc_metal_destroy_pipeline(SCGfxContext *ctx, SCGfxPipeline pip) {
    if (!ctx || !ctx->backend_data || pip.id == 0) return;
    _SCMtlState *s = (_SCMtlState*)ctx->backend_data;
    if (pip.id >= SC_GFX_MAX_PIPELINES) return;
    s->user_pipelines[pip.id] = nil;
    s->user_depth_states[pip.id] = nil;
    _sc_gfx_free_slot(ctx->pip_slots, pip.id, &ctx->pip_free_head);
}

SCResult sc_metal_resize(SCGfxContext *ctx, u32 width, u32 height) {
    if (!ctx || !ctx->backend_data || width == 0 || height == 0) {
        return SC_ERR_INVALID_ARG;
    }
    _SCMtlState *s = (_SCMtlState*)ctx->backend_data;
    s->width  = width;
    s->height = height;
    if (s->layer) {
        s->layer.drawableSize = CGSizeMake(width, height);
    }
    return SC_OK;
}

#endif /* SC_BACKEND_METAL_IMPLEMENTATION */
#endif /* SC_BACKEND_METAL_H */
