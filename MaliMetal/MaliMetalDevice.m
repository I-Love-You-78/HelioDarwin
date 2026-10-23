/*
 * MaliMetal: In-Memory Metal ICD Driver Skeleton
 * Provides MTLDevice, command queues, and surface buffers to satisfy
 * CoreAnimation / Metal.framework validation on ARM Mali-G57 MC2.
 * Roadmap: Bridge to Mesa panvk / Vulkan runtime.
 */

#import "MaliMetalDevice.h"
#import "MaliMetalCommandQueue.h"
#include <stdlib.h>
#include <string.h>
#include <alloca.h>

@implementation MaliMetalObject

- (void)dealloc {
    [_label release];
    [super dealloc];
}

- (NSString *)label {
    return _label;
}

- (void)setLabel:(NSString *)label {
    if (_label != label) {
        [_label release];
        _label = [label copy];
    }
}

- (BOOL)respondsToSelector:(SEL)aSelector {
    if ([super respondsToSelector:aSelector]) return YES;
    return YES;
}

- (NSMethodSignature *)methodSignatureForSelector:(SEL)aSelector {
    NSMethodSignature *sig = [super methodSignatureForSelector:aSelector];
    if (!sig) {
        /* Generic fallback signature returning pointer/integer/zero */
        sig = [NSMethodSignature signatureWithObjCTypes:"@@:@@@@@@@@"];
    }
    return sig;
}

- (void)forwardInvocation:(NSInvocation *)anInvocation {
    NSUInteger retLen = [[anInvocation methodSignature] methodReturnLength];
    if (retLen > 0) {
        void *buf = alloca(retLen);
        memset(buf, 0, retLen);
        [anInvocation setReturnValue:buf];
    }
}

@end

/*
 * MaliMetalBuffer: Memory-backed Metal Buffer
 */
@interface MaliMetalBuffer : MaliMetalObject <MTLBuffer> {
    void *_pointer;
    NSUInteger _length;
    id<MTLDevice> _device;
}
- (instancetype)initWithLength:(NSUInteger)length device:(id<MTLDevice>)device;
- (instancetype)initWithBytes:(const void *)bytes length:(NSUInteger)length device:(id<MTLDevice>)device;
- (void *)contents;
- (NSUInteger)length;
- (id<MTLDevice>)device;
- (uint64_t)gpuAddress;
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
- (uint64_t)gpuAddress { return (uint64_t)_pointer; }

@end

extern void *IOSurfaceGetBaseAddress(void *token) __attribute__((weak_import));
extern size_t IOSurfaceGetWidth(void *token) __attribute__((weak_import));
extern size_t IOSurfaceGetHeight(void *token) __attribute__((weak_import));
extern size_t IOSurfaceGetBytesPerRow(void *token) __attribute__((weak_import));

/*
 * MaliMetalTexture: Memory-backed Metal Texture for CoreAnimation
 */
@interface MaliMetalTexture : MaliMetalObject <MTLTexture> {
    id<MTLDevice> _device;
    NSUInteger _width;
    NSUInteger _height;
    void *_pixels;
    NSUInteger _bytesPerRow;
    BOOL _isIOSurface;
    void *_iosurface;
    NSUInteger _iosurfacePlane;
    MTLPixelFormat _pixelFormat;
    MTLTextureType _textureType;
    MTLTextureUsage _usage;
}
- (instancetype)initWithDescriptor:(MTLTextureDescriptor *)descriptor device:(id<MTLDevice>)device;
- (instancetype)initWithDescriptor:(MTLTextureDescriptor *)descriptor iosurface:(void *)iosurface plane:(NSUInteger)plane device:(id<MTLDevice>)device;
- (NSUInteger)width;
- (NSUInteger)height;
- (NSUInteger)depth;
- (NSUInteger)mipmapLevelCount;
- (NSUInteger)sampleCount;
- (NSUInteger)arrayLength;
- (MTLPixelFormat)pixelFormat;
- (MTLTextureType)textureType;
- (MTLTextureUsage)usage;
- (BOOL)isFramebufferOnly;
- (void *)iosurface;
- (NSUInteger)iosurfacePlane;
- (id<MTLResource>)rootResource;
- (id<MTLTexture>)parentTexture;
- (id<MTLDevice>)device;
- (void *)pixels;
- (void)getBytes:(void *)pixelBytes bytesPerRow:(NSUInteger)bytesPerRow fromRegion:(MTLRegion)region mipmapLevel:(NSUInteger)level;
- (void)replaceRegion:(MTLRegion)region mipmapLevel:(NSUInteger)level withBytes:(const void *)pixelBytes bytesPerRow:(NSUInteger)bytesPerRow;
@end

@implementation MaliMetalTexture

- (instancetype)initWithDescriptor:(MTLTextureDescriptor *)descriptor device:(id<MTLDevice>)device {
    self = [super init];
    if (self) {
        _device = device;
        _width = descriptor ? [descriptor width] : 1080;
        _height = descriptor ? [descriptor height] : 2400;
        if (_width == 0) _width = 1080;
        if (_height == 0) _height = 2400;
        _pixelFormat = descriptor ? [descriptor pixelFormat] : MTLPixelFormatBGRA8Unorm;
        _textureType = descriptor ? [descriptor textureType] : MTLTextureType2D;
        _usage = descriptor ? [descriptor usage] : MTLTextureUsageRenderTarget;
        _bytesPerRow = _width * 4;
        _pixels = calloc(1, _bytesPerRow * _height);
        _isIOSurface = NO;
        _iosurface = NULL;
        _iosurfacePlane = 0;
    }
    return self;
}

- (instancetype)initWithDescriptor:(MTLTextureDescriptor *)descriptor iosurface:(void *)iosurface plane:(NSUInteger)plane device:(id<MTLDevice>)device {
    self = [super init];
    if (self) {
        _device = device;
        _iosurface = iosurface;
        _iosurfacePlane = plane;
        _pixelFormat = descriptor ? [descriptor pixelFormat] : MTLPixelFormatBGRA8Unorm;
        _textureType = descriptor ? [descriptor textureType] : MTLTextureType2D;
        _usage = descriptor ? [descriptor usage] : MTLTextureUsageRenderTarget;
        _isIOSurface = YES;
        if (iosurface != NULL && IOSurfaceGetBaseAddress != NULL) {
            _pixels = IOSurfaceGetBaseAddress(iosurface);
            _width = IOSurfaceGetWidth ? IOSurfaceGetWidth(iosurface) : 1080;
            _height = IOSurfaceGetHeight ? IOSurfaceGetHeight(iosurface) : 2400;
            _bytesPerRow = IOSurfaceGetBytesPerRow ? IOSurfaceGetBytesPerRow(iosurface) : (_width * 4);
        } else {
            _width = descriptor ? [descriptor width] : 1080;
            _height = descriptor ? [descriptor height] : 2400;
            if (_width == 0) _width = 1080;
            if (_height == 0) _height = 2400;
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
- (NSUInteger)depth { return 1; }
- (NSUInteger)mipmapLevelCount { return 1; }
- (NSUInteger)sampleCount { return 1; }
- (NSUInteger)arrayLength { return 1; }
- (MTLPixelFormat)pixelFormat { return _pixelFormat; }
- (MTLTextureType)textureType { return _textureType; }
- (MTLTextureUsage)usage { return _usage; }
- (BOOL)isFramebufferOnly { return NO; }
- (void *)iosurface { return _iosurface; }
- (NSUInteger)iosurfacePlane { return _iosurfacePlane; }
- (id<MTLResource>)rootResource { return (id<MTLResource>)self; }
- (id<MTLTexture>)parentTexture { return (id<MTLTexture>)0; }
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
 * MaliMetalFunction: Function Stub
 */
@interface MaliMetalFunction : MaliMetalObject <MTLFunction> {
    NSString *_name;
    id<MTLDevice> _device;
    MTLFunctionType _functionType;
}
- (instancetype)initWithName:(NSString *)name device:(id<MTLDevice>)device functionType:(MTLFunctionType)type;
- (NSString *)name;
- (id<MTLDevice>)device;
- (MTLFunctionType)functionType;
@end

@implementation MaliMetalFunction

- (instancetype)initWithName:(NSString *)name device:(id<MTLDevice>)device functionType:(MTLFunctionType)type {
    self = [super init];
    if (self) {
        _name = [name copy];
        _device = device;
        _functionType = type;
    }
    return self;
}

- (void)dealloc {
    [_name release];
    [super dealloc];
}

- (NSString *)name { return _name; }
- (id<MTLDevice>)device { return _device; }
- (MTLFunctionType)functionType { return _functionType; }

@end

/*
 * MaliMetalLibrary: Library Stub
 */
@interface MaliMetalLibrary : MaliMetalObject <MTLLibrary> {
    id<MTLDevice> _device;
}
- (instancetype)initWithDevice:(id<MTLDevice>)device;
- (id<MTLDevice>)device;
- (id<MTLFunction>)newFunctionWithName:(NSString *)functionName;
@end

@implementation MaliMetalLibrary

- (instancetype)initWithDevice:(id<MTLDevice>)device {
    self = [super init];
    if (self) {
        _device = device;
    }
    return self;
}

- (id<MTLDevice>)device { return _device; }

- (id<MTLFunction>)newFunctionWithName:(NSString *)functionName {
    return (id<MTLFunction>)[[MaliMetalFunction alloc] initWithName:functionName device:_device functionType:MTLFunctionTypeVertex];
}

@end

/*
 * MaliMetalRenderPipelineState: Pipeline State Stub
 */
@interface MaliMetalRenderPipelineState : MaliMetalObject <MTLRenderPipelineState> {
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
 * MaliMetalComputePipelineState: Compute Pipeline State Stub
 */
@interface MaliMetalComputePipelineState : MaliMetalObject <MTLComputePipelineState> {
    id<MTLDevice> _device;
}
- (instancetype)initWithDevice:(id<MTLDevice>)device;
- (id<MTLDevice>)device;
- (NSUInteger)maxTotalThreadsPerThreadgroup;
@end

@implementation MaliMetalComputePipelineState

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
        /* ARM vendor in the high bits; the low 16 are filled from the real GPU_ID
         * once the hardware actually answers (see below). Do not bake in a product
         * ID the hardware has not reported. */
        _registryID = 0x13b50000ULL;
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
                /* Reflect only what the hardware actually returned (0 while the GPU
                 * is unpowered); no spoofed product ID. */
                if (_gpuID != 0 && _gpuID != ~0u) {
                    _registryID = 0x13b50000ULL | (uint64_t)((_gpuID >> 16) & 0xffff);
                }
                NSLog(@"[MaliMetal] Mapped hardware MMIO at %p, read GPU_ID=0x%08x", (void *)mmioAddr, _gpuID);
            }

            /* Map shared command ringbuffer / job descriptors (type 1, 1 MiB) */
            mach_vm_address_t ringAddr = 0;
            mach_vm_size_t ringSize = 1024 * 1024;
            kr = IOConnectMapMemory64(_userClient, 1, mach_task_self(), &ringAddr, &ringSize, kIOMapAnywhere);
            if (kr == KERN_SUCCESS && ringAddr != 0) {
                _ringBuffer = (void *)ringAddr;
                NSLog(@"[MaliMetal] Mapped shared job ringbuffer at %p (1 MiB)", _ringBuffer);
            }
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
    return MTLSizeMake(1024, 1024, 64);
}

- (BOOL)hasUnifiedMemory {
    return YES;
}

- (uint64_t)recommendedMaxWorkingSetSize {
    return 6ULL * 1024 * 1024 * 1024;
}

- (uint64_t)currentAllocatedSize {
    return 0;
}

- (NSUInteger)maxBufferLength {
    return 1024ULL * 1024 * 1024;
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
    return (id<MTLLibrary>)[[MaliMetalLibrary alloc] initWithDevice:self];
}

- (id<MTLLibrary>)newLibraryWithSource:(NSString *)source options:(MTLCompileOptions *)options error:(NSError **)error {
    (void)source;
    (void)options;
    if (error) *error = (NSError *)0;
    return (id<MTLLibrary>)[[MaliMetalLibrary alloc] initWithDevice:self];
}

- (id<MTLLibrary>)newLibraryWithData:(dispatch_data_t)data error:(NSError **)error {
    (void)data;
    if (error) *error = (NSError *)0;
    return (id<MTLLibrary>)[[MaliMetalLibrary alloc] initWithDevice:self];
}

- (id<MTLRenderPipelineState>)newRenderPipelineStateWithDescriptor:(MTLRenderPipelineDescriptor *)descriptor error:(NSError **)error {
    (void)descriptor;
    if (error) *error = (NSError *)0;
    return (id<MTLRenderPipelineState>)[[MaliMetalRenderPipelineState alloc] initWithDevice:self];
}

- (id<MTLComputePipelineState>)newComputePipelineStateWithFunction:(id<MTLFunction>)computeFunction error:(NSError **)error {
    (void)computeFunction;
    if (error) *error = (NSError *)0;
    return (id<MTLComputePipelineState>)[[MaliMetalComputePipelineState alloc] initWithDevice:self];
}

/* Forward unhandled device queries gracefully */
- (BOOL)respondsToSelector:(SEL)aSelector {
    if ([super respondsToSelector:aSelector]) return YES;
    return YES;
}

- (NSMethodSignature *)methodSignatureForSelector:(SEL)aSelector {
    NSMethodSignature *sig = [super methodSignatureForSelector:aSelector];
    if (!sig) {
        sig = [NSMethodSignature signatureWithObjCTypes:"@@:@@@@@@@@"];
    }
    return sig;
}

- (void)forwardInvocation:(NSInvocation *)anInvocation {
    NSUInteger retLen = [[anInvocation methodSignature] methodReturnLength];
    if (retLen > 0) {
        void *buf = alloca(retLen);
        memset(buf, 0, retLen);
        [anInvocation setReturnValue:buf];
    }
}

@end
