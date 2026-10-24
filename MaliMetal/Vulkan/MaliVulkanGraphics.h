/* SPDX-License-Identifier: MIT */
#ifndef MALI_VULKAN_GRAPHICS_H
#define MALI_VULKAN_GRAPHICS_H
#include "MaliVulkanBackend.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { MMVK_GRAPHICS_COLOR_SLOTS=8,MMVK_GRAPHICS_ATTACHMENTS=18,MMVK_GRAPHICS_PASSES=16 };
enum { MMVK_GRAPHICS_EXT_RASTER_ORDER=1u,MMVK_GRAPHICS_ARM_RASTER_ORDER=2u,
       MMVK_GRAPHICS_EXT_INTERLOCK=4u,MMVK_GRAPHICS_EXT_TILE_IMAGE=8u,
       MMVK_GRAPHICS_KHR_DYNAMIC_RENDERING=16u };
typedef struct {
    uint32_t size,extensions,dynamic_rendering;
    uint32_t raster_order_color,raster_order_depth,raster_order_stencil;
    uint32_t pixel_interlock,sample_interlock,shading_rate_interlock;
    uint32_t tile_color_read,tile_depth_read,tile_stencil_read;
    uint32_t tile_coherent_accelerated,tile_sample_from_pixel,tile_helper_reads;
} MMVKGraphicsCapabilities;
/* Before VkDevice creation this returns AVAILABLE extension/features, not enabled
 * state. The caller must retain a separate exact enabled selection after passing
 * its feature chain and extension names to vkCreateDevice. */
MMVKResult mmvk_graphics_query_capabilities(MMVKDevice *,MMVKGraphicsCapabilities *,MMVKError *);
/* Numeric raster, compare and stencil values are Vulkan core enums. Sample
 * count is the actual number of samples (1,2,4,...), never a logarithm. */
typedef struct {
    uint32_t fail_op,pass_op,depth_fail_op,compare_op,read_mask,write_mask;
} MMVKStencilFace;
typedef struct {
    uint32_t topology,cull_mode,front_face,polygon_mode,primitive_restart;
    uint32_t depth_clamp,rasterizer_discard,depth_bias;
    uint32_t depth_test,depth_write,depth_compare,stencil_test;
    MMVKStencilFace front,back;
    uint32_t sample_count,alpha_to_coverage,alpha_to_one,sample_shading;
    uint32_t sample_mask[2];
    uint32_t tile_image_color_reads; /* Compiler-selected coherent fetch slots. */
} MMVKGraphicsState;
typedef struct {
    uint32_t color_count,sample_count;
    MMVKFormat color_formats[8],depth_stencil_format;
    uint32_t resolve_mask;
    uint32_t depth_resolve_mode,stencil_resolve_mode; /* Vulkan VkResolveModeFlagBits. */
} MMVKPassLayout;
typedef struct {
    const MMVKSpecializationEntry *entries;
    uint32_t count;
    const void *data;
    size_t size;
} MMVKGraphicsSpecialization;
typedef struct {
    MMVKRenderPipelineDescriptor base;
    MMVKGraphicsState state;
    MMVKPassLayout pass;
    MMVKGraphicsSpecialization vertex,fragment;
} MMVKGraphicsPipelineDescriptor;
void mmvk_graphics_state_default(MMVKGraphicsState *);
MMVKResult mmvk_graphics_pipeline_create(MMVKDevice *,const MMVKGraphicsPipelineDescriptor *,MMVKPipeline **,MMVKError *);

enum { MMVK_LOAD_DONT_CARE=0,MMVK_LOAD_LOAD=1,MMVK_LOAD_CLEAR=2 };
enum { MMVK_STORE_DONT_CARE=0,MMVK_STORE_STORE=1,MMVK_STORE_RESOLVE=2,MMVK_STORE_STORE_AND_RESOLVE=3 };
typedef struct {
    MMVKImage *image;
    uint32_t level,base_layer,layer_count;
} MMVKAttachmentView;
typedef struct {
    MMVKAttachmentView view,resolve;
    uint32_t load,store;
    /* Depth and stencil use separate load/store operations on one packed view. */
    uint32_t stencil_load,stencil_store;
    uint32_t depth_resolve_mode,stencil_resolve_mode;
    double color[4];
    double depth;
    uint32_t stencil;
} MMVKPassAttachment;
typedef struct {
    uint32_t color_count,width,height,layers;
    MMVKPassAttachment colors[8],depth_stencil;
} MMVKRenderPassDescriptor;
MMVKResult mmvk_commands_begin_graphics_pass(MMVKCommands *,const MMVKRenderPassDescriptor *,MMVKError *);
MMVKResult mmvk_graphics_end_native_pass(MMVKCommands *,MMVKError *);
MMVKResult mmvk_commands_set_blend_constants(MMVKCommands *,const float rgba[4],MMVKError *);
MMVKResult mmvk_commands_set_stencil_reference(MMVKCommands *,uint32_t front,uint32_t back,MMVKError *);
MMVKResult mmvk_commands_set_depth_bias(MMVKCommands *,float constant,float slope,float clamp,MMVKError *);

/* Called by the backend's compatibility, alias-hazard and destruction paths.
 * All images remain caller-owned until GPU completion; transient views are
 * owned by the command buffer and destroyed only after that same lifetime. */
uint32_t mmvk_graphics_pipeline_compatible(const MMVKCommands *,const MMVKPipeline *);
uint32_t mmvk_graphics_attachment_active(const MMVKCommands *,const MMVKImage *);
void mmvk_graphics_commands_cleanup(MMVKCommands *);
void mmvk_graphics_pass_ended(MMVKCommands *);

/* Checks the actual enabled coherent tile-image mechanism. Compiler output must
 * select it explicitly; ordinary sampled feedback and draw snapshots do not
 * satisfy this contract. */
MMVKResult mmvk_graphics_require_ordered_fetch(MMVKDevice *,uint32_t attachment_mask,MMVKError *);
#ifdef __cplusplus
}
#endif
#endif
