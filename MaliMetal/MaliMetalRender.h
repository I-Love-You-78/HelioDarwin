/* SPDX-License-Identifier: MIT */
#ifndef MALI_METAL_RENDER_OBJECTS_H
#define MALI_METAL_RENDER_OBJECTS_H
#import "MaliMetalTypes.h"
#include "Shader/MaliMetalRenderVulkan.h"
@class MaliMetalDevice;
@interface MaliMetalRenderProgram : NSObject {
    MaliMetalDevice *_device;
    MMShaderVulkanRenderCache *_cache;
}
- (instancetype)initWithDevice:(MaliMetalDevice *)device error:(MMVKError *)error;
- (MMShaderVulkanRenderCache *)cache;
@end
@interface MaliMetalRenderDraw : NSObject {
    id _pipeline;
    MaliMetalRenderProgram *_program;
    MMShaderVulkanRenderExecution *_execution;
}
- (instancetype)initWithPipeline:(id)pipeline program:(MaliMetalRenderProgram *)program
                       resources:(const MMShaderVulkanBindings *)resources
                        bindings:(const MMShaderRenderBindings *)bindings
                         request:(const MMShaderRenderDrawRequest *)request
                           state:(const MMVKGraphicsState *)state
                            pass:(const MMVKPassLayout *)pass error:(MMVKError *)error;
- (MMVKResult)record:(MMVKCommands *)commands indices:(MMVKBuffer *)indices error:(MMVKError *)error;
@end
#endif
