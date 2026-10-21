/*
 * MaliMetal: Command Queue & Buffer Implementation
 */

#import "MaliMetalCommandQueue.h"
#import "MaliMetalDevice.h"

@implementation MaliMetalCommandQueue {
    MaliMetalDevice *_device;
    NSUInteger _maxCount;
}

- (instancetype)initWithDevice:(MaliMetalDevice *)device maxCommandBufferCount:(NSUInteger)count {
    self = [super init];
    if (self) {
        _device = device;
        _maxCount = count;
    }
    return self;
}

- (id<MTLDevice>)device {
    return _device;
}

- (id<MTLCommandBuffer>)commandBuffer {
    return [[MaliMetalCommandBuffer alloc] initWithQueue:self];
}

- (id<MTLCommandBuffer>)commandBufferWithUnretainedReferences {
    return [self commandBuffer];
}

@end

@implementation MaliMetalCommandBuffer {
    MaliMetalCommandQueue *_queue;
    MTLCommandBufferStatus _status;
}

- (instancetype)initWithQueue:(MaliMetalCommandQueue *)queue {
    self = [super init];
    if (self) {
        _queue = queue;
        _status = MTLCommandBufferStatusNotEnqueued;
    }
    return self;
}

- (id<MTLDevice>)device {
    return _queue.device;
}

- (id<MTLCommandQueue>)commandQueue {
    return _queue;
}

- (MTLCommandBufferStatus)status {
    return _status;
}

- (NSError *)error {
    return nil;
}

- (void)enqueue {
    _status = MTLCommandBufferStatusEnqueued;
}

- (void)commit {
    _status = MTLCommandBufferStatusCommitted;
    /* Dispatch command buffer to Mesa panvk / Vulkan command queue */
    _status = MTLCommandBufferStatusCompleted;
}

- (void)waitUntilCompleted {
    /* Wait on Vulkan fence / GPU interrupt */
}

- (void)presentDrawable:(id<MTLDrawable>)drawable {
    /* Zero-copy flip into AppleCLCD framebuffer */
}

- (id<MTLRenderCommandEncoder>)renderCommandEncoderWithDescriptor:(MTLRenderPassDescriptor *)renderPassDescriptor {
    return nil;
}

- (id<MTLComputeCommandEncoder>)computeCommandEncoder {
    return nil;
}

- (id<MTLBlitCommandEncoder>)blitCommandEncoder {
    return nil;
}

@end
