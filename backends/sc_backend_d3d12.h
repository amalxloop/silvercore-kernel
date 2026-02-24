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

SCResult sc_d3d12_init    (SCGfxContext *ctx, const SCGfxDesc *desc,
                           const SCD3D12Desc *d3d_desc);
void     sc_d3d12_shutdown(SCGfxContext *ctx);
void     sc_d3d12_begin_frame(SCGfxContext *ctx, SCColor clear);
void     sc_d3d12_submit     (SCGfxContext *ctx,
                               const SCGfxDrawCmd *cmds, u32 count);
void     sc_d3d12_end_frame  (SCGfxContext *ctx);

#ifdef SC_BACKEND_D3D12_IMPLEMENTATION
#include <stdio.h>

SCResult sc_d3d12_init(SCGfxContext *ctx, const SCGfxDesc *desc,
                       const SCD3D12Desc *d3d_desc) {
    SC_UNUSED(ctx); SC_UNUSED(desc); SC_UNUSED(d3d_desc);
    /*
     * TODO:
     *   D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, ...)
     *   CreateCommandQueue / CreateSwapChain / CreateDescriptorHeaps
     *   CreateRootSignature / CompileShader (D3DCompileFromFile)
     *   CreateGraphicsPipelineState
     *   CreateCommandAllocator / CreateCommandList
     *   per-frame fence + event
     */
    fprintf(stderr, "[sc_d3d12] stub – link d3d12.lib on Windows\n");
    return SC_ERR_NOT_SUPPORTED;
}
void sc_d3d12_shutdown(SCGfxContext *ctx)  { SC_UNUSED(ctx); }
void sc_d3d12_begin_frame(SCGfxContext *ctx, SCColor c) { SC_UNUSED(ctx); SC_UNUSED(c); }
void sc_d3d12_submit(SCGfxContext *ctx, const SCGfxDrawCmd *cmds, u32 n) {
    SC_UNUSED(ctx); SC_UNUSED(cmds); SC_UNUSED(n);
}
void sc_d3d12_end_frame(SCGfxContext *ctx) { SC_UNUSED(ctx); }
#endif /* SC_BACKEND_D3D12_IMPLEMENTATION */
#endif /* SC_BACKEND_D3D12_H */
