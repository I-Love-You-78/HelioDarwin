/* SPDX-License-Identifier: MIT */
#pragma once
#import "MaliMetalDevice.h"
#include "Vulkan/MaliVulkanResources.h"
#include "Shader/MaliMetalRender.h"

@interface MaliMetalBuffer : MaliMetalObject <MTLBuffer> {
    MaliMetalDevice *_device; MMVKBuffer *_buffer; NSUInteger _length,_allocatedSize,_purgeableState;
    MTLStorageMode _storageMode; MTLCPUCacheMode _cpuCacheMode; MTLHazardTrackingMode _hazardMode;
    id _heap;
}
- (instancetype)initWithLength:(NSUInteger)length device:(id<MTLDevice>)device storageMode:(MTLStorageMode)mode;
- (instancetype)initWithBytes:(const void *)bytes length:(NSUInteger)length device:(id<MTLDevice>)device;
- (instancetype)initWithLength:(NSUInteger)length device:(MaliMetalDevice *)device options:(MTLResourceOptions)options heap:(id)heap;
- (MMVKBuffer *)maliBuffer;
- (NSUInteger)length;
- (void *)contents;
- (uint64_t)gpuAddress;
- (uint64_t)maliDeviceAddress;
- (uint64_t)maliResourceID;
- (NSUInteger)protectionOptions;
@end
typedef struct { uint8_t red,green,blue,alpha; } MaliMetalTextureSwizzleChannels;

@interface MaliMetalTexture : MaliMetalObject <MTLTexture> {
    MaliMetalDevice *_device; MMVKImage *_image; MaliMetalTexture *_parent; id _heap;
    MTLPixelFormat _pixelFormat; MTLTextureType _textureType; MTLTextureUsage _usage;
    MTLStorageMode _storageMode; MTLCPUCacheMode _cpuCacheMode; MTLHazardTrackingMode _hazardMode;
    NSUInteger _allocatedSize,_purgeableState,_bytesPerRow,_parentLevel,_parentSlice;
    uint32_t _swizzle[4];
    void *_pixels,*_iosurface; NSUInteger _iosurfacePlane; BOOL _surfaceTwoPlanes;
}
- (instancetype)initWithDescriptor:(MTLTextureDescriptor *)descriptor device:(id<MTLDevice>)device;
- (instancetype)initWithDescriptor:(MTLTextureDescriptor *)descriptor iosurface:(void *)surface plane:(NSUInteger)plane device:(id<MTLDevice>)device;
- (instancetype)initWithDescriptor:(MTLTextureDescriptor *)descriptor device:(MaliMetalDevice *)device heap:(id)heap;
- (MMVKImage *)maliImage;
- (uint64_t)maliResourceID;
- (NSUInteger)protectionOptions;
- (BOOL)maliPrepareImage:(BOOL)preserve error:(MMVKError *)error;
- (BOOL)maliReadbackImage:(MMVKError *)error;
- (MMShaderRenderTexture)maliRenderTextureFacts;
- (MaliMetalTextureSwizzleChannels)swizzle;
- (id<MTLTexture>)newTextureViewWithPixelFormat:(MTLPixelFormat)format textureType:(MTLTextureType)type levels:(NSRange)levels slices:(NSRange)slices swizzle:(MaliMetalTextureSwizzleChannels)channels;
- (id<MTLTexture>)newTextureViewWithPixelFormat:(MTLPixelFormat)format;
- (id<MTLTexture>)newTextureViewWithPixelFormat:(MTLPixelFormat)format textureType:(MTLTextureType)type levels:(NSRange)levels slices:(NSRange)slices;
@end
@interface MaliMetalSampler : MaliMetalObject <MTLSamplerState> {
    MaliMetalDevice *_device; MMVKSampler *_sampler; MMShaderRenderSampler _facts; BOOL _supportsArgumentBuffers;
}
- (instancetype)initWithDevice:(MaliMetalDevice *)device descriptor:(MTLSamplerDescriptor *)descriptor;
- (MMVKSampler *)maliSampler;
- (uint64_t)maliResourceID;
- (BOOL)supportArgumentBuffers;
- (MMShaderRenderSampler)maliRenderSamplerFacts;
@end
@protocol MTLHeap
@end

@interface MaliMetalHeap : MaliMetalObject <MTLHeap> {
    MaliMetalDevice *_device; MMVKHeap *_heap; MTLStorageMode _storageMode;
    MTLCPUCacheMode _cpuCacheMode; MTLHazardTrackingMode _hazardMode; NSUInteger _purgeableState;
}
- (instancetype)initWithDevice:(MaliMetalDevice *)device descriptor:(id)descriptor;
- (MMVKHeap *)maliHeap;
- (NSUInteger)protectionOptions;
- (id<MTLBuffer>)newBufferWithLength:(NSUInteger)length options:(MTLResourceOptions)options;
- (id<MTLTexture>)newTextureWithDescriptor:(MTLTextureDescriptor *)descriptor;
@end
/* Device factories belong to MaliMetalDevice.m; see private integration patch. */
BOOL MaliMetalResourceOptions(MTLResourceOptions,MTLStorageMode *,MTLCPUCacheMode *,MTLHazardTrackingMode *);
BOOL MaliMetalTextureDescriptorNative(MTLTextureDescriptor *,MMVKImageDescriptorEx *);

typedef struct { NSUInteger size,align; } MaliMetalResourceSizeAndAlign;
@interface MaliMetalDevice (MaliMetalHeapCreation)
- (id)newHeapWithDescriptor:(id)descriptor;
- (MaliMetalResourceSizeAndAlign)heapBufferSizeAndAlignWithLength:(NSUInteger)length options:(MTLResourceOptions)options;
- (MaliMetalResourceSizeAndAlign)heapTextureSizeAndAlignWithDescriptor:(MTLTextureDescriptor *)descriptor;
@end
