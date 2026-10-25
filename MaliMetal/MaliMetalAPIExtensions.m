/* Metal library, reflection and asynchronous API extensions. */
#import "MaliMetalAPIExtensions.h"
#import "MaliMetalResources.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdio.h>

#ifndef NSUIntegerMax
#define NSUIntegerMax ((NSUInteger)-1)
#endif
static id MMGet(id dictionary, NSString *key);
static uint64_t MMUInt(id dictionary, NSString *key);
static id MMFunctionInfo(id function,uint32_t mode,NSUInteger index,NSError **error);
static id MMFail(NSError **error,const char *reason);
static void MMInvalid(const char *reason);

@interface NSObject (MMMetalAPISelectors)
- (id<MTLDevice>)device;
- (MMShaderLibrary *)maliLibrary;
- (uint32_t)maliIndex;
- (MMShaderStatus)copyConstants:(MMShaderConstantValue **)values count:(size_t *)count
                         error:(MMShaderError *)error;
- (BOOL)maliIsSpecialized;
- (id)computeFunction;
- (id)linkedFunctions;
- (NSArray *)functions;
- (NSArray *)binaryFunctions;
- (NSArray *)privateFunctions;
- (NSDictionary *)groups;
- (id)stageInputDescriptor;
- (id)attributes;
- (id)layouts;
- (NSUInteger)indexType;
- (NSUInteger)indexBufferIndex;
- (NSArray *)binaryArchives;
- (NSArray *)insertLibraries;
- (NSArray *)preloadedLibraries;
- (BOOL)supportAddingBinaryFunctions;
- (BOOL)supportIndirectCommandBuffers;
- (BOOL)threadGroupSizeIsMultipleOfThreadExecutionWidth;
- (id)driverCompilerOptions;
- (NSUInteger)textureWriteRoundingMode;
- (NSUInteger)maxTotalThreadsPerThreadgroup;
- (NSUInteger)maxCallStackDepth;
- (id)buffers;
- (NSUInteger)mutability;
- (NSString *)label;
- (NSUInteger)dataType;
- (NSUInteger)index;
- (NSUInteger)arrayLength;
- (NSUInteger)constantBlockAlignment;
- (id)newFunctionWithName:(NSString *)name constantValues:(id)constants error:(NSError **)error;
- (id)newFunctionWithDescriptor:(MTLFunctionDescriptor *)descriptor error:(NSError **)error;
@end

typedef struct MMArgumentSlot {
    uint32_t index, count, kind;
    uint64_t offset, stride, size;
    NSDictionary *nested;
} MMArgumentSlot;

@interface MaliMetalArgumentEncoder : MaliMetalObject {
    id<MTLDevice> _device;
    id<MTLBuffer> _target;
    NSUInteger _targetOffset, _encodedLength, _alignment;
    MMArgumentSlot *_slots;
    size_t _slotCount;
    uint32_t _requiredResources;
}
- (instancetype)initWithDevice:(id<MTLDevice>)device descriptors:(NSArray *)descriptors;
- (instancetype)initWithDevice:(id<MTLDevice>)device layout:(NSDictionary *)layout;
- (void)writeResources:(const id *)resources offsets:(const NSUInteger *)offsets
                 range:(NSRange)range kind:(uint32_t)kind;
@end

/* Sizes/alignment for scalar, vector and matrix constants in the target's Metal
 * ABI. A three-component vector occupies four lanes in an argument buffer;
 * function-constant serialization has a different, packed ABI. */
static BOOL MMConstantTypeLayout(NSUInteger type,NSUInteger *size,NSUInteger *alignment) {
    static const unsigned char sizes[]={
        4,8,16,16,16,32,32,24,48,48,32,64,64,
        2,4,8,8,8,16,16,12,24,24,16,32,32,
        4,8,16,16,4,8,16,16,2,4,8,8,2,4,8,8,
        1,2,4,4,1,2,4,4,1,2,4,4};
    static const unsigned char alignments[]={
        4,8,16,16,8,16,16,8,16,16,8,16,16,
        2,4,8,8,4,8,8,4,8,8,4,8,8,
        4,8,16,16,4,8,16,16,2,4,8,8,2,4,8,8,
        1,2,4,4,1,2,4,4,1,2,4,4};
    if (type>=3 && type<=56) {*size=sizes[type-3];*alignment=alignments[type-3];return YES;}
    if (type>=81 && type<=88) {
        NSUInteger lane=(type-81)%4;*size=lane==0?8:lane==1?16:32;*alignment=*size;return YES;
    }
    return NO;
}
static BOOL MMAppendArgumentSlot(MMArgumentSlot **slots,size_t *count,
                                  uint64_t index,uint64_t n,uint64_t offset,uint64_t stride,
                                  uint64_t size,uint64_t limit,uint32_t kind,id nested) {
    if (!n || n>UINT32_MAX || index>UINT32_MAX || n-1>UINT32_MAX-index ||
        *count>=65536 || !size || offset>limit || size>limit-offset ||
        (n>1 && stride>(limit-offset-size)/(n-1))) return NO;
    void *next=realloc(*slots,(*count+1)*sizeof(**slots));if (!next)return NO;
    *slots=next;(*slots)[(*count)++]=(MMArgumentSlot){(uint32_t)index,(uint32_t)n,
        kind,offset,stride,size,[nested retain]};return YES;
}
static BOOL MMCollectArgumentSlots(id layout,uint64_t baseID,uint64_t baseOffset,
                                    uint64_t limit,NSUInteger depth,
                                    MMArgumentSlot **slots,size_t *slotCount) {
    if (depth>32)return NO;
    NSArray *members=MMGet(layout,@"members");
    for (NSUInteger i=0;i<[members count];i++) {
        id member=[members objectAtIndex:i];uint64_t kind=MMUInt(member,@"kind");
        uint64_t index=MMUInt(member,@"id"),offset=MMUInt(member,@"offset");
        uint64_t count=MMUInt(member,@"count"),stride=MMUInt(member,@"stride");
        uint64_t size=MMUInt(member,@"size"),ids=MMUInt(member,@"idsPerElement");
        if (index>UINT32_MAX || baseID>UINT32_MAX-index || offset>limit || baseOffset>limit-offset ||
            !count || count>65536 || !ids || !size) return NO;
        index+=baseID;offset+=baseOffset;
        if (offset>limit || size>limit-offset || (count>1 && stride>(limit-offset-size)/(count-1))) return NO;
        if (kind<=3) {
            if (kind && size!=8)return NO;
            if (ids!=1 || !MMAppendArgumentSlot(slots,slotCount,index,count,offset,stride,size,limit,(uint32_t)kind,MMGet(member,@"layout")))return NO;
        } else if (kind==4) {
            id nested=MMGet(member,@"layout");if (!nested)return NO;
            for (uint64_t element=0;element<count;element++) {
                if (element>(UINT32_MAX-index)/ids ||
                    !MMCollectArgumentSlots(nested,index+element*ids,offset+element*stride,
                                             limit,depth+1,slots,slotCount))return NO;
            }
        } else return NO;
    }
    return YES;
}

static uint32_t MMArgumentConsumers(id device) {
    if (![device respondsToSelector:@selector(maliArgumentBufferConsumerCapabilities)])return 0;
    return [(MaliMetalDevice *)device maliArgumentBufferConsumerCapabilities]&7u;
}
static BOOL MMRequiredArgumentResources(id layout,NSUInteger depth,uint32_t *required) {
    if (depth>32)return NO;
    NSArray *members=MMGet(layout,@"members");
    for (NSUInteger i=0;i<[members count];i++) {
        id member=[members objectAtIndex:i];uint64_t kind=MMUInt(member,@"kind");
        if (kind>4)return NO;
        if (kind>=1 && kind<=3)*required|=1u<<(kind-1);
        id nested=MMGet(member,@"layout");
        if (nested && !MMRequiredArgumentResources(nested,depth+1,required))return NO;
    }
    return YES;
}
static BOOL MMArgumentError(MMVKError *error,const char *reason) {
    if (error) {error->code=MMVK_ERROR_ARGUMENT;strncpy(error->message,reason,sizeof(error->message)-1);}
    return NO;
}
typedef struct MMArgumentWrite {
    uint64_t offset,word;
    void *resource;
    MMVKResourceKind kind;
} MMArgumentWrite;
static BOOL MMArgumentResourceWord(MaliMetalDevice *device,id object,uint32_t kind,
                                    NSUInteger offset,MMArgumentWrite *write,MMVKError *error) {
    if (!object) {
        if (kind==1 && offset)return MMArgumentError(error,"A nil buffer requires offset zero");
        write->word=0;return YES;
    }
    MMVKDevice *backend=[device backendDevice];
    if (!backend)return MMArgumentError(error,"Native argument resource registry is unavailable");
    uint64_t resourceID=0;void *expected=NULL;
    if (kind==1) {
        if (![object isKindOfClass:[MaliMetalBuffer class]])return MMArgumentError(error,"Buffer argument requires a native Metal buffer");
        expected=[object maliBuffer];resourceID=[object maliResourceID];write->kind=MMVK_RESOURCE_BUFFER;
    } else if (kind==2) {
        if (![object isKindOfClass:[MaliMetalTexture class]])return MMArgumentError(error,"Texture argument requires a native Metal texture");
        expected=[object maliImage];resourceID=[object maliResourceID];write->kind=MMVK_RESOURCE_TEXTURE;
    } else if (kind==3) {
        if (![object isKindOfClass:[MaliMetalSampler class]] || ![object supportArgumentBuffers])
            return MMArgumentError(error,"Sampler argument requires supportArgumentBuffers on a native sampler");
        expected=[object maliSampler];resourceID=[object maliResourceID];write->kind=MMVK_RESOURCE_SAMPLER;
    } else return MMArgumentError(error,"Unsupported argument resource kind");
    if ([object device]!=(id)device)return MMArgumentError(error,"Argument resource belongs to another device");
    if (!resourceID || !expected)return MMArgumentError(error,"Argument resource has no live typed identity");
    if (mmvk_resource_acquire(backend,resourceID,write->kind,&write->resource,error)!=MMVK_OK)return NO;
    if (write->resource!=expected)return MMArgumentError(error,"Argument resource identity does not match its native allocation");
    if (kind==1) {
        MMVKBuffer *buffer=write->resource;uint64_t length=mmvk_buffer_logical_size(buffer);
        uint64_t address=mmvk_buffer_device_address(buffer);
        if (mmvk_buffer_is_aliasable(buffer) || !address || offset>=length ||
            offset>UINT64_MAX-address || length!=[object length])
            return MMArgumentError(error,"Buffer argument requires a live device address and an offset within its logical allocation");
        MMVKBuffer *resolved=NULL;uint64_t resolvedOffset=0;
        MMVKResult result=mmvk_resource_acquire_address(backend,address+offset,length-offset,
                                                       &resolved,&resolvedOffset,error);
        BOOL valid=result==MMVK_OK && resolved==buffer && resolvedOffset==offset;
        if (resolved)mmvk_resource_release(resolved,MMVK_RESOURCE_BUFFER);
        if (!valid) {
            if (result==MMVK_OK)MMArgumentError(error,"Buffer device address resolves to a different allocation");
            return NO;
        }
        write->word=address+offset;
    } else {
        if (kind==2 && mmvk_image_is_aliasable(write->resource))
            return MMArgumentError(error,"Aliasable texture cannot be encoded as an argument resource");
        write->word=resourceID;
    }
    return YES;
}
static void MMReleaseArgumentWrites(MMArgumentWrite *writes,NSUInteger count) {
    for (NSUInteger i=0;i<count;i++) if (writes[i].resource)
        mmvk_resource_release(writes[i].resource,writes[i].kind);
    free(writes);
}

@implementation MaliMetalArgumentEncoder
- (instancetype)initWithDevice:(id<MTLDevice>)device descriptors:(NSArray *)descriptors {
    self=[super init];if (!self)return nil;
    _device=(id<MTLDevice>)[(id)device retain];_alignment=1;
    NSUInteger count=[descriptors count],end=0;BOOL inConstantBlock=NO;
    if (!count || count>65536) {[self release];return nil;}
    for (NSUInteger i=0;i<count;i++) {
        id d=[descriptors objectAtIndex:i];NSUInteger size=0,alignment=0;
        NSUInteger block=[d constantBlockAlignment],n=[d arrayLength];if (!n)n=1;
        NSUInteger type=[d dataType];uint32_t kind=type==60?1:type==58?2:type==59?3:0;
        BOOL valid=kind?YES:MMConstantTypeLayout(type,&size,&alignment);
        if (kind) {size=alignment=8;_requiredResources|=1u<<(kind-1);}
        if (!valid ||
            [d index]>UINT32_MAX || n>UINT32_MAX || n-1>UINT32_MAX-[d index] ||
            (block && (kind || inConstantBlock || (block&(block-1))))) {[self release];return nil;}
        inConstantBlock=kind==0;
        if (alignment>_alignment)_alignment=alignment;
        if (block>alignment)alignment=block;
        if (end>NSUIntegerMax-(alignment-1)) {[self release];return nil;}
        NSUInteger offset=(end+alignment-1)&~(alignment-1);
        if (size && n>(NSUIntegerMax-offset)/size) {[self release];return nil;}
        end=offset+n*size;
        if (!MMAppendArgumentSlot(&_slots,&_slotCount,[d index],n,offset,size,size,end,kind,nil)) {
            [self release];return nil;
        }
    }
    if (end>NSUIntegerMax-(_alignment-1)) {[self release];return nil;}
    _encodedLength=(end+_alignment-1)&~(_alignment-1);
    if (_encodedLength>[(MaliMetalDevice *)device maxBufferLength] ||
        (_requiredResources&~MMArgumentConsumers((id)device))) {[self release];return nil;}
    return self;
}
- (instancetype)initWithDevice:(id<MTLDevice>)device layout:(NSDictionary *)layout {
    self=[super init];if (!self)return nil;
    _device=(id<MTLDevice>)[(id)device retain];
    _encodedLength=MMUInt(layout,@"encodedLength");_alignment=MMUInt(layout,@"alignment");
    if (!_alignment || (_alignment&(_alignment-1)) ||
        _encodedLength>[(MaliMetalDevice *)device maxBufferLength] ||
        !MMCollectArgumentSlots(layout,0,0,_encodedLength,0,&_slots,&_slotCount) ||
        !MMRequiredArgumentResources(layout,0,&_requiredResources) ||
        (_requiredResources&~MMArgumentConsumers((id)device))) {
        [self release];return nil;
    }
    return self;
}
- (void)dealloc {
    [(id)_target release];[(id)_device release];
    for (size_t i=0;i<_slotCount;i++)[_slots[i].nested release];
    free(_slots);[super dealloc];
}
- (id<MTLDevice>)device {return _device;}
- (NSUInteger)encodedLength {return _encodedLength;}
- (NSUInteger)alignment {return _alignment;}
- (void)setArgumentBuffer:(id<MTLBuffer>)buffer offset:(NSUInteger)offset {
    if (buffer && (![(id)buffer isKindOfClass:[MaliMetalBuffer class]] ||
        [buffer device]!=_device || ![buffer contents] ||
        mmvk_buffer_is_aliasable([(MaliMetalBuffer *)buffer maliBuffer]) ||
        ([buffer storageMode]!=MTLStorageModeShared && [buffer storageMode]!=MTLStorageModeManaged) ||
        offset>[buffer length] || _encodedLength>[buffer length]-offset)) {
        MMInvalid("Argument buffer must be CPU-accessible, belong to this device, and contain the encoded range");return;
    }
    @synchronized(self) {[(id)buffer retain];[(id)_target release];_target=buffer;_targetOffset=offset;}
}
- (void)setArgumentBuffer:(id<MTLBuffer>)buffer startOffset:(NSUInteger)offset arrayElement:(NSUInteger)element {
    if (_encodedLength && element>(NSUIntegerMax-offset)/_encodedLength) {
        MMInvalid("Argument-buffer array offset overflows");return;
    }
    [self setArgumentBuffer:buffer offset:offset+element*_encodedLength];
}
- (void *)constantDataAtIndex:(NSUInteger)index {
    @synchronized(self) {
        if (!_target) {MMInvalid("No argument buffer is set");return NULL;}
        for (size_t i=_slotCount;i>0;i--) {
            MMArgumentSlot *slot=_slots+i-1;
            if (index>=slot->index && index-slot->index<slot->count) {
                if (slot->kind) {MMInvalid("Argument index does not name constant data");return NULL;}
                return (unsigned char *)[_target contents]+_targetOffset+slot->offset+
                    (index-slot->index)*slot->stride;
            }
        }
    }
    MMInvalid("Argument index is absent from this constant layout");return NULL;
}
- (id)newArgumentEncoderForBufferAtIndex:(NSUInteger)index {
    for (size_t i=_slotCount;i>0;i--) {
        MMArgumentSlot *slot=_slots+i-1;
        if (index>=slot->index && index-slot->index<slot->count)
            return slot->kind==1 && slot->nested ?
                [[MaliMetalArgumentEncoder alloc] initWithDevice:_device layout:slot->nested]:nil;
    }
    return nil;
}
- (void)writeResources:(const id *)resources offsets:(const NSUInteger *)offsets
                 range:(NSRange)range kind:(uint32_t)kind {
    if (!range.length)return;
    if (!resources || (kind==1 && !offsets) || range.length>65536 ||
        range.location>UINT32_MAX || range.length-1>UINT32_MAX-range.location) {
        MMInvalid("Invalid argument resource range");return;
    }
    MMArgumentWrite *writes=calloc(range.length,sizeof(*writes));
    if (!writes) {MMInvalid("Cannot allocate argument resource writes");return;}
    MMVKError error={0};BOOL valid=YES;
    @synchronized(self) {
        if (!_target || ![_target contents] ||
            mmvk_buffer_is_aliasable([(MaliMetalBuffer *)_target maliBuffer]))
            valid=MMArgumentError(&error,"No live CPU-accessible argument buffer is set");
        if (valid && (_requiredResources&~MMArgumentConsumers((id)_device)))
            valid=MMArgumentError(&error,"Canonical argument resource consumers are unavailable");
        for (NSUInteger item=0;valid && item<range.length;item++) {
            NSUInteger index=range.location+item;MMArgumentSlot *slot=NULL;
            for (size_t i=_slotCount;i>0;i--) if (index>=_slots[i-1].index &&
                index-_slots[i-1].index<_slots[i-1].count) {slot=_slots+i-1;break;}
            if (!slot || slot->kind!=kind || slot->size!=8) {
                valid=MMArgumentError(&error,"Argument index is absent or has another resource type");break;
            }
            writes[item].offset=slot->offset+(index-slot->index)*slot->stride;
            valid=MMArgumentResourceWord((MaliMetalDevice *)_device,resources[item],kind,
                                         offsets?offsets[item]:0,writes+item,&error);
        }
        if (valid) {
            unsigned char *target=(unsigned char *)[_target contents]+_targetOffset;
            /* Target is arm64 little endian. Byte stores support an unaligned
             * Metal argument-buffer start without host alignment assumptions. */
            for (NSUInteger item=0;item<range.length;item++)
                for (unsigned byte=0;byte<8;byte++)
                    target[writes[item].offset+byte]=(unsigned char)(writes[item].word>>(8*byte));
        }
    }
    MMReleaseArgumentWrites(writes,range.length);
    if (!valid)MMInvalid(error.message);
}
- (void)setBuffer:(id)buffer offset:(NSUInteger)offset atIndex:(NSUInteger)index {
    [self writeResources:&buffer offsets:&offset range:(NSRange){index,1} kind:1];
}
- (void)setBuffers:(const id *)buffers offsets:(const NSUInteger *)offsets withRange:(NSRange)range {
    [self writeResources:buffers offsets:offsets range:range kind:1];
}
- (void)setTexture:(id)texture atIndex:(NSUInteger)index {
    [self writeResources:&texture offsets:NULL range:(NSRange){index,1} kind:2];
}
- (void)setTextures:(const id *)textures withRange:(NSRange)range {
    [self writeResources:textures offsets:NULL range:range kind:2];
}
- (void)setSamplerState:(id)sampler atIndex:(NSUInteger)index {
    [self writeResources:&sampler offsets:NULL range:(NSRange){index,1} kind:3];
}
- (void)setSamplerStates:(const id *)samplers withRange:(NSRange)range {
    [self writeResources:samplers offsets:NULL range:range kind:3];
}
@end

id MaliMetalNewFunctionArgumentEncoder(id function,NSUInteger index,NSError **error) {
    id layout=MMFunctionInfo(function,4,index,error);if (!layout)return nil;
    id result=[[MaliMetalArgumentEncoder alloc] initWithDevice:[function device] layout:layout];
    if (!result) MMFail(error,"Argument buffer requires resource address/handle resolution or an unsupported layout");
    return result;
}
@interface NSDictionary (MMMetalRead)
- (id)objectForKey:(id)key;
- (NSArray *)allKeys;
@end
@interface NSString (MMMetalCompare)
- (BOOL)isEqualToString:(NSString *)other;
@end
@interface NSNumber : NSObject
- (unsigned long long)unsignedLongLongValue;
- (long long)longLongValue;
- (BOOL)boolValue;
@end
@interface NSData (MMMetalBytes)
+ (instancetype)dataWithBytes:(const void *)bytes length:(NSUInteger)length;
@end
@interface NSNull : NSObject
+ (instancetype)null;
@end
@interface NSJSONSerialization : NSObject
+ (id)JSONObjectWithData:(NSData *)data options:(NSUInteger)options error:(NSError **)error;
@end

static NSError *MMAPIError(NSInteger code, const char *reason) {
    return [NSError errorWithDomain:@"MaliMetal" code:code
        userInfo:[NSDictionary dictionaryWithObject:[NSString stringWithUTF8String:reason]
                                             forKey:@"NSLocalizedDescription"]];
}
static id MMFail(NSError **error, const char *reason) {
    if (error) *error = MMAPIError(107, reason);
    return nil;
}
static void MMInvalid(const char *reason) {
    [NSException raise:@"NSInvalidArgumentException" format:@"%s", reason];
}
static id MMGet(id dictionary, NSString *key) {
    id value = [dictionary objectForKey:key];
    return value == [NSNull null] ? nil : value;
}
static uint64_t MMUInt(id dictionary, NSString *key) {
    return [MMGet(dictionary,key) unsignedLongLongValue];
}

/* A stable, owned snapshot of one typed reflection node. Nested nodes and arrays
 * are built from immutable JSON dictionaries; no compiler-owned pointer escapes. */
@interface MMMetalReflectionObject : NSObject {
    NSDictionary *_fields;
}
- (instancetype)initWithFields:(NSDictionary *)fields;
- (id)fields;
- (NSArray *)array:(NSString *)key;
- (id)node:(NSString *)key;
@end
static id MMNode(id fields) {
    return fields ? [[[MMMetalReflectionObject alloc] initWithFields:fields] autorelease] : nil;
}
@implementation MMMetalReflectionObject
- (instancetype)initWithFields:(NSDictionary *)fields {
    self=[super init]; if (self) _fields=[fields retain]; return self;
}
- (void)dealloc {[_fields release]; [super dealloc];}
- (id)fields {return _fields;}
- (id)node:(NSString *)key {return MMNode(MMGet(_fields,key));}
- (NSArray *)array:(NSString *)key {
    NSArray *source=MMGet(_fields,key); if (!source) return nil;
    NSUInteger count=[source count]; id *nodes=calloc(count?count:1,sizeof(id));
    if (!nodes) {MMInvalid("Cannot allocate reflection nodes"); return nil;}
    for (NSUInteger i=0;i<count;i++) nodes[i]=MMNode([source objectAtIndex:i]);
    NSArray *answer=[NSArray arrayWithObjects:nodes count:count]; free(nodes); return answer;
}
#define MM_NUMBER(method,key) - (NSUInteger)method {return (NSUInteger)MMUInt(_fields,@key);}
#define MM_BOOL(method,key) - (BOOL)method {return [MMGet(_fields,@key) boolValue];}
#define MM_NODE(method,key) - (id)method {return [self node:@key];}
#define MM_ARRAY(method,key) - (NSArray *)method {return [self array:@key];}
- (NSString *)name {return MMGet(_fields,@"name");}
- (NSString *)userAnnotation {return MMGet(_fields,@"userAnnotation");}
MM_NUMBER(type,"type") MM_NUMBER(dataType,"dataType") MM_NUMBER(index,"index")
MM_NUMBER(access,"access") MM_NUMBER(arrayLength,"arrayLength")
MM_BOOL(isActive,"active") MM_BOOL(isUsed,"active") MM_BOOL(isArgument,"argument")
MM_BOOL(required,"required")
MM_NUMBER(bufferAlignment,"bufferAlignment") MM_NUMBER(bufferDataSize,"bufferDataSize")
MM_NUMBER(bufferDataType,"bufferDataType") MM_NODE(bufferStructType,"bufferStructType")
MM_NODE(bufferPointerType,"bufferPointerType")
MM_NUMBER(textureType,"textureType") MM_NUMBER(textureDataType,"textureDataType")
MM_BOOL(isDepthTexture,"depthTexture")
MM_NUMBER(threadgroupMemoryAlignment,"threadgroupMemoryAlignment")
MM_NUMBER(threadgroupMemoryDataSize,"threadgroupMemoryDataSize")
MM_NUMBER(objectPayloadAlignment,"objectPayloadAlignment")
MM_NUMBER(objectPayloadDataSize,"objectPayloadDataSize")
MM_NUMBER(offset,"offset") MM_NUMBER(argumentIndex,"argumentIndex")
MM_NODE(structType,"structType") MM_NODE(arrayType,"arrayType")
MM_NODE(pointerType,"pointerType") MM_NODE(textureReferenceType,"textureReferenceType")
MM_NUMBER(elementType,"elementType") MM_NUMBER(stride,"stride")
MM_NUMBER(argumentIndexStride,"argumentIndexStride") MM_NUMBER(alignment,"alignment")
MM_NUMBER(dataSize,"dataSize") MM_BOOL(elementIsArgumentBuffer,"elementIsArgumentBuffer")
MM_NODE(elementStructType,"elementStructType") MM_NODE(elementArrayType,"elementArrayType")
MM_NODE(elementPointerType,"elementPointerType")
MM_NODE(elementTextureReferenceType,"elementTextureReferenceType")
MM_ARRAY(members,"members") MM_ARRAY(arguments,"arguments")
- (NSArray *)bindings {return MMGet(_fields,@"bindings")?[self array:@"bindings"]:[self arguments];}
MM_ARRAY(vertexArguments,"vertexArguments") MM_ARRAY(fragmentArguments,"fragmentArguments")
MM_ARRAY(tileArguments,"tileArguments")
- (NSArray *)vertexBindings {return [self vertexArguments];}
- (NSArray *)fragmentBindings {return [self fragmentArguments];}
- (NSArray *)tileBindings {return [self tileArguments];}
- (id)memberByName:(NSString *)name {
    NSArray *members=[self members];
    for (NSUInteger i=0;i<[members count];i++) {
        id member=[members objectAtIndex:i];
        if ([[member name] isEqualToString:name]) return member;
    }
    return nil;
}
MM_NUMBER(attributeIndex,"attributeIndex") MM_NUMBER(attributeType,"attributeType")
MM_BOOL(isPatchData,"patchData") MM_BOOL(isPatchControlPointData,"patchControlPointData")
#undef MM_NUMBER
#undef MM_BOOL
#undef MM_NODE
#undef MM_ARRAY
@end

#ifdef MM_SHADER_ENABLE_AIR_COMPILER
static id MMParseReflection(int32_t status,char *json,size_t size,MMShaderError *failure,
                            NSError **error) {
    const MMShaderCompiler *compiler=MMShaderGetNativeCompiler();
    id answer=nil;
    if (status!=MM_SHADER_OK) {
        if (error) *error=MMAPIError(100+status,failure->message);
    } else if (!json || size>64*1024*1024) {
        MMFail(error,"AIR provider returned invalid reflection bytes");
    } else {
        answer=[NSJSONSerialization JSONObjectWithData:[NSData dataWithBytes:json length:size]
                                               options:0 error:error];
        if (answer==[NSNull null]) answer=nil;
    }
    if (json && compiler && compiler->free_string) compiler->free_string(json);
    return answer;
}
#endif
static id MMFunctionInfo(id function,uint32_t mode,NSUInteger index,NSError **error) {
    if (error) *error=nil;
    if (![function respondsToSelector:@selector(maliLibrary)] ||
        ![function respondsToSelector:@selector(maliIndex)])
        return MMFail(error,"Reflection requires a function from this device");
#ifndef MM_SHADER_ENABLE_AIR_COMPILER
    (void)mode; (void)index;
    return MMFail(error,"Native AIR reflection provider is unavailable");
#else
    const MMShaderCompiler *compiler=MMShaderGetNativeCompiler();
    if (!compiler || !compiler->free_string)
        return MMFail(error,"Native AIR reflection provider is unavailable");
    MMShaderLibrary *library=[function maliLibrary];
    const MMShaderEntry *entry=MMShaderLibraryGetEntry(library,[function maliIndex]);
    if (!entry) return MMFail(error,"Function metadata no longer exists");
    MMShaderConstantValue *constants=NULL; size_t count=0,byteCount=0,size=0;
    MMShaderError failure={0}; char *json=NULL;
    MMShaderStatus status=[function copyConstants:&constants count:&count error:&failure];
    const void *bytes=MMShaderLibraryBytes(library,&byteCount);
    if (status==MM_SHADER_OK) {
        if (mode==4) {
            if (index>UINT32_MAX) {free(constants); return MMFail(error,"Argument index exceeds uint32");}
            status=MMMetalFunctionArgumentLayout(bytes,byteCount,entry->name,constants,count,
                                                 (uint32_t)index,&json,&size,&failure);
        } else status=MMMetalReflectFunction(bytes,byteCount,entry->name,constants,count,mode,
                                             &json,&size,&failure);
    }
    free(constants);
    return MMParseReflection(status,json,size,&failure,error);
#endif
}
static const char MMRawPropertiesKey, MMSpecializedPropertiesKey;
id MaliMetalFunctionProperties(id function,BOOL specialized,NSError **error) {
    if (error)*error=nil;
    if (!function)return MMFail(error,"Missing function for reflection");
    const void *key=specialized?&MMSpecializedPropertiesKey:&MMRawPropertiesKey;
    @synchronized(function) {
        id result=objc_getAssociatedObject(function,key);
        if (result)return result;
        result=MMNode(MMFunctionInfo(function,specialized?1:0,0,error));
        if (result)objc_setAssociatedObject(function,key,result,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        return result;
    }
}
NSDictionary *MaliMetalFunctionConstantsDictionary(id function,BOOL specialized,NSError **error) {
    id fields=[MaliMetalFunctionProperties(function,specialized,error) fields];
    id constants=MMGet(fields,@"constants"); NSArray *keys=[constants allKeys];
    NSUInteger count=[keys count]; id *values=calloc(count?count:1,sizeof(id));
    id *names=calloc(count?count:1,sizeof(id));
    if (!values || !names) {free(values);free(names);return MMFail(error,"Cannot allocate constant reflection");}
    for (NSUInteger i=0;i<count;i++) {names[i]=[keys objectAtIndex:i];values[i]=MMNode(MMGet(constants,names[i]));}
    NSDictionary *answer=fields?[NSDictionary dictionaryWithObjects:values forKeys:names count:count]:nil;
    free(values);free(names);return answer;
}
NSArray *MaliMetalFunctionAttributes(id properties,BOOL vertex) {
    return [properties array:vertex?@"vertexAttributes":@"stageInputAttributes"];
}
NSUInteger MaliMetalFunctionPatchType(id properties) {return MMUInt([properties fields],@"patchType");}
NSInteger MaliMetalFunctionPatchControlPointCount(id properties) {
    return properties?(NSInteger)[MMGet([properties fields],@"patchControlPointCount") longLongValue]:-1;
}
id MaliMetalLibraryFunctionReflection(id library,NSString *name,NSError **error) {
    if (error) *error=nil;
    if (![library respondsToSelector:@selector(maliLibrary)])
        return MMFail(error,"Reflection requires a library from this device");
#ifndef MM_SHADER_ENABLE_AIR_COMPILER
    (void)name; return MMFail(error,"Native AIR reflection provider is unavailable");
#else
    const MMShaderCompiler *compiler=MMShaderGetNativeCompiler();
    if (!compiler || !compiler->free_string) return MMFail(error,"Native AIR reflection provider is unavailable");
    size_t byteCount=0,size=0; char *json=NULL; MMShaderError failure={0};
    const void *bytes=MMShaderLibraryBytes([library maliLibrary],&byteCount);
    int32_t status=MMMetalReflectFunction(bytes,byteCount,[name UTF8String],NULL,0,2,&json,&size,&failure);
    return MMNode(MMParseReflection(status,json,size,&failure,error));
#endif
}

/* Async blocks execute on a private concurrent queue. Dispatch owns the copied
 * block and its captured descriptor snapshots until the callback has returned. */
static pthread_once_t MMAPIQueueOnce=PTHREAD_ONCE_INIT;
static dispatch_queue_t MMAPIQueue;
static void MMCreateAPIQueue(void) {
    /* DISPATCH_QUEUE_CONCURRENT is the address of this exported attribute. */
    extern const struct dispatch_queue_attr_s _dispatch_queue_attr_concurrent;
    MMAPIQueue=dispatch_queue_create("MaliMetal.compiler",(void *)&_dispatch_queue_attr_concurrent);
}
static void MMAsync(void (^work)(void)) {
    pthread_once(&MMAPIQueueOnce,MMCreateAPIQueue);
    if (!MMAPIQueue) {MMInvalid("Cannot create compiler completion queue");return;}
    dispatch_async(MMAPIQueue,^{@autoreleasepool {work();}});
}
static BOOL MMOptions(NSUInteger options,NSError **error) {
    /* ArgumentInfo=1, BufferTypeInfo=2. Every other option changes compilation. */
    if (options&~(NSUInteger)3) {MMFail(error,"Unsupported Metal pipeline compilation option");return NO;}
    return YES;
}

static id MMRenderReflection(MTLRenderPipelineDescriptor *descriptor,NSError **error) {
#ifndef MM_SHADER_ENABLE_AIR_COMPILER
    (void)descriptor; return MMFail(error,"Native AIR reflection provider is unavailable");
#else
    const MMShaderCompiler *compiler=MMShaderGetNativeCompiler();
    if (!compiler || !compiler->free_string) return MMFail(error,"Native AIR reflection provider is unavailable");
    id vertex=[descriptor vertexFunction],fragment=[descriptor fragmentFunction];
    if (![vertex respondsToSelector:@selector(maliLibrary)] ||
        (fragment && ![fragment respondsToSelector:@selector(maliLibrary)]))
        return MMFail(error,"Render reflection requires native AIR functions");
    MMShaderCompilerRequest request={0}; MMShaderError failure={0};
    const MMShaderEntry *entry=MMShaderLibraryGetEntry([vertex maliLibrary],[vertex maliIndex]);
    if (!entry) return MMFail(error,"Missing vertex function metadata");
    request.vertex_bytes=MMShaderLibraryBytes([vertex maliLibrary],&request.vertex_size);
    request.vertex_name=entry->name;
    if (fragment) {
        entry=MMShaderLibraryGetEntry([fragment maliLibrary],[fragment maliIndex]);
        if (!entry) return MMFail(error,"Missing fragment function metadata");
        request.fragment_bytes=MMShaderLibraryBytes([fragment maliLibrary],&request.fragment_size);
        request.fragment_name=entry->name;
    }
    MMShaderConstantValue *vc=NULL,*fc=NULL;
    MMShaderStatus status=[vertex copyConstants:&vc count:&request.vertex_constant_count error:&failure];
    if (status==MM_SHADER_OK && fragment)
        status=[fragment copyConstants:&fc count:&request.fragment_constant_count error:&failure];
    request.vertex_constants=vc; request.fragment_constants=fc;
    MMShaderVertexAttribute attributes[31];MMShaderVertexLayout layouts[31];
    MTLVertexDescriptor *vd=[descriptor vertexDescriptor];BOOL used[31]={0};
    for (NSUInteger i=0;vd && i<31;i++) {
        MTLVertexAttributeDescriptor *a=[[vd attributes] objectAtIndexedSubscript:i];
        if (![a format]) continue;
        if ([a format]>UINT32_MAX || [a offset]>UINT32_MAX || [a bufferIndex]>=31) {
            free(vc);free(fc);return MMFail(error,"Vertex reflection descriptor exceeds supported bounds");
        }
        attributes[request.attribute_count++]=(MMShaderVertexAttribute){(uint32_t)i,(uint32_t)[a format],
            (uint32_t)[a offset],(uint32_t)[a bufferIndex]}; used[[a bufferIndex]]=YES;
    }
    for (NSUInteger i=0;vd && i<31;i++) if (used[i]) {
        MTLVertexBufferLayoutDescriptor *l=[[vd layouts] objectAtIndexedSubscript:i];
        if ([l stride]>UINT32_MAX || [l stepFunction]>UINT32_MAX || [l stepRate]>UINT32_MAX) {
            free(vc);free(fc);return MMFail(error,"Vertex reflection layout exceeds supported bounds");
        }
        layouts[request.layout_count++]=(MMShaderVertexLayout){(uint32_t)i,(uint32_t)[l stride],
            (uint32_t)[l stepFunction],(uint32_t)[l stepRate]};
    }
    request.attributes=attributes;request.layouts=layouts;
    char *json=NULL;size_t size=0;
    if (status==MM_SHADER_OK) status=MMMetalReflectRender(&request,&json,&size,&failure);
    free(vc);free(fc);return MMNode(MMParseReflection(status,json,size,&failure,error));
#endif
}

static BOOL MMComputeControlUnsupported(NSError **error,const char *control) {
    char reason[256];
    snprintf(reason,sizeof(reason),"Compute descriptor control %s is unavailable on the function pipeline path",control);
    MMFail(error,reason);return NO;
}
static BOOL MMComputeStageInputEmpty(id stage,NSError **error) {
    if (!stage)return YES;
    if (![stage respondsToSelector:@selector(attributes)] || ![stage respondsToSelector:@selector(layouts)] ||
        ![stage respondsToSelector:@selector(indexType)] || ![stage respondsToSelector:@selector(indexBufferIndex)])
        return MMComputeControlUnsupported(error,"stageInputDescriptor public layout inspection");
    if ([stage indexType]!=MTLIndexTypeUInt16)
        return MMComputeControlUnsupported(error,"stageInput.indexType");
    if ([stage indexBufferIndex])return MMComputeControlUnsupported(error,"stageInput.indexBufferIndex");
    id attributes=[stage attributes],layouts=[stage layouts];
    for (NSUInteger i=0;i<31;i++) {
        MTLVertexAttributeDescriptor *a=[attributes objectAtIndexedSubscript:i];
        MTLVertexBufferLayoutDescriptor *l=[layouts objectAtIndexedSubscript:i];
        if ([a format] || [a offset] || [a bufferIndex]) {
            char control[128];
            snprintf(control,sizeof(control),"stageInput.attributes[%lu] format=%lu offset=%lu bufferIndex=%lu",
                (unsigned long)i,(unsigned long)[a format],(unsigned long)[a offset],(unsigned long)[a bufferIndex]);
            return MMComputeControlUnsupported(error,control);
        }
        /* Metal lazily creates default layouts with stride0, stepFunction1,
         * stepRate1. Only that empty state is equivalent to no stage input. */
        if (l && ([l stride] || [l stepFunction]!=1 || [l stepRate]!=1)) {
            char control[128];
            snprintf(control,sizeof(control),"stageInput.layouts[%lu] stride=%lu stepFunction=%lu stepRate=%lu",
                (unsigned long)i,(unsigned long)[l stride],(unsigned long)[l stepFunction],(unsigned long)[l stepRate]);
            return MMComputeControlUnsupported(error,control);
        }
    }
    return YES;
}
static BOOL MMComputeDescriptorSupported(id descriptor,NSError **error) {
    if (!descriptor || ![descriptor respondsToSelector:@selector(computeFunction)]) {
        MMFail(error,"Compute pipeline descriptor is missing or invalid"); return NO;
    }
    if (!MMComputeStageInputEmpty([descriptor stageInputDescriptor],error))return NO;
    id linked=[descriptor linkedFunctions];
    if ([descriptor respondsToSelector:@selector(driverCompilerOptions)] &&
        [descriptor driverCompilerOptions]) {
        MMFail(error,"Private compute driver compiler options are unsupported");return NO;
    }
    if ([descriptor respondsToSelector:@selector(textureWriteRoundingMode)] &&
        [descriptor textureWriteRoundingMode])
        return MMComputeControlUnsupported(error,"textureWriteRoundingMode");
    /* The threadgroup multiple is a caller promise. This function path does
     * not specialize for it; native pipeline and dispatch limits still apply. */
    if ([[descriptor binaryArchives] count])return MMComputeControlUnsupported(error,"binaryArchives");
    if ([[descriptor insertLibraries] count])return MMComputeControlUnsupported(error,"insertLibraries");
    if ([[descriptor preloadedLibraries] count])return MMComputeControlUnsupported(error,"preloadedLibraries");
    if ([descriptor supportAddingBinaryFunctions])return MMComputeControlUnsupported(error,"supportAddingBinaryFunctions");
    if ([descriptor supportIndirectCommandBuffers])return MMComputeControlUnsupported(error,"supportIndirectCommandBuffers");
    if ([descriptor maxTotalThreadsPerThreadgroup])return MMComputeControlUnsupported(error,"maxTotalThreadsPerThreadgroup");
    if ([descriptor maxCallStackDepth]>1)return MMComputeControlUnsupported(error,"maxCallStackDepth");
    if ([[linked functions] count])return MMComputeControlUnsupported(error,"linkedFunctions.functions");
    if ([[linked binaryFunctions] count])return MMComputeControlUnsupported(error,"linkedFunctions.binaryFunctions");
    if ([[linked privateFunctions] count])return MMComputeControlUnsupported(error,"linkedFunctions.privateFunctions");
    if ([[linked groups] allKeys].count)return MMComputeControlUnsupported(error,"linkedFunctions.groups");
    id buffers=[descriptor buffers];
    for (NSUInteger i=0;buffers && i<31;i++)
        if ([[buffers objectAtIndexedSubscript:i] mutability]) {
            char control[64];snprintf(control,sizeof(control),"buffers[%lu].mutability",(unsigned long)i);
            return MMComputeControlUnsupported(error,control);
        }
    return YES;
}

@implementation MaliMetalDevice (MaliMetalAPIExtensions)
- (id)newArgumentEncoderWithArguments:(NSArray *)arguments {
    return [[MaliMetalArgumentEncoder alloc] initWithDevice:self descriptors:arguments];
}
- (void)newLibraryWithSource:(NSString *)source options:(MTLCompileOptions *)options
           completionHandler:(MMMetalLibraryCompletion)handler {
    if (!handler) {MMInvalid("Library completion handler is nil");return;}
    NSString *snapshot=[source copy];id settings=[(id)options copy];
    MMAsync(^{NSError *error=nil;id result=[self newLibraryWithSource:snapshot options:settings error:&error];
        handler(result,error);[result release];});
    [snapshot release];[settings release];
}
- (void)newLibraryWithURL:(NSURL *)url completionHandler:(MMMetalLibraryCompletion)handler {
    if (!handler) {MMInvalid("Library completion handler is nil");return;}
    NSURL *snapshot=[url copy];MMAsync(^{NSError *error=nil;id result=[self newLibraryWithURL:snapshot error:&error];
        handler(result,error);[result release];});[snapshot release];
}
- (void)newLibraryWithData:(dispatch_data_t)data completionHandler:(MMMetalLibraryCompletion)handler {
    if (!handler) {MMInvalid("Library completion handler is nil");return;}
    if (data) dispatch_retain(data);
    MMAsync(^{NSError *error=nil;id result=[self newLibraryWithData:data error:&error];
        if (data) dispatch_release(data);handler(result,error);[result release];});
}
- (void)newRenderPipelineStateWithDescriptor:(MTLRenderPipelineDescriptor *)descriptor
                          completionHandler:(MMMetalPipelineCompletion)handler {
    if (!handler) {MMInvalid("Pipeline completion handler is nil");return;}
    id snapshot=[descriptor copy];MMAsync(^{NSError *error=nil;
        id result=[self newRenderPipelineStateWithDescriptor:snapshot error:&error];
        handler(result,error);[result release];});[snapshot release];
}
- (id)newRenderPipelineStateWithDescriptor:(MTLRenderPipelineDescriptor *)descriptor
                                   options:(NSUInteger)options reflection:(id *)reflection
                                     error:(NSError **)error {
    if (error) *error=nil;if (reflection) *reflection=nil;
    if (!MMOptions(options,error)) return nil;
    id metadata=nil;
    if (reflection && options) {metadata=MMRenderReflection(descriptor,error);if (!metadata)return nil;}
    id result=[self newRenderPipelineStateWithDescriptor:descriptor error:error];
    if (result && reflection) *reflection=metadata;
    return result;
}
- (void)newRenderPipelineStateWithDescriptor:(MTLRenderPipelineDescriptor *)descriptor
                                    options:(NSUInteger)options
                          completionHandler:(MMMetalPipelineReflectionCompletion)handler {
    if (!handler) {MMInvalid("Pipeline completion handler is nil");return;}
    id snapshot=[descriptor copy];MMAsync(^{NSError *error=nil;id reflection=nil;
        id result=[self newRenderPipelineStateWithDescriptor:snapshot options:options reflection:&reflection error:&error];
        handler(result,reflection,error);[result release];});[snapshot release];
}
- (void)newComputePipelineStateWithFunction:(id<MTLFunction>)function
                         completionHandler:(MMMetalPipelineCompletion)handler {
    if (!handler) {MMInvalid("Pipeline completion handler is nil");return;}
    MMAsync(^{NSError *error=nil;id result=[self newComputePipelineStateWithFunction:function error:&error];
        handler(result,error);[result release];});
}
- (id)newComputePipelineStateWithFunction:(id<MTLFunction>)function options:(NSUInteger)options
                               reflection:(id *)reflection error:(NSError **)error {
    if (error) *error=nil;if (reflection) *reflection=nil;
    if (!MMOptions(options,error)) return nil;
    id metadata=nil;
    if (reflection && options) {metadata=MMNode(MMFunctionInfo((id)function,3,0,error));if (!metadata)return nil;}
    id result=[self newComputePipelineStateWithFunction:function error:error];
    if (result && reflection) *reflection=metadata;return result;
}
- (void)newComputePipelineStateWithFunction:(id<MTLFunction>)function options:(NSUInteger)options
                         completionHandler:(MMMetalPipelineReflectionCompletion)handler {
    if (!handler) {MMInvalid("Pipeline completion handler is nil");return;}
    MMAsync(^{NSError *error=nil;id reflection=nil;
        id result=[self newComputePipelineStateWithFunction:function options:options reflection:&reflection error:&error];
        handler(result,reflection,error);[result release];});
}
- (id)newComputePipelineStateWithDescriptor:(id)descriptor error:(NSError **)error {
    return [self newComputePipelineStateWithDescriptor:descriptor options:0 reflection:NULL error:error];
}
- (id)newComputePipelineStateWithDescriptor:(id)descriptor options:(NSUInteger)options
                                 reflection:(id *)reflection error:(NSError **)error {
    if (error) *error=nil;if (reflection) *reflection=nil;
    if (!MMComputeDescriptorSupported(descriptor,error))return nil;
    id result=[self newComputePipelineStateWithFunction:[descriptor computeFunction] options:options
                                             reflection:reflection error:error];
    if (result) [result setLabel:[descriptor label]];return result;
}
- (void)newComputePipelineStateWithDescriptor:(id)descriptor
                           completionHandler:(MMMetalPipelineCompletion)handler {
    if (!handler) {MMInvalid("Pipeline completion handler is nil");return;}
    id snapshot=[descriptor copy];MMAsync(^{NSError *error=nil;
        id result=[self newComputePipelineStateWithDescriptor:snapshot error:&error];
        handler(result,error);[result release];});[snapshot release];
}
- (void)newComputePipelineStateWithDescriptor:(id)descriptor options:(NSUInteger)options
                           completionHandler:(MMMetalPipelineReflectionCompletion)handler {
    if (!handler) {MMInvalid("Pipeline completion handler is nil");return;}
    id snapshot=[descriptor copy];MMAsync(^{NSError *error=nil;id reflection=nil;
        id result=[self newComputePipelineStateWithDescriptor:snapshot options:options reflection:&reflection error:&error];
        handler(result,reflection,error);[result release];});[snapshot release];
}
@end

/* Library classes live in the facade's translation unit; categories add only
 * selectors and deliberately do not redeclare or access their ivars. */
@interface MaliMetalLibrary : MaliMetalObject @end
@implementation MaliMetalLibrary (MaliMetalAPIExtensions)
- (id)reflectionForFunctionWithName:(NSString *)name {
    NSError *error=nil;id result=MaliMetalLibraryFunctionReflection(self,name,&error);
    if (error) MMInvalid("AIR function reflection is unavailable for this library or function");
    return result;
}
- (void)newFunctionWithName:(NSString *)name constantValues:(id)values
          completionHandler:(MMMetalFunctionCompletion)handler {
    if (!handler) {MMInvalid("Function completion handler is nil");return;}
    id snapshot=[name copy],constants=[values copy];
    MMAsync(^{NSError *error=nil;id result=[self newFunctionWithName:snapshot constantValues:constants error:&error];
        handler(result,error);[result release];});[snapshot release];[constants release];
}
- (void)newFunctionWithDescriptor:(MTLFunctionDescriptor *)descriptor
                completionHandler:(MMMetalFunctionCompletion)handler {
    if (!handler) {MMInvalid("Function completion handler is nil");return;}
    id snapshot=[descriptor copy];MMAsync(^{NSError *error=nil;
        id result=[self newFunctionWithDescriptor:snapshot error:&error];
        handler(result,error);[result release];});[snapshot release];
}
@end

@interface MaliMetalFunction : MaliMetalObject @end
@implementation MaliMetalFunction (MaliMetalAPIExtensions)
- (NSDictionary *)functionConstantsDictionary {
    NSError *error=nil;
    id result=MaliMetalFunctionConstantsDictionary(self,[self maliIsSpecialized],&error);
    if (error) [NSException raise:@"NSInvalidArgumentException" format:@"Function constants reflection failed: %@",error];
    return result;
}
- (NSUInteger)patchType {
    NSError *error=nil;id properties=MaliMetalFunctionProperties(self,[self maliIsSpecialized],&error);
    if (error) [NSException raise:@"NSInvalidArgumentException" format:@"Function properties failed: %@",error];
    return MaliMetalFunctionPatchType(properties);
}
- (NSInteger)patchControlPointCount {
    NSError *error=nil;id properties=MaliMetalFunctionProperties(self,[self maliIsSpecialized],&error);
    if (error) [NSException raise:@"NSInvalidArgumentException" format:@"Function properties failed: %@",error];
    return MaliMetalFunctionPatchControlPointCount(properties);
}
- (id)newArgumentEncoderWithBufferIndex:(NSUInteger)index {
    return MaliMetalNewFunctionArgumentEncoder(self,index,NULL);
}
@end
