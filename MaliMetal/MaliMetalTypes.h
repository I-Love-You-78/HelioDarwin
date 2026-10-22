#pragma once
#include <stdint.h>
#include <stddef.h>
#include <objc/runtime.h>
#include <objc/message.h>

#ifndef __OBJC__
#error "MaliMetal requires Objective-C"
#endif

typedef unsigned long NSUInteger;
typedef long NSInteger;

typedef uint32_t mach_port_t;
typedef mach_port_t io_connect_t;
typedef mach_port_t io_service_t;
typedef int kern_return_t;
#define KERN_SUCCESS 0
#define MACH_PORT_NULL 0

extern mach_port_t mach_task_self(void);
extern kern_return_t IOServiceOpen(mach_port_t service, mach_port_t owningTask, uint32_t type, io_connect_t *connect);
extern kern_return_t IOServiceClose(io_connect_t connect);
extern void NSLog(id format, ...);

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

@interface NSObject
+ (id)alloc;
- (id)init;
- (void)dealloc;
@end

@class NSString, NSError, NSDictionary, NSArray, NSData;
typedef void *dispatch_data_t;

@protocol MTLDevice @end
@protocol MTLCommandQueue @end
@protocol MTLCommandBuffer @end
@protocol MTLBuffer @end
@protocol MTLTexture @end
@protocol MTLSamplerState @end
@protocol MTLLibrary @end
@protocol MTLRenderPipelineState @end
@protocol MTLComputePipelineState @end
@protocol MTLFunction @end
@protocol MTLDrawable @end
@protocol MTLRenderCommandEncoder @end
@protocol MTLComputeCommandEncoder @end
@protocol MTLBlitCommandEncoder @end

@class MTLTextureDescriptor;
@class MTLSamplerDescriptor;
@class MTLCompileOptions;
@class MTLRenderPipelineDescriptor;
@class MTLRenderPassDescriptor;
