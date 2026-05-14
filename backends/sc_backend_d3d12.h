/*
 * sc_backend_d3d12.h  --  Direct3D 12 backend interface stub
 *
 * Requires Windows 10 SDK (d3d12.h, dxgi1_4.h).
 * Link: d3d12.lib dxgi.lib d3dcompiler.lib
 *
 * To activate:
 *   #define SC_GFX_BACKEND_D3D12
 */
#ifndef SC_BACKEND_D3D12_H
#define SC_BACKEND_D3D12_H

#include "../include/sc_types.h"
#include "../include/sc_gfx.h"

typedef struct SCD3D12Desc {
    bool debug_layer;     /* D3D12 validation layer               */
    bool dred;            /* Device Removed Extended Data         */
    u32  frame_latency;   /* swap-chain frame latency (1–3)       */
} SCD3D12Desc;

SCResult sc_d3d12_init        (SCGfxContext *ctx, const SCGfxDesc *desc,
                               const SCD3D12Desc *d3d_desc);
void     sc_d3d12_shutdown    (SCGfxContext *ctx);
void     sc_d3d12_begin_frame (SCGfxContext *ctx, SCColor clear);
void     sc_d3d12_submit      (SCGfxContext *ctx,
                               const SCGfxDrawCmd *cmds, u32 count);
void     sc_d3d12_end_frame   (SCGfxContext *ctx);
SCGfxBuffer   sc_d3d12_make_buffer   (SCGfxContext *ctx, const SCGfxBufferDesc *desc);
void          sc_d3d12_destroy_buffer(SCGfxContext *ctx, SCGfxBuffer buf);
void          sc_d3d12_update_buffer (SCGfxContext *ctx, SCGfxBuffer buf,
                                      const void *data, usize size);
SCGfxTexture  sc_d3d12_make_texture  (SCGfxContext *ctx, const SCGfxTextureDesc *desc);
void          sc_d3d12_destroy_texture(SCGfxContext *ctx, SCGfxTexture tex);
SCGfxShader   sc_d3d12_make_shader   (SCGfxContext *ctx, const SCGfxShaderDesc *desc);
void          sc_d3d12_destroy_shader(SCGfxContext *ctx, SCGfxShader shd);
SCGfxPipeline sc_d3d12_make_pipeline (SCGfxContext *ctx, const SCGfxPipelineDesc *desc);
void          sc_d3d12_destroy_pipeline(SCGfxContext *ctx, SCGfxPipeline pip);
SCResult      sc_d3d12_resize         (SCGfxContext *ctx, u32 width, u32 height);

#ifdef SC_BACKEND_D3D12_IMPLEMENTATION
#include <stdio.h>

SCResult sc_d3d12_init(SCGfxContext *ctx, const SCGfxDesc *desc,
                       const SCD3D12Desc *d3d_desc) {
    SC_UNUSED(ctx); SC_UNUSED(desc); SC_UNUSED(d3d_desc);
    fprintf(stderr, "[sc_d3d12] stub – link d3d12.lib on Windows\n");
    return SC_ERR_NOT_SUPPORTED;
}
void sc_d3d12_shutdown(SCGfxContext *ctx)  { SC_UNUSED(ctx); }
void sc_d3d12_begin_frame(SCGfxContext *ctx, SCColor c) { SC_UNUSED(ctx); SC_UNUSED(c); }
void sc_d3d12_submit(SCGfxContext *ctx, const SCGfxDrawCmd *cmds, u32 n) {
    SC_UNUSED(ctx); SC_UNUSED(cmds); SC_UNUSED(n);
}
void sc_d3d12_end_frame(SCGfxContext *ctx) { SC_UNUSED(ctx); }
SCGfxBuffer sc_d3d12_make_buffer(SCGfxContext *ctx, const SCGfxBufferDesc *desc) {
    SC_UNUSED(ctx); SC_UNUSED(desc); SCGfxBuffer h = {0}; return h;
}
void sc_d3d12_destroy_buffer(SCGfxContext *ctx, SCGfxBuffer buf) {
    SC_UNUSED(ctx); SC_UNUSED(buf);
}
void sc_d3d12_update_buffer(SCGfxContext *ctx, SCGfxBuffer buf, const void *data, usize size) {
    SC_UNUSED(ctx); SC_UNUSED(buf); SC_UNUSED(data); SC_UNUSED(size);
}
SCGfxTexture sc_d3d12_make_texture(SCGfxContext *ctx, const SCGfxTextureDesc *desc) {
    SC_UNUSED(ctx); SC_UNUSED(desc); SCGfxTexture h = {0}; return h;
}
void sc_d3d12_destroy_texture(SCGfxContext *ctx, SCGfxTexture tex) {
    SC_UNUSED(ctx); SC_UNUSED(tex);
}
SCGfxShader sc_d3d12_make_shader(SCGfxContext *ctx, const SCGfxShaderDesc *desc) {
    SC_UNUSED(ctx); SC_UNUSED(desc); SCGfxShader h = {0}; return h;
}
void sc_d3d12_destroy_shader(SCGfxContext *ctx, SCGfxShader shd) {
    SC_UNUSED(ctx); SC_UNUSED(shd);
}
SCGfxPipeline sc_d3d12_make_pipeline(SCGfxContext *ctx, const SCGfxPipelineDesc *desc) {
    SC_UNUSED(ctx); SC_UNUSED(desc); SCGfxPipeline h = {0}; return h;
}
void sc_d3d12_destroy_pipeline(SCGfxContext *ctx, SCGfxPipeline pip) {
    SC_UNUSED(ctx); SC_UNUSED(pip);
}
SCResult sc_d3d12_resize(SCGfxContext *ctx, u32 width, u32 height) {
    SC_UNUSED(ctx); SC_UNUSED(width); SC_UNUSED(height);
    return SC_ERR_NOT_SUPPORTED;
}
#endif /* SC_BACKEND_D3D12_IMPLEMENTATION */
#endif /* SC_BACKEND_D3D12_H */
