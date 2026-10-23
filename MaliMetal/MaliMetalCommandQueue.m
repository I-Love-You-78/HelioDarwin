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
    id<MTLBuffer> _vertexBuffers[MALI_MAX_VERTEX_BUFFERS];
    id<MTLTexture> _fragmentTextures[MALI_MAX_TEXTURES];
}
- (instancetype)initWithCommandBuffer:(MaliMetalCommandBuffer *)cmdBuffer descriptor:(MTLRenderPassDescriptor *)desc;
@end

@implementation MaliMetalRenderCommandEncoder

- (instancetype)initWithCommandBuffer:(MaliMetalCommandBuffer *)cmdBuffer descriptor:(MTLRenderPassDescriptor *)desc {
    self = [super init];
    if (self) {
        _commandBuffer = [cmdBuffer retain];
        _renderPassDescriptor = [desc copy];
        memset(&_currentCmd, 0, sizeof(_currentCmd));
    }
    return self;
}

- (void)dealloc {
    for (NSUInteger i = 0; i < MALI_MAX_VERTEX_BUFFERS; i++) [(id)_vertexBuffers[i] release];
    for (NSUInteger i = 0; i < MALI_MAX_TEXTURES; i++) [(id)_fragmentTextures[i] release];
    [_renderPassDescriptor release];
    [_commandBuffer release];
    [super dealloc];
}

- (id<MTLDevice>)device { return [_commandBuffer device]; }
- (void)setRenderPipelineState:(id<MTLRenderPipelineState>)pipelineState { (void)pipelineState; }

- (void)setVertexBuffer:(id<MTLBuffer>)buffer offset:(NSUInteger)offset atIndex:(NSUInteger)index {
    if (index < MALI_MAX_VERTEX_BUFFERS) {
        if (offset > [buffer length]) {
            [NSException raise:@"NSInvalidArgumentException" format:@"MaliMetal vertex offset exceeds buffer length"];
            return;
        }
        [(id)buffer retain];
        [(id)_vertexBuffers[index] release];
        _vertexBuffers[index] = buffer;
        _currentCmd.vertexBufferPtr[index] = [buffer contents];
        _currentCmd.vertexBufferOffset[index] = offset;
        _currentCmd.vertexBufferLength[index] = [buffer length];
    }
}

- (void)setFragmentTexture:(id<MTLTexture>)texture atIndex:(NSUInteger)index {
    if (index < MALI_MAX_TEXTURES) {
        [(id)texture retain];
        [(id)_fragmentTextures[index] release];
        _fragmentTextures[index] = texture;
        _currentCmd.fragmentTexturePtr[index] = NULL;
        if ([(id)texture respondsToSelector:@selector(pixels)]) {
            _currentCmd.fragmentTexturePtr[index] = [(id)texture pixels];
        }
        _currentCmd.textureWidth[index] = (uint32_t)[texture width];
        _currentCmd.textureHeight[index] = (uint32_t)[texture height];
        _currentCmd.textureBytesPerRow[index] = (uint32_t)[texture bytesPerRow];
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
    _currentCmd.hasScissorRect = 1;
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
    [_commandBuffer enqueueDrawCommand:&_currentCmd forTexture:targetTex
                        vertexBuffers:_vertexBuffers fragmentTextures:_fragmentTextures];
}

- (void)endEncoding {
    /* Encoding completed */
}

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
        _device = [device retain];
        _maxCount = count;
    }
    return self;
}

- (void)dealloc {
    [_device release];
    [super dealloc];
}

- (id<MTLDevice>)device {
    return _device;
}

- (id<MTLCommandBuffer>)commandBuffer {
    return (id<MTLCommandBuffer>)[[[MaliMetalCommandBuffer alloc] initWithQueue:self] autorelease];
}

- (id<MTLCommandBuffer>)commandBufferWithUnretainedReferences {
    return (id<MTLCommandBuffer>)[[[MaliMetalCommandBuffer alloc] initWithQueue:self retainedReferences:NO] autorelease];
}

@end

/*
 * Command Buffer Implementation
 */
@implementation MaliMetalCommandBuffer {
    MaliMetalCommandQueue *_queue;
    MTLCommandBufferStatus _status;
    BOOL _retainedReferences;
    MaliMetalDrawCommand _drawCommands[MAX_COMMANDS_PER_BUFFER];
    id<MTLTexture> _targetTextures[MAX_COMMANDS_PER_BUFFER];
    id _drawResources[MAX_COMMANDS_PER_BUFFER][MALI_MAX_VERTEX_BUFFERS + MALI_MAX_TEXTURES];
    NSUInteger _drawCommandCount;
    id<MTLDrawable> _drawablesToPresent[MAX_DRAWABLES_PER_BUFFER];
    NSUInteger _drawableCount;
    MTLCommandBufferHandler _scheduledHandlers[MAX_HANDLERS_PER_BUFFER];
    NSUInteger _scheduledHandlerCount;
    MTLCommandBufferHandler _completedHandlers[MAX_HANDLERS_PER_BUFFER];
    NSUInteger _completedHandlerCount;
}

- (instancetype)initWithQueue:(MaliMetalCommandQueue *)queue {
    return [self initWithQueue:queue retainedReferences:YES];
}

- (instancetype)initWithQueue:(MaliMetalCommandQueue *)queue retainedReferences:(BOOL)retainedReferences {
    self = [super init];
    if (self) {
        _queue = [queue retain];
        _retainedReferences = retainedReferences;
        _status = MTLCommandBufferStatusNotEnqueued;
        _drawCommandCount = 0;
        _drawableCount = 0;
        _scheduledHandlerCount = 0;
        _completedHandlerCount = 0;
    }
    return self;
}

- (void)dealloc {
    for (NSUInteger i = 0; i < _drawCommandCount; i++) {
        if (_retainedReferences) {
            [(id)_targetTextures[i] release];
            for (NSUInteger j = 0; j < MALI_MAX_VERTEX_BUFFERS + MALI_MAX_TEXTURES; j++) {
                [_drawResources[i][j] release];
            }
        }
    }
    for (NSUInteger i = 0; i < _drawableCount; i++) {
        [(id)_drawablesToPresent[i] release];
    }
    for (NSUInteger i = 0; i < _scheduledHandlerCount; i++) {
        [_scheduledHandlers[i] release];
    }
    for (NSUInteger i = 0; i < _completedHandlerCount; i++) {
        [_completedHandlers[i] release];
    }
    [_queue release];
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

- (BOOL)retainedReferences { return _retainedReferences; }

- (void)enqueue {
    if (_status != MTLCommandBufferStatusNotEnqueued) {
        [NSException raise:@"NSInternalInconsistencyException" format:@"MaliMetal command buffer was already enqueued"];
        return;
    }
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

- (void)enqueueDrawCommand:(const MaliMetalDrawCommand *)cmd forTexture:(id<MTLTexture>)texture
            vertexBuffers:(id<MTLBuffer> const *)buffers fragmentTextures:(id<MTLTexture> const *)textures {
    if (_drawCommandCount < MAX_COMMANDS_PER_BUFFER) {
        _drawCommands[_drawCommandCount] = *cmd;
        _targetTextures[_drawCommandCount] = texture;
        if (_retainedReferences) {
            [(id)texture retain];
            for (NSUInteger i = 0; i < MALI_MAX_VERTEX_BUFFERS; i++) {
                _drawResources[_drawCommandCount][i] = [(id)buffers[i] retain];
            }
            for (NSUInteger i = 0; i < MALI_MAX_TEXTURES; i++) {
                _drawResources[_drawCommandCount][MALI_MAX_VERTEX_BUFFERS + i] = [(id)textures[i] retain];
            }
        }
        _drawCommandCount++;
    }
}

- (void)executeRecordedCommands {
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

    /* CPU prototype only. A NOP register write is not a GPU job submission. */
    for (NSUInteger i = 0; i < _drawCommandCount; i++) {
        id<MTLTexture> targetTex = _targetTextures[i];
        if (targetTex && [(id)targetTex respondsToSelector:@selector(pixels)]) {
            void *dstPixels = [(id)targetTex pixels];
            uint32_t w = (uint32_t)[targetTex width];
            uint32_t h = (uint32_t)[targetTex height];
            uint32_t stride = (uint32_t)[targetTex bytesPerRow];
            MaliMetalRasterizeCommand(&_drawCommands[i], dstPixels, w, h, stride);
        }
        if (_retainedReferences) {
            [(id)targetTex release];
            for (NSUInteger j = 0; j < MALI_MAX_VERTEX_BUFFERS + MALI_MAX_TEXTURES; j++) {
                [_drawResources[i][j] release];
                _drawResources[i][j] = nil;
            }
        }
        _targetTextures[i] = nil;
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

- (void)commit {
    if (_status >= MTLCommandBufferStatusCommitted) {
        [NSException raise:@"NSInternalInconsistencyException" format:@"MaliMetal command buffers are single-use"];
        return;
    }
    /* A callback may release the caller's last reference while commit runs. */
    [self retain];
    @try {
        [self executeRecordedCommands];
    } @finally {
        [self release];
    }
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
    return (id<MTLRenderCommandEncoder>)[[[MaliMetalRenderCommandEncoder alloc] initWithCommandBuffer:self descriptor:renderPassDescriptor] autorelease];
}

- (id<MTLComputeCommandEncoder>)computeCommandEncoder {
    return nil;
}

- (id<MTLBlitCommandEncoder>)blitCommandEncoder {
    return nil;
}

@end
