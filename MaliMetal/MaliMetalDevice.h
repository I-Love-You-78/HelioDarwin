/*
 * MaliMetal: Metal Installable Client Driver (ICD) for ARM Mali-G57 Valhall
 * Target: Apple Darwin / iOS 16.7.16 (20H392)
 *
 * Provides the MTLDevice implementation dynamically loaded by Metal.framework.
 */

#pragma once

#import "MaliMetalTypes.h"
#import "MaliMetalVulkan.h"

/* Forward-declare Metal.framework's base device class */
@interface _MTLDevice : NSObject
- (instancetype)init;
- (void)initLimits;
- (void)initFeatureQueries;
- (void)initGPUFamilySupport;
@end

@interface MaliMetalDevice : _MTLDevice <MTLDevice, MTLDeviceSPI>

@property(readonly) NSString *name;
@property(readonly) uint64_t registryID;
@property(readonly) MTLSize maxThreadsPerThreadgroup;
@property(readonly) BOOL hasUnifiedMemory;
@property(readonly) uint64_t recommendedMaxWorkingSetSize;
@property(readonly) uint64_t currentAllocatedSize;
@property(readonly) NSUInteger maxBufferLength;
@property(readonly) NSUInteger maxThreadgroupMemoryLength;
@property(readonly) volatile uint32_t *hardwareRegisters;
@property(readonly) void *ringBuffer;
@property(readonly) uint32_t gpuID;

/* Internal backend is unavailable until the native ICD verifies the kernel ABI. */
- (MMVKDevice *)backendDevice;
- (const MMVKCapabilities *)backendCapabilities;
- (const MMVKError *)backendError;
- (void)lockExecution;
- (void)unlockExecution;
- (void)accountAllocation:(NSUInteger)size release:(BOOL)release;
/* Lifecycle */
- (instancetype)initWithAcceleratorPort:(mach_port_t)acceleratorPort;

/* No Apple GPU profile is claimed by this partial frontend. */
- (NSUInteger)featureProfile;
+ (BOOL)featureProfile:(NSUInteger)profile supportsFeatureSet:(MTLFeatureSet)featureSet;

/* Feature Queries */
- (BOOL)supportsFeatureSet:(MTLFeatureSet)featureSet;
- (BOOL)supportsFamily:(MTLGPUFamily)gpuFamily;
- (NSUInteger)maxFragmentTextures;
- (NSUInteger)maxFragmentSamplers;

/* Command Queue Creation */
- (id<MTLCommandQueue>)newCommandQueue;
- (id<MTLCommandQueue>)newCommandQueueWithMaxCommandBufferCount:(NSUInteger)maxCommandBufferCount;

/* Resource Creation */
- (id<MTLBuffer>)newBufferWithLength:(NSUInteger)length options:(MTLResourceOptions)options;
- (id<MTLBuffer>)newBufferWithBytes:(const void *)pointer
                             length:(NSUInteger)length
                            options:(MTLResourceOptions)options;
- (id<MTLBuffer>)newBufferWithBytesNoCopy:(void *)pointer
                                   length:(NSUInteger)length
                                  options:(MTLResourceOptions)options
                              deallocator:(void (^)(void *, NSUInteger))deallocator;
- (id<MTLTexture>)newTextureWithDescriptor:(MTLTextureDescriptor *)descriptor;
- (id<MTLTexture>)newTextureWithDescriptor:(MTLTextureDescriptor *)descriptor
                                 iosurface:(void *)iosurface
                                     plane:(NSUInteger)plane;
- (id<MTLSamplerState>)newSamplerStateWithDescriptor:(MTLSamplerDescriptor *)descriptor;

/* Pipeline Compilation */
- (id<MTLLibrary>)newDefaultLibrary;
- (id<MTLLibrary>)newDefaultLibraryWithBundle:(NSBundle *)bundle error:(NSError **)error;
- (id<MTLLibrary>)newLibraryWithURL:(NSURL *)url error:(NSError **)error;
- (id<MTLLibrary>)newLibraryWithFile:(NSString *)path error:(NSError **)error;
- (id)newPipelineLibraryWithFilePath:(NSString *)path error:(NSError **)error;
- (id)newDepthStencilStateWithDescriptor:(id)descriptor;
- (id<MTLLibrary>)newLibraryWithSource:(NSString *)source
                               options:(MTLCompileOptions *)options
                                 error:(NSError **)error;
- (id<MTLLibrary>)newLibraryWithData:(dispatch_data_t)data error:(NSError **)error;
- (id<MTLRenderPipelineState>)newRenderPipelineStateWithDescriptor:
                                  (MTLRenderPipelineDescriptor *)descriptor
                                                             error:(NSError **)error;
- (id<MTLComputePipelineState>)newComputePipelineStateWithFunction:(id<MTLFunction>)computeFunction
                                                             error:(NSError **)error;

@end
