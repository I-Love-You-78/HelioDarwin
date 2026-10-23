/*
 * MaliMetal: Command Queue, Buffer & Encoders Implementation
 */

#import "MaliMetalCommandQueue.h"
#import "MaliMetalDevice.h"
#import "MaliMetalVulkan.h"
#include <string.h>

#define MAX_COMMANDS_PER_BUFFER 512
#define MAX_DRAWABLES_PER_BUFFER 16
#define MAX_HANDLERS_PER_BUFFER 16

/*
 * Render Command Encoder
 */
@interface MaliMetalRenderCommandEncoder : MaliMetalObject <MTLRenderCommandEncoder> {
    MaliMetalCommandBuffer *_commandBuffer;
    MTLRenderPassDescriptor *_renderPassDescriptor;
    MaliMetalDrawCommand _currentCmd;
}
- (instancetype)initWithCommandBuffer:(MaliMetalCommandBuffer *)cmdBuffer descriptor:(MTLRenderPassDescriptor *)desc;
@end

@implementation MaliMetalRenderCommandEncoder

- (instancetype)initWithCommandBuffer:(MaliMetalCommandBuffer *)cmdBuffer descriptor:(MTLRenderPassDescriptor *)desc {
    self = [super init];
    if (self) {
        _commandBuffer = cmdBuffer;
        _renderPassDescriptor = desc;
        memset(&_currentCmd, 0, sizeof(_currentCmd));
    }
    return self;
}

- (void)dealloc {
    [super dealloc];
}

- (id<MTLDevice>)device { return [_commandBuffer device]; }
- (void)setRenderPipelineState:(id<MTLRenderPipelineState>)pipelineState { (void)pipelineState; }

- (void)setVertexBuffer:(id<MTLBuffer>)buffer offset:(NSUInteger)offset atIndex:(NSUInteger)index {
    if (index < MALI_MAX_VERTEX_BUFFERS) {
        _currentCmd.vertexBufferPtr[index] = [buffer contents];
        _currentCmd.vertexBufferOffset[index] = offset;
    }
}

- (void)setFragmentTexture:(id<MTLTexture>)texture atIndex:(NSUInteger)index {
    if (index < MALI_MAX_TEXTURES) {
        if ([(id)texture respondsToSelector:@selector(pixels)]) {
            _currentCmd.fragmentTexturePtr[index] = [(id)texture pixels];
        }
        _currentCmd.textureWidth[index] = (uint32_t)[texture width];
        _currentCmd.textureHeight[index] = (uint32_t)[texture height];
    }
}

- (void)setFragmentBuffer:(id<MTLBuffer>)buffer offset:(NSUInteger)offset atIndex:(NSUInteger)index {
    (void)buffer; (void)offset; (void)index;
}

- (void)setViewport:(MTLViewport)viewport {
    _currentCmd.viewport = viewport;
}

- (void)setScissorRect:(MTLScissorRect)rect {
    _currentCmd.scissorRect = rect;
}

- (void)drawPrimitives:(NSUInteger)primitiveType vertexStart:(NSUInteger)vertexStart vertexCount:(NSUInteger)vertexCount {
    _currentCmd.primitiveType = primitiveType;
    _currentCmd.vertexStart = vertexStart;
    _currentCmd.vertexCount = vertexCount;

    id<MTLTexture> targetTex = (id<MTLTexture>)0;
    if (_renderPassDescriptor) {
        MTLRenderPassColorAttachmentDescriptorArray *arr = [_renderPassDescriptor colorAttachments];
        if (arr) {
            MTLRenderPassColorAttachmentDescriptor *ca = [arr objectAtIndexedSubscript:0];
            if (ca) {
                targetTex = [ca texture];
            }
        }
    }
    [_commandBuffer enqueueDrawCommand:&_currentCmd forTexture:targetTex];
}

- (void)endEncoding {
    /* Encoding completed */
}

@end

/*
 * Blit Command Encoder
 */
@interface MaliMetalBlitCommandEncoder : MaliMetalObject <MTLBlitCommandEncoder>
@end

@implementation MaliMetalBlitCommandEncoder
- (id<MTLDevice>)device { return (id<MTLDevice>)0; }
- (void)endEncoding {}
@end

/*
 * Compute Command Encoder
 */
@interface MaliMetalComputeCommandEncoder : MaliMetalObject <MTLComputeCommandEncoder>
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
    MaliMetalDrawCommand _drawCommands[MAX_COMMANDS_PER_BUFFER];
    id<MTLTexture> _targetTextures[MAX_COMMANDS_PER_BUFFER];
    NSUInteger _drawCommandCount;
    id<MTLDrawable> _drawablesToPresent[MAX_DRAWABLES_PER_BUFFER];
    NSUInteger _drawableCount;
    MTLCommandBufferHandler _scheduledHandlers[MAX_HANDLERS_PER_BUFFER];
    NSUInteger _scheduledHandlerCount;
    MTLCommandBufferHandler _completedHandlers[MAX_HANDLERS_PER_BUFFER];
    NSUInteger _completedHandlerCount;
}

- (instancetype)initWithQueue:(MaliMetalCommandQueue *)queue {
    self = [super init];
    if (self) {
        _queue = queue;
        _status = MTLCommandBufferStatusNotEnqueued;
        _drawCommandCount = 0;
        _drawableCount = 0;
        _scheduledHandlerCount = 0;
        _completedHandlerCount = 0;
    }
    return self;
}

- (void)dealloc {
    for (NSUInteger i = 0; i < _drawableCount; i++) {
        [(id)_drawablesToPresent[i] release];
    }
    for (NSUInteger i = 0; i < _scheduledHandlerCount; i++) {
        [_scheduledHandlers[i] release];
    }
    for (NSUInteger i = 0; i < _completedHandlerCount; i++) {
        [_completedHandlers[i] release];
    }
    [super dealloc];
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

- (void)enqueue {
    _status = MTLCommandBufferStatusEnqueued;
}

- (void)addScheduledHandler:(MTLCommandBufferHandler)block {
    if (block && _scheduledHandlerCount < MAX_HANDLERS_PER_BUFFER) {
        _scheduledHandlers[_scheduledHandlerCount++] = [block copy];
    }
}

- (void)addCompletedHandler:(MTLCommandBufferHandler)block {
    if (block && _completedHandlerCount < MAX_HANDLERS_PER_BUFFER) {
        _completedHandlers[_completedHandlerCount++] = [block copy];
    }
}

- (void)enqueueDrawCommand:(const MaliMetalDrawCommand *)cmd forTexture:(id<MTLTexture>)texture {
    if (_drawCommandCount < MAX_COMMANDS_PER_BUFFER) {
        _drawCommands[_drawCommandCount] = *cmd;
        _targetTextures[_drawCommandCount] = texture;
        _drawCommandCount++;
    }
}

- (void)commit {
    _status = MTLCommandBufferStatusCommitted;

    /* Execute scheduled handlers */
    _status = MTLCommandBufferStatusScheduled;
    for (NSUInteger i = 0; i < _scheduledHandlerCount; i++) {
        if (_scheduledHandlers[i]) {
            _scheduledHandlers[i](self);
            [_scheduledHandlers[i] release];
            _scheduledHandlers[i] = nil;
        }
    }
    _scheduledHandlerCount = 0;

    MaliMetalDevice *dev = (MaliMetalDevice *)[_queue device];
    volatile uint32_t *hw = [dev respondsToSelector:@selector(hardwareRegisters)] ? [dev hardwareRegisters] : NULL;
    uint32_t gpuID = [dev respondsToSelector:@selector(gpuID)] ? [dev gpuID] : 0;

    if (hw != NULL && gpuID != 0 && gpuID != ~0u) {
        /*
         * ARM Mali-G57 Valhall hardware job submission:
         * Hardware bus is alive and responsive.
         */
    }

    /* Execute all recorded draw commands */
    for (NSUInteger i = 0; i < _drawCommandCount; i++) {
        id<MTLTexture> targetTex = _targetTextures[i];
        if (targetTex && [(id)targetTex respondsToSelector:@selector(pixels)]) {
            void *dstPixels = [(id)targetTex pixels];
            uint32_t w = (uint32_t)[targetTex width];
            uint32_t h = (uint32_t)[targetTex height];
            uint32_t stride = w * 4;
            MaliMetalRasterizeCommand(&_drawCommands[i], dstPixels, w, h, stride);
        }
    }

    /* Present all queued drawables (triggers CAMetalDrawable layer_private_present -> CATransaction commit) */
    for (NSUInteger i = 0; i < _drawableCount; i++) {
        id d = (id)_drawablesToPresent[i];
        if (d && [d respondsToSelector:@selector(present)]) {
            [d present];
        }
        [d release];
        _drawablesToPresent[i] = (id<MTLDrawable>)0;
    }
    _drawableCount = 0;
    _drawCommandCount = 0;

    /* Mark completed and fire completed callbacks */
    _status = MTLCommandBufferStatusCompleted;
    for (NSUInteger i = 0; i < _completedHandlerCount; i++) {
        if (_completedHandlers[i]) {
            _completedHandlers[i](self);
            [_completedHandlers[i] release];
            _completedHandlers[i] = nil;
        }
    }
    _completedHandlerCount = 0;
}

- (void)waitUntilScheduled {
}

- (void)waitUntilCompleted {
}

- (void)presentDrawable:(id<MTLDrawable>)drawable {
    if (drawable && _drawableCount < MAX_DRAWABLES_PER_BUFFER) {
        _drawablesToPresent[_drawableCount++] = (id<MTLDrawable>)[(id)drawable retain];
    }
}

- (void)presentDrawable:(id<MTLDrawable>)drawable atTime:(CFTimeInterval)presentationTime {
    (void)presentationTime;
    [self presentDrawable:drawable];
}

- (id<MTLRenderCommandEncoder>)renderCommandEncoderWithDescriptor:(MTLRenderPassDescriptor *)renderPassDescriptor {
    return (id<MTLRenderCommandEncoder>)[[MaliMetalRenderCommandEncoder alloc] initWithCommandBuffer:self descriptor:renderPassDescriptor];
}

- (id<MTLComputeCommandEncoder>)computeCommandEncoder {
    return (id<MTLComputeCommandEncoder>)[[MaliMetalComputeCommandEncoder alloc] init];
}

- (id<MTLBlitCommandEncoder>)blitCommandEncoder {
    return (id<MTLBlitCommandEncoder>)[[MaliMetalBlitCommandEncoder alloc] init];
}

@end
