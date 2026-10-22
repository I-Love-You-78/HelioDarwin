/*
 * MaliMetal: Metal Command Queue & Buffer Interfaces
 */

#pragma once

#import "MaliMetalTypes.h"
#import "MaliMetalVulkan.h"

@class MaliMetalDevice;

@interface MaliMetalCommandQueue : NSObject <MTLCommandQueue>

@property (readonly) id<MTLDevice> device;
@property (copy) NSString *label;

- (instancetype)initWithDevice:(MaliMetalDevice *)device maxCommandBufferCount:(NSUInteger)count;
- (id<MTLCommandBuffer>)commandBuffer;
- (id<MTLCommandBuffer>)commandBufferWithUnretainedReferences;

@end

@interface MaliMetalCommandBuffer : NSObject <MTLCommandBuffer>

@property (readonly) id<MTLDevice> device;
@property (readonly) id<MTLCommandQueue> commandQueue;
@property (readonly) MTLCommandBufferStatus status;
@property (readonly) NSError *error;
@property (copy) NSString *label;

- (instancetype)initWithQueue:(MaliMetalCommandQueue *)queue;
- (void)enqueue;
- (void)commit;
- (void)waitUntilCompleted;
- (void)presentDrawable:(id<MTLDrawable>)drawable;

- (id<MTLRenderCommandEncoder>)renderCommandEncoderWithDescriptor:(MTLRenderPassDescriptor *)renderPassDescriptor;
- (id<MTLComputeCommandEncoder>)computeCommandEncoder;
- (id<MTLBlitCommandEncoder>)blitCommandEncoder;

/* Internal: Enqueue a draw command from an encoder */
- (void)enqueueDrawCommand:(const MaliMetalDrawCommand *)cmd forTexture:(id<MTLTexture>)texture;

@end
