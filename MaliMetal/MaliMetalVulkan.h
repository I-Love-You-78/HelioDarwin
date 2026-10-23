/* Metal/Vulkan value conversion. No CPU renderer or implicit vertex layout. */
#ifndef MALI_METAL_VULKAN_H
#define MALI_METAL_VULKAN_H
#include "MaliMetalTypes.h"
#include "Vulkan/MaliVulkanBackend.h"
#ifdef __cplusplus
extern "C" {
#endif
MMVKFormat MaliMetalPixelFormat(MTLPixelFormat format);
int MaliMetalVertexFormat(NSUInteger format, uint32_t *vkFormat);
int MaliMetalBlendFactor(NSUInteger factor, uint32_t *vkFactor);
int MaliMetalBlendOperation(NSUInteger operation, uint32_t *vkOperation);
#ifdef __cplusplus
}
#endif
#ifdef __OBJC__
struct MMShaderRenderOutput;
struct MMShaderVulkanConstants;
struct MMShaderComputeOutput;
struct MMShaderComputeRegion;
@interface NSObject (MaliMetalBackendResource)
- (MMVKBuffer *)maliBuffer;
- (MMVKImage *)maliImage;
- (MMVKSampler *)maliSampler;
- (MMVKPipeline *)maliPipeline;
- (MMVKComputeProgram *)maliComputeProgram;
- (const struct MMShaderComputeOutput *)maliComputeOutput;
- (MMVKBuffer *const *)maliComputeConstants;
- (MMVKPipeline *)maliComputeVariantForRegion:(const struct MMShaderComputeRegion *)region
                                        error:(MMVKError *)error;
- (MTLPixelFormat)maliColorFormat;
- (const struct MMShaderRenderOutput *)maliShaderOutput;
- (const struct MMShaderVulkanConstants *)maliShaderConstants;
- (BOOL)maliPrepareImage:(BOOL)preserve error:(MMVKError *)error;
- (BOOL)maliReadbackImage:(MMVKError *)error;
@end
NSError *MaliMetalMakeError(NSInteger code, const char *message);
#endif
#endif
