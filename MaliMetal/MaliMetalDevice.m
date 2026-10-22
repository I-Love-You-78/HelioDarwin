/*
 * MaliMetal: In-Memory Metal ICD Driver Skeleton
 * Provides mock MTLDevice, command queues, and surface buffers to satisfy
 * CoreAnimation / Metal.framework validation on ARM Mali-G57 MC2.
 * Long-term roadmap: Bridge to Mesa panvk / Vulkan runtime.
 */

#import "MaliMetalDevice.h"
#import "MaliMetalCommandQueue.h"
#include <stdlib.h>
#include <string.h>

/*
 * MaliMetalBuffer: Memory-backed Metal Buffer
 */
@interface MaliMetalBuffer : NSObject <MTLBuffer> {
    void *_pointer;
    NSUInteger _length;
    id<MTLDevice> _device;
}
- (instancetype)initWithLength:(NSUInteger)length device:(id<MTLDevice>)device;
- (instancetype)initWithBytes:(const void *)bytes length:(NSUInteger)length device:(id<MTLDevice>)device;
- (void *)contents;
- (NSUInteger)length;
- (id<MTLDevice>)device;
@end

@implementation MaliMetalBuffer

- (instancetype)initWithLength:(NSUInteger)length device:(id<MTLDevice>)device {
    self = [super init];
    if (self) {
        _length = length;
        _device = device;
        _pointer = calloc(1, length ? length : 1);
    }
    return self;
}

- (instancetype)initWithBytes:(const void *)bytes length:(NSUInteger)length device:(id<MTLDevice>)device {
    self = [self initWithLength:length device:device];
    if (self && bytes && _pointer) {
        memcpy(_pointer, bytes, length);
    }
    return self;
}

- (void)dealloc {
    if (_pointer) {
        free(_pointer);
        _pointer = NULL;
    }
    [super dealloc];
}

- (void *)contents { return _pointer; }
- (NSUInteger)length { return _length; }
- (id<MTLDevice>)device { return _device; }

@end

/*
 * MaliMetalTexture: Memory-backed Metal Texture for CoreAnimation
 */
@interface MaliMetalTexture : NSObject <MTLTexture> {
    id<MTLDevice> _device;
    NSUInteger _width;
    NSUInteger _height;
    void *_pixels;
    NSUInteger _bytesPerRow;
}
- (instancetype)initWithDescriptor:(MTLTextureDescriptor *)descriptor device:(id<MTLDevice>)device;
- (NSUInteger)width;
- (NSUInteger)height;
- (id<MTLDevice>)device;
- (void *)pixels;
- (void)getBytes:(void *)pixelBytes bytesPerRow:(NSUInteger)bytesPerRow fromRegion:(MTLRegion)region mipmapLevel:(NSUInteger)level;
- (void)replaceRegion:(MTLRegion)region mipmapLevel:(NSUInteger)level withBytes:(const void *)pixelBytes bytesPerRow:(NSUInteger)bytesPerRow;
@end

@implementation MaliMetalTexture

- (instancetype)initWithDescriptor:(MTLTextureDescriptor *)descriptor device:(id<MTLDevice>)device {
    self = [super init];
    if (self) {
        (void)descriptor;
        _device = device;
        _width = 1080;
        _height = 2400;
        _bytesPerRow = _width * 4;
        _pixels = calloc(1, _bytesPerRow * _height);
    }
    return self;
}

- (void)dealloc {
    if (_pixels) {
        free(_pixels);
        _pixels = NULL;
    }
    [super dealloc];
}

- (NSUInteger)width { return _width; }
- (NSUInteger)height { return _height; }
- (id<MTLDevice>)device { return _device; }
- (void *)pixels { return _pixels; }

- (void)getBytes:(void *)pixelBytes bytesPerRow:(NSUInteger)bytesPerRow fromRegion:(MTLRegion)region mipmapLevel:(NSUInteger)level {
    (void)level;
    if (!pixelBytes || !_pixels) return;
    for (NSUInteger y = 0; y < region.size.height; y++) {
        memcpy((char *)pixelBytes + y * bytesPerRow,
               (char *)_pixels + (region.origin.y + y) * _bytesPerRow + region.origin.x * 4,
               region.size.width * 4);
    }
}

- (void)replaceRegion:(MTLRegion)region mipmapLevel:(NSUInteger)level withBytes:(const void *)pixelBytes bytesPerRow:(NSUInteger)bytesPerRow {
    (void)level;
    if (!pixelBytes || !_pixels) return;
    for (NSUInteger y = 0; y < region.size.height; y++) {
        memcpy((char *)_pixels + (region.origin.y + y) * _bytesPerRow + region.origin.x * 4,
               (const char *)pixelBytes + y * bytesPerRow,
               region.size.width * 4);
    }
}

@end

/*
 * MaliMetalRenderPipelineState: Pipeline State Stub
 */
@interface MaliMetalRenderPipelineState : NSObject <MTLRenderPipelineState> {
    id<MTLDevice> _device;
}
- (instancetype)initWithDevice:(id<MTLDevice>)device;
- (id<MTLDevice>)device;
- (NSUInteger)maxTotalThreadsPerThreadgroup;
@end

@implementation MaliMetalRenderPipelineState

- (instancetype)initWithDevice:(id<MTLDevice>)device {
    self = [super init];
    if (self) {
        _device = device;
    }
    return self;
}

- (id<MTLDevice>)device { return _device; }
- (NSUInteger)maxTotalThreadsPerThreadgroup { return 1024; }

@end

/*
 * Core MaliMetalDevice Implementation
 */
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
            _userClient = MACH_PORT_NULL;
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
    return (NSString *)@"Apple Metal on ARM Mali-G57 (HelioDarwin)";
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
    (void)featureSet;
    return YES;
}

- (BOOL)supportsFamily:(MTLGPUFamily)gpuFamily {
    (void)gpuFamily;
    return YES;
}

/* Command Queue Creation */
- (id<MTLCommandQueue>)newCommandQueue {
    return [self newCommandQueueWithMaxCommandBufferCount:64];
}

- (id<MTLCommandQueue>)newCommandQueueWithMaxCommandBufferCount:(NSUInteger)maxCommandBufferCount {
    return (id<MTLCommandQueue>)[[MaliMetalCommandQueue alloc] initWithDevice:self maxCommandBufferCount:maxCommandBufferCount];
}

/* Buffer Allocation */
- (id<MTLBuffer>)newBufferWithLength:(NSUInteger)length options:(MTLResourceOptions)options {
    (void)options;
    return (id<MTLBuffer>)[[MaliMetalBuffer alloc] initWithLength:length device:self];
}

- (id<MTLBuffer>)newBufferWithBytes:(const void *)pointer length:(NSUInteger)length options:(MTLResourceOptions)options {
    (void)options;
    return (id<MTLBuffer>)[[MaliMetalBuffer alloc] initWithBytes:pointer length:length device:self];
}

/* Texture Allocation */
- (id<MTLTexture>)newTextureWithDescriptor:(MTLTextureDescriptor *)descriptor {
    return (id<MTLTexture>)[[MaliMetalTexture alloc] initWithDescriptor:descriptor device:self];
}

- (id<MTLSamplerState>)newSamplerStateWithDescriptor:(MTLSamplerDescriptor *)descriptor {
    (void)descriptor;
    return (id)0;
}

/* Library & Pipeline Compilation */
- (id<MTLLibrary>)newDefaultLibrary {
    return (id)0;
}

- (id<MTLLibrary>)newLibraryWithSource:(NSString *)source options:(MTLCompileOptions *)options error:(NSError **)error {
    (void)source;
    (void)options;
    if (error) *error = (NSError *)0;
    return (id)0;
}

- (id<MTLLibrary>)newLibraryWithData:(dispatch_data_t)data error:(NSError **)error {
    (void)data;
    if (error) *error = (NSError *)0;
    return (id)0;
}

- (id<MTLRenderPipelineState>)newRenderPipelineStateWithDescriptor:(MTLRenderPipelineDescriptor *)descriptor error:(NSError **)error {
    (void)descriptor;
    if (error) *error = (NSError *)0;
    return (id<MTLRenderPipelineState>)[[MaliMetalRenderPipelineState alloc] initWithDevice:self];
}

- (id<MTLComputePipelineState>)newComputePipelineStateWithFunction:(id<MTLFunction>)computeFunction error:(NSError **)error {
    (void)computeFunction;
    if (error) *error = (NSError *)0;
    return (id)0;
}

@end
