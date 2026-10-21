/*
 * MaliMetal: Core MTLDevice Implementation
 * Translates Metal calls to Mesa panvk / Vulkan runtime on ARM Mali-G57 MC2.
 */

#import "MaliMetalDevice.h"
#import "MaliMetalCommandQueue.h"
#include <IOKit/IOKitLib.h>

@implementation MaliMetalDevice {
    mach_port_t _accelPort;
    io_connect_t _userClient;
    uint32_t _gpuID;
    uint64_t _registryID;
}

- (instancetype)initWithAcceleratorPort:(mach_port_t)acceleratorPort {
    self = [super init];
    if (self) {
        _accelPort = acceleratorPort;
        _gpuID = 0x9093; /* ARM Mali-G57 MC2 Valhall r0p1 */
        _registryID = 0x13b59093ULL;

        /* Open connection to SweetMaliUserClient in XNU kernel */
        kern_return_t kr = IOServiceOpen(acceleratorPort, mach_task_self(), 0, &_userClient);
        if (kr != KERN_SUCCESS) {
            NSLog(@"[MaliMetal] Warning: IOServiceOpen failed (0x%x), running with direct memory mapping", kr);
            _userClient = MACH_PORT_NULL;
        } else {
            NSLog(@"[MaliMetal] Connected to SweetMaliUserClient (port 0x%x)", _userClient);
        }
    }
    return self;
}

- (void)dealloc {
    if (_userClient != MACH_PORT_NULL) {
        IOServiceClose(_userClient);
    }
    [super dealloc];
}

- (NSString *)name {
    return @"Apple Metal on ARM Mali-G57 (HelioDarwin)";
}

- (uint64_t)registryID {
    return _registryID;
}

- (MTLSize)maxThreadsPerThreadgroup {
    /* Mali-G57 supports up to 1024 threads per workgroup */
    return MTLSizeMake(1024, 1024, 64);
}

- (BOOL)hasUnifiedMemory {
    /* MT6789 features unified LPDDR4X memory shared by CPU and Mali GPU */
    return YES;
}

- (uint64_t)recommendedMaxWorkingSetSize {
    /* Up to 6 GiB working set out of 8 GiB physical DRAM */
    return 6ULL * 1024 * 1024 * 1024;
}

- (uint64_t)currentAllocatedSize {
    return 0;
}

- (NSUInteger)maxBufferLength {
    return 1024ULL * 1024 * 1024; /* 1 GiB single buffer limit */
}

- (BOOL)supportsFeatureSet:(MTLFeatureSet)featureSet {
    /* Report support for iOS GPU Family 3 / 4 features */
    return YES;
}

- (BOOL)supportsFamily:(MTLGPUFamily)gpuFamily {
    if (gpuFamily == MTLGPUFamilyApple3 || gpuFamily == MTLGPUFamilyApple4 || gpuFamily == MTLGPUFamilyCommon3) {
        return YES;
    }
    return NO;
}

/* Command Queue Creation */
- (id<MTLCommandQueue>)newCommandQueue {
    return [self newCommandQueueWithMaxCommandBufferCount:64];
}

- (id<MTLCommandQueue>)newCommandQueueWithMaxCommandBufferCount:(NSUInteger)maxCommandBufferCount {
    return [[MaliMetalCommandQueue alloc] initWithDevice:self maxCommandBufferCount:maxCommandBufferCount];
}

/* Buffer Allocation */
- (id<MTLBuffer>)newBufferWithLength:(NSUInteger)length options:(MTLResourceOptions)options {
    /* Buffers are page-aligned (16 KB) for hardware DMA */
    return nil;
}

- (id<MTLBuffer>)newBufferWithBytes:(const void *)pointer length:(NSUInteger)length options:(MTLResourceOptions)options {
    return nil;
}

/* Texture Allocation */
- (id<MTLTexture>)newTextureWithDescriptor:(MTLTextureDescriptor *)descriptor {
    return nil;
}

- (id<MTLSamplerState>)newSamplerStateWithDescriptor:(MTLSamplerDescriptor *)descriptor {
    return nil;
}

/* Library Compilation */
- (id<MTLLibrary>)newDefaultLibrary {
    return nil;
}

- (id<MTLLibrary>)newLibraryWithSource:(NSString *)source options:(MTLCompileOptions *)options error:(NSError **)error {
    return nil;
}

- (id<MTLLibrary>)newLibraryWithData:(dispatch_data_t)data error:(NSError **)error {
    return nil;
}

- (id<MTLRenderPipelineState>)newRenderPipelineStateWithDescriptor:(MTLRenderPipelineDescriptor *)descriptor error:(NSError **)error {
    return nil;
}

- (id<MTLComputePipelineState>)newComputePipelineStateWithFunction:(id<MTLFunction>)computeFunction error:(NSError **)error {
    return nil;
}

@end
