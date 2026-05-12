/*
 * sc_backend_metal.h  --  Metal backend interface stub
 *
 * Requires macOS 10.13+ / iOS 11+ and Objective-C compilation.
 * Link: -framework Metal -framework MetalKit -framework QuartzCore
 *
 * To activate:
 *   #define SC_GFX_BACKEND_METAL
 *   Compile the .mm bridge file alongside this header.
 */
#ifndef SC_BACKEND_METAL_H
#define SC_BACKEND_METAL_H

#include "../include/sc_types.h"
#include "../include/sc_gfx.h"

SCResult sc_metal_init        (SCGfxContext *ctx, const SCGfxDesc *desc);
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

#ifdef SC_BACKEND_METAL_IMPLEMENTATION
#include <stdio.h>

SCResult sc_metal_init(SCGfxContext *ctx, const SCGfxDesc *desc) {
    SC_UNUSED(ctx); SC_UNUSED(desc);
    fprintf(stderr, "[sc_metal] stub – compile as Obj-C and link Metal\n");
    return SC_ERR_NOT_SUPPORTED;
}
void sc_metal_shutdown(SCGfxContext *ctx)  { SC_UNUSED(ctx); }
void sc_metal_begin_frame(SCGfxContext *ctx, SCColor c) { SC_UNUSED(ctx); SC_UNUSED(c); }
void sc_metal_submit(SCGfxContext *ctx, const SCGfxDrawCmd *cmds, u32 n) {
    SC_UNUSED(ctx); SC_UNUSED(cmds); SC_UNUSED(n);
}
void sc_metal_end_frame(SCGfxContext *ctx) { SC_UNUSED(ctx); }
SCGfxBuffer sc_metal_make_buffer(SCGfxContext *ctx, const SCGfxBufferDesc *desc) {
    SC_UNUSED(ctx); SC_UNUSED(desc); SCGfxBuffer h = {0}; return h;
}
void sc_metal_destroy_buffer(SCGfxContext *ctx, SCGfxBuffer buf) {
    SC_UNUSED(ctx); SC_UNUSED(buf);
}
void sc_metal_update_buffer(SCGfxContext *ctx, SCGfxBuffer buf, const void *data, usize size) {
    SC_UNUSED(ctx); SC_UNUSED(buf); SC_UNUSED(data); SC_UNUSED(size);
}
SCGfxTexture sc_metal_make_texture(SCGfxContext *ctx, const SCGfxTextureDesc *desc) {
    SC_UNUSED(ctx); SC_UNUSED(desc); SCGfxTexture h = {0}; return h;
}
void sc_metal_destroy_texture(SCGfxContext *ctx, SCGfxTexture tex) {
    SC_UNUSED(ctx); SC_UNUSED(tex);
}
SCGfxShader sc_metal_make_shader(SCGfxContext *ctx, const SCGfxShaderDesc *desc) {
    SC_UNUSED(ctx); SC_UNUSED(desc); SCGfxShader h = {0}; return h;
}
void sc_metal_destroy_shader(SCGfxContext *ctx, SCGfxShader shd) {
    SC_UNUSED(ctx); SC_UNUSED(shd);
}
SCGfxPipeline sc_metal_make_pipeline(SCGfxContext *ctx, const SCGfxPipelineDesc *desc) {
    SC_UNUSED(ctx); SC_UNUSED(desc); SCGfxPipeline h = {0}; return h;
}
void sc_metal_destroy_pipeline(SCGfxContext *ctx, SCGfxPipeline pip) {
    SC_UNUSED(ctx); SC_UNUSED(pip);
}
#endif /* SC_BACKEND_METAL_IMPLEMENTATION */
#endif /* SC_BACKEND_METAL_H */
