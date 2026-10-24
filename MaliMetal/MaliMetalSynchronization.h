/* SPDX-License-Identifier: MIT */
#pragma once
#import "MaliMetalDevice.h"
#import "MaliMetalCommandQueue.h"
#include "Vulkan/MaliVulkanSynchronization.h"
#include <pthread.h>

@protocol MTLEvent
- (id<MTLDevice>)device;
@end
@protocol MTLFence
- (id<MTLDevice>)device;
@end
@interface MaliMetalEvent : MaliMetalObject <MTLEvent> {
    MaliMetalDevice *_eventDevice;
    MMVKEvent *_event;
}
- (instancetype)initWithDevice:(MaliMetalDevice *)device;
- (id<MTLDevice>)device;
- (MMVKEvent *)maliEvent;
@end
/* Fences reserve generations in queue publication order. Failed unpublished
 * generations propagate errors; they never advance the native timeline. */
@interface MaliMetalFence : MaliMetalEvent <MTLFence> {
    pthread_mutex_t _generationLock;
    BOOL _generationLockInitialized;
    uint64_t _generation;
    uint64_t _failedGeneration;
    MMVKError _generationFailure;
}
- (BOOL)reserveUpdate:(uint64_t *)value;
- (uint64_t)waitValue;
- (MMVKResult)failureForValue:(uint64_t)value error:(MMVKError *)error;
- (void)failUnsubmittedValue:(uint64_t)value result:(MMVKResult)result error:(const MMVKError *)error;
- (MMVKResult)waitForValue:(uint64_t)value timeout:(uint64_t)timeout error:(MMVKError *)error;
@end
@interface MaliMetalFenceCommand : NSObject {
    MaliMetalFence *_fence;
    uint64_t _value;
    BOOL _update, _prepared;
}
- (instancetype)initWithFence:(MaliMetalFence *)fence update:(BOOL)update;
- (BOOL)prepareForSubmission;
- (MMVKResult)prepareForSubmissionWithError:(MMVKError *)error;
- (void)failUnsubmittedWithResult:(MMVKResult)result error:(const MMVKError *)error;
- (MaliMetalFence *)fence;
- (uint64_t)value;
- (BOOL)isUpdate;
@end
@interface MaliMetalFenceSubmission : NSObject {
    MaliMetalFenceCommand **_commands;
    NSUInteger _count;
    BOOL _prepared;
}
- (MaliMetalFenceCommand *)addFence:(MaliMetalFence *)fence update:(BOOL)update;
/* Call once when draining queue FIFO reservations, before waking a worker.
 * Serializes fence generations across queues without reversing enqueue order
 * when a later reserved buffer is committed before an earlier one. */
- (BOOL)prepareForSubmission;
- (MMVKResult)prepareForSubmissionWithError:(MMVKError *)error;
- (void)failUnsubmittedWithResult:(MMVKResult)result error:(const MMVKError *)error;
@end
@interface MaliMetalDevice (Synchronization)
- (id<MTLEvent>)newEvent;
- (id<MTLFence>)newFence;
- (id)newSharedEvent;
- (id)newSharedEventWithHandle:(id)handle;
@end

/* Central command-buffer implementation supplies this method because it owns
 * _commands, encoder-state validation, and its native segment stream. */
@interface MaliMetalCommandBuffer (SynchronizationIntegration)
- (void)appendSynchronizationWithEvent:(id)event
                                 value:(uint64_t)value signal:(BOOL)signal;
- (NSUInteger)synchronizationOperationCount;
- (void)insertFence:(MaliMetalFence *)fence update:(BOOL)update
  atOperationIndex:(NSUInteger)index;
- (void)recordExecutedWrite:(id)resource;
- (void)recordExecutedCompletion:(id)resource;
@end
@interface MaliMetalCommandBuffer (Synchronization)
- (void)encodeWaitForEvent:(id<MTLEvent>)event value:(uint64_t)value;
- (void)encodeSignalEvent:(id<MTLEvent>)event value:(uint64_t)value;
@end

/* Whole-encoder fences conservatively satisfy vertex/fragment stage scopes.
 * Create before recording the encoder's begin operation; end after recording
 * its native end operation. Waits insert before begin; updates append at end. */
@interface MaliMetalEncoderSynchronization : NSObject {
    void *_commandBuffer;
    NSUInteger _waitInsertion;
    MaliMetalFence **_updates;
    NSUInteger _updateCount;
    BOOL _ended;
}
- (instancetype)initWithBuffer:(MaliMetalCommandBuffer *)buffer;
- (void)waitForFence:(id<MTLFence>)fence;
- (void)updateFence:(id<MTLFence>)fence;
- (void)endEncoding;
@end

/* Caller holds the execution mutex. Signal submits preceding commands. Wait
 * submits preceding commands, releases execution mutex during the native wait,
 * then reacquires it before recording the next segment. The first accepted
 * submission callback runs outside the execution mutex before any CPU wait.
 * No VkQueue lock is held during waits. Timeout never signals an event. */
MMVKResult MaliMetalSynchronizationBoundary(MaliMetalDevice *, MMVKSyncStream **,
    MMVKCommands **, MaliMetalEvent *, uint64_t, BOOL signal,void (^didSubmitPrefix)(void),MMVKError *);
MMVKResult MaliMetalWaitForEvent(MaliMetalDevice *, MaliMetalEvent *, uint64_t, MMVKError *);

/* Executed-operation bookkeeping, never the whole encoded output list. Only
 * noteWrite/noteCompletion reached while replaying operations enter a prefix. */
@interface MaliMetalExecutedResources : NSObject {
    void *_writes;
    NSUInteger _writeCount;
    id *_completions;
    NSUInteger _completionCount;
    uint64_t _generation;
}
- (MMVKResult)noteWrite:(id)resource error:(MMVKError *)error;
- (MMVKResult)noteCompletion:(id)resource error:(MMVKError *)error;
- (MMVKResult)publishExecutedPrefix:(MMVKError *)error;
- (MMVKResult)refreshConsumerResources:(id const *)resources count:(NSUInteger)count
                                error:(MMVKError *)error;
@end

BOOL MaliMetalSharedEventTransportReady(void);
BOOL MaliMetalIsNativeSharedEvent(id event);
MMVKResult MaliMetalSharedEventPublish(id event,uint64_t value,MMVKError *);
MMVKResult MaliMetalSharedEventWait(id event,uint64_t value,uint64_t timeout_ns,MMVKError *);
/* Caller holds execution mutex. Submits/drains the actual prefix, publishes
 * executed writes before external signal, waits outside execution mutex, then
 * refreshes future consumers through the caller's queue-owned resource list. */
MMVKResult MaliMetalSharedEventBoundary(MaliMetalDevice *,MMVKSyncStream **,
    MMVKCommands **,id event,uint64_t value,BOOL signal,MaliMetalExecutedResources *,
    void (^didSubmitPrefix)(void),MMVKResult (^refreshConsumers)(MMVKError *),MMVKError *);
