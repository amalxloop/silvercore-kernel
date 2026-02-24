/*
 * sc_backend_vulkan.h  --  Vulkan backend interface stub
 *
 * Full implementation requires linking against vulkan-1 / libvulkan.so.
 * This stub provides the surface between the platform layer and sc_gfx.
 *
 * To activate:
 *   #define SC_GFX_BACKEND_VULKAN
 *   Link: -lvulkan   (Linux/Android)
 *         -framework Vulkan  (MoltenVK on macOS/iOS)
 *         vulkan-1.lib       (Windows)
 */
#ifndef SC_BACKEND_VULKAN_H
#define SC_BACKEND_VULKAN_H

#include "../include/sc_types.h"
#include "../include/sc_gfx.h"

/* -------------------------------------------------------------------------
 * Vulkan-specific init params (extend SCGfxDesc.native_window)
 * ---------------------------------------------------------------------- */
typedef struct SCVulkanDesc {
    const char **instance_extensions;
    u32          instance_extension_count;
    u32          api_version;          /* e.g. VK_API_VERSION_1_3          */
    bool         enable_validation;    /* VK_LAYER_KHRONOS_validation       */
} SCVulkanDesc;

/*
 * Hook called by sc_gfx_init when backend == SC_BACKEND_VULKAN.
 * Returns SC_OK on success, SC_ERR_BACKEND otherwise.
 */
SCResult sc_vulkan_init(SCGfxContext *ctx, const SCGfxDesc *desc,
                        const SCVulkanDesc *vk_desc);
void     sc_vulkan_shutdown(SCGfxContext *ctx);

void     sc_vulkan_begin_frame(SCGfxContext *ctx, SCColor clear);
void     sc_vulkan_submit     (SCGfxContext *ctx,
                               const SCGfxDrawCmd *cmds, u32 count);
void     sc_vulkan_end_frame  (SCGfxContext *ctx);

#ifdef SC_BACKEND_VULKAN_IMPLEMENTATION
/*
 * =========================================================================
 * Vulkan backend – stub implementation
 *
 * Replace each function body with real Vulkan calls.
 * Minimal structure:
 *   Instance → PhysDevice → Device → Swapchain → RenderPass →
 *   DescriptorPool → PipelineCache → per-frame CommandBuffers
 * =========================================================================
 */
#include <stdio.h>
#include <string.h>

SCResult sc_vulkan_init(SCGfxContext *ctx, const SCGfxDesc *desc,
                        const SCVulkanDesc *vk_desc) {
    SC_UNUSED(ctx); SC_UNUSED(desc); SC_UNUSED(vk_desc);
    /*
     * TODO:
     *   vkCreateInstance(...)
     *   vkEnumeratePhysicalDevices(...)
     *   vkCreateDevice(...)
     *   vkCreateSwapchainKHR(...)
     *   vkCreateRenderPass(...)
     *   (allocate per-frame command buffers)
     */
    fprintf(stderr, "[sc_vulkan] stub – no Vulkan ops performed\n");
    return SC_ERR_NOT_SUPPORTED;
}

void sc_vulkan_shutdown(SCGfxContext *ctx) { SC_UNUSED(ctx); }

void sc_vulkan_begin_frame(SCGfxContext *ctx, SCColor clear) {
    SC_UNUSED(ctx); SC_UNUSED(clear);
    /* vkAcquireNextImageKHR + vkBeginCommandBuffer + clear attachment */
}

void sc_vulkan_submit(SCGfxContext *ctx, const SCGfxDrawCmd *cmds, u32 count) {
    SC_UNUSED(ctx); SC_UNUSED(cmds); SC_UNUSED(count);
    /* vkCmdBindPipeline / vkCmdDraw* per cmd */
}

void sc_vulkan_end_frame(SCGfxContext *ctx) {
    SC_UNUSED(ctx);
    /* vkEndCommandBuffer + vkQueueSubmit + vkQueuePresentKHR */
}
#endif /* SC_BACKEND_VULKAN_IMPLEMENTATION */
#endif /* SC_BACKEND_VULKAN_H */
