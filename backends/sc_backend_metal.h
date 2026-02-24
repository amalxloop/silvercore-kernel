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

SCResult sc_metal_init    (SCGfxContext *ctx, const SCGfxDesc *desc);
void     sc_metal_shutdown(SCGfxContext *ctx);
void     sc_metal_begin_frame(SCGfxContext *ctx, SCColor clear);
void     sc_metal_submit     (SCGfxContext *ctx,
                               const SCGfxDrawCmd *cmds, u32 count);
void     sc_metal_end_frame  (SCGfxContext *ctx);

#ifdef SC_BACKEND_METAL_IMPLEMENTATION
#include <stdio.h>

SCResult sc_metal_init(SCGfxContext *ctx, const SCGfxDesc *desc) {
    SC_UNUSED(ctx); SC_UNUSED(desc);
    /*
     * TODO (ObjC/Swift):
     *   id<MTLDevice>        device  = MTLCreateSystemDefaultDevice();
     *   id<MTLCommandQueue>  queue   = [device newCommandQueue];
     *   CAMetalLayer        *layer   = [CAMetalLayer layer];
     *   layer.device = device; layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
     *   MTLRenderPipelineDescriptor *pd = ...
     */
    fprintf(stderr, "[sc_metal] stub – compile as Obj-C and link Metal\n");
    return SC_ERR_NOT_SUPPORTED;
}
void sc_metal_shutdown(SCGfxContext *ctx)  { SC_UNUSED(ctx); }
void sc_metal_begin_frame(SCGfxContext *ctx, SCColor c) { SC_UNUSED(ctx); SC_UNUSED(c); }
void sc_metal_submit(SCGfxContext *ctx, const SCGfxDrawCmd *cmds, u32 n) {
    SC_UNUSED(ctx); SC_UNUSED(cmds); SC_UNUSED(n);
}
void sc_metal_end_frame(SCGfxContext *ctx) { SC_UNUSED(ctx); }
#endif /* SC_BACKEND_METAL_IMPLEMENTATION */
#endif /* SC_BACKEND_METAL_H */
