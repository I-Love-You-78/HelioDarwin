/*
 * MaliMetal: Command Queue & Buffer Implementation
 */

#import "MaliMetalCommandQueue.h"
#import "MaliMetalDevice.h"

@implementation MaliMetalCommandQueue {
    MaliMetalDevice *_device;
    NSUInteger _maxCount;
    NSString *_label;
}

- (instancetype)initWithDevice:(MaliMetalDevice *)device maxCommandBufferCount:(NSUInteger)count {
    self = [super init];
    if (self) {
        _device = device;
        _maxCount = count;
        _label = (NSString *)0;
    }
    return self;
}

- (id<MTLDevice>)device {
    return (id<MTLDevice>)_device;
}

- (NSString *)label {
    return _label;
}

- (void)setLabel:(NSString *)label {
    _label = label;
}

- (id<MTLCommandBuffer>)commandBuffer {
    return (id<MTLCommandBuffer>)[[MaliMetalCommandBuffer alloc] initWithQueue:self];
}

- (id<MTLCommandBuffer>)commandBufferWithUnretainedReferences {
    return [self commandBuffer];
}

@end

@implementation MaliMetalCommandBuffer {
    MaliMetalCommandQueue *_queue;
    MTLCommandBufferStatus _status;
    NSString *_label;
}

- (instancetype)initWithQueue:(MaliMetalCommandQueue *)queue {
    self = [super init];
    if (self) {
        _queue = queue;
        _status = MTLCommandBufferStatusNotEnqueued;
        _label = (NSString *)0;
    }
    return self;
}

- (id<MTLDevice>)device {
    return _queue.device;
}

- (id<MTLCommandQueue>)commandQueue {
    return (id<MTLCommandQueue>)_queue;
}

- (MTLCommandBufferStatus)status {
    return _status;
}

- (NSError *)error {
    return (NSError *)0;
}

- (NSString *)label {
    return _label;
}

- (void)setLabel:(NSString *)label {
    _label = label;
}

- (void)enqueue {
    _status = MTLCommandBufferStatusEnqueued;
}

- (void)commit {
    _status = MTLCommandBufferStatusCommitted;
    /* Completed signal */
    _status = MTLCommandBufferStatusCompleted;
}

- (void)waitUntilCompleted {
}

- (void)presentDrawable:(id<MTLDrawable>)drawable {
    (void)drawable;
}

- (id<MTLRenderCommandEncoder>)renderCommandEncoderWithDescriptor:(MTLRenderPassDescriptor *)renderPassDescriptor {
    (void)renderPassDescriptor;
    return (id)0;
}

- (id<MTLComputeCommandEncoder>)computeCommandEncoder {
    return (id)0;
}

- (id<MTLBlitCommandEncoder>)blitCommandEncoder {
    return (id)0;
}

@end
