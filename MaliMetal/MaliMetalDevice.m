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

extern void *IOSurfaceGetBaseAddress(void *token) __attribute__((weak_import));
extern size_t IOSurfaceGetWidth(void *token) __attribute__((weak_import));
extern size_t IOSurfaceGetHeight(void *token) __attribute__((weak_import));
extern size_t IOSurfaceGetBytesPerRow(void *token) __attribute__((weak_import));

/*
 * MaliMetalTexture: Memory-backed Metal Texture for CoreAnimation
 */
@interface MaliMetalTexture : NSObject <MTLTexture> {
    id<MTLDevice> _device;
    NSUInteger _width;
    NSUInteger _height;
    void *_pixels;
    NSUInteger _bytesPerRow;
    BOOL _isIOSurface;
}
- (instancetype)initWithDescriptor:(MTLTextureDescriptor *)descriptor device:(id<MTLDevice>)device;
- (instancetype)initWithDescriptor:(MTLTextureDescriptor *)descriptor iosurface:(void *)iosurface plane:(NSUInteger)plane device:(id<MTLDevice>)device;
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
        _isIOSurface = NO;
    }
    return self;
}

- (instancetype)initWithDescriptor:(MTLTextureDescriptor *)descriptor iosurface:(void *)iosurface plane:(NSUInteger)plane device:(id<MTLDevice>)device {
    self = [super init];
    if (self) {
        (void)descriptor;
        (void)plane;
        _device = device;
        _isIOSurface = YES;
        if (iosurface != NULL && IOSurfaceGetBaseAddress != NULL) {
            _pixels = IOSurfaceGetBaseAddress(iosurface);
            _width = IOSurfaceGetWidth ? IOSurfaceGetWidth(iosurface) : 1080;
            _height = IOSurfaceGetHeight ? IOSurfaceGetHeight(iosurface) : 2400;
            _bytesPerRow = IOSurfaceGetBytesPerRow ? IOSurfaceGetBytesPerRow(iosurface) : (_width * 4);
        } else {
            _width = 1080;
            _height = 2400;
            _bytesPerRow = _width * 4;
            _pixels = calloc(1, _bytesPerRow * _height);
            _isIOSurface = NO;
        }
    }
    return self;
}

- (void)dealloc {
    if (_pixels && !_isIOSurface) {
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
    volatile uint32_t *_hardwareRegisters;
    void *_ringBuffer;
}

- (instancetype)initWithAcceleratorPort:(mach_port_t)acceleratorPort {
    self = [super init];
    if (self) {
        _accelPort = acceleratorPort;
        _gpuID = 0;
        _registryID = 0x13b59093ULL;
        _hardwareRegisters = NULL;
        _ringBuffer = NULL;

        /* Open connection to SweetMaliUserClient in XNU kernel */
        kern_return_t kr = IOServiceOpen(acceleratorPort, mach_task_self(), 0, &_userClient);
        if (kr == KERN_SUCCESS) {
            /* Map physical Mali-G57 MMIO registers (type 0, 0x13000000) */
            mach_vm_address_t mmioAddr = 0;
            mach_vm_size_t mmioSize = 0x10000;
            kr = IOConnectMapMemory64(_userClient, 0, mach_task_self(), &mmioAddr, &mmioSize, kIOMapAnywhere);
            if (kr == KERN_SUCCESS && mmioAddr != 0) {
                _hardwareRegisters = (volatile uint32_t *)mmioAddr;
                _gpuID = _hardwareRegisters[0x0000 / 4];
                NSLog(@"[MaliMetal] Mapped hardware MMIO at %p, read GPU_ID=0x%08x", (void *)mmioAddr, _gpuID);
            }

            /* Map shared command ringbuffer (type 1) */
            mach_vm_address_t ringAddr = 0;
            mach_vm_size_t ringSize = 1024 * 1024;
            kr = IOConnectMapMemory64(_userClient, 1, mach_task_self(), &ringAddr, &ringSize, kIOMapAnywhere);
            if (kr == KERN_SUCCESS && ringAddr != 0) {
                _ringBuffer = (void *)ringAddr;
            }
        } else {
            _userClient = MACH_PORT_NULL;
        }
    }
    return self;
}

- (volatile uint32_t *)hardwareRegisters { return _hardwareRegisters; }
- (void *)ringBuffer { return _ringBuffer; }
- (uint32_t)gpuID { return _gpuID; }

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

- (id<MTLTexture>)newTextureWithDescriptor:(MTLTextureDescriptor *)descriptor iosurface:(void *)iosurface plane:(NSUInteger)plane {
    return (id<MTLTexture>)[[MaliMetalTexture alloc] initWithDescriptor:descriptor iosurface:iosurface plane:plane device:self];
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
