#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __OBJC__
#include <objc/runtime.h>
#include <objc/message.h>
#endif

typedef unsigned long NSUInteger;
typedef long NSInteger;
#ifndef BOOL
#ifndef _OBJC_OBJC_H_
typedef signed char BOOL;
#define YES ((BOOL)1)
#define NO ((BOOL)0)
#endif
#endif

typedef double CFTimeInterval;

typedef uint32_t mach_port_t;
typedef mach_port_t io_connect_t;
typedef mach_port_t io_service_t;
typedef int kern_return_t;
#define KERN_SUCCESS 0
#define MACH_PORT_NULL 0

typedef uint64_t mach_vm_address_t;
typedef uint64_t mach_vm_size_t;
#define kIOMapAnywhere 0x00000001

#ifndef __OBJC__
typedef void *id;
#endif

extern mach_port_t mach_task_self(void);
extern kern_return_t IOServiceOpen(mach_port_t service, mach_port_t owningTask, uint32_t type, io_connect_t *connect);
extern kern_return_t IOServiceClose(io_connect_t connect);
extern kern_return_t IORegistryEntryGetRegistryEntryID(io_service_t entry, uint64_t *entry_id);
extern kern_return_t IOConnectMapMemory64(io_connect_t connect, uint32_t memory_type, mach_port_t into_task, mach_vm_address_t *at_address, mach_vm_size_t *of_size, uint32_t options);
extern kern_return_t IOConnectUnmapMemory64(io_connect_t connect, uint32_t memory_type, mach_port_t from_task, mach_vm_address_t at_address);
extern void NSLog(id format, ...);
extern const void *CFRetain(const void *object);
extern void CFRelease(const void *object);

typedef struct {
    NSUInteger width;
    NSUInteger height;
    NSUInteger depth;
} MTLSize;

static inline MTLSize MTLSizeMake(NSUInteger width, NSUInteger height, NSUInteger depth) {
    MTLSize size;
    size.width = width;
    size.height = height;
    size.depth = depth;
    return size;
}

typedef struct {
    NSUInteger x;
    NSUInteger y;
    NSUInteger z;
} MTLOrigin;

static inline MTLOrigin MTLOriginMake(NSUInteger x, NSUInteger y, NSUInteger z) {
    MTLOrigin origin;
    origin.x = x;
    origin.y = y;
    origin.z = z;
    return origin;
}

typedef struct {
    MTLOrigin origin;
    MTLSize size;
} MTLRegion;

static inline MTLRegion MTLRegionMake2D(NSUInteger x, NSUInteger y, NSUInteger width, NSUInteger height) {
    MTLRegion region;
    region.origin.x = x;
    region.origin.y = y;
    region.origin.z = 0;
    region.size.width = width;
    region.size.height = height;
    region.size.depth = 1;
    return region;
}

typedef enum {
    MTLGPUFamilyApple1 = 1001,
    MTLGPUFamilyApple2 = 1002,
    MTLGPUFamilyApple3 = 1003,
    MTLGPUFamilyApple4 = 1004,
    MTLGPUFamilyApple5 = 1005,
    MTLGPUFamilyApple6 = 1006,
    MTLGPUFamilyApple7 = 1007,
    MTLGPUFamilyApple8 = 1008,
    MTLGPUFamilyCommon1 = 3001,
    MTLGPUFamilyCommon2 = 3002,
    MTLGPUFamilyCommon3 = 3003,
    MTLGPUFamilyMetal3 = 5001,
} MTLGPUFamily;

typedef enum {
    MTLFeatureSet_iOS_GPUFamily1_v1 = 0,
    MTLFeatureSet_iOS_GPUFamily2_v1 = 1,
    MTLFeatureSet_iOS_GPUFamily3_v1 = 2,
    MTLFeatureSet_iOS_GPUFamily4_v1 = 3,
} MTLFeatureSet;

typedef enum {
    MTLPixelFormatInvalid = 0,
    MTLPixelFormatRGBA8Unorm = 70,
    MTLPixelFormatRGBA8Unorm_sRGB = 71,
    MTLPixelFormatBGRA8Unorm = 80,
    MTLPixelFormatBGRA8Unorm_sRGB = 81,
    MTLPixelFormatRGB10A2Unorm = 90,
    MTLPixelFormatBGR10A2Unorm = 91,
} MTLPixelFormat;

typedef enum {
    MTLTextureType2D = 2,
    MTLTextureType2DMultisample = 4,
} MTLTextureType;

typedef enum {
    MTLTextureUsageUnknown = 0x0000,
    MTLTextureUsageShaderRead = 0x0001,
    MTLTextureUsageShaderWrite = 0x0002,
    MTLTextureUsageRenderTarget = 0x0004,
    MTLTextureUsagePixelFormatView = 0x0010,
} MTLTextureUsage;

typedef enum {
    MTLResourceCPUCacheModeDefaultCache = 0,
    MTLResourceStorageModeShared = 0 << 4,
    MTLResourceHazardTrackingModeDefault = 0 << 8,
} MTLResourceOptions;

typedef enum {
    MTLCommandBufferStatusNotEnqueued = 0,
    MTLCommandBufferStatusEnqueued = 1,
    MTLCommandBufferStatusCommitted = 2,
    MTLCommandBufferStatusScheduled = 3,
    MTLCommandBufferStatusCompleted = 4,
    MTLCommandBufferStatusError = 5,
} MTLCommandBufferStatus;

typedef struct {
    double originX;
    double originY;
    double width;
    double height;
    double znear;
    double zfar;
} MTLViewport;

typedef struct {
    NSUInteger x;
    NSUInteger y;
    NSUInteger width;
    NSUInteger height;
} MTLScissorRect;

typedef enum {
    MTLPrimitiveTypePoint = 0,
    MTLPrimitiveTypeLine = 1,
    MTLPrimitiveTypeLineStrip = 2,
    MTLPrimitiveTypeTriangle = 3,
    MTLPrimitiveTypeTriangleStrip = 4,
} MTLPrimitiveType;

typedef enum {
    MTLFunctionTypeVertex = 1,
    MTLFunctionTypeFragment = 2,
    MTLFunctionTypeKernel = 3,
} MTLFunctionType;

#ifdef __OBJC__

@class NSMethodSignature;
@class NSInvocation;

@interface NSObject
+ (id)alloc;
- (id)init;
- (void)dealloc;
- (id)retain;
- (id)autorelease;
- (oneway void)release;
- (id)copy;
- (BOOL)respondsToSelector:(SEL)aSelector;
- (NSMethodSignature *)methodSignatureForSelector:(SEL)aSelector;
- (void)forwardInvocation:(NSInvocation *)anInvocation;
@end

@interface NSMethodSignature : NSObject
+ (NSMethodSignature *)signatureWithObjCTypes:(const char *)types;
- (NSUInteger)methodReturnLength;
- (const char *)methodReturnType;
@end

@interface NSInvocation : NSObject
- (NSMethodSignature *)methodSignature;
- (void)setReturnValue:(void *)retLoc;
- (void)invoke;
@end

@class NSString, NSError, NSDictionary, NSArray, NSData;
@interface NSDictionary : NSObject
+ (instancetype)dictionaryWithObject:(id)object forKey:(id)key;
@end
@interface NSError : NSObject
+ (instancetype)errorWithDomain:(NSString *)domain code:(NSInteger)code userInfo:(NSDictionary *)userInfo;
@end
@interface NSException : NSObject
+ (void)raise:(NSString *)name format:(NSString *)format, ...;
@end
typedef void *dispatch_data_t;

/*
 * Base class for all MaliMetal objects providing:
 * 1. NSString *label support
 * Unsupported selectors retain NSObject's normal lookup/error behavior.
 */
@interface MaliMetalObject : NSObject {
    NSString *_label;
}
@property (copy) NSString *label;
- (NSString *)label;
- (void)setLabel:(NSString *)label;
@end

@protocol MTLDevice @end
@protocol MTLCommandQueue @end
@protocol MTLCommandBuffer @end
@protocol MTLResource @end
@protocol NSObject
- (BOOL)respondsToSelector:(SEL)aSelector;
@end

@protocol MTLBuffer <MTLResource>
- (void *)contents;
- (NSUInteger)length;
- (uint64_t)gpuAddress;
@end

@protocol MTLTexture <MTLResource>
- (NSUInteger)width;
- (NSUInteger)height;
- (void *)pixels;
- (NSUInteger)bytesPerRow;
- (MTLPixelFormat)pixelFormat;
- (MTLTextureType)textureType;
- (MTLTextureUsage)usage;
- (BOOL)isFramebufferOnly;
- (void *)iosurface;
- (NSUInteger)iosurfacePlane;
@end

@protocol MTLSamplerState @end
@protocol MTLLibrary @end
@protocol MTLRenderPipelineState @end
@protocol MTLComputePipelineState @end
@protocol MTLFunction @end
@protocol MTLDrawable
- (void)present;
- (void)presentAtTime:(CFTimeInterval)presentationTime;
@end
@protocol MTLRenderCommandEncoder @end
@protocol MTLComputeCommandEncoder @end
@protocol MTLBlitCommandEncoder @end

typedef void (^MTLCommandBufferHandler)(id<MTLCommandBuffer>);

@interface MTLTextureDescriptor : NSObject
@property (nonatomic) NSUInteger width;
@property (nonatomic) NSUInteger height;
@property (nonatomic) MTLPixelFormat pixelFormat;
@property (nonatomic) MTLTextureType textureType;
@property (nonatomic) MTLTextureUsage usage;
@property (nonatomic) NSUInteger depth;
@property (nonatomic) NSUInteger mipmapLevelCount;
@property (nonatomic) NSUInteger sampleCount;
@property (nonatomic) NSUInteger arrayLength;
+ (MTLTextureDescriptor *)texture2DDescriptorWithPixelFormat:(MTLPixelFormat)pixelFormat width:(NSUInteger)width height:(NSUInteger)height mipmapped:(BOOL)mipmapped;
@end

@class MTLSamplerDescriptor;
@class MTLCompileOptions;
@class MTLRenderPipelineDescriptor;

@interface MTLRenderPassColorAttachmentDescriptor : NSObject
- (id<MTLTexture>)texture;
- (void)setTexture:(id<MTLTexture>)texture;
@end

@interface MTLRenderPassColorAttachmentDescriptorArray : NSObject
- (MTLRenderPassColorAttachmentDescriptor *)objectAtIndexedSubscript:(NSUInteger)attachmentIndex;
@end

@interface MTLRenderPassDescriptor : NSObject
- (MTLRenderPassColorAttachmentDescriptorArray *)colorAttachments;
@end
#endif
