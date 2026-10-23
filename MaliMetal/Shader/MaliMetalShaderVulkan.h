#ifndef MALI_METAL_SHADER_VULKAN_H
#define MALI_METAL_SHADER_VULKAN_H
#include "../Vulkan/MaliVulkanBackend.h"
#include "MaliMetalShader.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MMShaderVulkanDescription {
    MMVKRenderPipelineDescriptor descriptor;
    MMVKBindingLayout bindings[MM_SHADER_MAX_BINDINGS];
    MMVKVertexBinding vertex_bindings[31];
    MMVKVertexAttribute vertex_attributes[31];
} MMShaderVulkanDescription;

/* Views in descriptor refer to this description and the retained shader output.
 * The requested state is checked against compiler facts; nothing is inferred
 * from Metal slot numbers or a CPU vertex structure. */
MMVKResult MMShaderVulkanDescribe(const MMShaderRenderOutput *output,
                                  const MMVKRenderPipelineDescriptor *requested,
                                  MMShaderVulkanDescription *description, MMVKError *error);

typedef struct MMShaderVulkanBuffer {
    MMVKBuffer *buffer;
    uint64_t offset;
} MMShaderVulkanBuffer;

typedef struct MMVKSampler MMVKSampler;
typedef struct MMShaderVulkanBindings {
    MMShaderVulkanBuffer vertex_buffers[31], fragment_buffers[31];
    MMVKImage *vertex_textures[128], *fragment_textures[128];
    MMVKSampler *vertex_samplers[16], *fragment_samplers[16];
    MMVKBuffer *constant_buffers[MM_SHADER_MAX_BINDINGS];
} MMShaderVulkanBindings;

typedef struct MMShaderVulkanConstants {
    MMVKBuffer *buffers[MM_SHADER_MAX_BINDINGS];
} MMShaderVulkanConstants;
/* Create once with the pipeline; retain until all pipeline commands complete.
 * Buffers contain the exact compiler data with zeroed 16-byte tail padding. */
MMVKResult MMShaderVulkanCreateConstants(MMVKDevice *device, const MMShaderRenderOutput *output,
                                         MMShaderVulkanConstants *constants, MMVKError *error);
void MMShaderVulkanDestroyConstants(MMShaderVulkanConstants *constants);

/* Sampled resources initially support linear RGBA/BGRA UNORM 2D textures and
 * normalized, ordinary nearest/linear samplers. The backend's sampler creation
 * enforces these sampler constraints; shader variants remain explicit refusals. */
MMVKResult MMShaderVulkanBindResources(MMVKCommands *commands, const MMShaderRenderOutput *output,
                                       const MMShaderVulkanBindings *bindings, MMVKError *error);

/* The caller retains the shader output and all buffers until GPU completion.
 * This first execution path requires shader argument offsets aligned to the
 * actual device's descriptor alignment and 16 bytes. It binds proven buffer
 * windows, fixed vertex inputs and exact push constants. Unaligned offsets
 * need range variants and are refused. Vertex-stream offsets remain unrestricted. */
MMVKResult MMShaderVulkanBindBuffers(MMVKCommands *commands, const MMShaderRenderOutput *output,
                                     const MMShaderVulkanBuffer vertex_buffers[31],
                                     const MMShaderVulkanBuffer fragment_buffers[31],
                                     MMVKError *error);

#ifdef __cplusplus
}
#endif
#endif
