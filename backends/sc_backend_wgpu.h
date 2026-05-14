/*
 * sc_backend_wgpu.h  --  WebGPU (wgpu-native / Dawn) backend stub
 *
 * Requires the webgpu.h header from the WebGPU native specification.
 * Link: -lwgpu_native  (or Dawn equivalent)
 *
 * To activate:
 *   #define SC_GFX_BACKEND_WGPU
 *   #define SC_BACKEND_WGPU_IMPLEMENTATION
 */
#ifndef SC_BACKEND_WGPU_H
#define SC_BACKEND_WGPU_H

#include "../include/sc_types.h"
#include "../include/sc_gfx.h"

typedef struct SCWGPUDesc {
    bool   headless;
    bool   enable_validation;
    const char **instance_extensions;
    u32          instance_extension_count;
} SCWGPUDesc;

SCResult sc_wgpu_init        (SCGfxContext *ctx, const SCGfxDesc *desc,
                               const SCWGPUDesc *wgpu_desc);
void     sc_wgpu_shutdown    (SCGfxContext *ctx);
void     sc_wgpu_begin_frame (SCGfxContext *ctx, SCColor clear);
void     sc_wgpu_submit      (SCGfxContext *ctx,
                               const SCGfxDrawCmd *cmds, u32 count);
void     sc_wgpu_end_frame   (SCGfxContext *ctx);
SCGfxBuffer   sc_wgpu_make_buffer   (SCGfxContext *ctx, const SCGfxBufferDesc *desc);
void          sc_wgpu_destroy_buffer(SCGfxContext *ctx, SCGfxBuffer buf);
void          sc_wgpu_update_buffer (SCGfxContext *ctx, SCGfxBuffer buf,
                                      const void *data, usize size);
SCGfxTexture  sc_wgpu_make_texture  (SCGfxContext *ctx, const SCGfxTextureDesc *desc);
void          sc_wgpu_destroy_texture(SCGfxContext *ctx, SCGfxTexture tex);
SCGfxShader   sc_wgpu_make_shader   (SCGfxContext *ctx, const SCGfxShaderDesc *desc);
void          sc_wgpu_destroy_shader(SCGfxContext *ctx, SCGfxShader shd);
SCGfxPipeline sc_wgpu_make_pipeline (SCGfxContext *ctx, const SCGfxPipelineDesc *desc);
void          sc_wgpu_destroy_pipeline(SCGfxContext *ctx, SCGfxPipeline pip);
SCResult      sc_wgpu_resize         (SCGfxContext *ctx, u32 width, u32 height);

#ifdef SC_BACKEND_WGPU_IMPLEMENTATION
#include <stdio.h>

SCResult sc_wgpu_init(SCGfxContext *ctx, const SCGfxDesc *desc,
                       const SCWGPUDesc *wgpu_desc) {
    SC_UNUSED(ctx); SC_UNUSED(desc); SC_UNUSED(wgpu_desc);
    fprintf(stderr, "[sc_wgpu] stub – link wgpu-native or Dawn\n");
    return SC_ERR_NOT_SUPPORTED;
}
void sc_wgpu_shutdown(SCGfxContext *ctx)  { SC_UNUSED(ctx); }
void sc_wgpu_begin_frame(SCGfxContext *ctx, SCColor c) { SC_UNUSED(ctx); SC_UNUSED(c); }
void sc_wgpu_submit(SCGfxContext *ctx, const SCGfxDrawCmd *cmds, u32 n) {
    SC_UNUSED(ctx); SC_UNUSED(cmds); SC_UNUSED(n);
}
void sc_wgpu_end_frame(SCGfxContext *ctx) { SC_UNUSED(ctx); }
SCGfxBuffer sc_wgpu_make_buffer(SCGfxContext *ctx, const SCGfxBufferDesc *desc) {
    SC_UNUSED(ctx); SC_UNUSED(desc); SCGfxBuffer h = {0}; return h;
}
void sc_wgpu_destroy_buffer(SCGfxContext *ctx, SCGfxBuffer buf) {
    SC_UNUSED(ctx); SC_UNUSED(buf);
}
void sc_wgpu_update_buffer(SCGfxContext *ctx, SCGfxBuffer buf,
                            const void *data, usize size) {
    SC_UNUSED(ctx); SC_UNUSED(buf); SC_UNUSED(data); SC_UNUSED(size);
}
SCGfxTexture sc_wgpu_make_texture(SCGfxContext *ctx, const SCGfxTextureDesc *desc) {
    SC_UNUSED(ctx); SC_UNUSED(desc); SCGfxTexture h = {0}; return h;
}
void sc_wgpu_destroy_texture(SCGfxContext *ctx, SCGfxTexture tex) {
    SC_UNUSED(ctx); SC_UNUSED(tex);
}
SCGfxShader sc_wgpu_make_shader(SCGfxContext *ctx, const SCGfxShaderDesc *desc) {
    SC_UNUSED(ctx); SC_UNUSED(desc); SCGfxShader h = {0}; return h;
}
void sc_wgpu_destroy_shader(SCGfxContext *ctx, SCGfxShader shd) {
    SC_UNUSED(ctx); SC_UNUSED(shd);
}
SCGfxPipeline sc_wgpu_make_pipeline(SCGfxContext *ctx, const SCGfxPipelineDesc *desc) {
    SC_UNUSED(ctx); SC_UNUSED(desc); SCGfxPipeline h = {0}; return h;
}
void sc_wgpu_destroy_pipeline(SCGfxContext *ctx, SCGfxPipeline pip) {
    SC_UNUSED(ctx); SC_UNUSED(pip);
}
SCResult sc_wgpu_resize(SCGfxContext *ctx, u32 width, u32 height) {
    SC_UNUSED(ctx); SC_UNUSED(width); SC_UNUSED(height);
    return SC_ERR_NOT_SUPPORTED;
}
#endif /* SC_BACKEND_WGPU_IMPLEMENTATION */
#endif /* SC_BACKEND_WGPU_H */
