/*
 * MaliMetal: Command Queue, Buffer & Encoders Implementation
 */

#import "MaliMetalCommandQueue.h"
#import "MaliMetalDevice.h"

/*
 * Render Command Encoder
 */
@interface MaliMetalRenderCommandEncoder : NSObject <MTLRenderCommandEncoder> {
    MaliMetalCommandBuffer *_commandBuffer;
}
- (instancetype)initWithCommandBuffer:(MaliMetalCommandBuffer *)cmdBuffer;
@end

@implementation MaliMetalRenderCommandEncoder

- (instancetype)initWithCommandBuffer:(MaliMetalCommandBuffer *)cmdBuffer {
    self = [super init];
    if (self) {
        _commandBuffer = cmdBuffer;
    }
    return self;
}

- (id<MTLDevice>)device { return [_commandBuffer device]; }
- (NSString *)label { return (NSString *)0; }
- (void)setLabel:(NSString *)label { (void)label; }
- (void)setRenderPipelineState:(id<MTLRenderPipelineState>)pipelineState { (void)pipelineState; }
- (void)setVertexBuffer:(id<MTLBuffer>)buffer offset:(NSUInteger)offset atIndex:(NSUInteger)index { (void)buffer; (void)offset; (void)index; }
- (void)setFragmentTexture:(id<MTLTexture>)texture atIndex:(NSUInteger)index { (void)texture; (void)index; }
- (void)setFragmentBuffer:(id<MTLBuffer>)buffer offset:(NSUInteger)offset atIndex:(NSUInteger)index { (void)buffer; (void)offset; (void)index; }
- (void)setViewport:(MTLViewport)viewport { (void)viewport; }
- (void)setScissorRect:(MTLScissorRect)rect { (void)rect; }
- (void)drawPrimitives:(NSUInteger)primitiveType vertexStart:(NSUInteger)vertexStart vertexCount:(NSUInteger)vertexCount {
    (void)primitiveType;
    (void)vertexStart;
    (void)vertexCount;
}
- (void)endEncoding {}

@end

/*
 * Blit Command Encoder
 */
@interface MaliMetalBlitCommandEncoder : NSObject <MTLBlitCommandEncoder>
@end

@implementation MaliMetalBlitCommandEncoder
- (id<MTLDevice>)device { return (id<MTLDevice>)0; }
- (void)endEncoding {}
@end

/*
 * Compute Command Encoder
 */
@interface MaliMetalComputeCommandEncoder : NSObject <MTLComputeCommandEncoder>
@end

@implementation MaliMetalComputeCommandEncoder
- (id<MTLDevice>)device { return (id<MTLDevice>)0; }
- (void)setComputePipelineState:(id<MTLComputePipelineState>)state { (void)state; }
- (void)endEncoding {}
@end

/*
 * Command Queue Implementation
 */
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

/*
 * Command Buffer Implementation
 */
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
    return (id<MTLRenderCommandEncoder>)[[MaliMetalRenderCommandEncoder alloc] initWithCommandBuffer:self];
}

- (id<MTLComputeCommandEncoder>)computeCommandEncoder {
    return (id<MTLComputeCommandEncoder>)[[MaliMetalComputeCommandEncoder alloc] init];
}

- (id<MTLBlitCommandEncoder>)blitCommandEncoder {
    return (id<MTLBlitCommandEncoder>)[[MaliMetalBlitCommandEncoder alloc] init];
}

@end
