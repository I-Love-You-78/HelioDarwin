#ifndef MALI_METAL_COMPUTE_VULKAN_H
#define MALI_METAL_COMPUTE_VULKAN_H
#include "MaliMetalCompute.h"
#include "MaliMetalShaderVulkan.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MMShaderVulkanComputeDescription {
    MMVKShader shader;
    MMVKPipelineLayout layout;
    MMVKBindingLayout bindings[MM_SHADER_MAX_BINDINGS];
} MMShaderVulkanComputeDescription;

typedef struct MMShaderVulkanComputeResources {
    MMShaderVulkanBuffer buffers[31];
    uint64_t logical_lengths[31];
    MMVKImage *textures[128];
    uint32_t threadgroup_lengths[31];
    MMVKBuffer *constant_buffers[MM_SHADER_MAX_BINDINGS];
} MMShaderVulkanComputeResources;

/* Views refer to this description and the retained compiler output. A native
 * program validates this generic module/layout; only a compiled specialization
 * of a dispatch region can execute it. */
MMVKResult MMShaderVulkanDescribeCompute(const MMShaderComputeOutput *,
                                         MMShaderVulkanComputeDescription *, MMVKError *);
MMVKResult MMShaderVulkanCreateComputeConstants(MMVKDevice *, const MMShaderComputeOutput *,
                                                MMShaderVulkanConstants *, MMVKError *);

/* Capture the actual Metal logical lengths and native image formats before
 * calling MMShaderPrepareComputeDispatch. Padded native allocations are never
 * presented as larger Metal buffers to the AIR binder. */
MMVKResult MMShaderVulkanComputeBindingFacts(const MMShaderVulkanComputeResources *,
                                             MMShaderComputeBindings *, MMVKError *);

/* Bind the compiler's instantiated descriptors exactly. The caller prepares
 * images before dispatch, binds the matching native pipeline variant, then
 * applies each region's pushes and dispatches that region's group counts.
 * Retain output, dispatch plan and resources until recording finishes, and all
 * native resources through GPU completion. */
MMVKResult MMShaderVulkanBindComputeResources(MMVKCommands *, const MMShaderComputeOutput *,
                                              const MMShaderVulkanComputeResources *,
                                              const MMShaderComputeDispatchPlan *, MMVKError *);
MMVKResult MMShaderVulkanPushComputeRegion(MMVKCommands *, const MMShaderComputeOutput *,
                                           const MMShaderComputeRegion *, MMVKError *);

#ifdef __cplusplus
}
#endif
#endif
