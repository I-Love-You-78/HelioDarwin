/* SPDX-License-Identifier: MIT */
#import "MaliMetalRender.h"
#import "MaliMetalDevice.h"
#import "MaliMetalVulkan.h"
@implementation MaliMetalRenderProgram
- (instancetype)initWithDevice:(MaliMetalDevice *)device error:(MMVKError *)error {
    self=[super init];if(!self)return nil;
    _device=[device retain];
    if(MMShaderVulkanRenderCacheCreate([_device backendDevice],&_cache,error)!=MMVK_OK){[self release];return nil;}
    return self;
}
- (void)dealloc {MMShaderVulkanRenderCacheDestroy(_cache);[_device release];[super dealloc];}
- (id<MTLDevice>)device {return _device;}
- (MMShaderVulkanRenderCache *)cache {return _cache;}
@end
@implementation MaliMetalRenderDraw
- (instancetype)initWithPipeline:(id)pipeline program:(MaliMetalRenderProgram *)program
                       resources:(const MMShaderVulkanBindings *)resources
                        bindings:(const MMShaderRenderBindings *)bindings
                         request:(const MMShaderRenderDrawRequest *)request
                           state:(const MMVKGraphicsState *)state
                            pass:(const MMVKPassLayout *)pass error:(MMVKError *)error {
    self=[super init];if(!self)return nil;
    if(!pipeline||![program isKindOfClass:[MaliMetalRenderProgram class]]||
       ![pipeline respondsToSelector:@selector(device)]||
       ![pipeline respondsToSelector:@selector(maliRenderProgram)]||
       ![pipeline respondsToSelector:@selector(maliShaderOutput)]||
       ![program device]||[pipeline device]!=[program device]||[pipeline maliRenderProgram]!=program){
        if(error)*error=(MMVKError){.code=MMVK_ERROR_ARGUMENT,
            .message="Render draw requires its pipeline's owning program and device"};
        [self release];return nil;
    }
    _pipeline=[pipeline retain];_program=[program retain];
    if(MMShaderVulkanPrepareRender([program cache],[pipeline maliShaderOutput],resources,bindings,
        request,state,pass,&_execution,error)!=MMVK_OK){[self release];return nil;}
    return self;
}
- (void)dealloc {MMShaderVulkanRenderExecutionDestroy(_execution);[_program release];[_pipeline release];[super dealloc];}
- (id<MTLDevice>)device {return [_program device];}
- (MMVKResult)record:(MMVKCommands *)commands indices:(MMVKBuffer *)indices error:(MMVKError *)error {
    return MMShaderVulkanRecordRender(commands,_execution,indices,error);
}
@end
