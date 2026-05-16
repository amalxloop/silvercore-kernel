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

    /* Extent / MSAA */
    u32               width, height;
    VkSampleCountFlagBits samples;
    bool              headless;

    /* Per-swapchain MSAA color attachments (when samples > 1) */
    VkImage         *msaa_images;
    VkDeviceMemory  *msaa_mems;
    VkImageView     *msaa_views;

    /* Headless resolve target (when samples > 1, headless mode) */
    VkImage           os_resolve_image;
    VkDeviceMemory    os_resolve_mem;
    VkImageView       os_resolve_view;

    VkCommandPool     cmd_pool;
    _SCVkFrame        frames[SC_VK_FRAME_OVERLAP];
    u32               frame_idx;
    u32               cur_image;
    bool              in_frame;

    /* Texture support */
    VkImage           tex_images[SC_GFX_MAX_TEXTURES];
    VkDeviceMemory    tex_mems[SC_GFX_MAX_TEXTURES];
    VkImageView       tex_views[SC_GFX_MAX_TEXTURES];
    VkSampler         sampler;
    VkDescriptorPool  desc_pool;
    VkDescriptorSet   desc_set;

    /* User buffer storage */
    VkBuffer         buf_buffers[SC_GFX_MAX_BUFFERS];
    VkDeviceMemory   buf_mems[SC_GFX_MAX_BUFFERS];
    VkDeviceSize     buf_sizes[SC_GFX_MAX_BUFFERS];

    /* User shader modules */
    VkShaderModule   shd_modules[SC_GFX_MAX_SHADERS];
    VkShaderModule   shd_fs_modules[SC_GFX_MAX_SHADERS];

    /* User pipelines */
    VkPipeline       user_pipelines[SC_GFX_MAX_PIPELINES];
    VkPipelineLayout user_pip_layouts[SC_GFX_MAX_PIPELINES];

    /* Pipeline cache: deduplicate identical descs */
#define _SC_VK_PIP_CACHE_SIZE 64
    struct { u64 hash; VkPipeline pip; VkPipelineLayout layout; } pip_cache[_SC_VK_PIP_CACHE_SIZE];
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
    VkSampleCountFlagBits samples,
    const SCGfxDepthState *depth,
    VkPipeline *out) {

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

    VkDynamicState dynamic_states[2] = {
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamic_state = {0};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = 2;
    dynamic_state.pDynamicStates    = dynamic_states;

    VkPipelineViewportStateCreateInfo vsi = {0};
    vsi.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vsi.viewportCount = 1; vsi.pViewports = NULL;
    vsi.scissorCount  = 1; vsi.pScissors  = NULL;

    VkPipelineRasterizationStateCreateInfo rs = {0};
    rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode    = VK_CULL_MODE_NONE;
    rs.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms = {0};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = samples;

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

    VkPipelineDepthStencilStateCreateInfo ds = {0};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    if (depth) {
        ds.depthTestEnabled  = depth->depth_test ? VK_TRUE : VK_FALSE;
        ds.depthWriteEnabled = depth->depth_write ? VK_TRUE : VK_FALSE;
        ds.depthCompareOp    = (VkCompareOp)depth->depth_compare;
        ds.stencilTestEnabled = depth->stencil_test ? VK_TRUE : VK_FALSE;
        ds.front.failOp      = (VkStencilOp)depth->stencil_front.fail_op;
        ds.front.passOp      = (VkStencilOp)depth->stencil_front.pass_op;
        ds.front.depthFailOp = (VkStencilOp)depth->stencil_front.depth_fail_op;
        ds.front.compareOp   = (VkCompareOp)depth->stencil_front.compare;
        ds.front.compareMask = depth->stencil_front.read_mask;
        ds.front.writeMask   = depth->stencil_front.write_mask;
        ds.front.reference   = depth->stencil_front.reference;
        ds.back.failOp       = (VkStencilOp)depth->stencil_back.fail_op;
        ds.back.passOp       = (VkStencilOp)depth->stencil_back.pass_op;
        ds.back.depthFailOp  = (VkStencilOp)depth->stencil_back.depth_fail_op;
        ds.back.compareOp    = (VkCompareOp)depth->stencil_back.compare;
        ds.back.compareMask  = depth->stencil_back.read_mask;
        ds.back.writeMask    = depth->stencil_back.write_mask;
        ds.back.reference    = depth->stencil_back.reference;
    }

    VkGraphicsPipelineCreateInfo ci = {0};
    ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    ci.stageCount = 2; ci.pStages = stages;
    ci.pVertexInputState    = &vi;
    ci.pInputAssemblyState  = &ia;
    ci.pViewportState       = &vsi;
    ci.pRasterizationState  = &rs;
    ci.pMultisampleState    = &ms;
    ci.pDepthStencilState   = &ds;
    ci.pColorBlendState     = &cbs;
    ci.layout      = layout;
    ci.renderPass  = rp;
    ci.pDynamicState = &dynamic_state;

    return vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &ci, NULL, out);
}

/* -------------------------------------------------------------------------
 * Render pass (supports MSAA resolve when samples > 1)
 * ---------------------------------------------------------------------- */
static VkResult _sc_vk_create_rp(VkDevice dev, VkFormat fmt,
                                  VkSampleCountFlagBits samples,
                                  VkRenderPass *out, bool present) {
    VkAttachmentDescription att[2] = {{0},{0}};
    u32 att_count;

    if (samples > VK_SAMPLE_COUNT_1_BIT) {
        /* Attachment 0: MSAA color */
        att[0].format  = fmt;
        att[0].samples = samples;
        att[0].loadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR;
        att[0].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        att[0].finalLayout   = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        /* Attachment 1: Resolve target (single-sampled) */
        att[1].format  = fmt;
        att[1].samples = VK_SAMPLE_COUNT_1_BIT;
        att[1].loadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        att[1].finalLayout   = present ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                                       : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference color_ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference resolve_ref = {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

        VkSubpassDescription sp = {0};
        sp.pipelineBindPoint     = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sp.colorAttachmentCount  = 1;
        sp.pColorAttachments     = &color_ref;
        sp.pResolveAttachments   = &resolve_ref;

        VkRenderPassCreateInfo ci = {0};
        ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        ci.attachmentCount = 2; ci.pAttachments = att;
        ci.subpassCount    = 1; ci.pSubpasses   = &sp;
        att_count = 2;
        return vkCreateRenderPass(dev, &ci, NULL, out);
    }

    /* Single-sampled (fast path) */
    att[0].format  = fmt;
    att[0].samples = VK_SAMPLE_COUNT_1_BIT;
    att[0].loadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att[0].finalLayout   = present ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                                   : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

    VkSubpassDescription sp = {0};
    sp.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sp.colorAttachmentCount = 1;
    sp.pColorAttachments    = &ref;

    VkRenderPassCreateInfo ci = {0};
    ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = 1; ci.pAttachments = att;
    ci.subpassCount    = 1; ci.pSubpasses   = &sp;
    return vkCreateRenderPass(dev, &ci, NULL, out);
}

/* -------------------------------------------------------------------------
 * Create offscreen image + view + framebuffer (headless mode)
 * ---------------------------------------------------------------------- */
static VkResult _sc_vk_create_offscreen(VkDevice dev, VkPhysicalDevice phy,
    VkQueue queue, u32 w, u32 h, VkFormat fmt, VkRenderPass rp,
    VkSampleCountFlagBits samples,
    VkImage *img, VkDeviceMemory *mem, VkImageView *view, VkFramebuffer *fbo,
    VkImage *resolve_img, VkDeviceMemory *resolve_mem, VkImageView *resolve_view) {

    VkImageCreateInfo ici = {0};
    ici.sType     = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format    = fmt;
    ici.extent    = (VkExtent3D){w, h, 1};
    ici.mipLevels = 1; ici.arrayLayers = 1;
    ici.samples   = samples;
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

    /* Create resolve target for MSAA */
    if (samples > VK_SAMPLE_COUNT_1_BIT) {
        VkImageCreateInfo rci = {0};
        rci.sType     = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        rci.imageType = VK_IMAGE_TYPE_2D;
        rci.format    = fmt;
        rci.extent    = (VkExtent3D){w, h, 1};
        rci.mipLevels = 1; rci.arrayLayers = 1;
        rci.samples   = VK_SAMPLE_COUNT_1_BIT;
        rci.tiling    = VK_IMAGE_TILING_OPTIMAL;
        rci.usage     = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        rci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        r = vkCreateImage(dev, &rci, NULL, resolve_img);
        if (r != VK_SUCCESS) return r;

        VkMemoryRequirements rmr;
        vkGetImageMemoryRequirements(dev, *resolve_img, &rmr);
        VkMemoryAllocateInfo rai = {0};
        rai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        rai.allocationSize = rmr.size;
        rai.memoryTypeIndex = _sc_vk_find_mem_type(phy, rmr.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        r = vkAllocateMemory(dev, &rai, NULL, resolve_mem);
        if (r != VK_SUCCESS) { vkDestroyImage(dev, *resolve_img, NULL); return r; }
        vkBindImageMemory(dev, *resolve_img, *resolve_mem, 0);

        r = _sc_vk_make_view(dev, *resolve_img, fmt, resolve_view);
        if (r != VK_SUCCESS) return r;
    } else {
        *resolve_img = VK_NULL_HANDLE;
        *resolve_mem = VK_NULL_HANDLE;
        *resolve_view = VK_NULL_HANDLE;
    }

    /* Transition layouts to color-attachment-optimal */
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

    VkImageMemoryBarrier barriers[2] = {{0},{0}};
    barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].image = *img;
    barriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barriers[0].subresourceRange.levelCount = 1;
    barriers[0].subresourceRange.layerCount = 1;

    u32 barrier_count = 1;
    if (samples > VK_SAMPLE_COUNT_1_BIT) {
        barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barriers[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barriers[1].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[1].image = *resolve_img;
        barriers[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barriers[1].subresourceRange.levelCount = 1;
        barriers[1].subresourceRange.layerCount = 1;
        barrier_count = 2;
    }
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
        0, NULL, 0, NULL, barrier_count, barriers);
    vkEndCommandBuffer(cb);

    VkSubmitInfo si = {0};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1; si.pCommandBuffers = &cb;
    vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
    vkDeviceWaitIdle(dev);
    vkDestroyCommandPool(dev, tmp_pool, NULL);

    VkImageView fb_attachments[2];
    u32 att_count;
    if (samples > VK_SAMPLE_COUNT_1_BIT) {
        fb_attachments[0] = *view;
        fb_attachments[1] = *resolve_view;
        att_count = 2;
    } else {
        fb_attachments[0] = *view;
        att_count = 1;
    }

    VkFramebufferCreateInfo fci = {0};
    fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fci.renderPass = rp;
    fci.attachmentCount = att_count;
    fci.pAttachments = fb_attachments;
    fci.width = w; fci.height = h; fci.layers = 1;
    return vkCreateFramebuffer(dev, &fci, NULL, fbo);
}

/* -------------------------------------------------------------------------
 * Create swapchain + image views + framebuffers
 * ---------------------------------------------------------------------- */
static VkResult _sc_vk_create_swapchain(VkDevice dev, VkPhysicalDevice phy,
    VkSurfaceKHR surface, u32 width, u32 height, VkFormat fmt, VkRenderPass rp,
    VkSampleCountFlagBits samples,
    VkSwapchainKHR *out_swap, u32 *out_len,
    VkImage **out_images, VkImageView **out_views, VkFramebuffer **out_fbos,
    VkFormat *out_fmt,
    VkImage **out_msaa_images, VkDeviceMemory **out_msaa_mems, VkImageView **out_msaa_views) {

    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phy, surface, &caps);

    u32 fmt_count;
    vkGetPhysicalDeviceSurfaceFormatsKHR(phy, surface, &fmt_count, NULL);
    VkSurfaceFormatKHR *fmts = (VkSurfaceFormatKHR*)malloc(fmt_count * sizeof(VkSurfaceFormatKHR));
    if (!fmts) return VK_ERROR_OUT_OF_HOST_MEMORY;
    vkGetPhysicalDeviceSurfaceFormatsKHR(phy, surface, &fmt_count, fmts);

    VkSurfaceFormatKHR sf = fmts[0];
    for (u32 i = 0; i < fmt_count; i++) {
        if (fmts[i].format == fmt) { sf = fmts[i]; break; }
    }
    free(fmts);
    if (out_fmt) *out_fmt = sf.format;

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
    if (!*out_images || !*out_views || !*out_fbos) {
        free(*out_images); free(*out_views); free(*out_fbos);
        *out_images = NULL; *out_views = NULL; *out_fbos = NULL;
        vkDestroySwapchainKHR(dev, *out_swap, NULL);
        *out_swap = VK_NULL_HANDLE; *out_len = 0;
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    vkGetSwapchainImagesKHR(dev, *out_swap, out_len, *out_images);

    /* Allocate MSAA arrays if needed */
    if (samples > VK_SAMPLE_COUNT_1_BIT) {
        *out_msaa_images = (VkImage*)calloc(*out_len, sizeof(VkImage));
        *out_msaa_mems   = (VkDeviceMemory*)calloc(*out_len, sizeof(VkDeviceMemory));
        *out_msaa_views  = (VkImageView*)calloc(*out_len, sizeof(VkImageView));
        if (!*out_msaa_images || !*out_msaa_mems || !*out_msaa_views) {
            free(*out_msaa_images); *out_msaa_images = NULL;
            free(*out_msaa_mems);   *out_msaa_mems   = NULL;
            free(*out_msaa_views);  *out_msaa_views  = NULL;
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
    } else {
        *out_msaa_images = NULL;
        *out_msaa_mems   = NULL;
        *out_msaa_views  = NULL;
    }

    for (u32 i = 0; i < *out_len; i++) {
        _sc_vk_make_view(dev, (*out_images)[i], sf.format, &(*out_views)[i]);

        VkImageView fb_attachments[2];
        u32 att_count;

        if (samples > VK_SAMPLE_COUNT_1_BIT) {
            /* Create per-swapchain MSAA color image */
            VkImageCreateInfo ici = {0};
            ici.sType     = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            ici.imageType = VK_IMAGE_TYPE_2D;
            ici.format    = sf.format;
            ici.extent    = (VkExtent3D){extent.width, extent.height, 1};
            ici.mipLevels = 1; ici.arrayLayers = 1;
            ici.samples   = samples;
            ici.tiling    = VK_IMAGE_TILING_OPTIMAL;
            ici.usage     = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
            ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            vkCreateImage(dev, &ici, NULL, &(*out_msaa_images)[i]);

            VkMemoryRequirements mr;
            vkGetImageMemoryRequirements(dev, (*out_msaa_images)[i], &mr);
            VkMemoryAllocateInfo ai = {0};
            ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            ai.allocationSize = mr.size;
            ai.memoryTypeIndex = _sc_vk_find_mem_type(phy, mr.memoryTypeBits,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            vkAllocateMemory(dev, &ai, NULL, &(*out_msaa_mems)[i]);
            vkBindImageMemory(dev, (*out_msaa_images)[i], (*out_msaa_mems)[i], 0);

            _sc_vk_make_view(dev, (*out_msaa_images)[i], sf.format, &(*out_msaa_views)[i]);

            fb_attachments[0] = (*out_msaa_views)[i];
            fb_attachments[1] = (*out_views)[i];
            att_count = 2;
        } else {
            fb_attachments[0] = (*out_views)[i];
            att_count = 1;
        }

        VkFramebufferCreateInfo fci = {0};
        fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fci.renderPass      = rp;
        fci.attachmentCount = att_count;
        fci.pAttachments    = fb_attachments;
        fci.width  = extent.width;
        fci.height = extent.height;
        fci.layers = 1;
        vkCreateFramebuffer(dev, &fci, NULL, &(*out_fbos)[i]);
    }

    return VK_SUCCESS;
}

/* -------------------------------------------------------------------------
 * Destroy swapchain (internal helper, does NOT free ptr arrays)
 * ---------------------------------------------------------------------- */
static void _sc_vk_destroy_swapchain_resources(VkDevice dev, _SCVkState *s) {
    for (u32 i = 0; i < s->swap_len; i++) {
        if (s->swap_fbos[i])  vkDestroyFramebuffer(dev, s->swap_fbos[i], NULL);
        if (s->swap_views[i]) vkDestroyImageView(dev, s->swap_views[i], NULL);
        if (s->msaa_images && s->msaa_images[i]) {
            vkDestroyImageView(dev, s->msaa_views[i], NULL);
            vkDestroyImage(dev, s->msaa_images[i], NULL);
            vkFreeMemory(dev, s->msaa_mems[i], NULL);
        }
    }
    free(s->swap_images); s->swap_images = NULL;
    free(s->swap_views);  s->swap_views  = NULL;
    free(s->swap_fbos);   s->swap_fbos   = NULL;
    free(s->msaa_images); s->msaa_images = NULL;
    free(s->msaa_mems);   s->msaa_mems   = NULL;
    free(s->msaa_views);  s->msaa_views  = NULL;
    if (s->swapchain) vkDestroySwapchainKHR(dev, s->swapchain, NULL);
    s->swapchain = VK_NULL_HANDLE;
    s->swap_len  = 0;
}

/* -------------------------------------------------------------------------
 * Recreate swapchain (handle out-of-date / suboptimal)
 * ---------------------------------------------------------------------- */
static VkResult _sc_vk_recreate_swapchain(_SCVkState *s) {
    vkDeviceWaitIdle(s->device);
    _sc_vk_destroy_swapchain_resources(s->device, s);
    return _sc_vk_create_swapchain(s->device, s->phy_dev, s->surface,
        s->width, s->height, s->swap_fmt, s->render_pass, s->samples,
        &s->swapchain, &s->swap_len,
        &s->swap_images, &s->swap_views, &s->swap_fbos,
        &s->swap_fmt,
        &s->msaa_images, &s->msaa_mems, &s->msaa_views);
}

/* -------------------------------------------------------------------------
 * Sampler helper
 * ---------------------------------------------------------------------- */
static VkResult _sc_vk_create_sampler(VkDevice dev, VkSampler *out) {
    VkSamplerCreateInfo ci = {0};
    ci.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    ci.magFilter    = VK_FILTER_LINEAR;
    ci.minFilter    = VK_FILTER_LINEAR;
    ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ci.maxLod       = 1.0f;
    return vkCreateSampler(dev, &ci, NULL, out);
}

/* -------------------------------------------------------------------------
 * Descriptor pool helper
 * ---------------------------------------------------------------------- */
static VkResult _sc_vk_create_desc_pool(VkDevice dev, VkDescriptorPool *out) {
    VkDescriptorPoolSize ps = {0};
    ps.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ps.descriptorCount = SC_GFX_MAX_TEXTURES;
    VkDescriptorPoolCreateInfo ci = {0};
    ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ci.maxSets       = 1;
    ci.poolSizeCount = 1;
    ci.pPoolSizes    = &ps;
    return vkCreateDescriptorPool(dev, &ci, NULL, out);
}

/* -------------------------------------------------------------------------
 * Descriptor set helper (single set, updated per draw call)
 * ---------------------------------------------------------------------- */
static VkResult _sc_vk_alloc_desc_set(VkDevice dev,
    VkDescriptorPool pool, VkDescriptorSetLayout layout, VkDescriptorSet *out) {
    VkDescriptorSetAllocateInfo ai = {0};
    ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool     = pool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts        = &layout;
    return vkAllocateDescriptorSets(dev, &ai, out);
}

/* -------------------------------------------------------------------------
 * Upload pixel data to a VkImage (staging buffer + layout transition)
 * ---------------------------------------------------------------------- */
static VkResult _sc_vk_upload_tex_data(VkDevice dev, VkPhysicalDevice phy,
    VkQueue queue, VkCommandPool pool, VkImage image,
    u32 w, u32 h, VkDeviceSize data_size, const void *data) {
    if (!data || data_size == 0) {
        /* No data: just transition to shader-read-only */
        VkCommandBufferAllocateInfo cai = {0};
        cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cai.commandPool = pool;
        cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
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
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
            0, NULL, 0, NULL, 1, &barrier);
        vkEndCommandBuffer(cb);
        VkSubmitInfo si = {0};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1; si.pCommandBuffers = &cb;
        vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
        vkDeviceWaitIdle(dev);
        vkFreeCommandBuffers(dev, pool, 1, &cb);
        return VK_SUCCESS;
    }

    /* Create staging buffer */
    VkBuffer       staging_buf;
    VkDeviceMemory staging_mem;
    VkResult r = _sc_vk_create_buf(dev, phy, data_size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &staging_buf, &staging_mem);
    if (r != VK_SUCCESS) return r;

    void *map;
    vkMapMemory(dev, staging_mem, 0, data_size, 0, &map);
    memcpy(map, data, (usize)data_size);
    vkUnmapMemory(dev, staging_mem);

    /* One-time command buffer: copy staging → image + transition */
    VkCommandBufferAllocateInfo cai = {0};
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cb;
    vkAllocateCommandBuffers(dev, &cai, &cb);

    VkCommandBufferBeginInfo bi = {0};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bi);

    VkImageMemoryBarrier barrier_to_dst = {0};
    barrier_to_dst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier_to_dst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier_to_dst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier_to_dst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier_to_dst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier_to_dst.image = image;
    barrier_to_dst.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier_to_dst.subresourceRange.levelCount = 1;
    barrier_to_dst.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
        0, NULL, 0, NULL, 1, &barrier_to_dst);

    VkBufferImageCopy copy_region = {0};
    copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy_region.imageSubresource.layerCount = 1;
    copy_region.imageExtent = (VkExtent3D){w, h, 1};
    vkCmdCopyBufferToImage(cb, staging_buf, image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);

    VkImageMemoryBarrier barrier_to_read = {0};
    barrier_to_read.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier_to_read.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier_to_read.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier_to_read.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier_to_read.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier_to_read.image = image;
    barrier_to_read.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier_to_read.subresourceRange.levelCount = 1;
    barrier_to_read.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
        0, NULL, 0, NULL, 1, &barrier_to_read);

    vkEndCommandBuffer(cb);

    VkSubmitInfo si = {0};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1; si.pCommandBuffers = &cb;
    vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
    vkDeviceWaitIdle(dev);

    vkFreeCommandBuffers(dev, pool, 1, &cb);
    vkDestroyBuffer(dev, staging_buf, NULL);
    vkFreeMemory(dev, staging_mem, NULL);
    return VK_SUCCESS;
}

/* -------------------------------------------------------------------------
 * Staging upload for device-local buffers (one-shot copy)
 * ---------------------------------------------------------------------- */
static VkResult _sc_vk_staging_upload(VkDevice dev, VkPhysicalDevice phy,
    VkQueue queue, VkCommandPool pool,
    VkBuffer dst, VkDeviceSize size, const void *data) {
    if (!data || size == 0) return VK_SUCCESS;

    VkBuffer stage_buf;
    VkDeviceMemory stage_mem;
    VkResult r = _sc_vk_create_buf(dev, phy, size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &stage_buf, &stage_mem);
    if (r != VK_SUCCESS) return r;

    void *map;
    vkMapMemory(dev, stage_mem, 0, size, 0, &map);
    memcpy(map, data, (usize)size);
    vkUnmapMemory(dev, stage_mem);

    VkCommandBufferAllocateInfo cai = {0};
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cb;
    vkAllocateCommandBuffers(dev, &cai, &cb);

    VkCommandBufferBeginInfo bi = {0};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bi);

    VkBufferCopy region = {0, 0, size};
    vkCmdCopyBuffer(cb, stage_buf, dst, 1, &region);

    vkEndCommandBuffer(cb);

    VkSubmitInfo si = {0};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
    vkDeviceWaitIdle(dev);

    vkFreeCommandBuffers(dev, pool, 1, &cb);
    vkDestroyBuffer(dev, stage_buf, NULL);
    vkFreeMemory(dev, stage_mem, NULL);
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
    if (vk_desc) {
        s->headless = vk_desc->headless;
    } else {
        s->headless = (desc->native_window == NULL);
    }

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
#endif
#if defined(VK_USE_PLATFORM_XLIB_KHR)
        exts[ext_count++] = "VK_KHR_xlib_surface";
#endif
#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
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
    if (!phys) { sc_vulkan_shutdown(ctx); return SC_ERR_OOM; }
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

    /* ---- MSAA sample count ------------------------------------------- */
    {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(s->phy_dev, &props);
        VkSampleCountFlags fb_samples = props.limits.framebufferColorSampleCounts;
        u32 want = desc->sample_count;
        VkSampleCountFlagBits sc = VK_SAMPLE_COUNT_1_BIT;
        if (want >= 64 && (fb_samples & VK_SAMPLE_COUNT_64_BIT)) sc = VK_SAMPLE_COUNT_64_BIT;
        else if (want >= 32 && (fb_samples & VK_SAMPLE_COUNT_32_BIT)) sc = VK_SAMPLE_COUNT_32_BIT;
        else if (want >= 16 && (fb_samples & VK_SAMPLE_COUNT_16_BIT)) sc = VK_SAMPLE_COUNT_16_BIT;
        else if (want >= 8 && (fb_samples & VK_SAMPLE_COUNT_8_BIT)) sc = VK_SAMPLE_COUNT_8_BIT;
        else if (want >= 4 && (fb_samples & VK_SAMPLE_COUNT_4_BIT)) sc = VK_SAMPLE_COUNT_4_BIT;
        else if (want >= 2 && (fb_samples & VK_SAMPLE_COUNT_2_BIT)) sc = VK_SAMPLE_COUNT_2_BIT;
        s->samples = sc;
    }

    /* ---- Queue family ------------------------------------------------ */
    u32 qf_count;
    vkGetPhysicalDeviceQueueFamilyProperties(s->phy_dev, &qf_count, NULL);
    VkQueueFamilyProperties *qf = (VkQueueFamilyProperties*)malloc(
        qf_count * sizeof(VkQueueFamilyProperties));
    if (!qf) { sc_vulkan_shutdown(ctx); return SC_ERR_OOM; }
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
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
        PFN_vkCreateWaylandSurfaceKHR vkCreateWaylandSurfaceKHR =
            (PFN_vkCreateWaylandSurfaceKHR)vkGetInstanceProcAddr(
                s->instance, "vkCreateWaylandSurfaceKHR");
        if (vkCreateWaylandSurfaceKHR) {
            VkWaylandSurfaceCreateInfoKHR wci = {0};
            wci.sType  = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
            wci.display = (struct wl_display*)desc->native_display;
            wci.surface = (struct wl_surface*)desc->native_window;
            r = vkCreateWaylandSurfaceKHR(s->instance, &wci, NULL, &s->surface);
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
    r = _sc_vk_create_rp(s->device, fmt, s->samples, &s->render_pass, !s->headless);
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

    /* ---- Graphics pipeline (default depth: no test, no write) --------- */
    {
        SCGfxDepthState depth_def = {0};
        r = _sc_vk_create_pipeline(s->device, vs, fs, s->pipeline_layout,
                                    s->render_pass, s->samples, &depth_def, &s->pipeline);
    }
    vkDestroyShaderModule(s->device, fs, NULL);
    vkDestroyShaderModule(s->device, vs, NULL);
    if (r != VK_SUCCESS) { sc_vulkan_shutdown(ctx); return SC_ERR_GFX; }

    /* ---- Swapchain / offscreen ---------------------------------------- */
    if (s->headless) {
        r = _sc_vk_create_offscreen(s->device, s->phy_dev, s->gfx_queue,
            s->width, s->height, fmt, s->render_pass, s->samples,
            &s->os_image, &s->os_mem, &s->os_view, &s->os_fbo,
            &s->os_resolve_image, &s->os_resolve_mem, &s->os_resolve_view);
        if (r != VK_SUCCESS) { sc_vulkan_shutdown(ctx); return SC_ERR_GFX; }
    } else {
        r = _sc_vk_create_swapchain(s->device, s->phy_dev, s->surface,
            s->width, s->height, fmt, s->render_pass, s->samples,
            &s->swapchain, &s->swap_len,
            &s->swap_images, &s->swap_views, &s->swap_fbos,
            &s->swap_fmt,
            &s->msaa_images, &s->msaa_mems, &s->msaa_views);
        if (r != VK_SUCCESS) { sc_vulkan_shutdown(ctx); return SC_ERR_GFX; }
    }

    /* ---- Command pool ------------------------------------------------- */
    VkCommandPoolCreateInfo cpci = {0};
    cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    r = vkCreateCommandPool(s->device, &cpci, NULL, &s->cmd_pool);
    if (r != VK_SUCCESS) { sc_vulkan_shutdown(ctx); return SC_ERR_GFX; }

    /* ---- Texture sampler & descriptor pool ---------------------------- */
    memset(s->tex_images, 0, sizeof(s->tex_images));
    memset(s->tex_mems,   0, sizeof(s->tex_mems));
    memset(s->tex_views,  0, sizeof(s->tex_views));

    r = _sc_vk_create_sampler(s->device, &s->sampler);
    if (r != VK_SUCCESS) { sc_vulkan_shutdown(ctx); return SC_ERR_GFX; }

    r = _sc_vk_create_desc_pool(s->device, &s->desc_pool);
    if (r != VK_SUCCESS) { sc_vulkan_shutdown(ctx); return SC_ERR_GFX; }

    r = _sc_vk_alloc_desc_set(s->device, s->desc_pool, s->ds_layout, &s->desc_set);
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

    /* Destroy user buffers */
    for (u32 i = 0; i < SC_GFX_MAX_BUFFERS; i++) {
        if (s->buf_buffers[i]) vkDestroyBuffer(s->device, s->buf_buffers[i], NULL);
        if (s->buf_mems[i])    vkFreeMemory(s->device, s->buf_mems[i], NULL);
    }

    /* Destroy user shader modules */
    for (u32 i = 0; i < SC_GFX_MAX_SHADERS; i++) {
        if (s->shd_modules[i])    vkDestroyShaderModule(s->device, s->shd_modules[i], NULL);
        if (s->shd_fs_modules[i]) vkDestroyShaderModule(s->device, s->shd_fs_modules[i], NULL);
    }

    /* Destroy user pipelines (non-cached only; cache owns its own) */
    for (u32 i = 0; i < SC_GFX_MAX_PIPELINES; i++) {
        bool cached = s->user_pipelines[i] && _sc_vk_pip_is_cached(s, s->user_pipelines[i]);
        if (!cached) {
            if (s->user_pipelines[i])  vkDestroyPipeline(s->device, s->user_pipelines[i], NULL);
            if (s->user_pip_layouts[i]) vkDestroyPipelineLayout(s->device, s->user_pip_layouts[i], NULL);
        }
    }

    /* Destroy cached pipelines */
    for (u32 i = 0; i < _SC_VK_PIP_CACHE_SIZE; i++) {
        if (s->pip_cache[i].pip)    vkDestroyPipeline(s->device, s->pip_cache[i].pip, NULL);
        if (s->pip_cache[i].layout) vkDestroyPipelineLayout(s->device, s->pip_cache[i].layout, NULL);
    }

    /* Destroy textures */
    for (u32 i = 0; i < SC_GFX_MAX_TEXTURES; i++) {
        if (s->tex_views[i]) vkDestroyImageView(s->device, s->tex_views[i], NULL);
        if (s->tex_images[i]) vkDestroyImage(s->device, s->tex_images[i], NULL);
        if (s->tex_mems[i])   vkFreeMemory(s->device, s->tex_mems[i], NULL);
    }
    if (s->desc_pool) vkDestroyDescriptorPool(s->device, s->desc_pool, NULL);
    if (s->sampler)   vkDestroySampler(s->device, s->sampler, NULL);

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
        if (s->os_resolve_view)  vkDestroyImageView(s->device, s->os_resolve_view, NULL);
        if (s->os_resolve_image) vkDestroyImage(s->device, s->os_resolve_image, NULL);
        if (s->os_resolve_mem)   vkFreeMemory(s->device, s->os_resolve_mem, NULL);
    } else {
        _sc_vk_destroy_swapchain_resources(s->device, s);
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
        VkResult ar = vkAcquireNextImageKHR(s->device, s->swapchain, UINT64_MAX,
            f->acquire_sem, VK_NULL_HANDLE, &s->cur_image);
        if (ar == VK_ERROR_OUT_OF_DATE_KHR || ar == VK_SUBOPTIMAL_KHR) {
            VkResult rr = _sc_vk_recreate_swapchain(s);
            if (rr != VK_SUCCESS) { s->in_frame = false; return; }
            ar = vkAcquireNextImageKHR(s->device, s->swapchain, UINT64_MAX,
                f->acquire_sem, VK_NULL_HANDLE, &s->cur_image);
        }
        if (ar != VK_SUCCESS && ar != VK_SUBOPTIMAL_KHR) { s->in_frame = false; return; }
    }

    VkCommandBufferBeginInfo bi = {0};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(f->cmd, &bi);

    VkFramebuffer fb = s->headless ? s->os_fbo : s->swap_fbos[s->cur_image];

    VkClearValue cv[2] = {{0},{0}};
    cv[0].color.float32[0] = clear.r;
    cv[0].color.float32[1] = clear.g;
    cv[0].color.float32[2] = clear.b;
    cv[0].color.float32[3] = clear.a;

    bool msaa = s->samples > VK_SAMPLE_COUNT_1_BIT;

    VkRenderPassBeginInfo rpbi = {0};
    rpbi.sType       = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpbi.renderPass  = s->render_pass;
    rpbi.framebuffer = fb;
    rpbi.renderArea.offset.x = 0;
    rpbi.renderArea.offset.y = 0;
    rpbi.renderArea.extent.width  = s->width;
    rpbi.renderArea.extent.height = s->height;
    rpbi.clearValueCount = msaa ? 2 : 1;
    rpbi.pClearValues    = cv;
    vkCmdBeginRenderPass(f->cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(f->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s->pipeline);

    VkViewport vp = {0.0f, 0.0f, (f32)s->width, (f32)s->height, 0.0f, 1.0f};
    vkCmdSetViewport(f->cmd, 0, 1, &vp);
    VkRect2D sci = {{0, 0}, {s->width, s->height}};
    vkCmdSetScissor(f->cmd, 0, 1, &sci);

    s->in_frame = true;
}

/* -------------------------------------------------------------------------
 * User buffer management
 * ---------------------------------------------------------------------- */
SCGfxBuffer sc_vulkan_make_buffer(SCGfxContext *ctx, const SCGfxBufferDesc *desc) {
    SCGfxBuffer h = {0};
    if (!ctx || !ctx->backend_data || !desc) return h;
    _SCVkState *s = (_SCVkState*)ctx->backend_data;
    if (!desc->data || desc->size == 0) return h;

    u32 id = _sc_gfx_alloc_slot(ctx->buf_slots, SC_GFX_MAX_BUFFERS, &ctx->buf_free_head);
    if (id == 0) return h;
    h.id = id;

    VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (desc->type == SC_BUFFER_INDEX)
        usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    else
        usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

    VkResult r = _sc_vk_create_buf(s->device, s->phy_dev, desc->size, usage,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        &s->buf_buffers[id], &s->buf_mems[id]);
    if (r != VK_SUCCESS) { _sc_gfx_free_slot(ctx->buf_slots, id, &ctx->buf_free_head); h.id = 0; return h; }
    s->buf_sizes[id] = desc->size;

    r = _sc_vk_staging_upload(s->device, s->phy_dev, s->gfx_queue,
        s->cmd_pool, s->buf_buffers[id], desc->size, desc->data);
    if (r != VK_SUCCESS) {
        vkDestroyBuffer(s->device, s->buf_buffers[id], NULL);
        vkFreeMemory(s->device, s->buf_mems[id], NULL);
        s->buf_buffers[id] = VK_NULL_HANDLE;
        s->buf_mems[id] = VK_NULL_HANDLE;
        _sc_gfx_free_slot(ctx->buf_slots, id, &ctx->buf_free_head);
        h.id = 0;
        return h;
    }
    return h;
}

void sc_vulkan_destroy_buffer(SCGfxContext *ctx, SCGfxBuffer buf) {
    if (!ctx || !ctx->backend_data || buf.id == 0) return;
    _SCVkState *s = (_SCVkState*)ctx->backend_data;
    if (buf.id >= SC_GFX_MAX_BUFFERS) return;
    if (s->buf_buffers[buf.id]) {
        vkDeviceWaitIdle(s->device);
        vkDestroyBuffer(s->device, s->buf_buffers[buf.id], NULL);
        s->buf_buffers[buf.id] = VK_NULL_HANDLE;
    }
    if (s->buf_mems[buf.id]) {
        vkFreeMemory(s->device, s->buf_mems[buf.id], NULL);
        s->buf_mems[buf.id] = VK_NULL_HANDLE;
    }
    s->buf_sizes[buf.id] = 0;
}

void sc_vulkan_update_buffer(SCGfxContext *ctx, SCGfxBuffer buf,
                              const void *data, usize size) {
    if (!ctx || !ctx->backend_data || buf.id == 0 || !data) return;
    _SCVkState *s = (_SCVkState*)ctx->backend_data;
    sc_vulkan_destroy_buffer(ctx, buf);
    if (size == 0) return;
    VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    VkResult r = _sc_vk_create_buf(s->device, s->phy_dev, size, usage,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        &s->buf_buffers[buf.id], &s->buf_mems[buf.id]);
    if (r != VK_SUCCESS) return;
    s->buf_sizes[buf.id] = size;
    _sc_vk_staging_upload(s->device, s->phy_dev, s->gfx_queue,
        s->cmd_pool, s->buf_buffers[buf.id], size, data);
}

/* -------------------------------------------------------------------------
 * User shader management (store bytecode as VkShaderModule)
 * ---------------------------------------------------------------------- */
SCGfxShader sc_vulkan_make_shader(SCGfxContext *ctx, const SCGfxShaderDesc *desc) {
    SCGfxShader h = {0};
    if (!ctx || !ctx->backend_data || !desc) return h;
    _SCVkState *s = (_SCVkState*)ctx->backend_data;

    u32 id = _sc_gfx_alloc_slot(ctx->shd_slots, SC_GFX_MAX_SHADERS, &ctx->shd_free_head);
    if (id == 0) return h;
    h.id = id;

    if (desc->vs_bytecode && desc->vs_bytecode_size > 0) {
        _sc_vk_make_shader(s->device, (const unsigned char*)desc->vs_bytecode,
                           desc->vs_bytecode_size, &s->shd_modules[id]);
    }
    if (desc->fs_bytecode && desc->fs_bytecode_size > 0) {
        _sc_vk_make_shader(s->device, (const unsigned char*)desc->fs_bytecode,
                           desc->fs_bytecode_size, &s->shd_fs_modules[id]);
    }
    return h;
}

void sc_vulkan_destroy_shader(SCGfxContext *ctx, SCGfxShader shd) {
    if (!ctx || !ctx->backend_data || shd.id == 0) return;
    _SCVkState *s = (_SCVkState*)ctx->backend_data;
    if (shd.id >= SC_GFX_MAX_SHADERS) return;
    if (s->shd_modules[shd.id]) {
        vkDestroyShaderModule(s->device, s->shd_modules[shd.id], NULL);
        s->shd_modules[shd.id] = VK_NULL_HANDLE;
    }
    if (s->shd_fs_modules[shd.id]) {
        vkDestroyShaderModule(s->device, s->shd_fs_modules[shd.id], NULL);
        s->shd_fs_modules[shd.id] = VK_NULL_HANDLE;
    }
}

/* -------------------------------------------------------------------------
 * Pipeline cache: FNV-1a hash of the relevant desc fields
 * ---------------------------------------------------------------------- */
static u64 _sc_vk_hash_pip_desc(const SCGfxPipelineDesc *desc) {
    u64 h = 0xcbf29ce484222325ull;
    u64 prime = 0x100000001b3ull;
    u32 buf[16] = {0};
    buf[0]  = desc->shader.id;
    buf[1]  = (u32)desc->prim_type;
    buf[2]  = desc->blend.enabled ? 1u : 0u;
    buf[3]  = (u32)desc->blend.src_factor;
    buf[4]  = (u32)desc->blend.dst_factor;
    buf[5]  = desc->depth.depth_test ? 1u : 0u;
    buf[6]  = desc->depth.depth_write ? 1u : 0u;
    buf[7]  = (u32)desc->depth.depth_compare;
    buf[8]  = desc->depth.stencil_test ? 1u : 0u;
    buf[9]  = (u32)desc->depth.stencil_front.fail_op;
    buf[10] = (u32)desc->depth.stencil_front.pass_op;
    buf[11] = (u32)desc->depth.stencil_front.depth_fail_op;
    buf[12] = (u32)desc->depth.stencil_front.compare;
    buf[13] = (u32)desc->depth.stencil_back.fail_op;
    buf[14] = (u32)desc->depth.stencil_back.pass_op;
    buf[15] = (u32)desc->depth.stencil_back.compare;
    for (int i = 0; i < 16; i++) { h ^= (u64)buf[i]; h *= prime; }
    return h;
}

/* Return true if the pipeline is held by the cache */
static bool _sc_vk_pip_is_cached(_SCVkState *s, VkPipeline pip) {
    for (u32 i = 0; i < _SC_VK_PIP_CACHE_SIZE; i++)
        if (s->pip_cache[i].hash && s->pip_cache[i].pip == pip) return true;
    return false;
}

/* -------------------------------------------------------------------------
 * User pipeline management (best-effort creation, cached)
 * ---------------------------------------------------------------------- */
SCGfxPipeline sc_vulkan_make_pipeline(SCGfxContext *ctx, const SCGfxPipelineDesc *desc) {
    SCGfxPipeline h = {0};
    if (!ctx || !ctx->backend_data || !desc) return h;
    _SCVkState *s = (_SCVkState*)ctx->backend_data;

    /* Hash the desc and check cache */
    u64 hash = _sc_vk_hash_pip_desc(desc);
    for (u32 ci = 0; ci < _SC_VK_PIP_CACHE_SIZE; ci++) {
        if (s->pip_cache[ci].hash == hash) {
            u32 id = _sc_gfx_alloc_slot(ctx->pip_slots, SC_GFX_MAX_PIPELINES, &ctx->pip_free_head);
            if (id == 0) return h;
            h.id = id;
            s->user_pipelines[id]  = s->pip_cache[ci].pip;
            s->user_pip_layouts[id] = s->pip_cache[ci].layout;
            return h;
        }
    }

    u32 id = _sc_gfx_alloc_slot(ctx->pip_slots, SC_GFX_MAX_PIPELINES, &ctx->pip_free_head);
    if (id == 0) return h;
    h.id = id;

    /* Build a pipeline layout with push constants */
    VkPushConstantRange pc = {VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4};
    VkPipelineLayoutCreateInfo plci = {0};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pc;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &s->ds_layout;
    VkResult r = vkCreatePipelineLayout(s->device, &plci, NULL, &s->user_pip_layouts[id]);
    if (r != VK_SUCCESS) { _sc_gfx_free_slot(ctx->pip_slots, id, &ctx->pip_free_head); h.id = 0; return h; }

    /* Use shader modules from the shader handle, or fallback to built-in */
    u32 shd_id = desc->shader.id;
    VkShaderModule vs = (shd_id > 0 && shd_id < SC_GFX_MAX_SHADERS && s->shd_modules[shd_id])
                        ? s->shd_modules[shd_id] : s->shd_modules[0];
    VkShaderModule fs = (shd_id > 0 && shd_id < SC_GFX_MAX_SHADERS && s->shd_fs_modules[shd_id])
                        ? s->shd_fs_modules[shd_id] : s->shd_fs_modules[0];
    if (!vs) { _sc_vk_make_shader(s->device, vk_vert_spv, vk_vert_spv_len, &vs); }
    if (!fs) { _sc_vk_make_shader(s->device, vk_frag_spv, vk_frag_spv_len, &fs); }

    r = _sc_vk_create_pipeline(s->device, vs, fs,
                               s->user_pip_layouts[id], s->render_pass,
                               s->samples, &desc->depth,
                               &s->user_pipelines[id]);
    if (r != VK_SUCCESS) {
        vkDestroyPipelineLayout(s->device, s->user_pip_layouts[id], NULL);
        s->user_pip_layouts[id] = VK_NULL_HANDLE;
        _sc_gfx_free_slot(ctx->pip_slots, id, &ctx->pip_free_head);
        h.id = 0;
        return h;
    }

    if (vs != s->shd_modules[0] && vs != VK_NULL_HANDLE)
        vkDestroyShaderModule(s->device, vs, NULL);
    if (fs != s->shd_fs_modules[0] && fs != VK_NULL_HANDLE)
        vkDestroyShaderModule(s->device, fs, NULL);

    /* Store in cache (linear scan for empty slot) */
    for (u32 ci = 0; ci < _SC_VK_PIP_CACHE_SIZE; ci++) {
        if (s->pip_cache[ci].hash == 0) {
            s->pip_cache[ci].hash   = hash;
            s->pip_cache[ci].pip    = s->user_pipelines[id];
            s->pip_cache[ci].layout = s->user_pip_layouts[id];
            break;
        }
    }

    return h;
}

void sc_vulkan_destroy_pipeline(SCGfxContext *ctx, SCGfxPipeline pip) {
    if (!ctx || !ctx->backend_data || pip.id == 0) return;
    _SCVkState *s = (_SCVkState*)ctx->backend_data;
    if (pip.id >= SC_GFX_MAX_PIPELINES) return;
    bool cached = s->user_pipelines[pip.id] && _sc_vk_pip_is_cached(s, s->user_pipelines[pip.id]);
    if (!cached) {
        if (s->user_pipelines[pip.id])  vkDestroyPipeline(s->device, s->user_pipelines[pip.id], NULL);
        if (s->user_pip_layouts[pip.id]) vkDestroyPipelineLayout(s->device, s->user_pip_layouts[pip.id], NULL);
    }
    s->user_pipelines[pip.id]  = VK_NULL_HANDLE;
    s->user_pip_layouts[pip.id] = VK_NULL_HANDLE;
}

void sc_vulkan_submit(SCGfxContext *ctx, const SCGfxDrawCmd *cmds, u32 count) {
    SC_UNUSED(ctx); SC_UNUSED(cmds); SC_UNUSED(count);
    /* Stats are counted in the common sc_gfx_submit path */
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
            u32 tex_id = ctx->batch_cmds[i].texture.id;
            u32 ht = tex_id != 0 ? 1u : 0u;

            if (tex_id > 0 && tex_id < SC_GFX_MAX_TEXTURES && s->tex_views[tex_id]) {
                VkDescriptorImageInfo img_info = {0};
                img_info.sampler     = s->sampler;
                img_info.imageView   = s->tex_views[tex_id];
                img_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                VkWriteDescriptorSet write = {0};
                write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet          = s->desc_set;
                write.dstBinding      = 0;
                write.descriptorCount = 1;
                write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                write.pImageInfo      = &img_info;
                vkUpdateDescriptorSets(s->device, 1, &write, 0, NULL);
                vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    s->pipeline_layout, 0, 1, &s->desc_set, 0, NULL);
            }

            vkCmdPushConstants(cb, s->pipeline_layout,
                VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4, &ht);
            vkCmdDraw(cb, ctx->batch_cmds[i].vertex_count, 1,
                ctx->batch_cmds[i].vertex_offset, 0);
        }
    }

    /* Process user-submitted draw commands */
    for (u32 i = 0; i < ctx->submit_ccount; i++) {
        SCGfxDrawCmd *cmd = &ctx->submit_cmds[i];
        u32 vb_id = cmd->vertex_buf.id;
        if (vb_id == 0 || vb_id >= SC_GFX_MAX_BUFFERS) continue;
        if (!s->buf_buffers[vb_id]) continue;

        /* Bind user pipeline if specified, else use built-in */
        u32 pip_id = cmd->pipeline.id;
        VkPipeline cur_pip = (pip_id > 0 && pip_id < SC_GFX_MAX_PIPELINES &&
                              s->user_pipelines[pip_id])
                             ? s->user_pipelines[pip_id] : s->pipeline;
        VkPipelineLayout cur_layout = (pip_id > 0 && pip_id < SC_GFX_MAX_PIPELINES &&
                                       s->user_pip_layouts[pip_id])
                                      ? s->user_pip_layouts[pip_id] : s->pipeline_layout;
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, cur_pip);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
            cur_layout, 0, 1, &s->desc_set, 0, NULL);

        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cb, 0, 1, &s->buf_buffers[vb_id], &offset);

        u32 tex_id = cmd->texture.id;
        if (tex_id > 0 && tex_id < SC_GFX_MAX_TEXTURES && s->tex_views[tex_id]) {
            VkDescriptorImageInfo img_info = {0};
            img_info.sampler     = s->sampler;
            img_info.imageView   = s->tex_views[tex_id];
            img_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            VkWriteDescriptorSet write = {0};
            write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet          = s->desc_set;
            write.dstBinding      = 0;
            write.descriptorCount = 1;
            write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo      = &img_info;
            vkUpdateDescriptorSets(s->device, 1, &write, 0, NULL);
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                cur_layout, 0, 1, &s->desc_set, 0, NULL);
        }

        u32 ht = tex_id != 0 ? 1u : 0u;
        vkCmdPushConstants(cb, cur_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4, &ht);

        u32 ib_id = cmd->index_buf.id;
        if (ib_id > 0 && ib_id < SC_GFX_MAX_BUFFERS && s->buf_buffers[ib_id]) {
            vkCmdBindIndexBuffer(cb, s->buf_buffers[ib_id], 0, VK_INDEX_TYPE_UINT32);
            u32 icount = cmd->index_count > 0 ? cmd->index_count
                        : (u32)(s->buf_sizes[ib_id] / sizeof(u32));
            vkCmdDrawIndexed(cb, icount, 1, cmd->base_index,
                             (i32)cmd->base_vertex, 0);
        } else {
            vkCmdDraw(cb, cmd->vertex_count, 1, cmd->base_vertex, 0);
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
        VkResult pr = vkQueuePresentKHR(s->gfx_queue, &pi);
        if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR) {
            _sc_vk_recreate_swapchain(s);
        }
    }

    s->frame_idx = (s->frame_idx + 1) % SC_VK_FRAME_OVERLAP;
    s->in_frame  = false;
}

SCGfxTexture sc_vulkan_make_texture(SCGfxContext *ctx, const SCGfxTextureDesc *desc) {
    SCGfxTexture h = {0};
    if (!ctx || !ctx->backend_data || !desc || desc->width == 0 || desc->height == 0)
        return h;
    _SCVkState *s = (_SCVkState*)ctx->backend_data;

    u32 id = 0;
    for (u32 i = 1; i < SC_GFX_MAX_TEXTURES; i++) {
        if (!s->tex_images[i]) { id = i; break; }
    }
    if (id == 0) return h;
    h.id = id;

    VkFormat vk_fmt = VK_FORMAT_R8G8B8A8_UNORM;

    /* Create image */
    VkImageCreateInfo ici = {0};
    ici.sType     = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format    = vk_fmt;
    ici.extent    = (VkExtent3D){desc->width, desc->height, 1};
    ici.mipLevels = 1; ici.arrayLayers = 1;
    ici.samples   = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling    = VK_IMAGE_TILING_OPTIMAL;
    ici.usage     = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkResult r = vkCreateImage(s->device, &ici, NULL, &s->tex_images[id]);
    if (r != VK_SUCCESS) { h.id = 0; return h; }

    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(s->device, s->tex_images[id], &mr);
    VkMemoryAllocateInfo ai = {0};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = _sc_vk_find_mem_type(s->phy_dev, mr.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    r = vkAllocateMemory(s->device, &ai, NULL, &s->tex_mems[id]);
    if (r != VK_SUCCESS) {
        vkDestroyImage(s->device, s->tex_images[id], NULL);
        s->tex_images[id] = VK_NULL_HANDLE;
        h.id = 0; return h;
    }
    vkBindImageMemory(s->device, s->tex_images[id], s->tex_mems[id], 0);

    /* Image view */
    r = _sc_vk_make_view(s->device, s->tex_images[id], vk_fmt, &s->tex_views[id]);
    if (r != VK_SUCCESS) {
        vkDestroyImage(s->device, s->tex_images[id], NULL);
        vkFreeMemory(s->device, s->tex_mems[id], NULL);
        s->tex_images[id] = VK_NULL_HANDLE;
        s->tex_mems[id]   = VK_NULL_HANDLE;
        h.id = 0; return h;
    }

    /* Upload pixel data */
    VkDeviceSize data_size = (VkDeviceSize)desc->width * (VkDeviceSize)desc->height * 4;
    void *rgba_data = NULL;
    const void *src_data = desc->data;

    /* Convert R8 → RGBA8 if needed */
    if (desc->fmt == SC_PIXFMT_R8 && desc->data) {
        rgba_data = malloc((usize)data_size);
        if (rgba_data) {
            const u8 *src8 = (const u8*)desc->data;
            u8 *dst4 = (u8*)rgba_data;
            for (u32 y = 0; y < desc->height; y++) {
                for (u32 x = 0; x < desc->width; x++) {
                    u8 a = src8[y * desc->width + x];
                    dst4[(y * desc->width + x) * 4 + 0] = 255;
                    dst4[(y * desc->width + x) * 4 + 1] = 255;
                    dst4[(y * desc->width + x) * 4 + 2] = 255;
                    dst4[(y * desc->width + x) * 4 + 3] = a;
                }
            }
        }
        src_data = rgba_data;
    } else if (desc->fmt != SC_PIXFMT_RGBA8 && desc->data) {
        /* For non-RGBA8/R8 formats, just use raw data as-is */
        data_size = desc->data_size;
    }

    _sc_vk_upload_tex_data(s->device, s->phy_dev, s->gfx_queue,
        s->cmd_pool, s->tex_images[id],
        desc->width, desc->height, data_size, src_data);

    free(rgba_data);
    return h;
}

SCResult sc_vulkan_resize(SCGfxContext *ctx, u32 width, u32 height) {
    if (!ctx || !ctx->backend_data || width == 0 || height == 0)
        return SC_ERR_INVALID_ARG;
    _SCVkState *s = (_SCVkState*)ctx->backend_data;

    vkDeviceWaitIdle(s->device);
    s->width  = width;
    s->height = height;
    ctx->width  = width;
    ctx->height = height;

    if (s->headless) {
        /* Destroy old offscreen resources */
        if (s->os_fbo)   vkDestroyFramebuffer(s->device, s->os_fbo, NULL);
        if (s->os_view)  vkDestroyImageView(s->device, s->os_view, NULL);
        if (s->os_image) vkDestroyImage(s->device, s->os_image, NULL);
        if (s->os_mem)   vkFreeMemory(s->device, s->os_mem, NULL);
        if (s->os_resolve_view)  vkDestroyImageView(s->device, s->os_resolve_view, NULL);
        if (s->os_resolve_image) vkDestroyImage(s->device, s->os_resolve_image, NULL);
        if (s->os_resolve_mem)   vkFreeMemory(s->device, s->os_resolve_mem, NULL);
        s->os_fbo = s->os_view = VK_NULL_HANDLE;
        s->os_image = s->os_resolve_image = VK_NULL_HANDLE;
        s->os_mem = s->os_resolve_mem = VK_NULL_HANDLE;
        s->os_resolve_view = VK_NULL_HANDLE;

        VkResult r = _sc_vk_create_offscreen(s->device, s->phy_dev, s->gfx_queue,
            s->width, s->height, s->swap_fmt, s->render_pass, s->samples,
            &s->os_image, &s->os_mem, &s->os_view, &s->os_fbo,
            &s->os_resolve_image, &s->os_resolve_mem, &s->os_resolve_view);
        return (r == VK_SUCCESS) ? SC_OK : SC_ERR_GFX;
    }

    /* Windowed: recreate swapchain */
    VkResult r = _sc_vk_recreate_swapchain(s);
    return (r == VK_SUCCESS) ? SC_OK : SC_ERR_GFX;
}

void sc_vulkan_destroy_texture(SCGfxContext *ctx, SCGfxTexture tex) {
    if (!ctx || !ctx->backend_data || tex.id == 0 || tex.id >= SC_GFX_MAX_TEXTURES)
        return;
    _SCVkState *s = (_SCVkState*)ctx->backend_data;
    if (!s->tex_images[tex.id]) return;
    if (s->device) vkDeviceWaitIdle(s->device);
    if (s->tex_views[tex.id])  vkDestroyImageView(s->device, s->tex_views[tex.id], NULL);
    if (s->tex_images[tex.id]) vkDestroyImage(s->device, s->tex_images[tex.id], NULL);
    if (s->tex_mems[tex.id])   vkFreeMemory(s->device, s->tex_mems[tex.id], NULL);
    s->tex_views[tex.id]  = VK_NULL_HANDLE;
    s->tex_images[tex.id] = VK_NULL_HANDLE;
    s->tex_mems[tex.id]   = VK_NULL_HANDLE;
}

#endif /* SC_BACKEND_VULKAN_IMPLEMENTATION */
#endif /* SC_BACKEND_VULKAN_H */
