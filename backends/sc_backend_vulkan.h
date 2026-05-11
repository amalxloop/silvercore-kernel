/*
 * sc_backend_vulkan.h  --  SilverCore Vulkan GPU Backend
 *
 * Full Vulkan backend for hardware-accelerated 2-D rendering.
 * Requires vulkan/vulkan.h and -lvulkan at link time.
 *
 * Supports windowed (swapchain via native_window) and headless (offscreen) modes.
 *
 * To activate:
 *   #define SC_GFX_BACKEND_VULKAN
 *   #define SC_BACKEND_VULKAN_IMPLEMENTATION
 *   Link: -lvulkan
 *
 * Architecture:
 *   Instance -> PhysicalDevice -> Device -> Swapchain (or offscreen) ->
 *   RenderPass -> PipelineLayout -> GraphicsPipeline (2D) ->
 *   per-frame CommandBuffers + VertexBuffer + Sync
 */
#ifndef SC_BACKEND_VULKAN_H
#define SC_BACKEND_VULKAN_H

#include "../include/sc_types.h"
#include "../include/sc_gfx.h"

typedef struct SCVulkanDesc {
    const char **instance_extensions;
    u32          instance_extension_count;
    u32          api_version;
    bool         enable_validation;
    bool         headless;
} SCVulkanDesc;

SCResult sc_vulkan_init        (SCGfxContext *ctx, const SCGfxDesc *desc,
                                const SCVulkanDesc *vk_desc);
void     sc_vulkan_shutdown    (SCGfxContext *ctx);
void     sc_vulkan_begin_frame (SCGfxContext *ctx, SCColor clear);
void     sc_vulkan_submit      (SCGfxContext *ctx,
                                const SCGfxDrawCmd *cmds, u32 count);
void     sc_vulkan_end_frame   (SCGfxContext *ctx);

/* =========================================================================
 * Implementation
 * ========================================================================= */
#ifdef SC_BACKEND_VULKAN_IMPLEMENTATION

#include <vulkan/vulkan.h>
#if defined(VK_USE_PLATFORM_XCB_KHR)
#include <xcb/xcb.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ---- Embedded SPIR-V shader bytecode ---------------------------------- */
#include "sc_vk_vert.h"
#include "sc_vk_frag.h"

/* ---- Constants --------------------------------------------------------- */
#define SC_VK_FRAME_OVERLAP 2
#define SC_VK_MAX_VERTS     (1 << 20)

/* ---- Per-frame data ---------------------------------------------------- */
typedef struct {
    VkCommandBuffer cmd;
    VkFence         fence;
    VkSemaphore     acquire_sem;
    VkSemaphore     submit_sem;
    VkBuffer        vert_buf;
    VkDeviceMemory  vert_mem;
    usize           vert_capacity;
} _SCVkFrame;

/* ---- Vulkan backend state (stored in SCGfxContext->vk_data) ------------ */
typedef struct {
    VkInstance        instance;
    VkPhysicalDevice  phy_dev;
    VkDevice          device;
    u32               queue_family;
    VkQueue           gfx_queue;

    /* Surface / swapchain */
    VkSurfaceKHR      surface;
    VkSwapchainKHR    swapchain;
    VkFormat          swap_fmt;
    u32               swap_len;
    VkImage          *swap_images;
    VkImageView      *swap_views;
    VkFramebuffer    *swap_fbos;

    /* Offscreen (headless) */
    VkImage           os_image;
    VkDeviceMemory    os_mem;
    VkImageView       os_view;
    VkFramebuffer     os_fbo;

    /* Render pass / pipeline */
    VkRenderPass          render_pass;
    VkPipelineLayout      pipeline_layout;
    VkPipeline            pipeline;
    VkDescriptorSetLayout ds_layout;

    /* Extent */
    u32               width, height;
    bool              headless;

    VkCommandPool     cmd_pool;
    _SCVkFrame        frames[SC_VK_FRAME_OVERLAP];
    u32               frame_idx;
    u32               cur_image;
    bool              in_frame;
} _SCVkState;

/* -------------------------------------------------------------------------
 * Memory-type helper
 * ---------------------------------------------------------------------- */
static u32 _sc_vk_find_mem_type(VkPhysicalDevice phy, u32 type_filter,
                                 VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(phy, &mp);
    for (u32 i = 0; i < mp.memoryTypeCount; i++) {
        if ((type_filter & (1u << i)) &&
            (mp.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    return 0;
}

/* -------------------------------------------------------------------------
 * Shader-module helper
 * ---------------------------------------------------------------------- */
static VkResult _sc_vk_make_shader(VkDevice dev, const unsigned char *code,
                                    usize size, VkShaderModule *out) {
    VkShaderModuleCreateInfo ci = {0};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = size;
    ci.pCode    = (const u32*)code;
    return vkCreateShaderModule(dev, &ci, NULL, out);
}

/* -------------------------------------------------------------------------
 * Buffer helper
 * ---------------------------------------------------------------------- */
static VkResult _sc_vk_create_buf(VkDevice dev, VkPhysicalDevice phy,
    VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags mem_flags,
    VkBuffer *buf, VkDeviceMemory *mem) {
    VkBufferCreateInfo bci = {0};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size  = size;
    bci.usage = usage;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkResult r = vkCreateBuffer(dev, &bci, NULL, buf);
    if (r != VK_SUCCESS) return r;

    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(dev, *buf, &mr);

    VkMemoryAllocateInfo ai = {0};
    ai.sType          = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = _sc_vk_find_mem_type(phy, mr.memoryTypeBits, mem_flags);

    r = vkAllocateMemory(dev, &ai, NULL, mem);
    if (r != VK_SUCCESS) { vkDestroyBuffer(dev, *buf, NULL); return r; }
    return vkBindBufferMemory(dev, *buf, *mem, 0);
}

/* -------------------------------------------------------------------------
 * Image view helper
 * ---------------------------------------------------------------------- */
static VkResult _sc_vk_make_view(VkDevice dev, VkImage img, VkFormat fmt,
                                  VkImageView *out) {
    VkImageViewCreateInfo ci = {0};
    ci.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ci.image    = img;
    ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ci.format   = fmt;
    ci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    ci.subresourceRange.levelCount     = 1;
    ci.subresourceRange.layerCount     = 1;
    return vkCreateImageView(dev, &ci, NULL, out);
}

/* -------------------------------------------------------------------------
 * Graphics pipeline (2D untextured + textured)
 * ---------------------------------------------------------------------- */
static VkResult _sc_vk_create_pipeline(VkDevice dev,
    VkShaderModule vs, VkShaderModule fs,
    VkPipelineLayout layout, VkRenderPass rp,
    u32 width, u32 height, VkPipeline *out) {

    VkPipelineShaderStageCreateInfo stages[2] = {{0},{0}};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    stages[1].pName  = "main";

    VkVertexInputBindingDescription vb = {0};
    vb.binding   = 0;
    vb.stride    = sizeof(SCGfxVertex2D);
    vb.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription va[3] = {{0},{0},{0}};
    va[0].location = 0; va[0].binding = 0;
    va[0].format   = VK_FORMAT_R32G32_SFLOAT;
    va[0].offset   = (u32)offsetof(SCGfxVertex2D, x);
    va[1].location = 1; va[1].binding = 0;
    va[1].format   = VK_FORMAT_R32G32_SFLOAT;
    va[1].offset   = (u32)offsetof(SCGfxVertex2D, u);
    va[2].location = 2; va[2].binding = 0;
    va[2].format   = VK_FORMAT_R8G8B8A8_UNORM;
    va[2].offset   = (u32)offsetof(SCGfxVertex2D, r);

    VkPipelineVertexInputStateCreateInfo vi = {0};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount   = 1;
    vi.pVertexBindingDescriptions      = &vb;
    vi.vertexAttributeDescriptionCount = 3;
    vi.pVertexAttributeDescriptions    = va;

    VkPipelineInputAssemblyStateCreateInfo ia = {0};
    ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport vp = {0.0f, 0.0f, (f32)width, (f32)height, 0.0f, 1.0f};
    VkRect2D   sc = {{0,0},{width,height}};
    VkPipelineViewportStateCreateInfo vsi = {0};
    vsi.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vsi.viewportCount = 1; vsi.pViewports = &vp;
    vsi.scissorCount  = 1; vsi.pScissors  = &sc;

    VkPipelineRasterizationStateCreateInfo rs = {0};
    rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode    = VK_CULL_MODE_NONE;
    rs.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms = {0};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState bl = {0};
    bl.blendEnable    = VK_TRUE;
    bl.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    bl.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    bl.colorBlendOp        = VK_BLEND_OP_ADD;
    bl.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    bl.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    bl.alphaBlendOp        = VK_BLEND_OP_ADD;
    bl.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo cbs = {0};
    cbs.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cbs.attachmentCount = 1; cbs.pAttachments = &bl;

    VkGraphicsPipelineCreateInfo ci = {0};
    ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    ci.stageCount = 2; ci.pStages = stages;
    ci.pVertexInputState   = &vi;
    ci.pInputAssemblyState = &ia;
    ci.pViewportState      = &vsi;
    ci.pRasterizationState = &rs;
    ci.pMultisampleState   = &ms;
    ci.pColorBlendState    = &cbs;
    ci.layout     = layout;
    ci.renderPass = rp;

    return vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &ci, NULL, out);
}

/* -------------------------------------------------------------------------
 * Render pass
 * ---------------------------------------------------------------------- */
static VkResult _sc_vk_create_rp(VkDevice dev, VkFormat fmt,
                                  VkRenderPass *out, bool present) {
    VkAttachmentDescription att = {0};
    att.format  = fmt;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout   = present ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                                : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

    VkSubpassDescription sp = {0};
    sp.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sp.colorAttachmentCount = 1;
    sp.pColorAttachments    = &ref;

    VkRenderPassCreateInfo ci = {0};
    ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = 1; ci.pAttachments = &att;
    ci.subpassCount    = 1; ci.pSubpasses    = &sp;
    return vkCreateRenderPass(dev, &ci, NULL, out);
}

/* -------------------------------------------------------------------------
 * Create offscreen image + view + framebuffer (headless mode)
 * ---------------------------------------------------------------------- */
static VkResult _sc_vk_create_offscreen(VkDevice dev, VkPhysicalDevice phy,
    VkQueue queue, u32 w, u32 h, VkFormat fmt, VkRenderPass rp,
    VkImage *img, VkDeviceMemory *mem, VkImageView *view, VkFramebuffer *fbo) {

    VkImageCreateInfo ici = {0};
    ici.sType     = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format    = fmt;
    ici.extent    = (VkExtent3D){w, h, 1};
    ici.mipLevels = 1; ici.arrayLayers = 1;
    ici.samples   = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling    = VK_IMAGE_TILING_OPTIMAL;
    ici.usage     = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkResult r = vkCreateImage(dev, &ici, NULL, img);
    if (r != VK_SUCCESS) return r;

    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(dev, *img, &mr);
    VkMemoryAllocateInfo ai = {0};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = _sc_vk_find_mem_type(phy, mr.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    r = vkAllocateMemory(dev, &ai, NULL, mem);
    if (r != VK_SUCCESS) { vkDestroyImage(dev, *img, NULL); return r; }
    vkBindImageMemory(dev, *img, *mem, 0);

    r = _sc_vk_make_view(dev, *img, fmt, view);
    if (r != VK_SUCCESS) return r;

    /* Transition layout to color-attachment-optimal */
    VkCommandPoolCreateInfo cpci = {0};
    cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

    VkCommandPool tmp_pool;
    vkCreateCommandPool(dev, &cpci, NULL, &tmp_pool);

    VkCommandBufferAllocateInfo cai = {0};
    cai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool        = tmp_pool;
    cai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cb;
    vkAllocateCommandBuffers(dev, &cai, &cb);

    VkCommandBufferBeginInfo bi = {0};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bi);

    VkImageMemoryBarrier barrier = {0};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = *img;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
        0, NULL, 0, NULL, 1, &barrier);
    vkEndCommandBuffer(cb);

    VkSubmitInfo si = {0};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1; si.pCommandBuffers = &cb;
    vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
    vkDeviceWaitIdle(dev);
    vkDestroyCommandPool(dev, tmp_pool, NULL);

    VkFramebufferCreateInfo fci = {0};
    fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fci.renderPass = rp;
    fci.attachmentCount = 1; fci.pAttachments = view;
    fci.width = w; fci.height = h; fci.layers = 1;
    return vkCreateFramebuffer(dev, &fci, NULL, fbo);
}

/* -------------------------------------------------------------------------
 * Create swapchain + image views + framebuffers
 * ---------------------------------------------------------------------- */
static VkResult _sc_vk_create_swapchain(VkDevice dev, VkPhysicalDevice phy,
    VkSurfaceKHR surface, u32 width, u32 height, VkFormat fmt, VkRenderPass rp,
    VkSwapchainKHR *out_swap, u32 *out_len,
    VkImage **out_images, VkImageView **out_views, VkFramebuffer **out_fbos) {

    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phy, surface, &caps);

    u32 fmt_count;
    vkGetPhysicalDeviceSurfaceFormatsKHR(phy, surface, &fmt_count, NULL);
    VkSurfaceFormatKHR *fmts = (VkSurfaceFormatKHR*)malloc(fmt_count * sizeof(VkSurfaceFormatKHR));
    vkGetPhysicalDeviceSurfaceFormatsKHR(phy, surface, &fmt_count, fmts);

    VkSurfaceFormatKHR sf = fmts[0];
    for (u32 i = 0; i < fmt_count; i++) {
        if (fmts[i].format == fmt) { sf = fmts[i]; break; }
    }
    free(fmts);

    VkExtent2D extent = caps.currentExtent;
    if (extent.width == 0xFFFFFFFFu) {
        extent.width  = width;
        extent.height = height;
    }

    VkSwapchainCreateInfoKHR sci = {0};
    sci.sType           = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface         = surface;
    sci.minImageCount   = SC_VK_FRAME_OVERLAP + 1;
    sci.imageFormat     = sf.format;
    sci.imageColorSpace = sf.colorSpace;
    sci.imageExtent     = extent;
    sci.imageArrayLayers = 1;
    sci.imageUsage      = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform    = caps.currentTransform;
    sci.compositeAlpha  = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode     = VK_PRESENT_MODE_FIFO_KHR;
    sci.clipped         = VK_TRUE;

    VkResult r = vkCreateSwapchainKHR(dev, &sci, NULL, out_swap);
    if (r != VK_SUCCESS) return r;

    vkGetSwapchainImagesKHR(dev, *out_swap, out_len, NULL);
    *out_images = (VkImage*)malloc(*out_len * sizeof(VkImage));
    *out_views  = (VkImageView*)malloc(*out_len * sizeof(VkImageView));
    *out_fbos   = (VkFramebuffer*)malloc(*out_len * sizeof(VkFramebuffer));
    vkGetSwapchainImagesKHR(dev, *out_swap, out_len, *out_images);

    for (u32 i = 0; i < *out_len; i++) {
        _sc_vk_make_view(dev, (*out_images)[i], sf.format, &(*out_views)[i]);
        VkFramebufferCreateInfo fci = {0};
        fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fci.renderPass      = rp;
        fci.attachmentCount = 1;
        fci.pAttachments    = &(*out_views)[i];
        fci.width  = extent.width;
        fci.height = extent.height;
        fci.layers = 1;
        vkCreateFramebuffer(dev, &fci, NULL, &(*out_fbos)[i]);
    }

    return VK_SUCCESS;
}

/* =========================================================================
 * Public API
 * ========================================================================= */

SCResult sc_vulkan_init(SCGfxContext *ctx, const SCGfxDesc *desc,
                        const SCVulkanDesc *vk_desc) {
    _SCVkState *s = (_SCVkState*)calloc(1, sizeof(_SCVkState));
    if (!s) return SC_ERR_OOM;
    ctx->backend_data = s;

    s->width    = desc->width  ? desc->width  : 1280;
    s->height   = desc->height ? desc->height : 720;
    s->headless = vk_desc ? vk_desc->headless : true;

    /* ---- Instance ---------------------------------------------------- */
    VkApplicationInfo app = {0};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = desc->app_name ? desc->app_name : "SilverCore";
    app.apiVersion = (vk_desc && vk_desc->api_version)
                     ? vk_desc->api_version : VK_API_VERSION_1_0;

    const char *exts[64];
    u32 ext_count = 0;
    if (!s->headless) {
        exts[ext_count++] = "VK_KHR_surface";
#if defined(VK_USE_PLATFORM_XCB_KHR)
        exts[ext_count++] = "VK_KHR_xcb_surface";
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
        exts[ext_count++] = "VK_KHR_xlib_surface";
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
        exts[ext_count++] = "VK_KHR_wayland_surface";
#endif
    }
    if (vk_desc) {
        for (u32 i = 0; i < vk_desc->instance_extension_count && ext_count < 64; i++) {
            bool dup = false;
            for (u32 j = 0; j < ext_count; j++) {
                if (strcmp(exts[j], vk_desc->instance_extensions[i]) == 0)
                    { dup = true; break; }
            }
            if (!dup) exts[ext_count++] = vk_desc->instance_extensions[i];
        }
    }

    const char *layers[8];
    u32 layer_count = 0;
    if (vk_desc && vk_desc->enable_validation) {
        layers[layer_count++] = "VK_LAYER_KHRONOS_validation";
    }

    VkInstanceCreateInfo ici = {0};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo        = &app;
    ici.enabledExtensionCount   = ext_count;
    ici.ppEnabledExtensionNames = exts;
    ici.enabledLayerCount       = layer_count;
    ici.ppEnabledLayerNames     = layers;

    VkResult r = vkCreateInstance(&ici, NULL, &s->instance);
    if (r != VK_SUCCESS) { free(s); ctx->backend_data = NULL; return SC_ERR_GFX; }

    /* ---- Physical device -------------------------------------------- */
    u32 phy_count;
    vkEnumeratePhysicalDevices(s->instance, &phy_count, NULL);
    if (phy_count == 0) { sc_vulkan_shutdown(ctx); return SC_ERR_GFX; }
    VkPhysicalDevice *phys = (VkPhysicalDevice*)malloc(phy_count * sizeof(VkPhysicalDevice));
    vkEnumeratePhysicalDevices(s->instance, &phy_count, phys);
    s->phy_dev = phys[0];
    for (u32 i = 0; i < phy_count; i++) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(phys[i], &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            s->phy_dev = phys[i]; break;
        }
    }
    free(phys);

    /* ---- Queue family ------------------------------------------------ */
    u32 qf_count;
    vkGetPhysicalDeviceQueueFamilyProperties(s->phy_dev, &qf_count, NULL);
    VkQueueFamilyProperties *qf = (VkQueueFamilyProperties*)malloc(
        qf_count * sizeof(VkQueueFamilyProperties));
    vkGetPhysicalDeviceQueueFamilyProperties(s->phy_dev, &qf_count, qf);

    u32 gfx_idx = UINT32_MAX;
    for (u32 i = 0; i < qf_count; i++) {
        if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            if (gfx_idx == UINT32_MAX) gfx_idx = i;
        }
    }
    free(qf);
    if (gfx_idx == UINT32_MAX) { sc_vulkan_shutdown(ctx); return SC_ERR_GFX; }
    s->queue_family = gfx_idx;

    /* ---- Logical device ---------------------------------------------- */
    f32 prio = 1.0f;
    VkDeviceQueueCreateInfo dq = {0};
    dq.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    dq.queueFamilyIndex = s->queue_family;
    dq.queueCount       = 1;
    dq.pQueuePriorities = &prio;

    const char *dev_exts[8];
    u32 dev_ext_count = 0;
    if (!s->headless) dev_exts[dev_ext_count++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;

    VkDeviceCreateInfo dc = {0};
    dc.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dc.queueCreateInfoCount    = 1;
    dc.pQueueCreateInfos      = &dq;
    dc.enabledExtensionCount  = dev_ext_count;
    dc.ppEnabledExtensionNames = dev_exts;

    r = vkCreateDevice(s->phy_dev, &dc, NULL, &s->device);
    if (r != VK_SUCCESS) { sc_vulkan_shutdown(ctx); return SC_ERR_GFX; }
    vkGetDeviceQueue(s->device, s->queue_family, 0, &s->gfx_queue);

    /* ---- Surface (windowed) ------------------------------------------ */
    if (!s->headless) {
#if defined(VK_USE_PLATFORM_XCB_KHR)
        PFN_vkCreateXcbSurfaceKHR vkCreateXcbSurfaceKHR =
            (PFN_vkCreateXcbSurfaceKHR)vkGetInstanceProcAddr(
                s->instance, "vkCreateXcbSurfaceKHR");
        if (vkCreateXcbSurfaceKHR) {
            VkXcbSurfaceCreateInfoKHR xci = {0};
            xci.sType      = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
            xci.connection = (xcb_connection_t*)desc->native_display;
            xci.window     = (xcb_window_t)(uintptr_t)desc->native_window;
            r = vkCreateXcbSurfaceKHR(s->instance, &xci, NULL, &s->surface);
            if (r != VK_SUCCESS) { sc_vulkan_shutdown(ctx); return SC_ERR_GFX; }
        } else
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
        PFN_vkCreateXlibSurfaceKHR vkCreateXlibSurfaceKHR =
            (PFN_vkCreateXlibSurfaceKHR)vkGetInstanceProcAddr(
                s->instance, "vkCreateXlibSurfaceKHR");
        if (vkCreateXlibSurfaceKHR) {
            VkXlibSurfaceCreateInfoKHR xci = {0};
            xci.sType  = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
            xci.dpy    = (Display*)desc->native_display;
            xci.window = (Window)(uintptr_t)desc->native_window;
            r = vkCreateXlibSurfaceKHR(s->instance, &xci, NULL, &s->surface);
            if (r != VK_SUCCESS) { sc_vulkan_shutdown(ctx); return SC_ERR_GFX; }
        } else
#endif
        {
            fprintf(stderr, "[sc_vulkan] no platform surface support compiled in\n");
            sc_vulkan_shutdown(ctx); return SC_ERR_NOT_SUPPORTED;
        }
    }

    /* ---- Format ------------------------------------------------------- */
    VkFormat fmt = VK_FORMAT_B8G8R8A8_UNORM;

    /* ---- Render pass -------------------------------------------------- */
    r = _sc_vk_create_rp(s->device, fmt, &s->render_pass, !s->headless);
    if (r != VK_SUCCESS) { sc_vulkan_shutdown(ctx); return SC_ERR_GFX; }

    /* ---- Descriptor set layout (for texture sampler) ------------------ */
    VkDescriptorSetLayoutBinding dslb = {0};
    dslb.binding         = 0;
    dslb.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    dslb.descriptorCount = 1;
    dslb.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dslc = {0};
    dslc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslc.bindingCount = 1;
    dslc.pBindings    = &dslb;
    r = vkCreateDescriptorSetLayout(s->device, &dslc, NULL, &s->ds_layout);
    if (r != VK_SUCCESS) { sc_vulkan_shutdown(ctx); return SC_ERR_GFX; }

    /* ---- Pipeline layout (push-constant + descriptor set) ------------- */
    VkPushConstantRange pc = {VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4};
    VkPipelineLayoutCreateInfo plc = {0};
    plc.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plc.setLayoutCount         = 1;
    plc.pSetLayouts            = &s->ds_layout;
    plc.pushConstantRangeCount = 1;
    plc.pPushConstantRanges    = &pc;
    r = vkCreatePipelineLayout(s->device, &plc, NULL, &s->pipeline_layout);
    if (r != VK_SUCCESS) { sc_vulkan_shutdown(ctx); return SC_ERR_GFX; }

    /* ---- Shader modules ----------------------------------------------- */
    VkShaderModule vs, fs;
    _sc_vk_make_shader(s->device, _sc_vk_vert_spv, _sc_vk_vert_spv_len, &vs);
    _sc_vk_make_shader(s->device, _sc_vk_frag_spv, _sc_vk_frag_spv_len, &fs);

    /* ---- Graphics pipeline -------------------------------------------- */
    r = _sc_vk_create_pipeline(s->device, vs, fs, s->pipeline_layout,
                                s->render_pass, s->width, s->height, &s->pipeline);
    vkDestroyShaderModule(s->device, fs, NULL);
    vkDestroyShaderModule(s->device, vs, NULL);
    if (r != VK_SUCCESS) { sc_vulkan_shutdown(ctx); return SC_ERR_GFX; }

    /* ---- Swapchain / offscreen ---------------------------------------- */
    if (s->headless) {
        r = _sc_vk_create_offscreen(s->device, s->phy_dev, s->gfx_queue,
            s->width, s->height, fmt, s->render_pass,
            &s->os_image, &s->os_mem, &s->os_view, &s->os_fbo);
        if (r != VK_SUCCESS) { sc_vulkan_shutdown(ctx); return SC_ERR_GFX; }
    } else {
        r = _sc_vk_create_swapchain(s->device, s->phy_dev, s->surface,
            s->width, s->height, fmt, s->render_pass,
            &s->swapchain, &s->swap_len,
            &s->swap_images, &s->swap_views, &s->swap_fbos);
        if (r != VK_SUCCESS) { sc_vulkan_shutdown(ctx); return SC_ERR_GFX; }
    }

    /* ---- Command pool ------------------------------------------------- */
    VkCommandPoolCreateInfo cpci = {0};
    cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    r = vkCreateCommandPool(s->device, &cpci, NULL, &s->cmd_pool);
    if (r != VK_SUCCESS) { sc_vulkan_shutdown(ctx); return SC_ERR_GFX; }

    /* ---- Per-frame resources ------------------------------------------ */
    for (u32 i = 0; i < SC_VK_FRAME_OVERLAP; i++) {
        _SCVkFrame *f = &s->frames[i];
        VkCommandBufferAllocateInfo cai = {0};
        cai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cai.commandPool        = s->cmd_pool;
        cai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cai.commandBufferCount = 1;
        r = vkAllocateCommandBuffers(s->device, &cai, &f->cmd);
        if (r != VK_SUCCESS) { sc_vulkan_shutdown(ctx); return SC_ERR_GFX; }

        VkFenceCreateInfo fci = {0};
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        vkCreateFence(s->device, &fci, NULL, &f->fence);

        VkSemaphoreCreateInfo sci = {0};
        sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        vkCreateSemaphore(s->device, &sci, NULL, &f->acquire_sem);
        vkCreateSemaphore(s->device, &sci, NULL, &f->submit_sem);

        _sc_vk_create_buf(s->device, s->phy_dev, 65536 * sizeof(SCGfxVertex2D),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &f->vert_buf, &f->vert_mem);
        f->vert_capacity = 65536 * sizeof(SCGfxVertex2D);
    }

    s->frame_idx = 0;
    s->in_frame  = false;

    fprintf(stderr, "[sc_vulkan] initialized (%s %ux%u)\n",
            s->headless ? "headless" : "windowed", s->width, s->height);
    return SC_OK;
}

void sc_vulkan_shutdown(SCGfxContext *ctx) {
    if (!ctx || !ctx->backend_data) return;
    _SCVkState *s = (_SCVkState*)ctx->backend_data;

    if (s->device) vkDeviceWaitIdle(s->device);

    for (u32 i = 0; i < SC_VK_FRAME_OVERLAP; i++) {
        _SCVkFrame *f = &s->frames[i];
        if (f->vert_buf) vkDestroyBuffer(s->device, f->vert_buf, NULL);
        if (f->vert_mem) vkFreeMemory(s->device, f->vert_mem, NULL);
        if (f->fence)         vkDestroyFence(s->device, f->fence, NULL);
        if (f->acquire_sem)   vkDestroySemaphore(s->device, f->acquire_sem, NULL);
        if (f->submit_sem)    vkDestroySemaphore(s->device, f->submit_sem, NULL);
    }

    if (s->cmd_pool) vkDestroyCommandPool(s->device, s->cmd_pool, NULL);

    if (s->ds_layout) vkDestroyDescriptorSetLayout(s->device, s->ds_layout, NULL);
    if (s->pipeline)  vkDestroyPipeline(s->device, s->pipeline, NULL);
    if (s->pipeline_layout) vkDestroyPipelineLayout(s->device, s->pipeline_layout, NULL);
    if (s->render_pass) vkDestroyRenderPass(s->device, s->render_pass, NULL);

    if (s->headless) {
        if (s->os_fbo)   vkDestroyFramebuffer(s->device, s->os_fbo, NULL);
        if (s->os_view)  vkDestroyImageView(s->device, s->os_view, NULL);
        if (s->os_image) vkDestroyImage(s->device, s->os_image, NULL);
        if (s->os_mem)   vkFreeMemory(s->device, s->os_mem, NULL);
    } else {
        for (u32 i = 0; i < s->swap_len; i++) {
            if (s->swap_fbos[i])   vkDestroyFramebuffer(s->device, s->swap_fbos[i], NULL);
            if (s->swap_views[i])  vkDestroyImageView(s->device, s->swap_views[i], NULL);
        }
        free(s->swap_images);
        free(s->swap_views);
        free(s->swap_fbos);
        if (s->swapchain) vkDestroySwapchainKHR(s->device, s->swapchain, NULL);
        if (s->surface)   vkDestroySurfaceKHR(s->instance, s->surface, NULL);
    }

    if (s->device)   vkDestroyDevice(s->device, NULL);
    if (s->instance) vkDestroyInstance(s->instance, NULL);

    free(s);
    ctx->backend_data = NULL;
}

void sc_vulkan_begin_frame(SCGfxContext *ctx, SCColor clear) {
    if (!ctx || !ctx->backend_data) return;
    _SCVkState *s = (_SCVkState*)ctx->backend_data;

    _SCVkFrame *f = &s->frames[s->frame_idx];
    vkWaitForFences(s->device, 1, &f->fence, VK_TRUE, UINT64_MAX);
    vkResetFences(s->device, 1, &f->fence);

    if (!s->headless) {
        vkAcquireNextImageKHR(s->device, s->swapchain, UINT64_MAX,
            f->acquire_sem, VK_NULL_HANDLE, &s->cur_image);
    }

    VkCommandBufferBeginInfo bi = {0};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(f->cmd, &bi);

    VkFramebuffer fb = s->headless ? s->os_fbo : s->swap_fbos[s->cur_image];

    VkClearValue cv = {0};
    cv.color.float32[0] = clear.r;
    cv.color.float32[1] = clear.g;
    cv.color.float32[2] = clear.b;
    cv.color.float32[3] = clear.a;

    VkRenderPassBeginInfo rpbi = {0};
    rpbi.sType       = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpbi.renderPass  = s->render_pass;
    rpbi.framebuffer = fb;
    rpbi.renderArea.offset.x = 0;
    rpbi.renderArea.offset.y = 0;
    rpbi.renderArea.extent.width  = s->width;
    rpbi.renderArea.extent.height = s->height;
    rpbi.clearValueCount = 1;
    rpbi.pClearValues    = &cv;
    vkCmdBeginRenderPass(f->cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(f->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s->pipeline);
    s->in_frame = true;
}

void sc_vulkan_submit(SCGfxContext *ctx, const SCGfxDrawCmd *cmds, u32 count) {
    if (!ctx) return;
    ctx->frame_stats.draw_calls += count;
    for (u32 i = 0; i < count; i++) {
        ctx->frame_stats.vertex_count += cmds[i].vertex_count;
        ctx->frame_stats.index_count  += cmds[i].index_count;
    }
}

void sc_vulkan_end_frame(SCGfxContext *ctx) {
    if (!ctx || !ctx->backend_data) return;
    _SCVkState *s = (_SCVkState*)ctx->backend_data;
    if (!s->in_frame) return;

    VkCommandBuffer cb = s->frames[s->frame_idx].cmd;

    if (ctx->batch_vcount > 0) {
        _SCVkFrame *f = &s->frames[s->frame_idx];
        usize needed = (usize)ctx->batch_vcount * sizeof(SCGfxVertex2D);
        if (needed > f->vert_capacity) {
            vkDeviceWaitIdle(s->device);
            if (f->vert_buf) vkDestroyBuffer(s->device, f->vert_buf, NULL);
            if (f->vert_mem) vkFreeMemory(s->device, f->vert_mem, NULL);
            _sc_vk_create_buf(s->device, s->phy_dev, needed,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                &f->vert_buf, &f->vert_mem);
            f->vert_capacity = needed;
        }

        void *data;
        vkMapMemory(s->device, f->vert_mem, 0, needed, 0, &data);
        memcpy(data, ctx->batch_verts, needed);
        vkUnmapMemory(s->device, f->vert_mem);

        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cb, 0, 1, &f->vert_buf, &offset);

        for (u32 i = 0; i < ctx->batch_ccount; i++) {
            u32 ht = ctx->batch_cmds[i].texture.id != 0 ? 1u : 0u;
            vkCmdPushConstants(cb, s->pipeline_layout,
                VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4, &ht);
            vkCmdDraw(cb, ctx->batch_cmds[i].vertex_count, 1,
                ctx->batch_cmds[i].vertex_offset, 0);
        }
    }

    vkCmdEndRenderPass(cb);
    VkResult r = vkEndCommandBuffer(cb);
    (void)r;

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si = {0};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount   = 1;
    si.pCommandBuffers      = &cb;
    si.pWaitDstStageMask    = &wait_stage;
    if (!s->headless) {
        si.waitSemaphoreCount   = 1;
        si.pWaitSemaphores      = &s->frames[s->frame_idx].acquire_sem;
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores    = &s->frames[s->frame_idx].submit_sem;
    }
    vkQueueSubmit(s->gfx_queue, 1, &si, s->frames[s->frame_idx].fence);

    if (!s->headless) {
        VkPresentInfoKHR pi = {0};
        pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores    = &s->frames[s->frame_idx].submit_sem;
        pi.swapchainCount     = 1;
        pi.pSwapchains        = &s->swapchain;
        pi.pImageIndices      = &s->cur_image;
        vkQueuePresentKHR(s->gfx_queue, &pi);
    }

    s->frame_idx = (s->frame_idx + 1) % SC_VK_FRAME_OVERLAP;
    s->in_frame  = false;
}

#endif /* SC_BACKEND_VULKAN_IMPLEMENTATION */
#endif /* SC_BACKEND_VULKAN_H */
