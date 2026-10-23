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
        _device = (id<MTLDevice>)[(id)device retain];
        _pointer = calloc(1, length ? length : 1);
        if (_pointer == NULL) {
            [self release];
            return nil;
        }
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
    [(id)_device release];
    [super dealloc];
}

- (void *)contents { return _pointer; }
- (NSUInteger)length { return _length; }
- (id<MTLDevice>)device { return _device; }
/* Host virtual addresses are not Mali GPU virtual addresses. */
- (uint64_t)gpuAddress { return 0; }

@end

extern void *IOSurfaceGetBaseAddress(void *token) __attribute__((weak_import));
extern size_t IOSurfaceGetWidth(void *token) __attribute__((weak_import));
extern size_t IOSurfaceGetHeight(void *token) __attribute__((weak_import));
extern size_t IOSurfaceGetBytesPerRow(void *token) __attribute__((weak_import));
extern size_t IOSurfaceGetPlaneCount(void *token) __attribute__((weak_import));
extern size_t IOSurfaceGetAllocSize(void *token) __attribute__((weak_import));
extern uint32_t IOSurfaceGetPixelFormat(void *token) __attribute__((weak_import));
extern kern_return_t IOSurfaceLock(void *token, uint32_t options, uint32_t *seed) __attribute__((weak_import));
extern kern_return_t IOSurfaceUnlock(void *token, uint32_t options, uint32_t *seed) __attribute__((weak_import));

static BOOL MaliMetalValidateTextureDescriptor(MTLTextureDescriptor *descriptor)
{
    if (descriptor == nil || [descriptor width] == 0 || [descriptor height] == 0 ||
        [descriptor textureType] != MTLTextureType2D || [descriptor depth] != 1 ||
        [descriptor mipmapLevelCount] != 1 || [descriptor sampleCount] != 1 ||
        [descriptor arrayLength] != 1) {
        return NO;
    }
    switch ([descriptor pixelFormat]) {
    case MTLPixelFormatRGBA8Unorm:
    case MTLPixelFormatRGBA8Unorm_sRGB:
    case MTLPixelFormatBGRA8Unorm:
    case MTLPixelFormatBGRA8Unorm_sRGB:
        return [descriptor width] <= SIZE_MAX / 4 &&
               [descriptor height] <= SIZE_MAX / ([descriptor width] * 4);
    default:
        return NO;
    }
}

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
- (NSUInteger)bytesPerRow;
- (BOOL)validateRegion:(MTLRegion)region mipmapLevel:(NSUInteger)level bytesPerRow:(NSUInteger)bytesPerRow;
- (void)getBytes:(void *)pixelBytes bytesPerRow:(NSUInteger)bytesPerRow fromRegion:(MTLRegion)region mipmapLevel:(NSUInteger)level;
- (void)replaceRegion:(MTLRegion)region mipmapLevel:(NSUInteger)level withBytes:(const void *)pixelBytes bytesPerRow:(NSUInteger)bytesPerRow;
@end

@implementation MaliMetalTexture

- (instancetype)initWithDescriptor:(MTLTextureDescriptor *)descriptor device:(id<MTLDevice>)device {
    self = [super init];
    if (self) {
        if (!MaliMetalValidateTextureDescriptor(descriptor)) {
            [self release];
            return nil;
        }
        _device = (id<MTLDevice>)[(id)device retain];
        _width = [descriptor width];
        _height = [descriptor height];
        _pixelFormat = [descriptor pixelFormat];
        _textureType = [descriptor textureType];
        _usage = [descriptor usage];
        _bytesPerRow = _width * 4;
        _pixels = calloc(1, _bytesPerRow * _height);
        if (_pixels == NULL) {
            [self release];
            return nil;
        }
        _isIOSurface = NO;
        _iosurface = NULL;
        _iosurfacePlane = 0;
    }
    return self;
}

- (instancetype)initWithDescriptor:(MTLTextureDescriptor *)descriptor iosurface:(void *)iosurface plane:(NSUInteger)plane device:(id<MTLDevice>)device {
    self = [super init];
    if (self) {
        if (!MaliMetalValidateTextureDescriptor(descriptor) || iosurface == NULL || plane != 0 ||
            IOSurfaceGetBaseAddress == NULL || IOSurfaceGetWidth == NULL ||
            IOSurfaceGetHeight == NULL || IOSurfaceGetBytesPerRow == NULL ||
            IOSurfaceGetPlaneCount == NULL || IOSurfaceGetAllocSize == NULL || IOSurfaceGetPixelFormat == NULL ||
            IOSurfaceLock == NULL || IOSurfaceUnlock == NULL || IOSurfaceGetPlaneCount(iosurface) != 0) {
            [self release];
            return nil;
        }
        _width = [descriptor width];
        _height = [descriptor height];
        _bytesPerRow = IOSurfaceGetBytesPerRow(iosurface);
        BOOL bgra = [descriptor pixelFormat] == MTLPixelFormatBGRA8Unorm ||
                    [descriptor pixelFormat] == MTLPixelFormatBGRA8Unorm_sRGB;
        uint32_t expectedFormat = bgra ? 0x42475241u : 0x52474241u;
        if (_width != IOSurfaceGetWidth(iosurface) || _height != IOSurfaceGetHeight(iosurface) ||
            IOSurfaceGetPixelFormat(iosurface) != expectedFormat ||
            _bytesPerRow < _width * 4 || _bytesPerRow > SIZE_MAX / _height ||
            IOSurfaceGetAllocSize(iosurface) < (_height - 1) * _bytesPerRow + _width * 4 ||
            IOSurfaceGetBaseAddress(iosurface) == NULL) {
            [self release];
            return nil;
        }
        _device = (id<MTLDevice>)[(id)device retain];
        _iosurface = (void *)CFRetain(iosurface);
        _iosurfacePlane = plane;
        _pixelFormat = [descriptor pixelFormat];
        _textureType = [descriptor textureType];
        _usage = [descriptor usage];
        _isIOSurface = YES;
        _pixels = IOSurfaceGetBaseAddress(iosurface);
    }
    return self;
}

- (void)dealloc {
    if (_pixels && !_isIOSurface) {
        free(_pixels);
        _pixels = NULL;
    }
    if (_iosurface != NULL) {
        CFRelease(_iosurface);
    }
    [(id)_device release];
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
- (NSUInteger)bytesPerRow { return _bytesPerRow; }

- (BOOL)validateRegion:(MTLRegion)region mipmapLevel:(NSUInteger)level bytesPerRow:(NSUInteger)bytesPerRow {
    return level == 0 && region.origin.z == 0 && region.size.depth == 1 &&
           region.origin.x <= _width && region.size.width <= _width - region.origin.x &&
           region.origin.y <= _height && region.size.height <= _height - region.origin.y &&
           bytesPerRow >= region.size.width * 4 &&
           (region.size.height <= 1 || bytesPerRow <=
               (SIZE_MAX - region.size.width * 4) / (region.size.height - 1));
}

- (void)getBytes:(void *)pixelBytes bytesPerRow:(NSUInteger)bytesPerRow fromRegion:(MTLRegion)region mipmapLevel:(NSUInteger)level {
    if (![self validateRegion:region mipmapLevel:level bytesPerRow:bytesPerRow] || pixelBytes == NULL) {
        [NSException raise:@"NSInvalidArgumentException" format:@"Invalid MaliMetal texture read region"];
        return;
    }
    if (_isIOSurface && IOSurfaceLock(_iosurface, 1, NULL) != KERN_SUCCESS) {
        [NSException raise:@"NSInternalInconsistencyException" format:@"Cannot lock MaliMetal IOSurface for reading"];
        return;
    }
    for (NSUInteger y = 0; y < region.size.height; y++) {
        memcpy((char *)pixelBytes + y * bytesPerRow,
               (char *)_pixels + (region.origin.y + y) * _bytesPerRow + region.origin.x * 4,
               region.size.width * 4);
    }
    if (_isIOSurface) IOSurfaceUnlock(_iosurface, 1, NULL);
}

- (void)replaceRegion:(MTLRegion)region mipmapLevel:(NSUInteger)level withBytes:(const void *)pixelBytes bytesPerRow:(NSUInteger)bytesPerRow {
    if (![self validateRegion:region mipmapLevel:level bytesPerRow:bytesPerRow] || pixelBytes == NULL) {
        [NSException raise:@"NSInvalidArgumentException" format:@"Invalid MaliMetal texture write region"];
        return;
    }
    if (_isIOSurface && IOSurfaceLock(_iosurface, 0, NULL) != KERN_SUCCESS) {
        [NSException raise:@"NSInternalInconsistencyException" format:@"Cannot lock MaliMetal IOSurface for writing"];
        return;
    }
    for (NSUInteger y = 0; y < region.size.height; y++) {
        memcpy((char *)_pixels + (region.origin.y + y) * _bytesPerRow + region.origin.x * 4,
               (const char *)pixelBytes + y * bytesPerRow,
               region.size.width * 4);
    }
    if (_isIOSurface) IOSurfaceUnlock(_iosurface, 0, NULL);
}

@end

static id MaliMetalUnsupported(NSError **error, NSString *reason)
{
    if (error != NULL) {
        NSDictionary *details = [NSDictionary dictionaryWithObject:reason forKey:@"NSLocalizedDescription"];
        *error = [NSError errorWithDomain:@"org.heliodarwin.MaliMetal" code:1 userInfo:details];
    }
    return nil;
}

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
        /* Metal identifies the IORegistry service, not the GPU product number. */
        kern_return_t kr = IORegistryEntryGetRegistryEntryID(acceleratorPort, &_registryID);
        if (kr != KERN_SUCCESS) {
            NSLog(@"[MaliMetal] Cannot identify accelerator: 0x%x", kr);
            [self release];
            return nil;
        }
        _hardwareRegisters = NULL;
        _ringBuffer = NULL;

        /* Open connection to SweetMaliUserClient in XNU kernel */
        kr = IOServiceOpen(acceleratorPort, mach_task_self(), 0, &_userClient);
        if (kr != KERN_SUCCESS) {
            NSLog(@"[MaliMetal] Cannot open accelerator: 0x%x", kr);
            [self release];
            return nil;
        }
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
        if (_ringBuffer != NULL) {
            IOConnectUnmapMemory64(_userClient, 1, mach_task_self(), (mach_vm_address_t)_ringBuffer);
        }
        if (_hardwareRegisters != NULL) {
            IOConnectUnmapMemory64(_userClient, 0, mach_task_self(), (mach_vm_address_t)_hardwareRegisters);
        }
        IOServiceClose(_userClient);
    }
    [super dealloc];
}

- (NSString *)name {
    return (NSString *)@"HelioDarwin Mali-G57 (CPU prototype)";
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
    return NO;
}

- (BOOL)supportsFamily:(MTLGPUFamily)gpuFamily {
    (void)gpuFamily;
    return NO;
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
    if (length == 0 || length > [self maxBufferLength]) return nil;
    return (id<MTLBuffer>)[[MaliMetalBuffer alloc] initWithLength:length device:self];
}

- (id<MTLBuffer>)newBufferWithBytes:(const void *)pointer length:(NSUInteger)length options:(MTLResourceOptions)options {
    (void)options;
    if (pointer == NULL || length == 0 || length > [self maxBufferLength]) return nil;
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
    return nil;
}

- (id<MTLLibrary>)newLibraryWithSource:(NSString *)source options:(MTLCompileOptions *)options error:(NSError **)error {
    (void)source;
    (void)options;
    return MaliMetalUnsupported(error, @"MSL compilation is not implemented");
}

- (id<MTLLibrary>)newLibraryWithData:(dispatch_data_t)data error:(NSError **)error {
    (void)data;
    return MaliMetalUnsupported(error, @"AIR/metallib loading and translation are not implemented");
}

- (id<MTLRenderPipelineState>)newRenderPipelineStateWithDescriptor:(MTLRenderPipelineDescriptor *)descriptor error:(NSError **)error {
    (void)descriptor;
    return MaliMetalUnsupported(error, @"Executable render pipelines are not implemented");
}

- (id<MTLComputePipelineState>)newComputePipelineStateWithFunction:(id<MTLFunction>)computeFunction error:(NSError **)error {
    (void)computeFunction;
    return MaliMetalUnsupported(error, @"Executable compute pipelines are not implemented");
}

@end
