/*
 * MaliMetal: Metal Installable Client Driver (ICD) for ARM Mali-G57 Valhall
 * Target: Apple Darwin / iOS 16.7.16 (20H392)
 *
 * Provides the MTLDevice implementation dynamically loaded by Metal.framework.
 */

#pragma once

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <mach/mach.h>

@interface MaliMetalDevice : NSObject <MTLDevice>

@property (readonly) NSString *name;
@property (readonly) uint64_t registryID;
@property (readonly) MTLSize maxThreadsPerThreadgroup;
@property (readonly) BOOL hasUnifiedMemory;
@property (readonly) uint64_t recommendedMaxWorkingSetSize;
@property (readonly) uint64_t currentAllocatedSize;
@property (readonly) NSUInteger maxBufferLength;

/* Lifecycle */
- (instancetype)initWithAcceleratorPort:(mach_port_t)acceleratorPort;

/* Command Queue Creation */
- (id<MTLCommandQueue>)newCommandQueue;
- (id<MTLCommandQueue>)newCommandQueueWithMaxCommandBufferCount:(NSUInteger)maxCommandBufferCount;

/* Resource Creation */
- (id<MTLBuffer>)newBufferWithLength:(NSUInteger)length options:(MTLResourceOptions)options;
- (id<MTLBuffer>)newBufferWithBytes:(const void *)pointer length:(NSUInteger)length options:(MTLResourceOptions)options;
- (id<MTLTexture>)newTextureWithDescriptor:(MTLTextureDescriptor *)descriptor;
- (id<MTLSamplerState>)newSamplerStateWithDescriptor:(MTLSamplerDescriptor *)descriptor;

/* Pipeline Compilation */
- (id<MTLLibrary>)newDefaultLibrary;
- (id<MTLLibrary>)newLibraryWithSource:(NSString *)source options:(MTLCompileOptions *)options error:(NSError **)error;
- (id<MTLLibrary>)newLibraryWithData:(dispatch_data_t)data error:(NSError **)error;
- (id<MTLRenderPipelineState>)newRenderPipelineStateWithDescriptor:(MTLRenderPipelineDescriptor *)descriptor error:(NSError **)error;
- (id<MTLComputePipelineState>)newComputePipelineStateWithFunction:(id<MTLFunction>)computeFunction error:(NSError **)error;

@end
