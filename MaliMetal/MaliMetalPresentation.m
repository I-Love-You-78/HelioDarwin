/* SPDX-License-Identifier: MIT */
#import "MaliMetalPresentation.h"
#include <math.h>
#include <string.h>

/* Present selectors are exported by iOS 16.7's drawable implementation. */
@interface NSObject (MaliMetalDrawablePresentation)
- (void)presentAfterMinimumDuration:(CFTimeInterval)duration;
@end
static MMVKResult presentation_error(MMVKError *error,MMVKResult result,const char *message) {
    if(error) {
        memset(error,0,sizeof(*error));error->code=result;
        size_t length=strlen(message);
        if(length>=sizeof(error->message)) length=sizeof(error->message)-1;
        memcpy(error->message,message,length);
    }
    return result;
}
@implementation MaliMetalPresentation
- (instancetype)initWithDrawable:(id<MTLDrawable>)drawable
                           mode:(MaliMetalPresentationMode)mode time:(CFTimeInterval)time {
    self=[super init];if(!self) return nil;
    if(pthread_mutex_init(&_presentationLock,NULL)) {
        /* No initialized ivars/resources to destroy on this failure path. */
        [super dealloc];return nil;
    }
    SEL selector=mode==MaliMetalPresentImmediately ? @selector(present) :
        mode==MaliMetalPresentAtTime ? @selector(presentAtTime:) : @selector(presentAfterMinimumDuration:);
    if(!drawable||mode>MaliMetalPresentAfterMinimumDuration||!isfinite(time)||time<0||
       ![(id)drawable respondsToSelector:selector]) {
        [self release];return nil;
    }
    _drawable=(id)[(id)drawable retain];_mode=mode;_time=time;return self;
}
- (void)dealloc {
    [(id)_drawable release];pthread_mutex_destroy(&_presentationLock);[super dealloc];
}
- (id<MTLDrawable>)drawable { return _drawable; }
- (MMVKResult)presentAfterGPUCompletion:(MMVKError *)error {
    pthread_mutex_lock(&_presentationLock);
    if(_consumed) {
        pthread_mutex_unlock(&_presentationLock);
        return presentation_error(error,MMVK_ERROR_STATE,"Drawable presentation was already consumed");
    }
    _consumed=YES;pthread_mutex_unlock(&_presentationLock);
    switch(_mode) {
        case MaliMetalPresentImmediately: [_drawable present];break;
        case MaliMetalPresentAtTime: [_drawable presentAtTime:_time];break;
        case MaliMetalPresentAfterMinimumDuration: [(id)_drawable presentAfterMinimumDuration:_time];break;
        default: return presentation_error(error,MMVK_ERROR_STATE,"Unknown drawable presentation mode");
    }
    return MMVK_OK;
}
@end
