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
    uint32_t metal_pixel_formats[128]; /* From actual MTLTexture.pixelFormat. */
    MMShaderComputeSampler sampler_facts[16]; /* Preserve the original Metal descriptor. */
    uint32_t stage_in_region_bound, stage_in_origin[3], stage_in_size[3], imageblock_size[2];
    MMVKBuffer *indirect_buffer;
    uint64_t indirect_length;
    /* Dispatch-owned resources indexed by the settled binding layout. */
    MMVKBuffer *dispatch_buffers[MM_SHADER_MAX_BINDINGS];
    MMVKImage *dispatch_images[MM_SHADER_MAX_BINDINGS];
    MMVKSampler *dispatch_samplers[MM_SHADER_MAX_BINDINGS];
} MMShaderVulkanComputeResources;

typedef struct MMShaderVulkanComputeExecution MMShaderVulkanComputeExecution;
/* Creates native pipelines from the plan's settled module, exact alias views,
 * translated samplers, immutable constant blobs and instantiated table uploads.
 * Owns them until destroy; caller keeps plan and Metal resources alive through
 * proven native retirement. Completion must be checked before Metal success.
 * A generic DEVICE_LOST or transport/wait failure is NOT retirement proof:
 * quarantine the entire command buffer, plan, device and resource owners while
 * retirement is unknown. Destroy is allowed after proven retirement, or before
 * any submission could own this execution. Destroy execution before its plan. */
MMVKResult MMShaderVulkanCreateComputeExecution(MMVKDevice *, const MMShaderComputeOutput *,
    const MMShaderVulkanComputeResources *, const MMShaderComputeDispatchPlan *,
    MMShaderVulkanComputeExecution **, MMVKError *);
MMVKResult MMShaderVulkanRecordComputeExecution(MMVKCommands *, MMShaderVulkanComputeExecution *, MMVKError *);
MMVKResult MMShaderVulkanCompleteComputeExecution(MMShaderVulkanComputeExecution *, MMVKError *);
void MMShaderVulkanDestroyComputeExecution(MMShaderVulkanComputeExecution *);
/* A conservative Metal compute memoryBarrier: recorded at the requested point. */
MMVKResult MMShaderVulkanComputeMemoryBarrier(MMVKCommands *, MMVKError *);

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
