/* Native command recording. Completion is derived from the Vulkan fence. */
#import "MaliMetalCommandQueue.h"
#import "MaliMetalDevice.h"
#import "Shader/MaliMetalShaderVulkan.h"
#import "Shader/MaliMetalComputeVulkan.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include <time.h>
#include <float.h>

extern NSString *const MTLCommandBufferErrorDomain;

static NSError *MaliMetalCommandError(MMVKResult result, const char *message) {
    /* Keep the public Metal command-error domain and preserve the native
     * backend status/message as the underlying driver error. */
    NSInteger code = result == MMVK_ERROR_MEMORY ? 8 : 1; /* OutOfMemory / Internal. */
    /* A Vulkan wait deadline or device loss does not establish Metal's
     * terminated-job or physically-removed-device conditions. Preserve that
     * distinction in the underlying error instead of guessing a public cause. */
    const id keys[] = {@"NSLocalizedDescription", @"NSUnderlyingError"};
    const id values[] = {[NSString stringWithUTF8String:message],
                         MaliMetalMakeError(result, message)};
    return [NSError errorWithDomain:MTLCommandBufferErrorDomain
                               code:code
                           userInfo:[NSDictionary dictionaryWithObjects:values
                                                                forKeys:keys
                                                                  count:2]];
}

@interface MaliMetalCommandQueue ()
- (void)runBuffer:(MaliMetalCommandBuffer *)buffer;
- (void)enqueueBuffer:(MaliMetalCommandBuffer *)buffer;
- (void)drainReadyBuffers;
@end
@interface MaliMetalCommandBuffer ()
- (void)execute;
- (void)notifyCompleted;
- (void)requireEncoding;
@end

@interface MaliMetalRenderCommandEncoder : MaliMetalObject <MTLRenderCommandEncoder> {
    MaliMetalCommandBuffer *_buffer;
    BOOL _ended;
    id _pipeline;
    MTLPixelFormat _colorFormats[8];
    id<MTLTexture> _targetTextures[8];
    id<MTLTexture> _vertexTextures[128], _fragmentTextures[128];
    id<MTLSamplerState> _vertexSamplers[16], _fragmentSamplers[16];
    id<MTLBuffer> _vertexBuffers[31];
    NSUInteger _vertexOffsets[31];
    id<MTLBuffer> _fragmentBuffers[31];
    NSUInteger _fragmentOffsets[31];
}
- (instancetype)initWithBuffer:(MaliMetalCommandBuffer *)buffer
                    descriptor:(MTLRenderPassDescriptor *)descriptor;
@end
@interface MaliMetalBlitCommandEncoder : MaliMetalObject <MTLBlitCommandEncoder> {
    MaliMetalCommandBuffer *_buffer;
    BOOL _ended;
}
- (instancetype)initWithBuffer:(MaliMetalCommandBuffer *)buffer;
@end
@interface MaliMetalComputeCommandEncoder : MaliMetalObject <MTLComputeCommandEncoder> {
    MaliMetalCommandBuffer *_buffer;
    id _pipeline;
    BOOL _ended;
    id<MTLBuffer> _buffers[31];
    NSUInteger _offsets[31];
    id<MTLTexture> _textures[128];
    id<MTLSamplerState> _samplers[16];
    uint32_t _threadgroupLengths[31];
}
- (instancetype)initWithBuffer:(MaliMetalCommandBuffer *)buffer;
@end
@interface MaliMetalComputeDispatchRecord : NSObject {
    MMShaderComputeDispatchPlan _plan;
}
- (instancetype)initWithPlan:(MMShaderComputeDispatchPlan *)plan;
- (const MMShaderComputeDispatchPlan *)plan;
@end
@implementation MaliMetalComputeDispatchRecord
- (instancetype)initWithPlan:(MMShaderComputeDispatchPlan *)plan {
    self = [super init];
    if (self) {
        _plan = *plan;
        memset(plan, 0, sizeof(*plan));
    }
    return self;
}
- (const MMShaderComputeDispatchPlan *)plan {
    return &_plan;
}
- (void)dealloc {
    MMShaderComputeDispatchPlanDestroy(&_plan);
    [super dealloc];
}
@end

@implementation MaliMetalCommandQueue {
    MaliMetalDevice *_device;
    NSUInteger _maxCount, _count;
    pthread_mutex_t _lock;
    pthread_cond_t _available;
    dispatch_queue_t _submissionQueue, _completionQueue;
    MaliMetalCommandBuffer **_pending;
    NSUInteger _pendingCount;
}
- (instancetype)initWithDevice:(MaliMetalDevice *)device maxCommandBufferCount:(NSUInteger)count {
    self = [super init];
    if (!self)
        return nil;
    pthread_mutex_init(&_lock, NULL);
    pthread_cond_init(&_available, NULL);
    if (!device || !count) {
        [self release];
        return nil;
    }
    _device = [device retain];
    _maxCount = count;
    _submissionQueue = dispatch_queue_create("org.heliodarwin.MaliMetal.submit", NULL);
    _completionQueue = dispatch_queue_create("org.heliodarwin.MaliMetal.complete", NULL);
    if (!_submissionQueue || !_completionQueue) {
        [self release];
        return nil;
    }
    return self;
}
- (void)dealloc {
    if (_submissionQueue)
        dispatch_release(_submissionQueue);
    if (_completionQueue)
        dispatch_release(_completionQueue);
    free(_pending);
    pthread_cond_destroy(&_available);
    pthread_mutex_destroy(&_lock);
    [_device release];
    [super dealloc];
}
- (id<MTLDevice>)device {
    return _device;
}
- (void)reserveCommandBuffer {
    pthread_mutex_lock(&_lock);
    while (_count >= _maxCount)
        pthread_cond_wait(&_available, &_lock);
    ++_count;
    pthread_mutex_unlock(&_lock);
}
- (void)releaseCommandBuffer {
    pthread_mutex_lock(&_lock);
    if (_count)
        --_count;
    pthread_cond_signal(&_available);
    pthread_mutex_unlock(&_lock);
}
/* These private Metal setters return BOOL. The single native Vulkan queue
 * has no enabled GPU-priority extension, so report refusal without claiming
 * to have changed its scheduling priority. QuartzCore accepts this result. */
- (BOOL)setGPUPriority:(NSUInteger)priority {
    (void)priority;
    return NO;
}
- (BOOL)setBackgroundGPUPriority:(NSUInteger)priority {
    (void)priority;
    return NO;
}
- (void)setSubmissionQueue:(dispatch_queue_t)queue {
    if (!queue) {
        [NSException raise:@"NSInvalidArgumentException"
                    format:@"Submission queue must not be nil"];
        return;
    }
    /* Keep an internal serial queue, even when the caller supplies a concurrent
     * target. Target queues affect scheduling without changing GPU FIFO order. */
    dispatch_set_target_queue(_submissionQueue, queue);
}
- (void)setCompletionQueue:(dispatch_queue_t)queue {
    if (!queue) {
        [NSException raise:@"NSInvalidArgumentException"
                    format:@"Completion queue must not be nil"];
        return;
    }
    dispatch_set_target_queue(_completionQueue, queue);
}
- (dispatch_queue_t)submissionQueue {
    return _submissionQueue;
}
- (dispatch_queue_t)completionQueue {
    return _completionQueue;
}
- (id<MTLCommandBuffer>)commandBuffer {
    return (
        id)[[[MaliMetalCommandBuffer alloc] initWithQueue:self retainedReferences:YES] autorelease];
}
- (id<MTLCommandBuffer>)commandBufferWithUnretainedReferences {
    return (
        id)[[[MaliMetalCommandBuffer alloc] initWithQueue:self retainedReferences:NO] autorelease];
}
- (void)enqueueBuffer:(MaliMetalCommandBuffer *)buffer {
    pthread_mutex_lock(&_lock);
    MaliMetalCommandBuffer **pending = realloc(_pending, (_pendingCount + 1) * sizeof(*pending));
    if (!pending) {
        pthread_mutex_unlock(&_lock);
        [NSException raise:@"NSMallocException" format:@"Cannot reserve command queue order"];
        return;
    }
    _pending = pending;
    _pending[_pendingCount++] = [buffer retain];
    pthread_mutex_unlock(&_lock);
}
/* Called with _lock held. An enqueued buffer owns its FIFO reservation until
 * it is committed; the dispatch block then owns it until execution. */
- (void)drainReadyBuffers {
    while (_pendingCount && [_pending[0] status] >= MTLCommandBufferStatusCommitted) {
        MaliMetalCommandBuffer *buffer = _pending[0];
        --_pendingCount;
        memmove(_pending, _pending + 1, _pendingCount * sizeof(*_pending));
        dispatch_async(_submissionQueue, ^{
            @autoreleasepool {
                [buffer execute];
            }
        });
        [buffer release];
    }
}
- (void)runBuffer:(MaliMetalCommandBuffer *)buffer {
    (void)buffer;
    pthread_mutex_lock(&_lock);
    [self drainReadyBuffers];
    pthread_mutex_unlock(&_lock);
}

@end

@implementation MaliMetalCommandBuffer {
    MaliMetalCommandQueue *_queue;
    MMVKCommands *_commands;
    MMVKResult (^*_operations)(MMVKCommands *, MMVKError *);
    NSUInteger _operationCount;
    MMVKFence *_fence;
    NSError *_error;
    MTLCommandBufferStatus _status;
    BOOL _retainedReferences, _activeEncoder, _slotReleased;
    BOOL _scheduledHandlersDone, _completedHandlersDone, _submitted;
    pthread_mutex_t _lock;
    pthread_cond_t _changed;
    id *_resources;
    NSUInteger _resourceCount;
    id *_ownedResources;
    NSUInteger _ownedResourceCount;
    id<MTLTexture> *_outputs;
    NSUInteger _outputCount;
    id<MTLTexture> *_sampledTextures;
    NSUInteger _sampledTextureCount;
    id<MTLBuffer> *_writtenBuffers;
    NSUInteger _writtenBufferCount;
    id<MTLDrawable> *_drawables;
    CFTimeInterval *_presentTimes;
    NSUInteger _drawableCount;
    MTLCommandBufferHandler *_scheduledHandlers, *_completedHandlers;
    NSUInteger _scheduledCount, _completedCount;
}
- (instancetype)initWithQueue:(MaliMetalCommandQueue *)queue {
    return [self initWithQueue:queue retainedReferences:YES];
}
- (instancetype)initWithQueue:(MaliMetalCommandQueue *)queue retainedReferences:(BOOL)retained {
    self = [super init];
    if (!self)
        return nil;
    pthread_mutex_init(&_lock, NULL);
    pthread_cond_init(&_changed, NULL);
    _queue = [queue retain];
    _retainedReferences = retained;
    [queue reserveCommandBuffer];
    MaliMetalDevice *device = (MaliMetalDevice *)[queue device];
    if (![device backendDevice])
        [self recordBackendResult:MMVK_ERROR_DRIVER_UNAVAILABLE error:[device backendError]];
    return self;
}
- (void)dealloc {
    if (_fence)
        mmvk_fence_destroy(_fence);
    if (_commands)
        mmvk_commands_destroy(_commands);
    for (NSUInteger i = 0; i < _operationCount; i++)
        [(id)_operations[i] release];
    if (_retainedReferences)
        for (NSUInteger i = 0; i < _resourceCount; i++)
            [_resources[i] release];
    for (NSUInteger i = 0; i < _ownedResourceCount; i++)
        [_ownedResources[i] release];
    for (NSUInteger i = 0; i < _drawableCount; i++)
        [(id)_drawables[i] release];
    for (NSUInteger i = 0; i < _scheduledCount; i++)
        [(id)_scheduledHandlers[i] release];
    for (NSUInteger i = 0; i < _completedCount; i++)
        [(id)_completedHandlers[i] release];
    free(_resources);
    free(_ownedResources);
    free(_outputs);
    free(_sampledTextures);
    free(_writtenBuffers);
    free(_drawables);
    free(_presentTimes);
    free(_operations);
    free(_scheduledHandlers);
    free(_completedHandlers);
    if (!_slotReleased)
        [_queue releaseCommandBuffer];
    [_error release];
    [_queue release];
    pthread_cond_destroy(&_changed);
    pthread_mutex_destroy(&_lock);
    [super dealloc];
}
- (id<MTLDevice>)device {
    return [_queue device];
}
- (id<MTLCommandQueue>)commandQueue {
    return _queue;
}
- (BOOL)retainedReferences {
    return _retainedReferences;
}
- (MTLCommandBufferStatus)status {
    pthread_mutex_lock(&_lock);
    MTLCommandBufferStatus s = _status;
    pthread_mutex_unlock(&_lock);
    return s;
}
- (NSError *)error {
    pthread_mutex_lock(&_lock);
    NSError *error = [[_error retain] autorelease];
    pthread_mutex_unlock(&_lock);
    return error;
}
- (void)requireEncoding {
    if ([self status] >= MTLCommandBufferStatusCommitted)
        [NSException raise:@"NSInternalInconsistencyException"
                    format:@"Command buffer has already been committed"];
}
- (BOOL)appendOperation:(MMVKResult (^)(MMVKCommands *, MMVKError *))operation {
    [self requireEncoding];
    if (!operation || [self error])
        return NO;
    void *operations = realloc(_operations, (_operationCount + 1) * sizeof(*_operations));
    if (!operations) {
        [self recordBackendResult:MMVK_ERROR_MEMORY error:NULL];
        return NO;
    }
    _operations = operations;
    _operations[_operationCount++] = [(id)operation copy];
    return YES;
}
- (void)recordBackendResult:(MMVKResult)result error:(const MMVKError *)error {
    if (result == MMVK_OK)
        return;
    pthread_mutex_lock(&_lock);
    if (!_error)
        _error = [MaliMetalCommandError(result, error && error->message[0]
                                                    ? error->message
                                                    : mmvk_result_string(result)) retain];
    pthread_mutex_unlock(&_lock);
}
- (void)recordUnsupported:(const char *)message {
    MMVKError error = {0};
    error.code = MMVK_ERROR_UNSUPPORTED;
    size_t n = strlen(message);
    if (n >= sizeof(error.message))
        n = sizeof(error.message) - 1;
    memcpy(error.message, message, n);
    [self recordBackendResult:MMVK_ERROR_UNSUPPORTED error:&error];
}
- (BOOL)useResource:(id)resource {
    [self requireEncoding];
    if (!resource)
        return YES;
    if ([resource device] != [self device]) {
        [self recordUnsupported:"Resource belongs to another Metal device"];
        return NO;
    }
    for (NSUInteger i = 0; i < _resourceCount; i++)
        if (_resources[i] == resource)
            return YES;
    id *resources = realloc(_resources, (_resourceCount + 1) * sizeof(id));
    if (!resources) {
        [self recordBackendResult:MMVK_ERROR_MEMORY error:NULL];
        return NO;
    }
    _resources = resources;
    _resources[_resourceCount++] = _retainedReferences ? [resource retain] : resource;
    return YES;
}
- (BOOL)useOwnedResource:(id)resource {
    if (![self useResource:resource])
        return NO;
    for (NSUInteger i = 0; i < _ownedResourceCount; i++)
        if (_ownedResources[i] == resource)
            return YES;
    id *objects = realloc(_ownedResources, (_ownedResourceCount + 1) * sizeof(id));
    if (!objects) {
        [self recordBackendResult:MMVK_ERROR_MEMORY error:NULL];
        return NO;
    }
    _ownedResources = objects;
    _ownedResources[_ownedResourceCount++] = [resource retain];
    return YES;
}
- (BOOL)sampleTexture:(id<MTLTexture>)texture {
    if (!texture)
        return NO;
    if ([texture usage] && !([texture usage] & MTLTextureUsageShaderRead)) {
        [self recordUnsupported:"Sampled texture lacks shader-read usage"];
        return NO;
    }
    return [self prepareTexture:texture];
}
- (BOOL)prepareTexture:(id<MTLTexture>)texture {
    if (!texture || ![self useResource:(id)texture])
        return NO;
    for (NSUInteger i = 0; i < _sampledTextureCount; i++)
        if (_sampledTextures[i] == texture)
            return YES;
    id<MTLTexture> *textures =
        realloc(_sampledTextures, (_sampledTextureCount + 1) * sizeof(*textures));
    if (!textures) {
        [self recordBackendResult:MMVK_ERROR_MEMORY error:NULL];
        return NO;
    }
    _sampledTextures = textures;
    _sampledTextures[_sampledTextureCount++] = texture;
    return YES;
}
- (BOOL)hasWrittenTexture:(id<MTLTexture>)texture {
    for (NSUInteger i = 0; i < _outputCount; i++)
        if (_outputs[i] == texture)
            return YES;
    return NO;
}
- (void)writeBuffer:(id<MTLBuffer>)buffer {
    for (NSUInteger i = 0; i < _writtenBufferCount; i++)
        if (_writtenBuffers[i] == buffer)
            return;
    id<MTLBuffer> *buffers = realloc(_writtenBuffers, (_writtenBufferCount + 1) * sizeof(*buffers));
    if (!buffers) {
        [self recordBackendResult:MMVK_ERROR_MEMORY error:NULL];
        return;
    }
    _writtenBuffers = buffers;
    _writtenBuffers[_writtenBufferCount++] = buffer;
}
- (BOOL)writeTexture:(id<MTLTexture>)texture {
    if (![self useResource:(id)texture])
        return NO;
    for (NSUInteger i = 0; i < _outputCount; i++)
        if (_outputs[i] == texture)
            return YES;
    id<MTLTexture> *outputs = realloc(_outputs, (_outputCount + 1) * sizeof(*outputs));
    if (!outputs) {
        [self recordBackendResult:MMVK_ERROR_MEMORY error:NULL];
        return NO;
    }
    _outputs = outputs;
    _outputs[_outputCount++] = texture;
    return YES;
}
- (BOOL)beginEncoder {
    [self requireEncoding];
    if (_activeEncoder)
        [NSException raise:@"NSInternalInconsistencyException"
                    format:@"Only one command encoder can be active"];
    if ([self error])
        return NO;
    _activeEncoder = YES;
    return YES;
}
- (void)endEncoder {
    _activeEncoder = NO;
}
- (void)enqueue {
    pthread_mutex_lock(&_lock);
    if (_status != MTLCommandBufferStatusNotEnqueued) {
        pthread_mutex_unlock(&_lock);
        [NSException raise:@"NSInternalInconsistencyException"
                    format:@"Command buffer already enqueued"];
        return;
    }
    _status = MTLCommandBufferStatusEnqueued;
    pthread_mutex_unlock(&_lock);
    [_queue enqueueBuffer:self];
}
- (void)addScheduledHandler:(MTLCommandBufferHandler)handler {
    [self requireEncoding];
    if (!handler)
        return;
    MTLCommandBufferHandler *handlers =
        realloc(_scheduledHandlers, (_scheduledCount + 1) * sizeof(*handlers));
    if (!handlers)
        [NSException raise:@"NSMallocException" format:@"Cannot store scheduled handler"];
    _scheduledHandlers = handlers;
    _scheduledHandlers[_scheduledCount++] = [(id)handler copy];
}
- (void)addCompletedHandler:(MTLCommandBufferHandler)handler {
    [self requireEncoding];
    if (!handler)
        return;
    MTLCommandBufferHandler *handlers =
        realloc(_completedHandlers, (_completedCount + 1) * sizeof(*handlers));
    if (!handlers)
        [NSException raise:@"NSMallocException" format:@"Cannot store completion handler"];
    _completedHandlers = handlers;
    _completedHandlers[_completedCount++] = [(id)handler copy];
}
- (void)presentDrawable:(id<MTLDrawable>)drawable {
    [self presentDrawable:drawable atTime:0];
}
- (void)presentDrawable:(id<MTLDrawable>)drawable atTime:(CFTimeInterval)time {
    [self requireEncoding];
    if (!drawable || !isfinite(time) || time < 0)
        [NSException raise:@"NSInvalidArgumentException"
                    format:@"Invalid drawable or presentation time"];
    for (NSUInteger i = 0; i < _drawableCount; i++)
        if (_drawables[i] == drawable)
            [NSException raise:@"NSInvalidArgumentException"
                        format:@"Drawable is already scheduled for presentation"];
    id<MTLDrawable> *drawables = realloc(_drawables, (_drawableCount + 1) * sizeof(*drawables));
    if (!drawables)
        [NSException raise:@"NSMallocException" format:@"Cannot store drawable"];
    _drawables = drawables;
    CFTimeInterval *times = realloc(_presentTimes, (_drawableCount + 1) * sizeof(CFTimeInterval));
    if (!times)
        [NSException raise:@"NSMallocException" format:@"Cannot store presentation time"];
    _presentTimes = times;
    _drawables[_drawableCount] = (id)[(id)drawable retain];
    _presentTimes[_drawableCount++] = time;
}
- (void)presentDrawable:(id<MTLDrawable>)drawable afterMinimumDuration:(CFTimeInterval)duration {
    [self requireEncoding];
    (void)drawable;
    (void)duration;
    [self recordUnsupported:"Minimum-duration presentation is unsupported"];
}
- (void)commit {
    if ([self status] == MTLCommandBufferStatusNotEnqueued)
        [self enqueue];
    pthread_mutex_lock(&_lock);
    if (_status >= MTLCommandBufferStatusCommitted || _activeEncoder) {
        pthread_mutex_unlock(&_lock);
        [NSException raise:@"NSInternalInconsistencyException"
                    format:@"Command buffer is committed or still has an active encoder"];
        return;
    }
    _status = MTLCommandBufferStatusCommitted;
    pthread_mutex_unlock(&_lock);
    [_queue runBuffer:self];
}
- (BOOL)commitAndWaitUntilSubmitted {
    [self commit];
    [self waitUntilScheduled];
    pthread_mutex_lock(&_lock);
    BOOL submitted = _submitted;
    pthread_mutex_unlock(&_lock);
    return submitted;
}
- (NSUInteger)protectionOptions {
    return 0;
}
- (void)setProtectionOptions:(NSUInteger)options {
    [self requireEncoding];
    if (_activeEncoder) {
        [NSException raise:@"NSInternalInconsistencyException"
                    format:@"End the encoder before changing protection options"];
        return;
    }
    if (options)
        [self recordUnsupported:
                  "Protected command buffers require a supported secure GPU memory path"];
}
- (void)execute {
    MaliMetalDevice *device = (MaliMetalDevice *)[self device];
    [device lockExecution];
    MMVKError error = {0};
    if (!_error)
        [self recordBackendResult:mmvk_commands_create([device backendDevice], &_commands, &error)
                            error:&error];
    if (!_error) {
        /* Shared Metal buffer writes made before commit must be visible even
         * when the resource was bound earlier during encoding. */
        for (NSUInteger i = 0; i < _resourceCount; i++)
            if ([_resources[i] respondsToSelector:@selector(maliBuffer)]) {
                MMVKBuffer *buffer = [_resources[i] maliBuffer];
                if (buffer)
                    [self recordBackendResult:mmvk_buffer_flush(buffer, 0, mmvk_buffer_size(buffer),
                                                                &error)
                                        error:&error];
                if (_error)
                    break;
            }
    }
    if (!_error)
        for (NSUInteger i = 0; i < _sampledTextureCount; i++) {
            /* Host uploads and image layout transitions must precede render passes.
             * Private textures retain the result of earlier GPU command buffers. */
            if (![(id)_sampledTextures[i] maliPrepareImage:YES error:&error]) {
                [self recordBackendResult:error.code ? error.code : MMVK_ERROR_IO error:&error];
                break;
            }
            [self recordBackendResult:mmvk_commands_prepare_image(
                                          _commands, [(id)_sampledTextures[i] maliImage], &error)
                                error:&error];
            if (_error)
                break;
        }
    if (!_error)
        for (NSUInteger i = 0; i < _operationCount; i++) {
            MMVKResult result = _operations[i](_commands, &error);
            [self recordBackendResult:result error:&error];
            if (result != MMVK_OK)
                break;
        }
    if (!_error)
        [self recordBackendResult:mmvk_commands_submit(_commands, &_fence, &error) error:&error];
    if (!_error) {
        pthread_mutex_lock(&_lock);
        _submitted = YES;
        _status = MTLCommandBufferStatusScheduled;
        pthread_cond_broadcast(&_changed);
        pthread_mutex_unlock(&_lock);
        for (NSUInteger i = 0; i < _scheduledCount; i++) {
            MTLCommandBufferHandler handler = _scheduledHandlers[i];
            _scheduledHandlers[i] = nil;
            handler(self);
            [(id)handler release];
        }
        _scheduledCount = 0;
        pthread_mutex_lock(&_lock);
        _scheduledHandlersDone = YES;
        pthread_cond_broadcast(&_changed);
        pthread_mutex_unlock(&_lock);
        MMVKResult result;
        /* Timeout is nonterminal: keep all native objects alive while the GPU
         * still owns them. Only a signaled fence or terminal native error ends
         * this worker. Kernel/device recovery must turn hangs into device loss. */
        do {
            result = mmvk_fence_wait(_fence, 5000000000ull, &error);
            if (result != MMVK_OK && result != MMVK_TIMEOUT && result != MMVK_NOT_READY &&
                result != MMVK_ERROR_DEVICE_LOST) {
                /* Host allocation or wait errors do not establish GPU quiescence. */
                [self recordBackendResult:result error:&error];
                struct timespec pause = {0, 100000000};
                nanosleep(&pause, NULL);
            }
        } while (result != MMVK_OK && result != MMVK_ERROR_DEVICE_LOST);
        [self recordBackendResult:result error:&error];
    }
    if (!_error) {
        for (NSUInteger i = 0; i < _writtenBufferCount; i++) {
            MMVKBuffer *buffer = [(id)_writtenBuffers[i] maliBuffer];
            [self recordBackendResult:mmvk_buffer_invalidate(buffer, 0, mmvk_buffer_size(buffer),
                                                             &error)
                                error:&error];
            if (_error)
                break;
        }
    }
    if (!_error) {
        for (NSUInteger i = 0; i < _outputCount; i++) {
            if (![(id)_outputs[i] maliReadbackImage:&error]) {
                [self recordBackendResult:error.code ? error.code : MMVK_ERROR_IO error:&error];
                break;
            }
        }
    }
    /* Release native command storage while referenced resources still live. */
    if (_fence) {
        mmvk_fence_destroy(_fence);
        _fence = NULL;
    }
    if (_commands) {
        mmvk_commands_destroy(_commands);
        _commands = NULL;
    }
    [device unlockExecution];
    if (!_error) {
        for (NSUInteger i = 0; i < _drawableCount; i++) {
            if (_presentTimes[i] > 0)
                [_drawables[i] presentAtTime:_presentTimes[i]];
            else
                [_drawables[i] present];
        }
    }
    pthread_mutex_lock(&_lock);
    _status = _error ? MTLCommandBufferStatusError : MTLCommandBufferStatusCompleted;
    _scheduledHandlersDone = YES;
    pthread_cond_broadcast(&_changed);
    pthread_mutex_unlock(&_lock);
    _slotReleased = YES;
    [_queue releaseCommandBuffer];
    dispatch_async([_queue completionQueue], ^{
        @autoreleasepool {
            [self notifyCompleted];
        }
    });
}
- (void)notifyCompleted {
    for (NSUInteger i = 0; i < _completedCount; i++) {
        MTLCommandBufferHandler handler = _completedHandlers[i];
        _completedHandlers[i] = nil;
        handler(self);
        [(id)handler release];
    }
    _completedCount = 0;
    /* Handlers may retain resources or this buffer; release their captures and
     * execution-only references once every completion callback has returned. */
    for (NSUInteger i = 0; i < _scheduledCount; i++) {
        [(id)_scheduledHandlers[i] release];
        _scheduledHandlers[i] = nil;
    }
    _scheduledCount = 0;
    for (NSUInteger i = 0; i < _operationCount; i++)
        [(id)_operations[i] release];
    _operationCount = 0;
    free(_operations);
    _operations = NULL;
    if (_retainedReferences)
        for (NSUInteger i = 0; i < _resourceCount; i++)
            [_resources[i] release];
    _resourceCount = 0;
    for (NSUInteger i = 0; i < _ownedResourceCount; i++)
        [_ownedResources[i] release];
    _ownedResourceCount = 0;
    for (NSUInteger i = 0; i < _drawableCount; i++)
        [(id)_drawables[i] release];
    _drawableCount = 0;
    _outputCount = 0;
    _sampledTextureCount = 0;
    _writtenBufferCount = 0;
    pthread_mutex_lock(&_lock);
    _completedHandlersDone = YES;
    pthread_cond_broadcast(&_changed);
    pthread_mutex_unlock(&_lock);
}
- (void)waitUntilScheduled {
    pthread_mutex_lock(&_lock);
    if (_status < MTLCommandBufferStatusCommitted) {
        pthread_mutex_unlock(&_lock);
        [NSException raise:@"NSInternalInconsistencyException"
                    format:@"Commit the command buffer before waiting"];
        return;
    }
    while (!_scheduledHandlersDone)
        pthread_cond_wait(&_changed, &_lock);
    pthread_mutex_unlock(&_lock);
}
- (void)waitUntilCompleted {
    pthread_mutex_lock(&_lock);
    if (_status < MTLCommandBufferStatusCommitted) {
        pthread_mutex_unlock(&_lock);
        [NSException raise:@"NSInternalInconsistencyException"
                    format:@"Commit the command buffer before waiting"];
        return;
    }
    while (!_completedHandlersDone)
        pthread_cond_wait(&_changed, &_lock);
    pthread_mutex_unlock(&_lock);
}

- (id<MTLRenderCommandEncoder>)renderCommandEncoderWithDescriptor:
    (MTLRenderPassDescriptor *)descriptor {
    if (![self beginEncoder])
        return nil;
    MaliMetalRenderCommandEncoder *encoder =
        [[MaliMetalRenderCommandEncoder alloc] initWithBuffer:self descriptor:descriptor];
    if (!encoder)
        [self endEncoder];
    return (id)[encoder autorelease];
}
- (id<MTLBlitCommandEncoder>)blitCommandEncoder {
    if (![self beginEncoder])
        return nil;
    return (id)[[[MaliMetalBlitCommandEncoder alloc] initWithBuffer:self] autorelease];
}
- (id<MTLComputeCommandEncoder>)computeCommandEncoderWithDispatchType:(MTLDispatchType)type {
    [self requireEncoding];
    if (type != MTLDispatchTypeSerial) {
        [self recordUnsupported:"Concurrent compute dispatch is not enabled in this native queue"];
        return nil;
    }
    return [self computeCommandEncoder];
}
- (id<MTLComputeCommandEncoder>)computeCommandEncoder {
    if (![self beginEncoder])
        return nil;
    MaliMetalComputeCommandEncoder *encoder =
        [[MaliMetalComputeCommandEncoder alloc] initWithBuffer:self];
    if (!encoder)
        [self endEncoder];
    return (id)[encoder autorelease];
}
@end

@implementation MaliMetalRenderCommandEncoder
- (instancetype)initWithBuffer:(MaliMetalCommandBuffer *)buffer
                    descriptor:(MTLRenderPassDescriptor *)descriptor {
    self = [super init];
    if (!self)
        return nil;
    _buffer = [buffer retain];
    if (descriptor &&
        ([[descriptor depthAttachment] texture] || [[descriptor stencilAttachment] texture])) {
        [buffer recordUnsupported:"Depth and stencil attachments are unsupported"];
        [self release];
        return nil;
    }
    if (!descriptor) {
        [buffer recordUnsupported:"Render pass descriptor is nil"];
        [self release];
        return nil;
    }
    struct {
        void *textures[8];
        BOOL preserve[8], clear[8];
        MTLClearColor colors[8];
        uint32_t count, width, height;
    } pass = {0};
    for (NSUInteger i = 0; i < 8; i++) {
        MTLRenderPassColorAttachmentDescriptor *attachment =
            [[descriptor colorAttachments] objectAtIndexedSubscript:i];
        id<MTLTexture> texture = [attachment texture];
        if (!texture)
            continue;
        BOOL previouslyWritten = [buffer hasWrittenTexture:texture];
        if (![buffer writeTexture:texture] || [attachment level] || [attachment slice] ||
            [attachment depthPlane] || [attachment resolveTexture] ||
            [attachment loadAction] > MTLLoadActionClear ||
            [attachment storeAction] != MTLStoreActionStore ||
            ([texture usage] && !([texture usage] & MTLTextureUsageRenderTarget)) ||
            [texture width] > UINT32_MAX || [texture height] > UINT32_MAX ||
            (pass.count && (pass.width != [texture width] || pass.height != [texture height]))) {
            [buffer recordUnsupported:
                "Unsupported render attachment, dimensions, subresource, usage or store action"];
            [self release];
            return nil;
        }
        for (NSUInteger k = 0; k < i; k++)
            if (texture == _targetTextures[k] ||
                ([texture iosurface] && [texture iosurface] == [_targetTextures[k] iosurface])) {
                [buffer recordUnsupported:"Render attachments alias the same texture"];
                [self release];
                return nil;
            }
        _colorFormats[i] = [texture pixelFormat];
        _targetTextures[i] = texture;
        pass.textures[i] = (void *)texture;
        pass.preserve[i] = !previouslyWritten && [attachment loadAction] == MTLLoadActionLoad;
        pass.clear[i] = [attachment loadAction] == MTLLoadActionClear;
        pass.colors[i] = [attachment clearColor];
        pass.count = (uint32_t)i + 1;
        pass.width = (uint32_t)[texture width];
        pass.height = (uint32_t)[texture height];
    }
    if (!pass.count) {
        [buffer recordUnsupported:"Render pass requires a color attachment"];
        [self release];
        return nil;
    }
    [buffer appendOperation:^MMVKResult(MMVKCommands *commands, MMVKError *error) {
        MMVKRenderTarget targets[8] = {0};
        for (uint32_t i = 0; i < pass.count; i++) {
            id target = (id)pass.textures[i];
            if (!target)
                continue;
            if (![target maliPrepareImage:pass.preserve[i] error:error])
                return error->code ? error->code : MMVK_ERROR_IO;
            targets[i].image = [target maliImage];
            targets[i].clear = pass.clear[i];
            MTLClearColor color = pass.colors[i];
            targets[i].rgba[0] = (float)color.red;
            targets[i].rgba[1] = (float)color.green;
            targets[i].rgba[2] = (float)color.blue;
            targets[i].rgba[3] = (float)color.alpha;
        }
        MMVKResult result =
            mmvk_commands_begin_render_targets(commands, targets, pass.count, error);
        if (result == MMVK_OK)
            result = mmvk_commands_set_viewport(commands, 0, pass.height, pass.width,
                                                -(float)pass.height, 0, 1, error);
        if (result == MMVK_OK)
            result = mmvk_commands_set_scissor(commands, 0, 0, pass.width, pass.height, error);
        return result;
    }];
    if ([buffer error]) {
        [self release];
        return nil;
    }
    return self;
}
- (void)dealloc {
    [_buffer release];
    [super dealloc];
}
- (id<MTLDevice>)device {
    return [_buffer device];
}
- (void)requireActive {
    if (_ended)
        [NSException raise:@"NSInternalInconsistencyException" format:@"Encoder has ended"];
}
- (void)setRenderPipelineState:(id<MTLRenderPipelineState>)pipeline {
    [self requireActive];
    if (!pipeline || ![(id)pipeline respondsToSelector:@selector(maliPipeline)] ||
        ![(id)pipeline maliPipeline]) {
        [_buffer recordUnsupported:"Render pipeline has no executable native shader"];
        return;
    }
    for (NSUInteger i = 0; i < 8; i++)
        if ([(id)pipeline maliColorFormatAtIndex:i] != _colorFormats[i]) {
            [_buffer recordUnsupported:"Pipeline formats differ from render attachments"];
            return;
        }
    if (![_buffer useResource:(id)pipeline])
        return;
    _pipeline = (id)pipeline;
    MMVKPipeline *native = [(id)pipeline maliPipeline];
    [_buffer appendOperation:^MMVKResult(MMVKCommands *commands, MMVKError *error) {
        return mmvk_commands_bind_pipeline(commands, native, error);
    }];
}
- (void)setVertexBuffer:(id<MTLBuffer>)buffer offset:(NSUInteger)offset atIndex:(NSUInteger)index {
    [self requireActive];
    if (index >= 31 || (buffer && offset > [buffer length]))
        [NSException raise:@"NSInvalidArgumentException" format:@"Invalid vertex buffer binding"];
    if (buffer &&
        (![(id)buffer respondsToSelector:@selector(maliBuffer)] || ![(id)buffer maliBuffer])) {
        [_buffer recordUnsupported:"Vertex buffer has no native allocation"];
        return;
    }
    if (![_buffer useResource:(id)buffer])
        return;
    _vertexBuffers[index] = buffer;
    _vertexOffsets[index] = offset;
    if (buffer)
        [_buffer writeBuffer:buffer];
}
- (void)setVertexBufferOffset:(NSUInteger)offset atIndex:(NSUInteger)index {
    if (index >= 31)
        [NSException raise:@"NSInvalidArgumentException" format:@"Vertex binding exceeds limit"];
    [self setVertexBuffer:_vertexBuffers[index] offset:offset atIndex:index];
}
- (void)setVertexBytes:(const void *)bytes length:(NSUInteger)length atIndex:(NSUInteger)index {
    id<MTLBuffer> buffer = [(MaliMetalDevice *)[self device] newBufferWithBytes:bytes
                                                                         length:length
                                                                        options:0];
    if (!buffer) {
        [_buffer recordBackendResult:MMVK_ERROR_MEMORY error:NULL];
        return;
    }
    if ([_buffer useOwnedResource:(id)buffer])
        [self setVertexBuffer:buffer offset:0 atIndex:index];
    [(id)buffer release];
}
- (void)setFragmentBuffer:(id<MTLBuffer>)buffer
                   offset:(NSUInteger)offset
                  atIndex:(NSUInteger)index {
    [self requireActive];
    if (index >= 31 || (buffer && offset > [buffer length])) {
        [NSException raise:@"NSInvalidArgumentException" format:@"Invalid fragment buffer binding"];
        return;
    }
    if (buffer &&
        (![(id)buffer respondsToSelector:@selector(maliBuffer)] || ![(id)buffer maliBuffer])) {
        [_buffer recordUnsupported:"Fragment buffer has no native allocation"];
        return;
    }
    if (![_buffer useResource:(id)buffer])
        return;
    _fragmentBuffers[index] = buffer;
    _fragmentOffsets[index] = offset;
    if (buffer)
        [_buffer writeBuffer:buffer];
}
- (void)setFragmentBufferOffset:(NSUInteger)offset atIndex:(NSUInteger)index {
    if (index >= 31) {
        [NSException raise:@"NSInvalidArgumentException" format:@"Fragment binding exceeds limit"];
        return;
    }
    [self setFragmentBuffer:_fragmentBuffers[index] offset:offset atIndex:index];
}
- (void)setFragmentBytes:(const void *)bytes length:(NSUInteger)length atIndex:(NSUInteger)index {
    [self requireActive];
    id<MTLBuffer> buffer = [(MaliMetalDevice *)[self device] newBufferWithBytes:bytes
                                                                         length:length
                                                                        options:0];
    if (!buffer) {
        [_buffer recordBackendResult:MMVK_ERROR_MEMORY error:NULL];
        return;
    }
    if ([_buffer useOwnedResource:(id)buffer])
        [self setFragmentBuffer:buffer offset:0 atIndex:index];
    [(id)buffer release];
}
- (void)setVertexBuffers:(const id<MTLBuffer> *)buffers
                 offsets:(const NSUInteger *)offsets
               withRange:(NSRange)range {
    if (range.location > 31 || range.length > 31 - range.location || !buffers || !offsets) {
        [NSException raise:@"NSInvalidArgumentException" format:@"Invalid vertex buffer range"];
        return;
    }
    for (NSUInteger i = 0; i < range.length; i++)
        [self setVertexBuffer:buffers[i] offset:offsets[i] atIndex:range.location + i];
}
- (void)setFragmentBuffers:(const id<MTLBuffer> *)buffers
                   offsets:(const NSUInteger *)offsets
                 withRange:(NSRange)range {
    if (range.location > 31 || range.length > 31 - range.location || !buffers || !offsets) {
        [NSException raise:@"NSInvalidArgumentException" format:@"Invalid fragment buffer range"];
        return;
    }
    for (NSUInteger i = 0; i < range.length; i++)
        [self setFragmentBuffer:buffers[i] offset:offsets[i] atIndex:range.location + i];
}
- (void)setDepthStencilState:(id)state {
    [self requireActive];
    if (state)
        [_buffer recordUnsupported:"Depth/stencil state is unsupported"];
}
- (void)setCullMode:(NSUInteger)mode {
    [self requireActive];
    if (mode)
        [_buffer recordUnsupported:"Face culling is unsupported"];
}
- (void)setDepthClipMode:(NSUInteger)mode {
    [self requireActive];
    if (mode)
        [_buffer recordUnsupported:"Depth clamping is unsupported"];
}
- (void)setFragmentTexture:(id<MTLTexture>)texture atIndex:(NSUInteger)index {
    [self requireActive];
    if (index >= 128) {
        [NSException raise:@"NSInvalidArgumentException"
                    format:@"Fragment texture index exceeds limit"];
        return;
    }
    if (texture &&
        (![(id)texture respondsToSelector:@selector(maliImage)] || ![(id)texture maliImage])) {
        [_buffer recordUnsupported:"Texture has no native image"];
        return;
    }
    if ([_buffer useResource:(id)texture])
        _fragmentTextures[index] = texture;
}
- (void)setVertexTexture:(id<MTLTexture>)texture atIndex:(NSUInteger)index {
    [self requireActive];
    if (index >= 128) {
        [NSException raise:@"NSInvalidArgumentException"
                    format:@"Vertex texture index exceeds limit"];
        return;
    }
    if (texture &&
        (![(id)texture respondsToSelector:@selector(maliImage)] || ![(id)texture maliImage])) {
        [_buffer recordUnsupported:"Texture has no native image"];
        return;
    }
    if ([_buffer useResource:(id)texture])
        _vertexTextures[index] = texture;
}
- (void)setFragmentSamplerState:(id<MTLSamplerState>)sampler atIndex:(NSUInteger)index {
    [self requireActive];
    if (index >= 16) {
        [NSException raise:@"NSInvalidArgumentException"
                    format:@"Fragment sampler index exceeds limit"];
        return;
    }
    if (sampler &&
        (![(id)sampler respondsToSelector:@selector(maliSampler)] || ![(id)sampler maliSampler])) {
        [_buffer recordUnsupported:"Sampler has no native state"];
        return;
    }
    if ([_buffer useResource:(id)sampler])
        _fragmentSamplers[index] = sampler;
}
/* QuartzCore 16.7 uses this combined binding selector directly. */
- (void)setFragmentTexture:(id<MTLTexture>)texture
            atTextureIndex:(NSUInteger)textureIndex
              samplerState:(id<MTLSamplerState>)sampler
            atSamplerIndex:(NSUInteger)samplerIndex {
    [self setFragmentTexture:texture atIndex:textureIndex];
    [self setFragmentSamplerState:sampler atIndex:samplerIndex];
}
- (void)setVertexSamplerState:(id<MTLSamplerState>)sampler atIndex:(NSUInteger)index {
    [self requireActive];
    if (index >= 16) {
        [NSException raise:@"NSInvalidArgumentException"
                    format:@"Vertex sampler index exceeds limit"];
        return;
    }
    if (sampler &&
        (![(id)sampler respondsToSelector:@selector(maliSampler)] || ![(id)sampler maliSampler])) {
        [_buffer recordUnsupported:"Sampler has no native state"];
        return;
    }
    if ([_buffer useResource:(id)sampler])
        _vertexSamplers[index] = sampler;
}
- (void)setFragmentTextures:(const id<MTLTexture> *)textures withRange:(NSRange)range {
    if (range.location > 128 || range.length > 128 - range.location ||
        (range.length && !textures)) {
        [NSException raise:@"NSInvalidArgumentException" format:@"Invalid fragment texture range"];
        return;
    }
    for (NSUInteger i = 0; i < range.length; i++)
        [self setFragmentTexture:textures[i] atIndex:range.location + i];
}
- (void)setVertexTextures:(const id<MTLTexture> *)textures withRange:(NSRange)range {
    if (range.location > 128 || range.length > 128 - range.location ||
        (range.length && !textures)) {
        [NSException raise:@"NSInvalidArgumentException" format:@"Invalid vertex texture range"];
        return;
    }
    for (NSUInteger i = 0; i < range.length; i++)
        [self setVertexTexture:textures[i] atIndex:range.location + i];
}
- (void)setFragmentSamplerStates:(const id<MTLSamplerState> *)samplers withRange:(NSRange)range {
    if (range.location > 16 || range.length > 16 - range.location || (range.length && !samplers)) {
        [NSException raise:@"NSInvalidArgumentException" format:@"Invalid fragment sampler range"];
        return;
    }
    for (NSUInteger i = 0; i < range.length; i++)
        [self setFragmentSamplerState:samplers[i] atIndex:range.location + i];
}
- (void)setVertexSamplerStates:(const id<MTLSamplerState> *)samplers withRange:(NSRange)range {
    if (range.location > 16 || range.length > 16 - range.location || (range.length && !samplers)) {
        [NSException raise:@"NSInvalidArgumentException" format:@"Invalid vertex sampler range"];
        return;
    }
    for (NSUInteger i = 0; i < range.length; i++)
        [self setVertexSamplerState:samplers[i] atIndex:range.location + i];
}
- (void)setViewport:(MTLViewport)v {
    [self requireActive];
    if (!isfinite(v.originX) || !isfinite(v.originY) || !isfinite(v.width) || !isfinite(v.height) ||
        !isfinite(v.znear) || !isfinite(v.zfar) || fabs(v.originX) > FLT_MAX ||
        fabs(v.originY + v.height) > FLT_MAX || v.width > FLT_MAX || v.height > FLT_MAX ||
        v.width <= 0 || v.height <= 0 || v.znear < 0 || v.znear > 1 || v.zfar < 0 || v.zfar > 1) {
        [_buffer recordUnsupported:"Invalid viewport"];
        return;
    }
    [_buffer appendOperation:^MMVKResult(MMVKCommands *commands, MMVKError *error) {
        /* Metal's upper-left viewport and Vulkan's negative-height viewport
         * preserve the shader's unmodified position Y. */
        return mmvk_commands_set_viewport(commands, v.originX, v.originY + v.height, v.width,
                                          -v.height, v.znear, v.zfar, error);
    }];
}
- (void)setScissorRect:(MTLScissorRect)rect {
    [self requireActive];
    if (rect.x > INT32_MAX || rect.y > INT32_MAX || rect.width > UINT32_MAX ||
        rect.height > UINT32_MAX) {
        [_buffer recordUnsupported:"Scissor exceeds backend range"];
        return;
    }
    [_buffer appendOperation:^MMVKResult(MMVKCommands *commands, MMVKError *error) {
        return mmvk_commands_set_scissor(commands, (int32_t)rect.x, (int32_t)rect.y,
                                         (uint32_t)rect.width, (uint32_t)rect.height, error);
    }];
}
- (BOOL)captureDrawBindings:(MMShaderVulkanBindings *)resources {
    const MMShaderRenderOutput *output = [_pipeline maliShaderOutput];
    if (!output || !output->backend_plan) {
        [_buffer recordUnsupported:"Pipeline has no retained buffer binding plan"];
        return NO;
    }
    MMShaderVulkanBindings bindings = {0};
    const MMShaderVulkanConstants *constants = [_pipeline maliShaderConstants];
    if (constants)
        memcpy(bindings.constant_buffers, constants->buffers, sizeof(bindings.constant_buffers));
    for (NSUInteger i = 0; i < 31; i++) {
        bindings.vertex_buffers[i] =
            (MMShaderVulkanBuffer){[(id)_vertexBuffers[i] maliBuffer], _vertexOffsets[i]};
        bindings.fragment_buffers[i] =
            (MMShaderVulkanBuffer){[(id)_fragmentBuffers[i] maliBuffer], _fragmentOffsets[i]};
    }
    const MMShaderBackendPlan *plan = output->backend_plan;
    if ((plan->binding_count > MM_SHADER_MAX_BINDINGS || plan->vertex_binding_count > 31)) {
        [_buffer recordUnsupported:"Invalid compiled resource binding count"];
        return NO;
    }
    for (uint32_t i = 0; i < plan->binding_count; i++)
        if (plan->bindings[i].vulkan_type == 1) {
            const MMShaderBackendBinding *binding = plan->bindings + i;
            if (binding->metal_index >= 128 || binding->sampler_index >= 16 ||
                binding->metal_stage > MM_SHADER_FRAGMENT) {
                [_buffer recordUnsupported:"Invalid compiled sampled resource slot"];
                return NO;
            }
            id<MTLTexture> texture = binding->metal_stage == MM_SHADER_VERTEX
                                         ? _vertexTextures[binding->metal_index]
                                         : _fragmentTextures[binding->metal_index];
            id<MTLSamplerState> sampler = binding->metal_stage == MM_SHADER_VERTEX
                                              ? _vertexSamplers[binding->sampler_index]
                                              : _fragmentSamplers[binding->sampler_index];
            BOOL aliases = NO;
            for (NSUInteger k = 0; k < 8; k++)
                if (texture == _targetTextures[k] ||
                    ([texture iosurface] && [texture iosurface] == [_targetTextures[k] iosurface]))
                    aliases = YES;
            if (!sampler || !texture || aliases) {
                [_buffer recordUnsupported:
                             "Sampled image/sampler is missing or aliases the render target"];
                return NO;
            }
            if (![_buffer sampleTexture:texture])
                return NO;
        }
    for (NSUInteger i = 0; i < 128; i++) {
        bindings.vertex_textures[i] = [(id)_vertexTextures[i] maliImage];
        bindings.fragment_textures[i] = [(id)_fragmentTextures[i] maliImage];
    }
    for (NSUInteger i = 0; i < 16; i++) {
        bindings.vertex_samplers[i] = [(id)_vertexSamplers[i] maliSampler];
        bindings.fragment_samplers[i] = [(id)_fragmentSamplers[i] maliSampler];
    }
    *resources = bindings;
    return YES;
}
- (BOOL)validateVertexBindingsForStart:(uint64_t)start
                                 count:(uint64_t)count
                          baseInstance:(uint64_t)baseInstance
                         instanceCount:(uint64_t)instances
                               indexed:(BOOL)indexed {
    const MMShaderBackendPlan *plan = [_pipeline maliShaderOutput]->backend_plan;
    for (uint32_t i = 0; i < plan->vertex_binding_count; i++) {
        const MMShaderBackendVertexBinding *binding = plan->vertex_bindings + i;
        if (binding->metal_index >= 31 || !binding->stride) {
            [_buffer recordUnsupported:"Invalid compiled vertex binding"];
            return NO;
        }
        id<MTLBuffer> buffer = _vertexBuffers[binding->metal_index];
        NSUInteger offset = _vertexOffsets[binding->metal_index];
        if (!buffer || offset >= [buffer length]) {
            [_buffer recordUnsupported:"Vertex stream is unbound or outside its logical buffer"];
            return NO;
        }
        /* Indexed values remain GPU-side, including values produced by earlier
         * GPU commands. No stale CPU read is used as a bounds oracle. */
        if (!indexed || binding->per_instance) {
            uint64_t end = binding->per_instance ? baseInstance + instances : start + count;
            if (end > ([buffer length] - offset) / binding->stride) {
                [_buffer recordUnsupported:"Draw exceeds the bound vertex stream"];
                return NO;
            }
        }
    }
    return YES;
}
- (void)drawPrimitives:(NSUInteger)type
           vertexStart:(NSUInteger)start
           vertexCount:(NSUInteger)count {
    [self drawPrimitives:type vertexStart:start vertexCount:count instanceCount:1 baseInstance:0];
}
- (void)drawPrimitives:(NSUInteger)type
           vertexStart:(NSUInteger)start
           vertexCount:(NSUInteger)count
         instanceCount:(NSUInteger)instances {
    [self drawPrimitives:type
             vertexStart:start
             vertexCount:count
           instanceCount:instances
            baseInstance:0];
}
- (void)drawPrimitives:(NSUInteger)type
           vertexStart:(NSUInteger)start
           vertexCount:(NSUInteger)count
         instanceCount:(NSUInteger)instances
          baseInstance:(NSUInteger)baseInstance {
    [self requireActive];
    if (!_pipeline || type != MTLPrimitiveTypeTriangle || start > UINT32_MAX ||
        count > UINT32_MAX || instances > UINT32_MAX || baseInstance > UINT32_MAX) {
        [_buffer
            recordUnsupported:"Draw requires an executable triangle pipeline and valid counts"];
        return;
    }
    if (!count || !instances)
        return;
    MMShaderVulkanBindings bindings = {0};
    if (![self captureDrawBindings:&bindings] || ![self validateVertexBindingsForStart:start
                                                                                 count:count
                                                                          baseInstance:baseInstance
                                                                         instanceCount:instances
                                                                               indexed:NO])
        return;
    const MMShaderRenderOutput *output = [_pipeline maliShaderOutput];
    [_buffer appendOperation:^MMVKResult(MMVKCommands *commands, MMVKError *error) {
        MMVKResult result = MMShaderVulkanBindResources(commands, output, &bindings, error);
        if (result == MMVK_OK)
            result = mmvk_commands_draw(commands, (uint32_t)start, (uint32_t)count,
                                        (uint32_t)instances, (uint32_t)baseInstance, error);
        return result;
    }];
}
- (void)drawIndexedPrimitives:(NSUInteger)type
                   indexCount:(NSUInteger)count
                    indexType:(MTLIndexType)indexType
                  indexBuffer:(id<MTLBuffer>)indexBuffer
            indexBufferOffset:(NSUInteger)offset {
    [self drawIndexedPrimitives:type
                     indexCount:count
                      indexType:indexType
                    indexBuffer:indexBuffer
              indexBufferOffset:offset
                  instanceCount:1
                     baseVertex:0
                   baseInstance:0];
}
- (void)drawIndexedPrimitives:(NSUInteger)type
                   indexCount:(NSUInteger)count
                    indexType:(MTLIndexType)indexType
                  indexBuffer:(id<MTLBuffer>)indexBuffer
            indexBufferOffset:(NSUInteger)offset
                instanceCount:(NSUInteger)instances {
    [self drawIndexedPrimitives:type
                     indexCount:count
                      indexType:indexType
                    indexBuffer:indexBuffer
              indexBufferOffset:offset
                  instanceCount:instances
                     baseVertex:0
                   baseInstance:0];
}
- (void)drawIndexedPrimitives:(NSUInteger)type
                   indexCount:(NSUInteger)count
                    indexType:(MTLIndexType)indexType
                  indexBuffer:(id<MTLBuffer>)indexBuffer
            indexBufferOffset:(NSUInteger)offset
                instanceCount:(NSUInteger)instances
                   baseVertex:(NSInteger)baseVertex
                 baseInstance:(NSUInteger)baseInstance {
    [self requireActive];
    if (!_pipeline || type != MTLPrimitiveTypeTriangle || count > UINT32_MAX ||
        instances > UINT32_MAX || baseVertex < INT32_MIN || baseVertex > INT32_MAX ||
        baseInstance > UINT32_MAX ||
        (indexType != MTLIndexTypeUInt16 && indexType != MTLIndexTypeUInt32)) {
        [_buffer recordUnsupported:
                     "Indexed draw requires valid native counts, index type and base offsets"];
        return;
    }
    if (!count || !instances)
        return;
    NSUInteger element = indexType == MTLIndexTypeUInt16 ? 2 : 4;
    if (!indexBuffer || offset % element || offset > [indexBuffer length] ||
        count > ([indexBuffer length] - offset) / element) {
        [NSException raise:@"NSInvalidArgumentException"
                    format:@"Indexed draw exceeds its logical index span or alignment"];
        return;
    }
    if (![_buffer useResource:(id)indexBuffer])
        return;
    if (![(id)indexBuffer respondsToSelector:@selector(maliBuffer)] ||
        ![(id)indexBuffer maliBuffer]) {
        [_buffer recordUnsupported:"Index buffer has no native allocation"];
        return;
    }
    MMShaderVulkanBindings bindings = {0};
    if (![self captureDrawBindings:&bindings] || ![self validateVertexBindingsForStart:0
                                                                                 count:count
                                                                          baseInstance:baseInstance
                                                                         instanceCount:instances
                                                                               indexed:YES])
        return;
    const MMShaderRenderOutput *output = [_pipeline maliShaderOutput];
    MMVKBuffer *indices = [(id)indexBuffer maliBuffer];
    [_buffer appendOperation:^MMVKResult(MMVKCommands *commands, MMVKError *error) {
        MMVKResult result = MMShaderVulkanBindResources(commands, output, &bindings, error);
        if (result == MMVK_OK)
            result = mmvk_commands_bind_index_buffer(commands, indices, offset, (uint32_t)indexType,
                                                     error);
        if (result == MMVK_OK)
            result = mmvk_commands_draw_indexed(commands, (uint32_t)count, (uint32_t)instances, 0,
                                                (int32_t)baseVertex, (uint32_t)baseInstance, error);
        return result;
    }];
}
- (void)endEncoding {
    [self requireActive];
    [_buffer appendOperation:^MMVKResult(MMVKCommands *commands, MMVKError *error) {
        return mmvk_commands_end_render(commands, error);
    }];
    _ended = YES;
    [_buffer endEncoder];
}
@end

@implementation MaliMetalBlitCommandEncoder
- (instancetype)initWithBuffer:(MaliMetalCommandBuffer *)buffer {
    self = [super init];
    if (self)
        _buffer = [buffer retain];
    return self;
}
- (void)dealloc {
    [_buffer release];
    [super dealloc];
}
- (id<MTLDevice>)device {
    return [_buffer device];
}
- (void)requireActive {
    if (_ended)
        [NSException raise:@"NSInternalInconsistencyException" format:@"Encoder has ended"];
}
- (void)copyFromBuffer:(id<MTLBuffer>)source
          sourceOffset:(NSUInteger)sourceOffset
              toBuffer:(id<MTLBuffer>)destination
     destinationOffset:(NSUInteger)destinationOffset
                  size:(NSUInteger)length {
    [self requireActive];
    if (!source || !destination || sourceOffset > [source length] ||
        length > [source length] - sourceOffset || destinationOffset > [destination length] ||
        length > [destination length] - destinationOffset ||
        (source == destination && sourceOffset < destinationOffset + length &&
         destinationOffset < sourceOffset + length))
        [NSException raise:@"NSInvalidArgumentException"
                    format:@"Invalid or overlapping buffer copy"];
    if (![_buffer useResource:(id)source] || ![_buffer useResource:(id)destination])
        return;
    if (![(id)source maliBuffer] || ![(id)destination maliBuffer]) {
        [_buffer recordUnsupported:"Copy needs native buffers"];
        return;
    }
    if (!length)
        return;
    [_buffer writeBuffer:destination];
    MMVKBuffer *src = [(id)source maliBuffer], *dst = [(id)destination maliBuffer];
    [_buffer appendOperation:^MMVKResult(MMVKCommands *commands, MMVKError *error) {
        return mmvk_commands_copy_buffer(commands, src, sourceOffset, dst, destinationOffset,
                                         length, error);
    }];
}
- (void)fillBuffer:(id<MTLBuffer>)buffer range:(NSRange)range value:(uint8_t)value {
    [self requireActive];
    if (!buffer || range.location > [buffer length] ||
        range.length > [buffer length] - range.location)
        [NSException raise:@"NSInvalidArgumentException" format:@"Invalid buffer fill range"];
    if ((range.location & 3) || (range.length & 3)) {
        [_buffer recordUnsupported:"Native fill requires four-byte aligned range"];
        return;
    }
    if (![_buffer useResource:(id)buffer])
        return;
    if (![(id)buffer maliBuffer]) {
        [_buffer recordUnsupported:"Fill needs a native buffer"];
        return;
    }
    if (!range.length)
        return;
    [_buffer writeBuffer:buffer];
    uint32_t word = (uint32_t)value * 0x01010101u;
    MMVKBuffer *native = [(id)buffer maliBuffer];
    [_buffer appendOperation:^MMVKResult(MMVKCommands *commands, MMVKError *error) {
        return mmvk_commands_fill_buffer(commands, native, range.location, range.length, word,
                                         error);
    }];
}
- (void)endEncoding {
    [self requireActive];
    _ended = YES;
    [_buffer endEncoder];
}
@end

@implementation MaliMetalComputeCommandEncoder
- (instancetype)initWithBuffer:(MaliMetalCommandBuffer *)buffer {
    self = [super init];
    if (self)
        _buffer = [buffer retain];
    return self;
}
- (void)dealloc {
    [_buffer release];
    [super dealloc];
}
- (MTLDispatchType)dispatchType {
    return MTLDispatchTypeSerial;
}
- (id<MTLDevice>)device {
    return [_buffer device];
}
- (void)requireActive {
    if (_ended)
        [NSException raise:@"NSInternalInconsistencyException" format:@"Compute encoder has ended"];
}
- (void)setComputePipelineState:(id<MTLComputePipelineState>)pipeline {
    [self requireActive];
    if (!pipeline || ![(id)pipeline respondsToSelector:@selector(maliComputeProgram)] ||
        ![(id)pipeline maliComputeProgram]) {
        [_buffer recordUnsupported:"Compute state has no validated native kernel program"];
        return;
    }
    if ([_buffer useResource:(id)pipeline])
        _pipeline = (id)pipeline;
}
- (void)setBuffer:(id<MTLBuffer>)buffer offset:(NSUInteger)offset atIndex:(NSUInteger)index {
    [self requireActive];
    if (index >= 31 || (buffer && offset > [buffer length])) {
        [NSException raise:@"NSInvalidArgumentException" format:@"Invalid compute buffer binding"];
        return;
    }
    if (buffer &&
        (![(id)buffer respondsToSelector:@selector(maliBuffer)] || ![(id)buffer maliBuffer])) {
        [_buffer recordUnsupported:"Compute buffer has no native allocation"];
        return;
    }
    if ([_buffer useResource:(id)buffer]) {
        _buffers[index] = buffer;
        _offsets[index] = offset;
    }
}
- (void)setBufferOffset:(NSUInteger)offset atIndex:(NSUInteger)index {
    [self requireActive];
    if (index >= 31) {
        [NSException raise:@"NSInvalidArgumentException"
                    format:@"Compute buffer index exceeds limit"];
        return;
    }
    [self setBuffer:_buffers[index] offset:offset atIndex:index];
}
- (void)setBuffers:(const id<MTLBuffer> *)buffers
           offsets:(const NSUInteger *)offsets
         withRange:(NSRange)range {
    [self requireActive];
    if (range.location > 31 || range.length > 31 - range.location ||
        (range.length && (!buffers || !offsets))) {
        [NSException raise:@"NSInvalidArgumentException" format:@"Invalid compute buffer range"];
        return;
    }
    for (NSUInteger i = 0; i < range.length; i++)
        [self setBuffer:buffers[i] offset:offsets[i] atIndex:range.location + i];
}
- (void)setBytes:(const void *)bytes length:(NSUInteger)length atIndex:(NSUInteger)index {
    [self requireActive];
    id<MTLBuffer> buffer = [(MaliMetalDevice *)[self device] newBufferWithBytes:bytes
                                                                         length:length
                                                                        options:0];
    if (!buffer) {
        [_buffer recordBackendResult:MMVK_ERROR_MEMORY error:NULL];
        return;
    }
    if ([_buffer useOwnedResource:(id)buffer])
        [self setBuffer:buffer offset:0 atIndex:index];
    [(id)buffer release];
}
- (void)setTexture:(id<MTLTexture>)texture atIndex:(NSUInteger)index {
    [self requireActive];
    if (index >= 128) {
        [NSException raise:@"NSInvalidArgumentException"
                    format:@"Compute texture index exceeds limit"];
        return;
    }
    if (texture &&
        (![(id)texture respondsToSelector:@selector(maliImage)] || ![(id)texture maliImage])) {
        [_buffer recordUnsupported:"Compute texture has no native image"];
        return;
    }
    if ([_buffer useResource:(id)texture])
        _textures[index] = texture;
}
- (void)setTextures:(const id<MTLTexture> *)textures withRange:(NSRange)range {
    [self requireActive];
    if (range.location > 128 || range.length > 128 - range.location ||
        (range.length && !textures)) {
        [NSException raise:@"NSInvalidArgumentException" format:@"Invalid compute texture range"];
        return;
    }
    for (NSUInteger i = 0; i < range.length; i++)
        [self setTexture:textures[i] atIndex:range.location + i];
}
- (void)setSamplerState:(id<MTLSamplerState>)sampler atIndex:(NSUInteger)index {
    [self requireActive];
    if (index >= 16) {
        [NSException raise:@"NSInvalidArgumentException"
                    format:@"Compute sampler index exceeds limit"];
        return;
    }
    if (sampler &&
        (![(id)sampler respondsToSelector:@selector(maliSampler)] || ![(id)sampler maliSampler])) {
        [_buffer recordUnsupported:"Compute sampler has no native state"];
        return;
    }
    /* Unused bindings are harmless. A required descriptor absent from the
     * native compiler plan is refused at pipeline creation. */
    if ([_buffer useResource:(id)sampler])
        _samplers[index] = sampler;
}
- (void)setSamplerStates:(const id<MTLSamplerState> *)samplers withRange:(NSRange)range {
    [self requireActive];
    if (range.location > 16 || range.length > 16 - range.location || (range.length && !samplers)) {
        [NSException raise:@"NSInvalidArgumentException" format:@"Invalid compute sampler range"];
        return;
    }
    for (NSUInteger i = 0; i < range.length; i++)
        [self setSamplerState:samplers[i] atIndex:range.location + i];
}
- (void)setThreadgroupMemoryLength:(NSUInteger)length atIndex:(NSUInteger)index {
    [self requireActive];
    if (index >= 31 || length > UINT32_MAX || (length & 15)) {
        [NSException raise:@"NSInvalidArgumentException"
                    format:@"Invalid threadgroup memory binding"];
        return;
    }
    _threadgroupLengths[index] = (uint32_t)length;
}
- (void)dispatchThreadgroups:(MTLSize)groups threadsPerThreadgroup:(MTLSize)threads {
    [self encodeDispatch:MM_SHADER_DISPATCH_THREADGROUPS grid:groups threadsPerThreadgroup:threads];
}
- (void)dispatchThreads:(MTLSize)grid threadsPerThreadgroup:(MTLSize)threads {
    [self encodeDispatch:MM_SHADER_DISPATCH_THREADS grid:grid threadsPerThreadgroup:threads];
}
- (void)encodeDispatch:(uint32_t)kind grid:(MTLSize)grid threadsPerThreadgroup:(MTLSize)threads {
    [self requireActive];
    if (!_pipeline) {
        [_buffer recordUnsupported:"Bind a validated compute pipeline before dispatch"];
        return;
    }
    if (!threads.width || !threads.height || !threads.depth || threads.width > UINT32_MAX ||
        threads.height > UINT32_MAX || threads.depth > UINT32_MAX || grid.width > UINT32_MAX ||
        grid.height > UINT32_MAX || grid.depth > UINT32_MAX) {
        [_buffer recordUnsupported:"Compute grid or threadgroup size exceeds the native ABI"];
        return;
    }
    if (!grid.width || !grid.height || !grid.depth)
        return;
    const MMShaderComputeOutput *output = [_pipeline maliComputeOutput];
    if (!output || !output->prepare_dispatch || output->binding_count > MM_SHADER_MAX_BINDINGS) {
        [_buffer recordUnsupported:"Compute pipeline has no native dispatch planner"];
        return;
    }
    const MMVKCapabilities *caps = [(MaliMetalDevice *)[self device] backendCapabilities];
    uint64_t axes[] = {threads.width, threads.height, threads.depth}, total = 1;
    for (uint32_t i = 0; i < 3; i++) {
        if (!caps || axes[i] > caps->max_compute_workgroup_size[i] ||
            axes[i] > output->max_total_threads_per_threadgroup / total) {
            [_buffer
                recordUnsupported:"Threadgroup size exceeds the compiled kernel or device limits"];
            return;
        }
        total *= axes[i];
    }
    MMShaderVulkanComputeResources resources = {0};
    for (NSUInteger i = 0; i < 31; i++) {
        resources.buffers[i] = (MMShaderVulkanBuffer){[(id)_buffers[i] maliBuffer], _offsets[i]};
        resources.logical_lengths[i] = [_buffers[i] length];
        resources.threadgroup_lengths[i] = _threadgroupLengths[i];
    }
    for (NSUInteger i = 0; i < 128; i++)
        resources.textures[i] = [(id)_textures[i] maliImage];
    MMVKBuffer *const *constants = [_pipeline maliComputeConstants];
    if (constants)
        memcpy(resources.constant_buffers, constants, sizeof(resources.constant_buffers));
    for (uint32_t i = 0; i < output->binding_count; i++) {
        const MMShaderComputeBinding *binding = output->bindings + i;
        if (binding->source_kind == MM_SHADER_COMPUTE_BUFFER && binding->write &&
            binding->metal_index < 31 && _buffers[binding->metal_index])
            [_buffer writeBuffer:_buffers[binding->metal_index]];
        if (binding->source_kind == MM_SHADER_COMPUTE_TEXTURE) {
            if (binding->metal_index >= 128 || !_textures[binding->metal_index]) {
                [_buffer recordUnsupported:"Required compute texture is unbound"];
                return;
            }
            id<MTLTexture> texture = _textures[binding->metal_index];
            MTLTextureUsage usage = [texture usage];
            if (usage && ((binding->read && !(usage & MTLTextureUsageShaderRead)) ||
                          (binding->write && !(usage & MTLTextureUsageShaderWrite)))) {
                [_buffer
                    recordUnsupported:"Compute texture usage does not permit its compiled access"];
                return;
            }
            if (![_buffer prepareTexture:texture])
                return;
            if (binding->write && ![_buffer writeTexture:texture])
                return;
        }
    }
    MMShaderComputeBindings facts = {0};
    MMVKError nativeError = {0};
    MMVKResult result = MMShaderVulkanComputeBindingFacts(&resources, &facts, &nativeError);
    if (result != MMVK_OK) {
        [_buffer recordBackendResult:result error:&nativeError];
        return;
    }
    MMShaderComputeDispatchRequest request = {
        kind,
        {(uint32_t)grid.width, (uint32_t)grid.height, (uint32_t)grid.depth},
        {(uint32_t)threads.width, (uint32_t)threads.height, (uint32_t)threads.depth}};
    MMShaderComputeDispatchPlan plan = {0};
    MMShaderError shaderError = {0};
    MMShaderStatus status =
        MMShaderPrepareComputeDispatch(output, &facts, &request, &plan, &shaderError);
    if (status != MM_SHADER_OK) {
        nativeError.code =
            status == MM_SHADER_OUT_OF_MEMORY ? MMVK_ERROR_MEMORY : MMVK_ERROR_SHADER;
        nativeError.native_code = status;
        size_t n = strlen(shaderError.message);
        if (n >= sizeof(nativeError.message))
            n = sizeof(nativeError.message) - 1;
        memcpy(nativeError.message, shaderError.message, n);
        nativeError.message[n] = 0;
        [_buffer recordBackendResult:nativeError.code error:&nativeError];
        MMShaderComputeDispatchPlanDestroy(&plan);
        return;
    }
    if (plan.abi_version != MM_SHADER_COMPUTE_ABI_VERSION || (plan.region_count && !plan.regions) ||
        (plan.descriptor_count && !plan.descriptors)) {
        [_buffer recordUnsupported:"Native compiler returned an invalid compute dispatch plan"];
        MMShaderComputeDispatchPlanDestroy(&plan);
        return;
    }
    MaliMetalComputeDispatchRecord *record =
        [[MaliMetalComputeDispatchRecord alloc] initWithPlan:&plan];
    if (!record) {
        MMShaderComputeDispatchPlanDestroy(&plan);
        [_buffer recordBackendResult:MMVK_ERROR_MEMORY error:NULL];
        return;
    }
    void *rawPipeline = (void *)_pipeline;
    [_buffer appendOperation:^MMVKResult(MMVKCommands *commands, MMVKError *error) {
        const MMShaderComputeDispatchPlan *dispatch = [record plan];
        id pipeline = (id)rawPipeline;
        for (uint32_t r = 0; r < dispatch->region_count; r++) {
            const MMShaderComputeRegion *region = dispatch->regions + r;
            MMVKPipeline *variant = [pipeline maliComputeVariantForRegion:region error:error];
            if (!variant)
                return error->code ? error->code : MMVK_ERROR_SHADER;
            MMVKResult native = mmvk_commands_bind_pipeline(commands, variant, error);
            if (native == MMVK_OK)
                native = MMShaderVulkanBindComputeResources(commands, output, &resources, dispatch,
                                                            error);
            if (native == MMVK_OK)
                native = MMShaderVulkanPushComputeRegion(commands, output, region, error);
            if (native == MMVK_OK)
                native =
                    mmvk_commands_dispatch(commands, region->group_count[0], region->group_count[1],
                                           region->group_count[2], error);
            if (native != MMVK_OK)
                return native;
        }
        return MMVK_OK;
    }];
    [record release];
}
- (void)endEncoding {
    [self requireActive];
    _ended = YES;
    [_buffer endEncoder];
}
@end
