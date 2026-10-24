/* SPDX-License-Identifier: MIT */
#pragma once
#import "MaliMetalTypes.h"
#include "Vulkan/MaliVulkanBackend.h"
#include <pthread.h>
typedef NSUInteger MaliMetalPresentationMode;
enum { MaliMetalPresentImmediately=0, MaliMetalPresentAtTime=1,
       MaliMetalPresentAfterMinimumDuration=2 };
/* The real CAMetalDrawable owns display scheduling, presentedTime, drawableID
 * and presented handlers. Preserve its timestamp and pacing contract. */
@interface MaliMetalPresentation : NSObject {
    id<MTLDrawable> _drawable;
    MaliMetalPresentationMode _mode;
    CFTimeInterval _time;
    BOOL _consumed;
    pthread_mutex_t _presentationLock;
}
- (instancetype)initWithDrawable:(id<MTLDrawable>)drawable
                           mode:(MaliMetalPresentationMode)mode time:(CFTimeInterval)time;
- (id<MTLDrawable>)drawable;
/* Call only after real GPU completion AND IOSurface copy/unlock succeeds.
 * Exactly once. Submission/readback failure cancels by releasing the request. */
- (MMVKResult)presentAfterGPUCompletion:(MMVKError *)error;
@end
