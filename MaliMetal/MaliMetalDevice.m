/* iOS Metal frontend. Native execution is available only through the
 * independently validated SweetMali Vulkan backend and AIR compiler. */

#import "MaliMetalDevice.h"
#import "MaliMetalCommandQueue.h"
#import "MaliMetalDeviceLimits.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <math.h>
#import "Shader/MaliMetalShader.h"
#import "Shader/MaliMetalShaderVulkan.h"
#import "Shader/MaliMetalCompute.h"
#import "Shader/MaliMetalComputeVulkan.h"

static MMMetalDeviceLimits *MaliMetalLimitsStorage(id device) {
    Class base = objc_getClass("_MTLDevice");
    Ivar limits = class_getInstanceVariable(base, "_limits");
    Ivar families = class_getInstanceVariable(base, "_supportedGPUFamilies");
    const char *encoding = limits ? ivar_getTypeEncoding(limits) : NULL;
    if (!encoding || !families ||
        strcmp(encoding, MMMetalDeviceLimitsEncoding) ||
        ivar_getOffset(limits) < (ptrdiff_t)sizeof(id) ||
        ivar_getOffset(families) - ivar_getOffset(limits) != (ptrdiff_t)sizeof(MMMetalDeviceLimits) ||
        (size_t)ivar_getOffset(families) > class_getInstanceSize(base))
        return NULL;
    return (MMMetalDeviceLimits *)((unsigned char *)(void *)device + ivar_getOffset(limits));
}

@implementation MaliMetalObject

@synthesize label = _label;

- (void)dealloc {
    [_label release];
    [super dealloc];
}

@end

/*
 * MaliMetalBuffer: Memory-backed Metal Buffer
 */
@interface MaliMetalBuffer : MaliMetalObject <MTLBuffer> {
    void *_pointer;
    NSUInteger _length;
    id<MTLDevice> _device;
    MMVKBuffer *_buffer;
    NSUInteger _allocatedSize;
    NSUInteger _purgeableState;
    MTLStorageMode _storageMode;
}
- (instancetype)initWithLength:(NSUInteger)length
                        device:(id<MTLDevice>)device
                   storageMode:(MTLStorageMode)mode;
- (instancetype)initWithBytes:(const void *)bytes
                       length:(NSUInteger)length
                       device:(id<MTLDevice>)device;
- (void *)contents;
- (NSUInteger)length;
- (id<MTLDevice>)device;
- (uint64_t)gpuAddress;
@end

@implementation MaliMetalBuffer

- (instancetype)initWithLength:(NSUInteger)length
                        device:(id<MTLDevice>)device
                   storageMode:(MTLStorageMode)mode {
    self = [super init];
    if (self) {
        _length = length;
        _storageMode = mode;
        _purgeableState = 2;
        _device = (id<MTLDevice>)[(id)device retain];
        if (length > SIZE_MAX - 15) {
            [self release];
            return nil;
        }
        NSUInteger nativeLength = (length + 15) & ~(NSUInteger)15;
        MMVKDevice *backend = [(MaliMetalDevice *)device backendDevice];
        if (backend) {
            MMVKError error = {0};
            uint32_t usage = MMVK_BUFFER_VERTEX | MMVK_BUFFER_INDEX | MMVK_BUFFER_UNIFORM |
                             MMVK_BUFFER_STORAGE | MMVK_BUFFER_TRANSFER_SRC |
                             MMVK_BUFFER_TRANSFER_DST;
            if (mmvk_buffer_create(backend, nativeLength, usage, &_buffer, &error) != MMVK_OK) {
                [self release];
                return nil;
            }
            _pointer = mmvk_buffer_contents(_buffer);
        } else {
            if (mode != MTLStorageModeShared) {
                [self release];
                return nil;
            }
            _pointer = calloc(1, nativeLength);
        }
        if (_pointer == NULL) {
            [self release];
            return nil;
        }
        /* AIR's proven uniform windows may use a 16-byte vector load for a
         * shorter logical Metal buffer. Padding is private and starts at zero. */
        if (_buffer) {
            memset(_pointer, 0, nativeLength);
            MMVKError error = {0};
            if (mmvk_buffer_flush(_buffer, 0, nativeLength, &error) != MMVK_OK) {
                [self release];
                return nil;
            }
        }
        _allocatedSize = _buffer ? mmvk_buffer_allocation_size(_buffer) : nativeLength;
        [(MaliMetalDevice *)device accountAllocation:_allocatedSize release:NO];
    }
    return self;
}

- (instancetype)initWithBytes:(const void *)bytes
                       length:(NSUInteger)length
                       device:(id<MTLDevice>)device {
    self = [self initWithLength:length device:device storageMode:MTLStorageModeShared];
    if (self && bytes && _pointer) {
        memcpy(_pointer, bytes, length);
    }
    return self;
}

- (void)dealloc {
    if (_buffer)
        mmvk_buffer_destroy(_buffer);
    else if (_pointer)
        free(_pointer);
    if (_allocatedSize)
        [(MaliMetalDevice *)_device accountAllocation:_allocatedSize release:YES];
    [(id)_device release];
    [super dealloc];
}

- (void *)contents {
    return _storageMode == MTLStorageModeShared ? _pointer : NULL;
}
- (NSUInteger)length {
    return _length;
}
- (id<MTLDevice>)device {
    return _device;
}
/* Host virtual addresses are not Mali GPU virtual addresses. */
- (uint64_t)gpuAddress {
    return 0;
}
- (MMVKBuffer *)maliBuffer {
    return _buffer;
}
- (MTLStorageMode)storageMode {
    return _storageMode;
}
- (MTLCPUCacheMode)cpuCacheMode {
    return MTLCPUCacheModeDefaultCache;
}
- (MTLHazardTrackingMode)hazardTrackingMode {
    return MTLHazardTrackingModeTracked;
}
- (NSUInteger)allocatedSize {
    return _allocatedSize;
}
- (NSUInteger)setPurgeableState:(NSUInteger)state {
    NSUInteger previous = _purgeableState;
    if (state == 1)
        return previous;
    if (state != 2 && state != 3) {
        [NSException raise:@"NSInvalidArgumentException"
                    format:@"Explicit empty buffer purge is unsupported"];
        return previous;
    }
    /* Volatile permits reclamation; keeping its contents resident is allowed. */
    _purgeableState = state;
    return previous;
}
- (BOOL)isAliasable {
    return NO;
}
- (id)heap {
    return nil;
}
- (NSUInteger)heapOffset {
    return 0;
}
- (id<MTLResource>)rootResource {
    return (id)self;
}
- (void)didModifyRange:(NSRange)range {
    if (_storageMode != MTLStorageModeShared) {
        [NSException raise:@"NSInvalidArgumentException"
                    format:@"Private buffers have no CPU-accessible contents"];
        return;
    }
    if (range.location > _length || range.length > _length - range.location)
        [NSException raise:@"NSInvalidArgumentException" format:@"Buffer range exceeds allocation"];
    if (_buffer) {
        MMVKError error = {0};
        if (mmvk_buffer_flush(_buffer, range.location, range.length, &error) != MMVK_OK)
            [NSException raise:@"NSInternalInconsistencyException" format:@"Buffer flush failed"];
    }
}

@end

extern void *IOSurfaceGetBaseAddress(void *token) __attribute__((weak_import));
extern size_t IOSurfaceGetWidth(void *token) __attribute__((weak_import));
extern size_t IOSurfaceGetHeight(void *token) __attribute__((weak_import));
extern size_t IOSurfaceGetBytesPerRow(void *token) __attribute__((weak_import));
extern size_t IOSurfaceGetPlaneCount(void *token) __attribute__((weak_import));
extern size_t IOSurfaceGetAllocSize(void *token) __attribute__((weak_import));
extern uint32_t IOSurfaceGetPixelFormat(void *token) __attribute__((weak_import));
extern kern_return_t IOSurfaceLock(void *token, uint32_t options, uint32_t *seed)
    __attribute__((weak_import));
extern kern_return_t IOSurfaceUnlock(void *token, uint32_t options, uint32_t *seed)
    __attribute__((weak_import));

static BOOL MaliMetalValidateTextureDescriptor(MTLTextureDescriptor *descriptor) {
    if (descriptor == nil || [descriptor width] == 0 || [descriptor height] == 0 ||
        [descriptor textureType] != MTLTextureType2D || [descriptor depth] != 1 ||
        [descriptor mipmapLevelCount] != 1 || [descriptor sampleCount] != 1 ||
        [descriptor arrayLength] != 1 ||
        ([descriptor storageMode] != MTLStorageModeShared &&
         [descriptor storageMode] != MTLStorageModePrivate) ||
        ([descriptor resourceOptions] & ~(0x30ul | 0x200ul)) ||
        [descriptor cpuCacheMode] != MTLCPUCacheModeDefaultCache ||
        [descriptor width] > UINT32_MAX || [descriptor height] > UINT32_MAX ||
        ([descriptor usage] &
         ~(MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite | MTLTextureUsageRenderTarget))) {
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
    MTLStorageMode _storageMode;
    MMVKImage *_image;
    NSUInteger _allocatedSize;
    NSUInteger _purgeableState;
}
- (instancetype)initWithDescriptor:(MTLTextureDescriptor *)descriptor device:(id<MTLDevice>)device;
- (instancetype)initWithDescriptor:(MTLTextureDescriptor *)descriptor
                         iosurface:(void *)iosurface
                             plane:(NSUInteger)plane
                            device:(id<MTLDevice>)device;
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
- (BOOL)validateRegion:(MTLRegion)region
           mipmapLevel:(NSUInteger)level
           bytesPerRow:(NSUInteger)bytesPerRow;
- (void)getBytes:(void *)pixelBytes
     bytesPerRow:(NSUInteger)bytesPerRow
      fromRegion:(MTLRegion)region
     mipmapLevel:(NSUInteger)level;
- (void)replaceRegion:(MTLRegion)region
          mipmapLevel:(NSUInteger)level
            withBytes:(const void *)pixelBytes
          bytesPerRow:(NSUInteger)bytesPerRow;
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
        _purgeableState = 2;
        _width = [descriptor width];
        _height = [descriptor height];
        _pixelFormat = [descriptor pixelFormat];
        _textureType = [descriptor textureType];
        _usage = [descriptor usage];
        _storageMode = [descriptor storageMode];
        _bytesPerRow = _width * 4;
        if (_storageMode == MTLStorageModeShared) {
            _pixels = calloc(1, _bytesPerRow * _height);
            if (_pixels == NULL) {
                [self release];
                return nil;
            }
            _allocatedSize = _bytesPerRow * _height;
        }
        [(MaliMetalDevice *)device accountAllocation:_allocatedSize release:NO];
        _isIOSurface = NO;
        _iosurface = NULL;
        _iosurfacePlane = 0;
        MMVKError error = {0};
        if (![self maliPrepareImage:NO error:&error]) {
            [self release];
            return nil;
        }
    }
    return self;
}

- (instancetype)initWithDescriptor:(MTLTextureDescriptor *)descriptor
                         iosurface:(void *)iosurface
                             plane:(NSUInteger)plane
                            device:(id<MTLDevice>)device {
    self = [super init];
    if (self) {
        if (!MaliMetalValidateTextureDescriptor(descriptor) ||
            [descriptor storageMode] != MTLStorageModeShared || iosurface == NULL || plane != 0 ||
            IOSurfaceGetBaseAddress == NULL || IOSurfaceGetWidth == NULL ||
            IOSurfaceGetHeight == NULL || IOSurfaceGetBytesPerRow == NULL ||
            IOSurfaceGetPlaneCount == NULL || IOSurfaceGetAllocSize == NULL ||
            IOSurfaceGetPixelFormat == NULL || IOSurfaceLock == NULL || IOSurfaceUnlock == NULL ||
            IOSurfaceGetPlaneCount(iosurface) != 0) {
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
            IOSurfaceGetPixelFormat(iosurface) != expectedFormat || _bytesPerRow < _width * 4 ||
            _bytesPerRow > SIZE_MAX / _height ||
            IOSurfaceGetAllocSize(iosurface) < (_height - 1) * _bytesPerRow + _width * 4 ||
            IOSurfaceGetBaseAddress(iosurface) == NULL) {
            [self release];
            return nil;
        }
        _device = (id<MTLDevice>)[(id)device retain];
        _purgeableState = 2;
        _iosurface = (void *)CFRetain(iosurface);
        _iosurfacePlane = plane;
        _pixelFormat = [descriptor pixelFormat];
        _textureType = [descriptor textureType];
        _usage = [descriptor usage];
        _storageMode = MTLStorageModeShared;
        _isIOSurface = YES;
        _pixels = IOSurfaceGetBaseAddress(iosurface);
        MMVKError error = {0};
        if (![self maliPrepareImage:NO error:&error]) {
            [self release];
            return nil;
        }
    }
    return self;
}

- (void)dealloc {
    if (_image)
        mmvk_image_destroy(_image);
    if (_allocatedSize)
        [(MaliMetalDevice *)_device accountAllocation:_allocatedSize release:YES];
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

- (NSUInteger)width {
    return _width;
}
- (NSUInteger)height {
    return _height;
}
- (NSUInteger)depth {
    return 1;
}
- (NSUInteger)mipmapLevelCount {
    return 1;
}
- (NSUInteger)sampleCount {
    return 1;
}
- (NSUInteger)arrayLength {
    return 1;
}
- (MTLPixelFormat)pixelFormat {
    return _pixelFormat;
}
- (MTLTextureType)textureType {
    return _textureType;
}
- (MTLTextureUsage)usage {
    return _usage;
}
- (BOOL)isFramebufferOnly {
    return NO;
}
- (void *)iosurface {
    return _iosurface;
}
- (NSUInteger)iosurfacePlane {
    return _iosurfacePlane;
}
- (id<MTLResource>)rootResource {
    return (id<MTLResource>)self;
}
- (id<MTLTexture>)parentTexture {
    return (id<MTLTexture>)0;
}
- (id<MTLDevice>)device {
    return _device;
}
- (void *)pixels {
    return _pixels;
}
- (NSUInteger)bytesPerRow {
    return _bytesPerRow;
}
- (MTLStorageMode)storageMode {
    return _storageMode;
}
- (MTLCPUCacheMode)cpuCacheMode {
    return MTLCPUCacheModeDefaultCache;
}
- (MTLHazardTrackingMode)hazardTrackingMode {
    return MTLHazardTrackingModeTracked;
}
- (NSUInteger)allocatedSize {
    return _allocatedSize;
}
- (NSUInteger)setPurgeableState:(NSUInteger)state {
    NSUInteger previous = _purgeableState;
    if (state == 1)
        return previous;
    if (state != 2 && state != 3) {
        [NSException raise:@"NSInvalidArgumentException"
                    format:@"Explicit empty texture purge is unsupported"];
        return previous;
    }
    _purgeableState = state;
    return previous;
}
- (BOOL)isAliasable {
    return NO;
}
- (id)heap {
    return nil;
}
- (NSUInteger)heapOffset {
    return 0;
}
- (id<MTLBuffer>)buffer {
    return nil;
}
- (NSUInteger)bufferOffset {
    return 0;
}
- (NSUInteger)bufferBytesPerRow {
    return 0;
}
- (MMVKImage *)maliImage {
    return _image;
}
- (BOOL)maliPrepareImage:(BOOL)preserve error:(MMVKError *)error {
    MMVKDevice *backend = [(MaliMetalDevice *)_device backendDevice];
    if (!backend) {
        if (error)
            *error = *[(MaliMetalDevice *)_device backendError];
        return NO;
    }
    if (!_image) {
        MMVKImageDescriptor desc = {
            (uint32_t)_width, (uint32_t)_height,
            MMVK_IMAGE_SAMPLED | MMVK_IMAGE_RENDER_TARGET | MMVK_IMAGE_TRANSFER_SRC |
                MMVK_IMAGE_TRANSFER_DST |
                ((_usage & MTLTextureUsageShaderWrite) ? MMVK_IMAGE_STORAGE : 0),
            MaliMetalPixelFormat(_pixelFormat)};
        if (mmvk_image_create(backend, &desc, &_image, error) != MMVK_OK)
            return NO;
        NSUInteger bytes = mmvk_image_allocation_size(_image);
        _allocatedSize += bytes;
        [(MaliMetalDevice *)_device accountAllocation:bytes release:NO];
    }
    if (!preserve || _storageMode == MTLStorageModePrivate)
        return YES;
    if (_isIOSurface && IOSurfaceLock(_iosurface, 1, NULL) != KERN_SUCCESS) {
        if (error) {
            error->code = MMVK_ERROR_IO;
            strcpy(error->message, "IOSurface read lock failed");
        }
        return NO;
    }
    const void *pixels = _isIOSurface ? IOSurfaceGetBaseAddress(_iosurface) : _pixels;
    MMVKResult result =
        pixels ? mmvk_image_upload(_image, pixels, _bytesPerRow, error) : MMVK_ERROR_IO;
    if (_isIOSurface && IOSurfaceUnlock(_iosurface, 1, NULL) != KERN_SUCCESS)
        result = MMVK_ERROR_IO;
    return result == MMVK_OK;
}
- (BOOL)maliReadbackImage:(MMVKError *)error {
    if (!_image || _storageMode == MTLStorageModePrivate)
        return YES;
    if (_isIOSurface && IOSurfaceLock(_iosurface, 0, NULL) != KERN_SUCCESS) {
        if (error) {
            error->code = MMVK_ERROR_IO;
            strcpy(error->message, "IOSurface write lock failed");
        }
        return NO;
    }
    void *pixels = _isIOSurface ? IOSurfaceGetBaseAddress(_iosurface) : _pixels;
    MMVKResult result =
        pixels ? mmvk_image_readback(_image, pixels, _bytesPerRow, 5000000000ull, error)
               : MMVK_ERROR_IO;
    if (_isIOSurface && IOSurfaceUnlock(_iosurface, 0, NULL) != KERN_SUCCESS)
        result = MMVK_ERROR_IO;
    return result == MMVK_OK;
}

- (BOOL)validateRegion:(MTLRegion)region
           mipmapLevel:(NSUInteger)level
           bytesPerRow:(NSUInteger)bytesPerRow {
    return level == 0 && region.origin.z == 0 && region.size.depth == 1 &&
           region.origin.x <= _width && region.size.width <= _width - region.origin.x &&
           region.origin.y <= _height && region.size.height <= _height - region.origin.y &&
           bytesPerRow >= region.size.width * 4 &&
           (region.size.height <= 1 ||
            bytesPerRow <= (SIZE_MAX - region.size.width * 4) / (region.size.height - 1));
}

- (void)getBytes:(void *)pixelBytes
     bytesPerRow:(NSUInteger)bytesPerRow
      fromRegion:(MTLRegion)region
     mipmapLevel:(NSUInteger)level {
    if (_storageMode != MTLStorageModeShared) {
        [NSException raise:@"NSInvalidArgumentException"
                    format:@"Private textures require a GPU blit for CPU access"];
        return;
    }
    if (![self validateRegion:region mipmapLevel:level bytesPerRow:bytesPerRow] ||
        pixelBytes == NULL) {
        [NSException raise:@"NSInvalidArgumentException"
                    format:@"Invalid MaliMetal texture read region"];
        return;
    }
    if (_isIOSurface && IOSurfaceLock(_iosurface, 1, NULL) != KERN_SUCCESS) {
        [NSException raise:@"NSInternalInconsistencyException"
                    format:@"Cannot lock MaliMetal IOSurface for reading"];
        return;
    }
    const void *pixels = _isIOSurface ? IOSurfaceGetBaseAddress(_iosurface) : _pixels;
    if (!pixels) {
        if (_isIOSurface)
            IOSurfaceUnlock(_iosurface, 1, NULL);
        [NSException raise:@"NSInternalInconsistencyException"
                    format:@"IOSurface has no readable storage"];
        return;
    }
    for (NSUInteger y = 0; y < region.size.height; y++) {
        memcpy((char *)pixelBytes + y * bytesPerRow,
               (const char *)pixels + (region.origin.y + y) * _bytesPerRow + region.origin.x * 4,
               region.size.width * 4);
    }
    if (_isIOSurface)
        IOSurfaceUnlock(_iosurface, 1, NULL);
}

- (void)replaceRegion:(MTLRegion)region
          mipmapLevel:(NSUInteger)level
            withBytes:(const void *)pixelBytes
          bytesPerRow:(NSUInteger)bytesPerRow {
    if (_storageMode != MTLStorageModeShared) {
        [NSException raise:@"NSInvalidArgumentException"
                    format:@"Private textures require a GPU blit for CPU access"];
        return;
    }
    if (![self validateRegion:region mipmapLevel:level bytesPerRow:bytesPerRow] ||
        pixelBytes == NULL) {
        [NSException raise:@"NSInvalidArgumentException"
                    format:@"Invalid MaliMetal texture write region"];
        return;
    }
    if (_isIOSurface && IOSurfaceLock(_iosurface, 0, NULL) != KERN_SUCCESS) {
        [NSException raise:@"NSInternalInconsistencyException"
                    format:@"Cannot lock MaliMetal IOSurface for writing"];
        return;
    }
    void *pixels = _isIOSurface ? IOSurfaceGetBaseAddress(_iosurface) : _pixels;
    if (!pixels) {
        if (_isIOSurface)
            IOSurfaceUnlock(_iosurface, 0, NULL);
        [NSException raise:@"NSInternalInconsistencyException"
                    format:@"IOSurface has no writable storage"];
        return;
    }
    for (NSUInteger y = 0; y < region.size.height; y++) {
        memcpy((char *)pixels + (region.origin.y + y) * _bytesPerRow + region.origin.x * 4,
               (const char *)pixelBytes + y * bytesPerRow, region.size.width * 4);
    }
    if (_isIOSurface)
        IOSurfaceUnlock(_iosurface, 0, NULL);
}

- (void)getBytes:(void *)bytes
      bytesPerRow:(NSUInteger)row
    bytesPerImage:(NSUInteger)image
       fromRegion:(MTLRegion)region
      mipmapLevel:(NSUInteger)level
            slice:(NSUInteger)slice {
    if (slice || (image && region.size.height &&
                  (row > SIZE_MAX / region.size.height || image < row * region.size.height))) {
        [NSException raise:@"NSInvalidArgumentException"
                    format:@"Unsupported texture slice or image stride"];
        return;
    }
    [self getBytes:bytes bytesPerRow:row fromRegion:region mipmapLevel:level];
}
- (void)replaceRegion:(MTLRegion)region
          mipmapLevel:(NSUInteger)level
                slice:(NSUInteger)slice
            withBytes:(const void *)bytes
          bytesPerRow:(NSUInteger)row
        bytesPerImage:(NSUInteger)image {
    if (slice || (image && region.size.height &&
                  (row > SIZE_MAX / region.size.height || image < row * region.size.height))) {
        [NSException raise:@"NSInvalidArgumentException"
                    format:@"Unsupported texture slice or image stride"];
        return;
    }
    [self replaceRegion:region mipmapLevel:level withBytes:bytes bytesPerRow:row];
}

@end

NSError *MaliMetalMakeError(NSInteger code, const char *message) {
    NSString *reason = [NSString stringWithUTF8String:message ? message : "Unknown backend error"];
    return [NSError errorWithDomain:@"org.heliodarwin.MaliMetal"
                               code:code
                           userInfo:[NSDictionary dictionaryWithObject:reason
                                                                forKey:@"NSLocalizedDescription"]];
}
static id MaliMetalUnsupported(NSError **error, NSString *reason) {
    if (error)
        *error = MaliMetalMakeError(MMVK_ERROR_UNSUPPORTED, [reason UTF8String]);
    return nil;
}

/* Library parsing is separate from executable pipeline creation. */
@interface NSObject (MaliMetalConstantValues)
- (NSArray *)newIndexedConstantArray;
- (NSArray *)newNamedConstantArray;
- (NSUInteger)index;
- (NSUInteger)dataType;
- (const void *)data;
- (NSString *)name;
@end
@interface MaliMetalAttribute : NSObject {
    NSString *_name;
    MMShaderAttribute _attribute;
}
- (instancetype)initWithAttribute:(const MMShaderAttribute *)attribute;
@end
@implementation MaliMetalAttribute
- (instancetype)initWithAttribute:(const MMShaderAttribute *)attribute {
    self = [super init];
    if (self) {
        _attribute = *attribute;
        _name = [[NSString stringWithUTF8String:attribute->name] copy];
    }
    return self;
}
- (void)dealloc {
    [_name release];
    [super dealloc];
}
- (NSString *)name {
    return _name;
}
- (NSUInteger)attributeIndex {
    return _attribute.index;
}
- (NSUInteger)attributeType {
    return _attribute.data_type;
}
- (BOOL)isActive {
    return _attribute.active != 0;
}
- (BOOL)isPatchData {
    return _attribute.patch_data != 0;
}
- (BOOL)isPatchControlPointData {
    return _attribute.patch_control_point_data != 0;
}
@end
@class MaliMetalLibrary;
@interface MaliMetalFunction : MaliMetalObject <MTLFunction> {
    MaliMetalLibrary *_library;
    uint32_t _index;
    NSArray *_indexedConstants, *_namedConstants;
}
- (instancetype)initWithLibrary:(MaliMetalLibrary *)library
                          index:(uint32_t)index
                      constants:(id)values;
- (MMShaderLibrary *)maliLibrary;
- (uint32_t)maliIndex;
- (MMShaderStatus)copyConstants:(MMShaderConstantValue **)out
                          count:(size_t *)count
                          error:(MMShaderError *)error;
@end
@interface MaliMetalLibrary : MaliMetalObject <MTLLibrary> {
    MaliMetalDevice *_device;
    MMShaderLibrary *_library;
}
- (instancetype)initWithBytes:(const void *)bytes
                       length:(NSUInteger)length
                       device:(MaliMetalDevice *)device
                        error:(NSError **)error;
- (MMShaderLibrary *)maliLibrary;
- (id<MTLDevice>)device;
- (NSArray *)functionNames;
- (id<MTLFunction>)newFunctionWithName:(NSString *)name;
- (id<MTLFunction>)newFunctionWithName:(NSString *)name
                        constantValues:(id)values
                                 error:(NSError **)error;
@end
@implementation MaliMetalLibrary
- (instancetype)initWithBytes:(const void *)bytes
                       length:(NSUInteger)length
                       device:(MaliMetalDevice *)device
                        error:(NSError **)error {
    self = [super init];
    if (!self)
        return nil;
    MMShaderError failure = {0};
    if (MMShaderLibraryCreate(bytes, length, &_library, &failure) != MM_SHADER_OK) {
        if (error)
            *error = MaliMetalMakeError(100 + failure.status, failure.message);
        [self release];
        return nil;
    }
    _device = [device retain];
    return self;
}
- (void)dealloc {
    MMShaderLibraryDestroy(_library);
    [_device release];
    [super dealloc];
}
- (MMShaderLibrary *)maliLibrary {
    return _library;
}
- (id<MTLDevice>)device {
    return _device;
}
- (NSUInteger)libraryType {
    return 0;
}
- (NSArray *)functionNames {
    uint32_t count = MMShaderLibraryEntryCount(_library);
    id *names = calloc(count ? count : 1, sizeof(id));
    if (!names)
        return nil;
    for (uint32_t i = 0; i < count; i++)
        names[i] = [NSString stringWithUTF8String:MMShaderLibraryGetEntry(_library, i)->name];
    NSArray *array = [NSArray arrayWithObjects:names count:count];
    free(names);
    return array;
}
- (id<MTLFunction>)newFunctionWithName:(NSString *)name {
    return [self newFunctionWithName:name constantValues:nil error:NULL];
}
- (id<MTLFunction>)newFunctionWithName:(NSString *)name
                        constantValues:(id)values
                                 error:(NSError **)error {
    uint32_t index = 0;
    MMShaderError failure = {0};
    if (MMShaderLibraryFindEntry(_library, [name UTF8String], &index, &failure) != MM_SHADER_OK) {
        if (error)
            *error = MaliMetalMakeError(100 + failure.status, failure.message);
        return nil;
    }
    const MMShaderEntry *entry = MMShaderLibraryGetEntry(_library, index);
    if (entry->stage > MM_SHADER_KERNEL)
        return MaliMetalUnsupported(error, @"Unsupported Metal function stage");
    if (values && (![values respondsToSelector:@selector(newIndexedConstantArray)] ||
                   ![values respondsToSelector:@selector(newNamedConstantArray)]))
        return MaliMetalUnsupported(
            error, @"Function constant container has no supported serialization interface");
    const MMShaderCompiler *compiler = MMShaderGetNativeCompiler();
    if (compiler && compiler->reflect && compiler->free_string) {
        size_t byteCount = 0, reflectionSize = 0;
        char *reflection = NULL;
        const void *bytes = MMShaderLibraryBytes(_library, &byteCount);
        MMShaderStatus reflected = compiler->reflect(bytes, byteCount, entry->name, &reflection,
                                                     &reflectionSize, &failure);
        if (reflection)
            compiler->free_string(reflection);
        /* Reflection validates actual AIR for the supported target functions.
         * Optional reflection may refuse a symbolic specialization that the
         * later paired pipeline compiler can still handle. */
        if (reflected != MM_SHADER_OK && reflected != MM_SHADER_UNSUPPORTED_FEATURE &&
            reflected != MM_SHADER_COMPILER_UNAVAILABLE) {
            if (error)
                *error = MaliMetalMakeError(100 + reflected, failure.message);
            return nil;
        }
    }
    MaliMetalFunction *function = [[MaliMetalFunction alloc] initWithLibrary:self
                                                                       index:index
                                                                   constants:values];
    if (!function)
        return nil;
    MMShaderConstantValue *constants = NULL;
    size_t count = 0;
    MMShaderStatus status = [function copyConstants:&constants count:&count error:&failure];
    free(constants);
    if (status != MM_SHADER_OK) {
        if (error)
            *error = MaliMetalMakeError(100 + status, failure.message);
        [function release];
        return nil;
    }
    return (id)function;
}
@end
static size_t MaliMetalConstantSize(uint32_t type) {
    /* Metal 16.7 _MTLConstantDataSize (0x181a3e274), table 0x181b60668.
     * Constants use tightly packed vector bytes, including uchar3 = 3 bytes. */
    if (type >= 3 && type <= 6)
        return 4 * (type - 2);
    if (type >= 16 && type <= 19)
        return 2 * (type - 15);
    if (type >= 29 && type <= 36)
        return 4 * ((type - 29) % 4 + 1);
    if (type >= 37 && type <= 44)
        return 2 * ((type - 37) % 4 + 1);
    if (type >= 45 && type <= 56)
        return (type - 45) % 4 + 1;
    if (type >= 81 && type <= 88)
        return 8 * ((type - 81) % 4 + 1);
    return 0;
}
@implementation MaliMetalFunction
- (instancetype)initWithLibrary:(MaliMetalLibrary *)library
                          index:(uint32_t)index
                      constants:(id)values {
    self = [super init];
    if (self) {
        _library = [library retain];
        _index = index;
        if (values) {
            _indexedConstants = [values newIndexedConstantArray];
            _namedConstants = [values newNamedConstantArray];
        }
    }
    return self;
}
- (void)dealloc {
    [_indexedConstants release];
    [_namedConstants release];
    [_library release];
    [super dealloc];
}
- (NSString *)name {
    return [NSString
        stringWithUTF8String:MMShaderLibraryGetEntry([_library maliLibrary], _index)->name];
}
- (id<MTLDevice>)device {
    return [_library device];
}
- (MTLFunctionType)functionType {
    return (MTLFunctionType)(MMShaderLibraryGetEntry([_library maliLibrary], _index)->stage + 1);
}
- (NSArray *)stageInputAttributes {
    const MMShaderEntry *entry = MMShaderLibraryGetEntry([_library maliLibrary], _index);
    id *attributes = calloc(entry->attribute_count ? entry->attribute_count : 1, sizeof(id));
    if (!attributes)
        return nil;
    for (uint32_t i = 0; i < entry->attribute_count; i++)
        attributes[i] =
            [[[MaliMetalAttribute alloc] initWithAttribute:entry->attributes + i] autorelease];
    NSArray *array = [NSArray arrayWithObjects:attributes count:entry->attribute_count];
    free(attributes);
    return array;
}
- (NSArray *)vertexAttributes {
    return [self functionType] == MTLFunctionTypeVertex ? [self stageInputAttributes] : nil;
}
- (MMShaderLibrary *)maliLibrary {
    return [_library maliLibrary];
}
- (uint32_t)maliIndex {
    return _index;
}
- (MMShaderStatus)copyConstants:(MMShaderConstantValue **)out
                          count:(size_t *)count
                          error:(MMShaderError *)error {
    *out = NULL;
    *count = 0;
    size_t total = [_indexedConstants count] + [_namedConstants count];
    if (!total)
        return MM_SHADER_OK;
    MMShaderConstantValue *constants = calloc(total, sizeof(*constants));
    if (!constants) {
        if (error) {
            error->status = MM_SHADER_OUT_OF_MEMORY;
            strcpy(error->message, "Cannot allocate function constant overrides");
        }
        return MM_SHADER_OUT_OF_MEMORY;
    }
    const MMShaderEntry *entry = MMShaderLibraryGetEntry([_library maliLibrary], _index);
    for (size_t i = 0; i < total; i++) {
        BOOL indexed = i < [_indexedConstants count];
        id value = indexed ? [_indexedConstants objectAtIndex:i]
                           : [_namedConstants objectAtIndex:i - [_indexedConstants count]];
        if (indexed && [value index] > UINT32_MAX)
            continue;
        uint32_t index = indexed ? (uint32_t)[value index] : UINT32_MAX;
        if (!indexed) {
            const char *name = [[value name] UTF8String];
            index = UINT32_MAX;
            for (uint32_t c = 0; c < entry->constant_count; c++)
                if (name && !strcmp(name, entry->constants[c].name)) {
                    index = entry->constants[c].index;
                    break;
                }
            if (index == UINT32_MAX)
                continue; /* Metal ignores values absent from this function. */
        }
        const MMShaderConstant *declared = NULL;
        for (uint32_t c = 0; c < entry->constant_count; c++)
            if (entry->constants[c].index == index) {
                declared = entry->constants + c;
                break;
            }
        if (!declared)
            continue;
        uint32_t type = (uint32_t)[value dataType];
        if ((declared->data_type && declared->data_type != type) || ![value data]) {
            if (error) {
                error->status = MM_SHADER_INVALID_ARGUMENT;
                strcpy(error->message,
                       "Function constant type or data does not match its declaration");
            }
            free(constants);
            return MM_SHADER_INVALID_ARGUMENT;
        }
        size_t size = MaliMetalConstantSize(type);
        if (!size) {
            if (error) {
                error->status = MM_SHADER_UNSUPPORTED_FEATURE;
                strcpy(error->message, "Function constant data type is unsupported");
            }
            free(constants);
            return MM_SHADER_UNSUPPORTED_FEATURE;
        }
        for (size_t j = 0; j < *count; j++)
            if (constants[j].index == index) {
                if (error) {
                    error->status = MM_SHADER_UNSUPPORTED_FEATURE;
                    strcpy(error->message,
                           "Conflicting named and indexed function constant overrides");
                }
                free(constants);
                return MM_SHADER_UNSUPPORTED_FEATURE;
            }
        constants[*count] = (MMShaderConstantValue){index, type, [value data], size};
        ++*count;
    }
    *out = constants;
    return MM_SHADER_OK;
}
@end

@interface MaliMetalSampler : MaliMetalObject <MTLSamplerState> {
    MaliMetalDevice *_device;
    MMVKSampler *_sampler;
}
- (instancetype)initWithDevice:(MaliMetalDevice *)device
                    descriptor:(MTLSamplerDescriptor *)descriptor;
@end
static BOOL MaliMetalSamplerAddress(NSUInteger mode, uint32_t *native) {
    switch (mode) {
    case 0:
        *native = 2;
        return YES;
    case 2:
        *native = 0;
        return YES;
    case 3:
        *native = 1;
        return YES;
    case 4:
    case 5:
        *native = 3;
        return YES;
    default:
        return NO;
    }
}
@implementation MaliMetalSampler
- (instancetype)initWithDevice:(MaliMetalDevice *)device
                    descriptor:(MTLSamplerDescriptor *)descriptor {
    self = [super init];
    if (!self)
        return nil;
    if (!descriptor || [descriptor minFilter] > 1 || [descriptor magFilter] > 1 ||
        [descriptor mipFilter] > 2 || ![descriptor normalizedCoordinates] ||
        [descriptor maxAnisotropy] != 1 || [descriptor compareFunction] != 0 ||
        [descriptor borderColor] > 2 || [descriptor lodAverage] ||
        [descriptor supportArgumentBuffers]) {
        [self release];
        return nil;
    }
    MMVKSamplerDescriptor native = {0};
    if (!MaliMetalSamplerAddress([descriptor sAddressMode], &native.address_u) ||
        !MaliMetalSamplerAddress([descriptor tAddressMode], &native.address_v) ||
        !MaliMetalSamplerAddress([descriptor rAddressMode], &native.address_w)) {
        [self release];
        return nil;
    }
    native.min_filter = (uint32_t)[descriptor minFilter];
    native.mag_filter = (uint32_t)[descriptor magFilter];
    native.mip_filter = [descriptor mipFilter] == 2 ? 1 : 0;
    native.normalized_coordinates = 1;
    native.max_anisotropy = 1;
    native.border_color = (uint32_t)[descriptor borderColor] * 2;
    native.min_lod = [descriptor lodMinClamp];
    native.max_lod = [descriptor lodMaxClamp];
    if (!isfinite(native.min_lod) || !isfinite(native.max_lod) || native.min_lod < 0 ||
        native.max_lod < native.min_lod) {
        [self release];
        return nil;
    }
    if ([descriptor mipFilter] == 0) {
        /* A small positive maximum preserves minification-vs-magnification
         * filter selection while preventing access to a second mip level. */
        native.max_lod = fminf(native.max_lod, 0.25f);
        native.min_lod = fminf(native.min_lod, native.max_lod);
    }
    MMVKError error = {0};
    if (mmvk_sampler_create([device backendDevice], &native, &_sampler, &error) != MMVK_OK) {
        [self release];
        return nil;
    }
    _device = [device retain];
    return self;
}
- (void)dealloc {
    if (_sampler)
        mmvk_sampler_destroy(_sampler);
    [_device release];
    [super dealloc];
}
- (id<MTLDevice>)device {
    return _device;
}
- (MMVKSampler *)maliSampler {
    return _sampler;
}
@end

@interface MaliMetalRenderPipeline : MaliMetalObject <MTLRenderPipelineState> {
    MaliMetalDevice *_device;
    MMVKPipeline *_pipeline;
    MTLPixelFormat _colorFormat;
    MMShaderRenderOutput _output;
    MMShaderVulkanConstants _constants;
    NSUInteger _constantAllocatedSize;
}
- (instancetype)initWithDevice:(MaliMetalDevice *)device
                      pipeline:(MMVKPipeline *)pipeline
                   colorFormat:(MTLPixelFormat)format
                        output:(MMShaderRenderOutput *)output
                     constants:(MMShaderVulkanConstants *)constants;
@end
@implementation MaliMetalRenderPipeline
- (instancetype)initWithDevice:(MaliMetalDevice *)device
                      pipeline:(MMVKPipeline *)pipeline
                   colorFormat:(MTLPixelFormat)format
                        output:(MMShaderRenderOutput *)output
                     constants:(MMShaderVulkanConstants *)constants {
    self = [super init];
    if (self) {
        _device = [device retain];
        _pipeline = pipeline;
        _colorFormat = format;
        _output = *output;
        memset(output, 0, sizeof(*output));
        _constants = *constants;
        memset(constants, 0, sizeof(*constants));
        for (NSUInteger i = 0; i < MM_SHADER_MAX_BINDINGS; i++)
            if (_constants.buffers[i])
                _constantAllocatedSize += mmvk_buffer_allocation_size(_constants.buffers[i]);
        [_device accountAllocation:_constantAllocatedSize release:NO];
    }
    return self;
}
- (void)dealloc {
    mmvk_pipeline_destroy(_pipeline);
    MMShaderVulkanDestroyConstants(&_constants);
    [_device accountAllocation:_constantAllocatedSize release:YES];
    MMShaderRenderOutputDestroy(&_output);
    [_device release];
    [super dealloc];
}
- (id<MTLDevice>)device {
    return _device;
}
- (MMVKPipeline *)maliPipeline {
    return _pipeline;
}
- (MTLPixelFormat)maliColorFormat {
    return _colorFormat;
}
- (const MMShaderRenderOutput *)maliShaderOutput {
    return &_output;
}
- (const MMShaderVulkanConstants *)maliShaderConstants {
    return &_constants;
}
- (BOOL)supportIndirectCommandBuffers {
    return NO;
}
- (NSUInteger)imageblockSampleLength {
    return 0;
}
@end

typedef struct MaliMetalComputeVariant {
    MMVKPipeline *pipeline;
    MMVKSpecializationEntry *entries;
    uint8_t *data;
    uint32_t count, stage_flags, required_subgroup_size;
    size_t data_size;
    struct MaliMetalComputeVariant *next;
} MaliMetalComputeVariant;

@interface MaliMetalComputePipeline : MaliMetalObject <MTLComputePipelineState> {
    MaliMetalDevice *_device;
    MMVKComputeProgram *_program;
    MMShaderComputeOutput _output;
    MMVKBuffer *_constantBuffers[MM_SHADER_MAX_BINDINGS];
    NSUInteger _constantAllocatedSize;
    MaliMetalComputeVariant *_variants;
    pthread_mutex_t _variantLock;
}
- (instancetype)initWithDevice:(MaliMetalDevice *)device
                       program:(MMVKComputeProgram *)program
                        output:(MMShaderComputeOutput *)output
                     constants:(MMVKBuffer **)constants;
@end
static void MaliMetalComputeSetError(MMVKError *error, MMVKResult code, const char *message) {
    if (!error)
        return;
    memset(error, 0, sizeof(*error));
    error->code = code;
    size_t n = strlen(message);
    if (n >= sizeof(error->message))
        n = sizeof(error->message) - 1;
    memcpy(error->message, message, n);
}
@implementation MaliMetalComputePipeline
- (instancetype)initWithDevice:(MaliMetalDevice *)device
                       program:(MMVKComputeProgram *)program
                        output:(MMShaderComputeOutput *)output
                     constants:(MMVKBuffer **)constants {
    self = [super init];
    if (!self)
        return nil;
    pthread_mutex_init(&_variantLock, NULL);
    _device = [device retain];
    _program = program;
    _output = *output;
    memset(output, 0, sizeof(*output));
    memcpy(_constantBuffers, constants, sizeof(_constantBuffers));
    memset(constants, 0, sizeof(_constantBuffers));
    for (NSUInteger i = 0; i < MM_SHADER_MAX_BINDINGS; i++)
        if (_constantBuffers[i])
            _constantAllocatedSize += mmvk_buffer_allocation_size(_constantBuffers[i]);
    [_device accountAllocation:_constantAllocatedSize release:NO];
    return self;
}
- (void)dealloc {
    MaliMetalComputeVariant *variant = _variants;
    while (variant) {
        MaliMetalComputeVariant *next = variant->next;
        mmvk_pipeline_destroy(variant->pipeline);
        free(variant->entries);
        free(variant->data);
        free(variant);
        variant = next;
    }
    mmvk_compute_program_destroy(_program);
    for (NSUInteger i = 0; i < MM_SHADER_MAX_BINDINGS; i++)
        mmvk_buffer_destroy(_constantBuffers[i]);
    [_device accountAllocation:_constantAllocatedSize release:YES];
    MMShaderComputeOutputDestroy(&_output);
    pthread_mutex_destroy(&_variantLock);
    [_device release];
    [super dealloc];
}
- (id<MTLDevice>)device {
    return _device;
}
- (NSUInteger)maxTotalThreadsPerThreadgroup {
    return _output.max_total_threads_per_threadgroup;
}
- (NSUInteger)threadExecutionWidth {
    return _output.thread_execution_width;
}
- (NSUInteger)staticThreadgroupMemoryLength {
    return _output.static_threadgroup_memory_length;
}
- (BOOL)supportIndirectCommandBuffers {
    return NO;
}
- (MMVKComputeProgram *)maliComputeProgram {
    return _program;
}
- (const MMShaderComputeOutput *)maliComputeOutput {
    return &_output;
}
- (MMVKBuffer *const *)maliComputeConstants {
    return _constantBuffers;
}
- (MMVKPipeline *)maliComputeVariantForRegion:(const MMShaderComputeRegion *)region
                                        error:(MMVKError *)error {
    if (!region || region->specialization_count > 128 ||
        (region->specialization_count && !region->specialization) ||
        (region->specialization_size && !region->specialization_data)) {
        MaliMetalComputeSetError(error, MMVK_ERROR_ARGUMENT,
                                 "Invalid native compute specialization");
        return NULL;
    }
    pthread_mutex_lock(&_variantLock);
    for (MaliMetalComputeVariant *v = _variants; v; v = v->next) {
        if (v->count != region->specialization_count ||
            v->data_size != region->specialization_size || v->stage_flags != region->stage_flags ||
            v->required_subgroup_size != region->required_subgroup_size)
            continue;
        BOOL same = YES;
        for (uint32_t i = 0; i < v->count; i++)
            if (v->entries[i].id != region->specialization[i].id ||
                v->entries[i].offset != region->specialization[i].offset ||
                v->entries[i].size != region->specialization[i].size) {
                same = NO;
                break;
            }
        if (same &&
            (!v->data_size || !memcmp(v->data, region->specialization_data, v->data_size))) {
            MMVKPipeline *pipeline = v->pipeline;
            pthread_mutex_unlock(&_variantLock);
            return pipeline;
        }
    }
    MaliMetalComputeVariant *v = calloc(1, sizeof(*v));
    if (v) {
        v->count = region->specialization_count;
        v->data_size = region->specialization_size;
        v->stage_flags = region->stage_flags;
        v->required_subgroup_size = region->required_subgroup_size;
        v->entries = calloc(v->count ? v->count : 1, sizeof(*v->entries));
        v->data = malloc(v->data_size ? v->data_size : 1);
    }
    if (!v || !v->entries || !v->data) {
        if (v) {
            free(v->entries);
            free(v->data);
            free(v);
        }
        pthread_mutex_unlock(&_variantLock);
        MaliMetalComputeSetError(error, MMVK_ERROR_MEMORY,
                                 "Compute variant cache allocation failed");
        return NULL;
    }
    for (uint32_t i = 0; i < v->count; i++)
        v->entries[i] = (MMVKSpecializationEntry){region->specialization[i].id,
                                                  region->specialization[i].offset,
                                                  region->specialization[i].size};
    if (v->data_size)
        memcpy(v->data, region->specialization_data, v->data_size);
    MMVKResult result = mmvk_compute_program_create_variant(
        _program, v->entries, v->count, v->data, v->data_size, v->stage_flags,
        v->required_subgroup_size, &v->pipeline, error);
    if (result != MMVK_OK) {
        free(v->entries);
        free(v->data);
        free(v);
        pthread_mutex_unlock(&_variantLock);
        return NULL;
    }
    v->next = _variants;
    _variants = v;
    MMVKPipeline *pipeline = v->pipeline;
    pthread_mutex_unlock(&_variantLock);
    return pipeline;
}
@end

/*
 * Core MaliMetalDevice Implementation
 */
@implementation MaliMetalDevice {
    io_connect_t _userClient;
    uint64_t _registryID;
    MMVKDevice *_backend;
    MMVKError _backendError;
    uint64_t _allocatedBytes;
    pthread_mutex_t _allocationLock, _executionLock;
    MMMetalDeviceLimits *_nativeLimits;
}

- (instancetype)initWithAcceleratorPort:(mach_port_t)acceleratorPort {
    self = [super init];
    if (!self)
        return nil;
    pthread_mutex_init(&_allocationLock, NULL);
    pthread_mutex_init(&_executionLock, NULL);
    _nativeLimits = MaliMetalLimitsStorage(self);
    if (!_nativeLimits) {
        NSLog(@"[MaliMetal] Unsupported Metal device-limits ABI");
        [self release];
        return nil;
    }
    kern_return_t kr = IORegistryEntryGetRegistryEntryID(acceleratorPort, &_registryID);
    if (kr != KERN_SUCCESS ||
        IOServiceOpen(acceleratorPort, mach_task_self(), 0, &_userClient) != KERN_SUCCESS) {
        [self release];
        return nil;
    }
    MMVKResult result = mmvk_device_create(_userClient, NULL, &_backend, &_backendError);
    if (result != MMVK_OK) {
        NSLog(@"[MaliMetal] Native Vulkan backend unavailable: %s", _backendError.message);
        [self release];
        return nil;
    }
    const MMVKCapabilities *caps = mmvk_device_capabilities(_backend);
    const MMShaderCompiler *compiler = MMShaderGetNativeCompiler();
    size_t profileSize = 0;
    const char *profile = mmvk_device_shader_profile_json(_backend, &profileSize);
    const uint32_t required = MMVK_CAP_BUFFER | MMVK_CAP_RENDER | MMVK_CAP_IMAGE_COPY;
    if (!caps || (caps->capabilities & required) != required || !compiler ||
        compiler->abi_version != MM_SHADER_ABI_VERSION || !compiler->compile_render || !profile ||
        !profileSize) {
        NSLog(
            @"[MaliMetal] Registration deferred: native execution/compiler profile is incomplete");
        [self release];
        return nil;
    }
    /* MTLAddDevice calls initLimits and initFeatureQueries after checking both
     * device protocols. Leave those initializers to the loader; the base limits
     * table only describes Apple profiles, while our override uses native caps. */
    return self;
}

- (void)initLimits {
    const MMVKCapabilities *caps = [self backendCapabilities];
    if (!_nativeLimits || !caps) {
        NSLog(@"[MaliMetal] Device limits requested before native initialization");
        abort();
    }
    const MMShaderCompiler *compiler = MMShaderGetNativeCompiler();
    BOOL compute = (caps->capabilities & MMVK_CAP_COMPUTE) && compiler && compiler->compile_compute;
    MMMetalDeviceLimits limits = {0};
    /* One RGBA8/BGRA8 target, one viewport and no optional tile/MSAA/depth path. */
    limits.maxFramebufferStorageBits = 32;
    limits.maxColorAttachments = 1;
    limits.maxVertexAttributes = caps->max_vertex_attributes < 31 ? caps->max_vertex_attributes : 31;
    limits.maxVertexBuffers = caps->max_vertex_buffers < 31 ? caps->max_vertex_buffers : 31;
    limits.maxFragmentBuffers = 31;
    limits.maxVertexTextures = limits.maxFragmentTextures = (uint32_t)[self maxFragmentTextures];
    limits.maxVertexSamplers = limits.maxFragmentSamplers = (uint32_t)[self maxFragmentSamplers];
    limits.maxVertexInlineDataSize = limits.maxFragmentInlineDataSize = caps->max_inline_uniform_bytes;
    limits.maxInterpolants = caps->max_varying_components / 4;
    limits.maxInterpolatedComponents = caps->max_varying_components;
    /* The frontend currently accepts triangle draws only. */
    limits.minConstantBufferAlignmentBytes = (uint32_t)caps->min_uniform_buffer_offset_alignment;
    limits.minBufferNoCopyAlignmentBytes = caps->cpu_page_size;
    limits.maxTextureWidth2D = limits.maxTextureHeight2D = caps->max_image_dimension_2d;
    limits.maxTextureLayers = 1;
    /* IOSurface access copies rows through the transfer path; no zero-copy
     * alignment or foreign-memory import capability is advertised. */
    limits.iosurfaceTextureAlignmentBytes = limits.iosurfaceReadOnlyTextureAlignmentBytes = 1;
    limits.maxFunctionConstantIndices = 65536;
    limits.maxViewportCount = 1;
    limits.maxVertexAmplificationFactor = limits.maxVertexAmplificationCount = 1;
    limits.maxBufferLength = caps->max_buffer_size;
    if (compute) {
        limits.maxComputeBuffers = limits.maxComputeLocalMemorySizes = 31;
        limits.maxComputeTextures = limits.maxFragmentTextures;
        limits.maxComputeSamplers = limits.maxFragmentSamplers;
        limits.maxComputeInlineDataSize = caps->max_inline_uniform_bytes;
        limits.maxTotalComputeThreadsPerThreadgroup = caps->max_compute_workgroup_invocations;
        limits.maxComputeThreadgroupMemory = caps->max_compute_shared_memory_size;
        limits.maxComputeThreadgroupMemoryAlignmentBytes = 16;
    }
    *_nativeLimits = limits;
}
- (MMVKDevice *)backendDevice {
    return _backend;
}
- (const MMVKCapabilities *)backendCapabilities {
    return _backend ? mmvk_device_capabilities(_backend) : NULL;
}
- (const MMVKError *)backendError {
    return &_backendError;
}
- (void)lockExecution {
    pthread_mutex_lock(&_executionLock);
}
- (void)unlockExecution {
    pthread_mutex_unlock(&_executionLock);
}
- (void)accountAllocation:(NSUInteger)size release:(BOOL)release {
    pthread_mutex_lock(&_allocationLock);
    if (release)
        _allocatedBytes -= size;
    else
        _allocatedBytes += size;
    pthread_mutex_unlock(&_allocationLock);
}
/* MMIO/ring memory is kernel-owned and is never mapped into this frontend. */
- (volatile uint32_t *)hardwareRegisters {
    return NULL;
}
- (void *)ringBuffer {
    return NULL;
}
- (uint32_t)gpuID {
    const MMVKCapabilities *c = [self backendCapabilities];
    return c ? c->gpu_id : 0;
}
- (void)dealloc {
    if (_backend)
        mmvk_device_destroy(_backend);
    if (_userClient)
        IOServiceClose(_userClient);
    pthread_mutex_destroy(&_executionLock);
    pthread_mutex_destroy(&_allocationLock);
    [super dealloc];
}
- (NSString *)name {
    return @"HelioDarwin Mali-G57";
}
- (NSString *)vendorName {
    return @"Arm";
}

- (uint64_t)registryID {
    return _registryID;
}

- (MTLSize)maxThreadsPerThreadgroup {
    const MMVKCapabilities *caps = [self backendCapabilities];
    const MMShaderCompiler *compiler = MMShaderGetNativeCompiler();
    if (!caps || !(caps->capabilities & MMVK_CAP_COMPUTE) || !compiler ||
        !compiler->compile_compute)
        return MTLSizeMake(0, 0, 0);
    uint32_t maximum = caps->max_compute_workgroup_invocations;
    MTLSize size = MTLSizeMake(caps->max_compute_workgroup_size[0],
                               caps->max_compute_workgroup_size[1],
                               caps->max_compute_workgroup_size[2]);
    if (size.width > maximum) size.width = maximum;
    if (size.height > maximum) size.height = maximum;
    if (size.depth > maximum) size.depth = maximum;
    return size;
}

- (NSUInteger)maxThreadgroupMemoryLength {
    const MMVKCapabilities *caps = [self backendCapabilities];
    const MMShaderCompiler *compiler = MMShaderGetNativeCompiler();
    return caps && (caps->capabilities & MMVK_CAP_COMPUTE) && compiler && compiler->compile_compute
               ? caps->max_compute_shared_memory_size
               : 0;
}
- (BOOL)hasUnifiedMemory {
    return YES;
}

- (uint64_t)recommendedMaxWorkingSetSize {
    return 0;
}

- (uint64_t)currentAllocatedSize {
    pthread_mutex_lock(&_allocationLock);
    uint64_t bytes = _allocatedBytes;
    pthread_mutex_unlock(&_allocationLock);
    return bytes;
}

- (NSUInteger)maxBufferLength {
    const MMVKCapabilities *c = [self backendCapabilities];
    return c ? c->max_buffer_size : 0;
}

- (NSUInteger)featureProfile {
    return (NSUInteger)-1;
}
+ (BOOL)featureProfile:(NSUInteger)profile supportsFeatureSet:(MTLFeatureSet)featureSet {
    (void)profile;
    (void)featureSet;
    return NO;
}

- (BOOL)supportsFeatureSet:(MTLFeatureSet)featureSet {
    (void)featureSet;
    return NO;
}

- (BOOL)supportsFamily:(MTLGPUFamily)gpuFamily {
    (void)gpuFamily;
    return NO;
}

/* iOS 16 QuartzCore queries these selectors directly. Native Vulkan feature
 * support alone does not prove a complete Apple GPU family or Metal feature. */
- (BOOL)supportsNonUniformThreadgroupSize {
    const MMVKCapabilities *caps = [self backendCapabilities];
    const MMShaderCompiler *compiler = MMShaderGetNativeCompiler();
    /* Exact native interior/edge dispatch regions implement this feature;
     * it is not a claim about any Apple GPU family. */
    return caps && (caps->capabilities & MMVK_CAP_COMPUTE) && compiler && compiler->compile_compute;
}
- (BOOL)supportsMemorylessRenderTargets {
    return NO;
}
- (BOOL)supportsRasterOrderGroups {
    return NO;
}
- (BOOL)supportsYCBCRFormats12 {
    return NO;
}
- (BOOL)supportsYCBCRPackedFormats12 {
    return NO;
}
- (BOOL)supportsNativeHardwareFP16 {
    const MMVKCapabilities *caps = [self backendCapabilities];
    /* CA uses this query to select actual lph/lpf metallib entry points. */
    return caps && (caps->capabilities & MMVK_CAP_SHADER_FLOAT16) != 0;
}
- (BOOL)supportsTileShaders {
    return NO;
}
- (BOOL)supportsSIMDGroup {
    return NO;
}
- (BOOL)supportsTextureSampleCount:(NSUInteger)count {
    return count == 1 && [self backendDevice] != NULL;
}
- (NSUInteger)maxFragmentTextures {
    const MMVKCapabilities *c = [self backendCapabilities];
    return c ? (c->max_per_stage_sampled_images < 128 ? c->max_per_stage_sampled_images : 128) : 0;
}
- (NSUInteger)maxFragmentSamplers {
    const MMVKCapabilities *c = [self backendCapabilities];
    return c ? (c->max_per_stage_samplers < 16 ? c->max_per_stage_samplers : 16) : 0;
}
- (NSUInteger)maxVertexTextures {
    return [self maxFragmentTextures];
}
- (NSUInteger)maxVertexSamplers {
    return [self maxFragmentSamplers];
}
- (NSUInteger)maxColorAttachments {
    return _backend ? 1 : 0;
}
- (NSUInteger)maxTextureWidth2D {
    const MMVKCapabilities *c = [self backendCapabilities];
    return c ? c->max_image_dimension_2d : 0;
}
- (NSUInteger)maxTextureHeight2D {
    return [self maxTextureWidth2D];
}
- (NSUInteger)minConstantBufferAlignmentBytes {
    const MMVKCapabilities *c = [self backendCapabilities];
    return c ? c->min_uniform_buffer_offset_alignment : 1;
}
- (NSUInteger)iosurfaceReadOnlyTextureAlignmentBytes {
    return 4;
}

/* Command Queue Creation */
- (id<MTLCommandQueue>)newCommandQueue {
    return [self newCommandQueueWithMaxCommandBufferCount:64];
}

- (id<MTLCommandQueue>)newCommandQueueWithMaxCommandBufferCount:(NSUInteger)maxCommandBufferCount {
    return (id<MTLCommandQueue>)[[MaliMetalCommandQueue alloc]
               initWithDevice:self
        maxCommandBufferCount:maxCommandBufferCount];
}

/* Buffer Allocation */
- (id<MTLBuffer>)newBufferWithLength:(NSUInteger)length options:(MTLResourceOptions)options {
    MTLStorageMode mode = (options >> 4) & 3;
    if ((options & ~(0x30ul | 0x200ul)) ||
        (mode != MTLStorageModeShared && mode != MTLStorageModePrivate))
        return nil;
    if (length == 0 || length > [self maxBufferLength])
        return nil;
    return (id<MTLBuffer>)[[MaliMetalBuffer alloc] initWithLength:length
                                                           device:self
                                                      storageMode:mode];
}

- (id<MTLBuffer>)newBufferWithBytes:(const void *)pointer
                             length:(NSUInteger)length
                            options:(MTLResourceOptions)options {
    if (options != MTLResourceStorageModeShared && options != 0x200)
        return nil;
    if (pointer == NULL || length == 0 || length > [self maxBufferLength])
        return nil;
    return (id<MTLBuffer>)[[MaliMetalBuffer alloc] initWithBytes:pointer length:length device:self];
}

/* Texture Allocation */
- (id<MTLTexture>)newTextureWithDescriptor:(MTLTextureDescriptor *)descriptor {
    return (id<MTLTexture>)[[MaliMetalTexture alloc] initWithDescriptor:descriptor device:self];
}

- (id<MTLTexture>)newTextureWithDescriptor:(MTLTextureDescriptor *)descriptor
                                 iosurface:(void *)iosurface
                                     plane:(NSUInteger)plane {
    return (id<MTLTexture>)[[MaliMetalTexture alloc] initWithDescriptor:descriptor
                                                              iosurface:iosurface
                                                                  plane:plane
                                                                 device:self];
}

- (id<MTLBuffer>)newBufferWithBytesNoCopy:(void *)pointer
                                   length:(NSUInteger)length
                                  options:(MTLResourceOptions)options
                              deallocator:(void (^)(void *, NSUInteger))deallocator {
    (void)pointer;
    (void)length;
    (void)options;
    (void)deallocator;
    /* No ownership is transferred on this failed import. */
    return nil;
}
- (id<MTLSamplerState>)newSamplerStateWithDescriptor:(MTLSamplerDescriptor *)descriptor {
    return (id)[[MaliMetalSampler alloc] initWithDevice:self descriptor:descriptor];
}

/* Library & Pipeline Compilation */
- (id<MTLLibrary>)newDefaultLibrary {
    return [self newDefaultLibraryWithBundle:[NSBundle mainBundle] error:NULL];
}
- (id<MTLLibrary>)newDefaultLibraryWithBundle:(NSBundle *)bundle error:(NSError **)error {
    NSURL *url = [bundle URLForResource:@"default" withExtension:@"metallib"];
    if (!url)
        return MaliMetalUnsupported(error, @"The bundle has no default.metallib");
    return [self newLibraryWithURL:url error:error];
}
- (id<MTLLibrary>)newLibraryWithURL:(NSURL *)url error:(NSError **)error {
    if (!url)
        return MaliMetalUnsupported(error, @"Library URL is nil");
    NSData *data = [NSData dataWithContentsOfURL:url options:0 error:error];
    if (!data)
        return nil;
    return (id)[[MaliMetalLibrary alloc] initWithBytes:[data bytes]
                                                length:[data length]
                                                device:self
                                                 error:error];
}
- (id<MTLLibrary>)newLibraryWithFile:(NSString *)path error:(NSError **)error {
    return [self newLibraryWithURL:[NSURL fileURLWithPath:path] error:error];
}
- (id)newPipelineLibraryWithFilePath:(NSString *)path error:(NSError **)error {
    (void)path;
    return MaliMetalUnsupported(
        error, @"Apple precompiled pipeline libraries contain hardware-specific binaries");
}
- (id)newDepthStencilStateWithDescriptor:(id)descriptor {
    (void)descriptor;
    return nil;
}

- (id<MTLLibrary>)newLibraryWithSource:(NSString *)source
                               options:(MTLCompileOptions *)options
                                 error:(NSError **)error {
    (void)source;
    (void)options;
    return MaliMetalUnsupported(error, @"MSL compilation is not implemented");
}

- (id<MTLLibrary>)newLibraryWithData:(dispatch_data_t)data error:(NSError **)error {
    if (!data)
        return MaliMetalUnsupported(error, @"Library data is nil");
    const void *bytes = NULL;
    size_t length = 0;
    dispatch_data_t map = dispatch_data_create_map(data, &bytes, &length);
    if (!map)
        return MaliMetalUnsupported(error, @"Cannot map library data");
    id library = [[MaliMetalLibrary alloc] initWithBytes:bytes
                                                  length:length
                                                  device:self
                                                   error:error];
    dispatch_release(map);
    return library;
}

- (id<MTLRenderPipelineState>)newRenderPipelineStateWithDescriptor:
                                  (MTLRenderPipelineDescriptor *)descriptor
                                                             error:(NSError **)error {
    if (!descriptor)
        return MaliMetalUnsupported(error, @"Render pipeline descriptor is nil");
    MaliMetalFunction *vertex = (id)[descriptor vertexFunction],
                      *fragment = (id)[descriptor fragmentFunction];
    if (![(id)vertex isKindOfClass:[MaliMetalFunction class]] || [(id)vertex device] != self ||
        [vertex functionType] != MTLFunctionTypeVertex ||
        ![(id)fragment isKindOfClass:[MaliMetalFunction class]] || [(id)fragment device] != self ||
        [fragment functionType] != MTLFunctionTypeFragment)
        return MaliMetalUnsupported(
            error, @"Render pipeline requires vertex and fragment functions from this device");
    if (![descriptor isRasterizationEnabled] || [descriptor isAlphaToCoverageEnabled] ||
        [descriptor isAlphaToOneEnabled] || [descriptor maxVertexAmplificationCount] != 1 ||
        [descriptor supportIndirectCommandBuffers] ||
        ([descriptor inputPrimitiveTopology] != 0 && [descriptor inputPrimitiveTopology] != 3))
        return MaliMetalUnsupported(
            error,
            @"Rasterization state, amplification or indirect command support is unsupported");
    if ([descriptor rasterSampleCount] != 1 || [descriptor depthAttachmentPixelFormat] ||
        [descriptor stencilAttachmentPixelFormat])
        return MaliMetalUnsupported(error,
                                    @"Multisampling, depth and stencil pipelines are unsupported");
    MMShaderCompilerRequest request = {0};
    MMVKRenderPipelineDescriptor native = {0};
    MMShaderVertexAttribute attributes[31];
    MMShaderVertexLayout layouts[31];
    MMVKVertexAttribute vkAttributes[31];
    MMVKVertexBinding vkBindings[31];
    size_t attributeCount = 0, layoutCount = 0;
    BOOL used[31] = {0};
    MTLVertexDescriptor *vertexDescriptor = [descriptor vertexDescriptor];
    for (NSUInteger i = 0; i < 31; i++) {
        MTLVertexAttributeDescriptor *a =
            [[vertexDescriptor attributes] objectAtIndexedSubscript:i];
        NSUInteger format = [a format];
        if (!format)
            continue;
        NSUInteger binding = [a bufferIndex], offset = [a offset];
        uint32_t vkFormat;
        if (binding >= 31 || offset > UINT32_MAX || !MaliMetalVertexFormat(format, &vkFormat))
            return MaliMetalUnsupported(
                error, @"Vertex attribute format, offset or binding is unsupported");
        attributes[attributeCount] = (MMShaderVertexAttribute){(uint32_t)i, (uint32_t)format,
                                                               (uint32_t)offset, (uint32_t)binding};
        vkAttributes[attributeCount++] =
            (MMVKVertexAttribute){(uint32_t)i, (uint32_t)binding, vkFormat, (uint32_t)offset};
        used[binding] = YES;
    }
    for (NSUInteger i = 0; i < 31; i++)
        if (used[i]) {
            MTLVertexBufferLayoutDescriptor *l =
                [[vertexDescriptor layouts] objectAtIndexedSubscript:i];
            NSUInteger stride = [l stride], step = [l stepFunction], rate = [l stepRate];
            if (!stride || stride > UINT32_MAX || (step != 1 && step != 2) || rate != 1)
                return MaliMetalUnsupported(error, @"Vertex layout requires a finite per-vertex or "
                                                   @"per-instance stride with step rate one");
            layouts[layoutCount] = (MMShaderVertexLayout){(uint32_t)i, (uint32_t)stride,
                                                          (uint32_t)step, (uint32_t)rate};
            vkBindings[layoutCount++] =
                (MMVKVertexBinding){(uint32_t)i, (uint32_t)stride, step == 2};
        }
    for (NSUInteger i = 0; i < 8; i++) {
        MTLRenderPipelineColorAttachmentDescriptor *a =
            [[descriptor colorAttachments] objectAtIndexedSubscript:i];
        request.color_formats[i] = [a pixelFormat];
        if (i && [a pixelFormat])
            return MaliMetalUnsupported(error, @"Multiple render targets are unsupported");
    }
    MTLRenderPipelineColorAttachmentDescriptor *color =
        [[descriptor colorAttachments] objectAtIndexedSubscript:0];
    native.color_format = MaliMetalPixelFormat([color pixelFormat]);
    if (!native.color_format)
        return MaliMetalUnsupported(error, @"Render color format is unsupported");
    native.blend.enabled = [color isBlendingEnabled];
    if (native.blend.enabled)
        return MaliMetalUnsupported(
            error, @"AIR blend specialization has not been connected to the shader compiler");
    if (!MaliMetalBlendFactor([color sourceRGBBlendFactor], &native.blend.src_rgb) ||
        !MaliMetalBlendFactor([color destinationRGBBlendFactor], &native.blend.dst_rgb) ||
        !MaliMetalBlendFactor([color sourceAlphaBlendFactor], &native.blend.src_alpha) ||
        !MaliMetalBlendFactor([color destinationAlphaBlendFactor], &native.blend.dst_alpha) ||
        !MaliMetalBlendOperation([color rgbBlendOperation], &native.blend.rgb_op) ||
        !MaliMetalBlendOperation([color alphaBlendOperation], &native.blend.alpha_op))
        return MaliMetalUnsupported(error, @"Blend state is unsupported");
    NSUInteger mask = [color writeMask];
    if (mask > 15)
        return MaliMetalUnsupported(error, @"Invalid color write mask");
    native.blend.write_mask =
        ((mask & 8) >> 3) | ((mask & 4) >> 1) | ((mask & 2) << 1) | ((mask & 1) << 3);
    if (!_backend) {
        if (error)
            *error = MaliMetalMakeError(MMVK_ERROR_DRIVER_UNAVAILABLE, _backendError.message);
        return nil;
    }
    const MMShaderCompiler *compiler = MMShaderGetNativeCompiler();
    if (!compiler)
        return MaliMetalUnsupported(error, @"Native AIR compiler dependency is unavailable");
    request.device_profile_json =
        mmvk_device_shader_profile_json(_backend, &request.device_profile_size);
    if (!request.device_profile_json)
        return MaliMetalUnsupported(
            error, @"Native backend has no verified enabled Vulkan shader profile");
    request.attributes = attributes;
    request.attribute_count = attributeCount;
    request.layouts = layouts;
    request.layout_count = layoutCount;
    request.sample_count = 1;
    request.timeout_ms = 30000;
    MMShaderConstantValue *vertexConstants = NULL, *fragmentConstants = NULL;
    MMShaderError failure = {0};
    MMShaderStatus result = [vertex copyConstants:&vertexConstants
                                            count:&request.vertex_constant_count
                                            error:&failure];
    if (result == MM_SHADER_OK)
        result = [fragment copyConstants:&fragmentConstants
                                   count:&request.fragment_constant_count
                                   error:&failure];
    request.vertex_constants = vertexConstants;
    request.fragment_constants = fragmentConstants;
    MMShaderRenderOutput output = {0};
    if (result == MM_SHADER_OK)
        result = MMShaderCompileRender(compiler, [vertex maliLibrary], [vertex maliIndex],
                                       [fragment maliLibrary], [fragment maliIndex], &request,
                                       &output, &failure);
    free(vertexConstants);
    free(fragmentConstants);
    if (result != MM_SHADER_OK) {
        if (error)
            *error = MaliMetalMakeError(100 + result, failure.message);
        MMShaderRenderOutputDestroy(&output);
        return nil;
    }
    native.vertex_bindings = vkBindings;
    native.vertex_binding_count = (uint32_t)layoutCount;
    native.vertex_attributes = vkAttributes;
    native.vertex_attribute_count = (uint32_t)attributeCount;
    native.topology = 3;
    MMVKPipeline *pipeline = NULL;
    MMVKError vkError = {0};
    MMVKResult vkResult =
        mmvk_render_pipeline_create_translated(_backend, &output, &native, &pipeline, &vkError);
    if (vkResult != MMVK_OK) {
        if (error)
            *error = MaliMetalMakeError(vkResult, vkError.message);
        MMShaderRenderOutputDestroy(&output);
        return nil;
    }
    MMShaderVulkanConstants constants = {0};
    vkResult = MMShaderVulkanCreateConstants(_backend, &output, &constants, &vkError);
    if (vkResult != MMVK_OK) {
        if (error)
            *error = MaliMetalMakeError(vkResult, vkError.message);
        MMShaderVulkanDestroyConstants(&constants);
        mmvk_pipeline_destroy(pipeline);
        MMShaderRenderOutputDestroy(&output);
        return nil;
    }
    /* VkPipeline owns the compiled shader; retain only execution metadata and
     * immutable constant data needed by subsequent draws. */
    MMShaderRenderOutputReleaseCode(&output);
    id state = [[MaliMetalRenderPipeline alloc] initWithDevice:self
                                                      pipeline:pipeline
                                                   colorFormat:[color pixelFormat]
                                                        output:&output
                                                     constants:&constants];
    if (!state) {
        MMShaderVulkanDestroyConstants(&constants);
        mmvk_pipeline_destroy(pipeline);
        MMShaderRenderOutputDestroy(&output);
    }
    return state;
}

- (id<MTLComputePipelineState>)newComputePipelineStateWithFunction:(id<MTLFunction>)computeFunction
                                                             error:(NSError **)error {
    MaliMetalFunction *function = (id)computeFunction;
    if (![(id)function isKindOfClass:[MaliMetalFunction class]] || [function device] != self ||
        [function functionType] != MTLFunctionTypeKernel)
        return MaliMetalUnsupported(error,
                                    @"Compute pipeline requires an AIR kernel from this device");
    const MMVKCapabilities *caps = [self backendCapabilities];
    const MMShaderCompiler *compiler = MMShaderGetNativeCompiler();
    if (!caps || !(caps->capabilities & MMVK_CAP_COMPUTE) || !compiler ||
        !compiler->compile_compute)
        return MaliMetalUnsupported(
            error, @"Native AIR compute compiler or Vulkan compute capability is unavailable");
    MMShaderComputeRequest request = {0};
    request.device_profile_json =
        mmvk_device_shader_profile_json(_backend, &request.device_profile_size);
    if (!request.device_profile_json || !request.device_profile_size)
        return MaliMetalUnsupported(error,
                                    @"No verified enabled native compute profile is available");
    request.max_total_threads_per_threadgroup = caps->max_compute_workgroup_invocations;
    request.timeout_ms = 30000;
    MMShaderConstantValue *constants = NULL;
    MMShaderError shaderError = {0};
    MMShaderStatus status = [function copyConstants:&constants
                                              count:&request.constant_count
                                              error:&shaderError];
    request.constants = constants;
    MMShaderComputeOutput output = {0};
    if (status == MM_SHADER_OK)
        status = MMShaderCompileCompute(compiler, [function maliLibrary], [function maliIndex],
                                        &request, &output, &shaderError);
    free(constants);
    if (status != MM_SHADER_OK) {
        if (error)
            *error = MaliMetalMakeError(100 + status, shaderError.message);
        MMShaderComputeOutputDestroy(&output);
        return nil;
    }
    MMShaderVulkanComputeDescription description = {0};
    MMShaderVulkanConstants constantSet = {0};
    MMVKComputeProgram *program = NULL;
    MMVKError nativeError = {0};
    MMVKResult result = MMVK_OK;
    if (output.abi_version != MM_SHADER_COMPUTE_ABI_VERSION || !output.prepare_dispatch ||
        !output.max_total_threads_per_threadgroup || !output.thread_execution_width ||
        output.max_total_threads_per_threadgroup > caps->max_compute_workgroup_invocations ||
        output.static_threadgroup_memory_length > caps->max_compute_shared_memory_size) {
        MaliMetalComputeSetError(&nativeError, MMVK_ERROR_SHADER,
                                 "Native compiler returned invalid compute execution limits");
        result = MMVK_ERROR_SHADER;
    }
    if (result == MMVK_OK)
        result = MMShaderVulkanDescribeCompute(&output, &description, &nativeError);
    if (result == MMVK_OK)
        result =
            MMShaderVulkanCreateComputeConstants(_backend, &output, &constantSet, &nativeError);
    if (result == MMVK_OK)
        result = mmvk_compute_program_create(_backend, &description.shader, &description.layout,
                                             &program, &nativeError);
    id state = nil;
    if (result == MMVK_OK)
        state = [[MaliMetalComputePipeline alloc] initWithDevice:self
                                                         program:program
                                                          output:&output
                                                       constants:constantSet.buffers];
    if (!state) {
        if (error)
            *error = MaliMetalMakeError(result == MMVK_OK ? MMVK_ERROR_MEMORY : result,
                                        result == MMVK_OK ? "Cannot allocate compute pipeline state"
                                                          : nativeError.message);
        mmvk_compute_program_destroy(program);
        MMShaderVulkanDestroyConstants(&constantSet);
        MMShaderComputeOutputDestroy(&output);
    }
    return state;
}

@end
