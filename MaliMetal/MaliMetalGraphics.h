/* SPDX-License-Identifier: MIT */
#ifndef MALI_METAL_GRAPHICS_H
#define MALI_METAL_GRAPHICS_H
#import "MaliMetalTypes.h"
#include "Vulkan/MaliVulkanGraphics.h"

/* Captures copied values; the command buffer must retain every non-nil texture
 * before recording. Preparation of CPU/IOSurface contents remains its job. */
typedef struct {
    MMVKRenderPassDescriptor native;
    id colors[8],resolves[8],depth_stencil,depth_stencil_resolve;
    BOOL preserve_color[8],preserve_depth,preserve_stencil,preserve_depth_stencil_resolve;
} MaliMetalGraphicsPass;
@interface MaliMetalDepthStencilState : NSObject {
    id _device;
    NSString *_label;
    MMVKGraphicsState _state;
}
- (instancetype)initWithDevice:(id)device descriptor:(id)descriptor error:(MMVKError *)error;
- (id)device;
- (NSString *)label;
- (void)setLabel:(NSString *)label;
- (const MMVKGraphicsState *)maliGraphicsDepthStencilState;
@end
MMVKResult MaliMetalGraphicsTopology(NSUInteger primitive,uint32_t *topology,MMVKError *);
MMVKResult MaliMetalGraphicsCullMode(NSUInteger mode,MMVKGraphicsState *,MMVKError *);
MMVKResult MaliMetalGraphicsWinding(NSUInteger winding,MMVKGraphicsState *,MMVKError *);
MMVKResult MaliMetalGraphicsFillMode(NSUInteger mode,MMVKGraphicsState *,MMVKError *);
MMVKResult MaliMetalGraphicsDepthClipMode(NSUInteger mode,MMVKGraphicsState *,MMVKError *);
MMVKResult MaliMetalGraphicsApplyDepthStencil(id state,id device,MMVKGraphicsState *,MMVKError *);
MMVKResult MaliMetalGraphicsPipelineDescription(id descriptor,MMVKGraphicsState *,MMVKPassLayout *,MMVKError *);
MMVKResult MaliMetalGraphicsCapturePass(id descriptor,MaliMetalGraphicsPass *,MMVKError *);
#endif
