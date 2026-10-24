/* SPDX-License-Identifier: MIT */
#import "MaliMetalRender.h"
#import "MaliMetalDevice.h"
#import "MaliMetalVulkan.h"
@implementation MaliMetalRenderProgram
- (instancetype)initWithDevice:(MaliMetalDevice *)device error:(MMVKError *)error {
    self=[super init];if(!self)return nil;
    if(MMShaderVulkanRenderCacheCreate([device backendDevice],&_cache,error)!=MMVK_OK){[self release];return nil;}
    _device=[device retain];return self;
}
- (void)dealloc {MMShaderVulkanRenderCacheDestroy(_cache);[_device release];[super dealloc];}
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
    _pipeline=[pipeline retain];_program=[program retain];
    if(MMShaderVulkanPrepareRender([program cache],[pipeline maliShaderOutput],resources,bindings,
        request,state,pass,&_execution,error)!=MMVK_OK){[self release];return nil;}
    return self;
}
- (void)dealloc {MMShaderVulkanRenderExecutionDestroy(_execution);[_program release];[_pipeline release];[super dealloc];}
- (MMVKResult)record:(MMVKCommands *)commands indices:(MMVKBuffer *)indices error:(MMVKError *)error {
    return MMShaderVulkanRecordRender(commands,_execution,indices,error);
}
@end
