/*
 * MaliMetal: Metal Command Queue & Buffer Interfaces
 */

#pragma once

#import "MaliMetalTypes.h"
#import "MaliMetalVulkan.h"

@class MaliMetalDevice;

@interface MaliMetalCommandQueue : MaliMetalObject <MTLCommandQueue>

@property (readonly) id<MTLDevice> device;

- (instancetype)initWithDevice:(MaliMetalDevice *)device maxCommandBufferCount:(NSUInteger)count;
- (id<MTLCommandBuffer>)commandBuffer;
- (id<MTLCommandBuffer>)commandBufferWithUnretainedReferences;

@end

@interface MaliMetalCommandBuffer : MaliMetalObject <MTLCommandBuffer>

@property (readonly) id<MTLDevice> device;
@property (readonly) id<MTLCommandQueue> commandQueue;
@property (readonly) MTLCommandBufferStatus status;
@property (readonly) NSError *error;

- (instancetype)initWithQueue:(MaliMetalCommandQueue *)queue;
- (void)enqueue;
- (void)commit;
- (void)waitUntilCompleted;
- (void)waitUntilScheduled;
- (void)presentDrawable:(id<MTLDrawable>)drawable;
- (void)presentDrawable:(id<MTLDrawable>)drawable atTime:(CFTimeInterval)presentationTime;

- (void)addScheduledHandler:(MTLCommandBufferHandler)block;
- (void)addCompletedHandler:(MTLCommandBufferHandler)block;

- (id<MTLRenderCommandEncoder>)renderCommandEncoderWithDescriptor:(MTLRenderPassDescriptor *)renderPassDescriptor;
- (id<MTLComputeCommandEncoder>)computeCommandEncoder;
- (id<MTLBlitCommandEncoder>)blitCommandEncoder;

/* Internal: Enqueue a draw command from an encoder */
- (void)enqueueDrawCommand:(const MaliMetalDrawCommand *)cmd forTexture:(id<MTLTexture>)texture;

@end
