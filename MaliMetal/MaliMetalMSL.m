/* Genuine iOS Metal source frontend. The system compiler supplies the executable
 * metallib; no Apple GPU plugin, hardware identity or machine code is selected. */
#import "MaliMetalMSL.h"
#import "Shader/MaliMetalShader.h"
#include <dlfcn.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <errno.h>

#define MM_MSL_MAX_LIBRARY (64u*1024u*1024u)
#define MM_MSL_MAX_SOURCE (32u*1024u*1024u)
#define MM_MSL_MAX_OPTIONS (1024u*1024u)
#define MM_MSL_MAX_REQUESTS 4u
#define MM_MSL_RETAINED_BYTES (128u*1024u*1024u)
#define MM_MSL_TIMEOUT_MS 20000u

@interface MTLCompileOptions : NSObject
- (NSDictionary *)preprocessorMacros;
- (BOOL)fastMathEnabled;
- (NSUInteger)languageVersion;
- (BOOL)preserveInvariance;
- (NSUInteger)libraryType;
- (NSString *)installName;
- (NSArray *)libraries;
- (NSString *)additionalCompilerArguments;
- (BOOL)tracingEnabled;
- (BOOL)debuggingEnabled;
- (BOOL)compileTimeStatisticsEnabled;
- (BOOL)glBufferBindPoints;
- (NSUInteger)sourceLanguage;
- (NSUInteger)optimizationLevel;
- (NSUInteger)compileSymbolVisibility;
- (BOOL)allowReferencingUndefinedSymbols;
- (NSUInteger)maxTotalThreadsPerThreadgroup;
@end
@interface NSDictionary (MMMSLDictionary)
- (NSArray *)allKeys;
- (id)objectForKey:(id)key;
@end
@interface NSString (MMMSLString)
- (NSData *)dataUsingEncoding:(NSUInteger)encoding allowLossyConversion:(BOOL)allow;
- (NSUInteger)length;
- (instancetype)initWithBytes:(const void *)bytes length:(NSUInteger)length encoding:(NSUInteger)encoding;
@end
@interface NSNumber : NSObject
- (NSString *)stringValue;
@end
@interface NSData (MMMSLData)
- (instancetype)initWithBytes:(const void *)bytes length:(NSUInteger)length;
@end
@interface MaliMetalLibrary : MaliMetalObject
- (instancetype)initWithBytes:(const void *)bytes length:(NSUInteger)length
                         device:(MaliMetalDevice *)device error:(NSError **)error;
@end

static NSError *MSLError(NSInteger code,const char *message) {
    NSString *reason=[NSString stringWithUTF8String:message?message:"Unknown native source compiler error"];
    if (!reason)reason=@"Native source compiler returned a non-UTF-8 diagnostic";
    return [NSError errorWithDomain:@"org.heliodarwin.MaliMetal.MSL" code:code
        userInfo:[NSDictionary dictionaryWithObject:reason forKey:@"NSLocalizedDescription"]];
}
#ifdef MM_SHADER_ENABLE_AIR_COMPILER
static BOOL MSLFail(NSError **error,NSInteger code,const char *message) {
    if (error)*error=MSLError(code,message);return NO;
}
typedef struct MSLString { char *bytes;size_t length; } MSLString;
static BOOL MSLAppend(MSLString *s,const void *bytes,size_t count) {
    if (count>MM_MSL_MAX_OPTIONS-s->length)return NO;
    char *next=realloc(s->bytes,s->length+count+1);if (!next)return NO;
    s->bytes=next;if (count)memcpy(next+s->length,bytes,count);s->length+=count;next[s->length]=0;return YES;
}
static BOOL MSLFormat(MSLString *s,const char *format,...) {
    char buffer[256];va_list args;va_start(args,format);
    int n=vsnprintf(buffer,sizeof(buffer),format,args);va_end(args);
    return n>=0 && (size_t)n<sizeof(buffer) && MSLAppend(s,buffer,(size_t)n);
}
static BOOL MSLUTF8(NSString *string,size_t limit,const void **bytes,size_t *length) {
    if (![(id)string isKindOfClass:[NSString class]])return NO;
    NSData *data=[string dataUsingEncoding:4 allowLossyConversion:NO];
    if (!data || [data length]>limit || ([data length] && memchr([data bytes],0,[data length])))return NO;
    *bytes=[data bytes];*length=[data length];return YES;
}
/* The target frontend uses LLVM TokenizeGNUCommandLine, not a shell. Quoting
 * preserves each complete -Dname=value token, including spaces and quotes. */
static BOOL MSLQuote(MSLString *options,const void *bytes,size_t length) {
    const unsigned char *p=bytes;if (!MSLAppend(options,"\"",1))return NO;
    for (size_t i=0;i<length;i++) {
        if ((p[i]=='\\' || p[i]=='\"') && !MSLAppend(options,"\\",1))return NO;
        if (!MSLAppend(options,p+i,1))return NO;
    }
    return MSLAppend(options,"\" ",2);
}
static BOOL MSLBuildOptions(MTLCompileOptions *settings,MSLString *out,NSError **error) {
    if (![settings isKindOfClass:[MTLCompileOptions class]])
        return MSLFail(error,2,"Source options must be native Metal compile options");
    SEL required[]={@selector(sourceLanguage),@selector(libraryType),@selector(libraries),
        @selector(installName),@selector(additionalCompilerArguments),@selector(tracingEnabled),
        @selector(debuggingEnabled),@selector(compileTimeStatisticsEnabled),@selector(glBufferBindPoints),
        @selector(compileSymbolVisibility),@selector(allowReferencingUndefinedSymbols),
        @selector(maxTotalThreadsPerThreadgroup)};
    for (size_t i=0;i<sizeof(required)/sizeof(required[0]);i++)
        if (![settings respondsToSelector:required[i]])
            return MSLFail(error,1,"System compile-options ABI differs from the verified iOS 16.7 contract");
    if ([settings sourceLanguage] || [settings libraryType] || [[settings libraries] count] ||
        [[settings installName] length] || [[settings additionalCompilerArguments] length] ||
        [settings tracingEnabled] || [settings debuggingEnabled] ||
        [settings compileTimeStatisticsEnabled] || [settings glBufferBindPoints] ||
        [settings compileSymbolVisibility] || [settings allowReferencingUndefinedSymbols])
        return MSLFail(error,2,"Requested source/link/debug/instrumentation options are unsupported by this source route");
    NSUInteger language=[settings languageVersion],optimization=[settings optimizationLevel];
    if (!language || language>UINT32_MAX || !(language>>16) || optimization>1 ||
        [settings maxTotalThreadsPerThreadgroup]>UINT32_MAX)
        return MSLFail(error,2,"Invalid Metal language, optimization, or threadgroup compilation option");
    BOOL built=MSLFormat(out,"--driver-mode=metal -x metal -std=%s%lu.%lu %s",
        (language>>16)<=2?"ios-metal":"metal",(unsigned long)(language>>16),
        (unsigned long)(language&0xffff),[settings fastMathEnabled]?"-ffast-math ":"-fno-fast-math ");
    if (built && [settings preserveInvariance])built=MSLAppend(out,"-fpreserve-invariance ",22);
    if (built && optimization==1)built=MSLAppend(out,"-Os ",4);
    if (built && [settings maxTotalThreadsPerThreadgroup])
        built=MSLFormat(out,"-fmax-total-threads-per-threadgroup=%lu ",
                        (unsigned long)[settings maxTotalThreadsPerThreadgroup]);
    NSDictionary *macros=[settings preprocessorMacros];
    if (macros && ![(id)macros isKindOfClass:[NSDictionary class]])
        return MSLFail(error,2,"Metal preprocessor macros must be a dictionary");
    NSArray *keys=[macros allKeys];
    if ([keys count]>4096)return MSLFail(error,2,"Too many Metal preprocessor definitions");
    for (NSUInteger i=0;built && i<[keys count];i++) {
        id key=[keys objectAtIndex:i],value=[macros objectForKey:key];
        const void *name=NULL,*text=NULL;size_t nameLength=0,textLength=0;
        if (!MSLUTF8(key,65536,&name,&nameLength) || !nameLength)
            return MSLFail(error,2,"Metal macro names must be nonempty UTF-8 strings without NUL bytes");
        if ([value isKindOfClass:[NSNumber class]])value=[value stringValue];
        if (!MSLUTF8(value,65536,&text,&textLength))
            return MSLFail(error,2,"Metal macro values must be strings or numbers without NUL bytes");
        /* Equals, whitespace and quoting are valid in a replacement list but
         * cannot be part of a macro name. Unicode identifier validation is left
         * to the real compiler rather than silently changing the name. */
        const unsigned char *n=name;
        for (size_t k=0;k<nameLength;k++)if (n[k]<=32 || n[k]=='=' || n[k]=='\"' || n[k]=='\'' || n[k]=='\\')
            return MSLFail(error,2,"Invalid characters in Metal macro name");
        MSLString argument={0};
        built=MSLAppend(&argument,"-D",2) && MSLAppend(&argument,name,nameLength) &&
            MSLAppend(&argument,"=",1) && MSLAppend(&argument,text,textLength) &&
            MSLQuote(out,argument.bytes,argument.length);free(argument.bytes);
    }
    if (!built)return MSLFail(error,2,"Metal compiler arguments exceed the bounded source request or cannot be allocated");
    return YES;
}
static uint32_t MSLRead32(const unsigned char *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void MSLWrite32(unsigned char *p,uint32_t n) {
    for (unsigned i=0;i<4;i++)p[i]=(unsigned char)(n>>(8*i));
}
static BOOL MSLSpan(size_t total,uint32_t offset,uint32_t length) {
    return offset<=total && length<=total-offset;
}

typedef void (^MSLCompletion)(uint32_t,const void *,size_t,const char *);
typedef void *(*MSLCreate)(void);
typedef void (*MSLDestroy)(void *);
typedef void (*MSLBuild)(void *,uint32_t,uint32_t,const void *,size_t,MSLCompletion);
extern void *_Block_copy(const void *);
extern void _Block_release(const void *);
static pthread_once_t MSLLoadOnce=PTHREAD_ONCE_INIT;
static void *MSLLibrary;
static MSLCreate MSLNativeCreate;
static MSLDestroy MSLNativeDestroy;
static MSLBuild MSLNativeBuild;
static dispatch_queue_t MSLQueue;
static char MSLLoadFailure[768];
static void MSLLoad(void) {
    const char *path="/System/Library/PrivateFrameworks/MTLCompiler.framework/MTLCompiler";
    MSLLibrary=dlopen(path,RTLD_NOW|RTLD_LOCAL);
    if (!MSLLibrary) {
        const char *cause=dlerror();
        snprintf(MSLLoadFailure,sizeof(MSLLoadFailure),"Cannot load native MSL frontend %s: %s",path,
                 cause?cause:"dynamic loader supplied no diagnostic");return;
    }
    MSLNativeCreate=(MSLCreate)dlsym(MSLLibrary,"MTLCodeGenServiceCreate");
    MSLNativeDestroy=(MSLDestroy)dlsym(MSLLibrary,"MTLCodeGenServiceDestroy");
    MSLNativeBuild=(MSLBuild)dlsym(MSLLibrary,"MTLCodeGenServiceBuildRequest");
    if (!MSLNativeCreate || !MSLNativeDestroy || !MSLNativeBuild) {
        const char *missing=!MSLNativeCreate?"MTLCodeGenServiceCreate":
                            !MSLNativeDestroy?"MTLCodeGenServiceDestroy":"MTLCodeGenServiceBuildRequest";
        snprintf(MSLLoadFailure,sizeof(MSLLoadFailure),"Native MSL frontend is missing export %s",missing);return;
    }
    MSLQueue=dispatch_queue_create("MaliMetal.MSL.frontend",NULL);
    if (!MSLQueue)snprintf(MSLLoadFailure,sizeof(MSLLoadFailure),"Cannot create serialized native MSL compiler queue");
    /* The image remains loaded while any native callback/context can exist. */
}

@interface MMMSLRequest : NSObject {
@public
    pthread_mutex_t lock;
    pthread_cond_t condition;
    unsigned char *packet,*reply;
    size_t packetSize,replySize;
    void *context;
    MSLCompletion nativeCallback;
    BOOL callbackSeen,finished,cancelled,quarantined,asynchronousCallback;
    pthread_t nativeThread;
    unsigned callbacksActive,slot;
    uint32_t nativeError;
    char diagnostic[1024];
    NSData *result;
    NSError *error,*warning;
}
@end
@implementation MMMSLRequest
- (id)init {self=[super init];if (self) {pthread_mutex_init(&lock,NULL);pthread_cond_init(&condition,NULL);}return self;}
- (void)dealloc {
    /* Only nonquarantined, quiescent requests reach dealloc. Context lifetime is
     * explicitly closed by the worker after BuildRequest and its callback end. */
    if (nativeCallback)_Block_release(nativeCallback);
    free(packet);free(reply);[result release];[error release];[warning release];
    pthread_cond_destroy(&condition);pthread_mutex_destroy(&lock);[super dealloc];
}
@end
static pthread_mutex_t MSLAdmissionLock=PTHREAD_MUTEX_INITIALIZER;
static MMMSLRequest *MSLRequests[MM_MSL_MAX_REQUESTS];
static size_t MSLRetainedBytes;
static BOOL MSLAdmit(MMMSLRequest *request) {
    BOOL admitted=NO;pthread_mutex_lock(&MSLAdmissionLock);
    for (unsigned i=0;i<MM_MSL_MAX_REQUESTS;i++)if (!MSLRequests[i] &&
        request->packetSize<=MM_MSL_RETAINED_BYTES-MSLRetainedBytes) {
        MSLRequests[i]=[request retain];request->slot=i;admitted=YES;
        MSLRetainedBytes+=request->packetSize;break;
    }
    pthread_mutex_unlock(&MSLAdmissionLock);return admitted;
}
static void MSLReleaseAdmission(MMMSLRequest *request) {
    pthread_mutex_lock(&MSLAdmissionLock);
    MMMSLRequest *held=MSLRequests[request->slot];MSLRequests[request->slot]=nil;
    MSLRetainedBytes-=request->packetSize+request->replySize;
    pthread_mutex_unlock(&MSLAdmissionLock);[held release];
}
static void MSLNativeCompleted(MMMSLRequest *request,uint32_t code,const void *bytes,
                                size_t length,const char *message) {
    pthread_mutex_lock(&request->lock);request->callbacksActive++;
    if (!pthread_equal(pthread_self(),request->nativeThread))request->asynchronousCallback=YES;
    if (!request->callbackSeen && !request->finished) {
        request->callbackSeen=YES;request->nativeError=code;
        if (message)snprintf(request->diagnostic,sizeof(request->diagnostic),"%.*s",1023,message);
        if (!code) {
            if (!bytes || length<4 || length>MM_MSL_MAX_LIBRARY) {
                request->nativeError=2;strcpy(request->diagnostic,"Native MSL compiler returned an invalid or oversized reply");
            } else {
                pthread_mutex_lock(&MSLAdmissionLock);
                BOOL admitted=length<=MM_MSL_RETAINED_BYTES-MSLRetainedBytes;
                if (admitted)MSLRetainedBytes+=length;
                pthread_mutex_unlock(&MSLAdmissionLock);
                if (admitted)request->reply=malloc(length);
                if (request->reply) {memcpy(request->reply,bytes,length);request->replySize=length;}
                else {
                    if (admitted) {pthread_mutex_lock(&MSLAdmissionLock);MSLRetainedBytes-=length;pthread_mutex_unlock(&MSLAdmissionLock);}
                    request->nativeError=2;strcpy(request->diagnostic,"Native MSL compiler reply exceeds the retained-byte budget or cannot be copied");
                }
            }
        }
    }
    request->callbacksActive--;pthread_mutex_unlock(&request->lock);
}

static BOOL MSLValidateReply(MMMSLRequest *request,NSData **result,NSError **warning,NSError **error) {
    const unsigned char *library=request->reply;size_t length=request->replySize;
    if (request->nativeError) {
        char message[1200];snprintf(message,sizeof(message),"Native MSL compiler error %u: %s",
            request->nativeError,request->diagnostic[0]?request->diagnostic:"no diagnostic supplied");
        return MSLFail(error,3,message);
    }
    if (length<4)return MSLFail(error,3,"Native MSL compiler returned no library");
    if (request->diagnostic[0])*warning=MSLError(0,request->diagnostic);
    if (memcmp(library,"MTLB",4)) {
        if (length<104 || library[0]!=3 || !MSLSpan(length,MSLRead32(library+8),MSLRead32(library+12)) ||
            !MSLSpan(length,MSLRead32(library+40),MSLRead32(library+44)) ||
            !MSLSpan(length,MSLRead32(library+48),MSLRead32(library+52)))
            return MSLFail(error,3,"Native MSL compiler returned an invalid archive reply header");
        uint32_t offset=MSLRead32(library+40),size=MSLRead32(library+44);
        if (offset<104 || size<4 || memcmp(library+offset,"MTLB",4))
            return MSLFail(error,3,"Native MSL source reply contains no executable MTLB archive");
        uint32_t logOffset=MSLRead32(library+8),logSize=MSLRead32(library+12);
        uint32_t statisticsOffset=MSLRead32(library+48),statisticsSize=MSLRead32(library+52);
        uint64_t archiveEnd=(uint64_t)offset+size;
        if (logOffset!=104 || (uint64_t)logOffset+logSize!=offset ||
            (statisticsSize?statisticsOffset!=archiveEnd:statisticsOffset!=0) ||
            archiveEnd+statisticsSize!=length)
            return MSLFail(error,3,"Native MSL archive reply contains overlapping or noncanonical data spans");
        if (logSize) {
            if (logOffset<104 || logSize>1024*1024)return MSLFail(error,3,"Native MSL warning log has invalid bounds");
            char *log=malloc((size_t)logSize+1);if (!log)return MSLFail(error,3,"Cannot copy native compiler warning log");
            memcpy(log,library+logOffset,logSize);log[logSize]=0;
            for (uint32_t i=0;i<logSize;i++)if (!log[i])log[i]='\n';
            *warning=MSLError(0,log);free(log);
        }
        library+=offset;length=size;
    }
    MMShaderLibrary *parsed=NULL;MMShaderError failure={0};
    MMShaderStatus status=MMShaderLibraryCreate(library,length,&parsed,&failure);
    if (status!=MM_SHADER_OK) return MSLFail(error,3,failure.message);
    uint32_t count=MMShaderLibraryEntryCount(parsed);
    if (count>4096) {MMShaderLibraryDestroy(parsed);return MSLFail(error,3,"Native source library exceeds the bounded function count");}
#ifndef MM_SHADER_ENABLE_AIR_COMPILER
    MMShaderLibraryDestroy(parsed);
    return MSLFail(error,1,"Native MSL output cannot be accepted without the actual AIR decoder provider");
#else
    for (uint32_t i=0;i<count;i++) {
        pthread_mutex_lock(&request->lock);BOOL cancelled=request->cancelled;pthread_mutex_unlock(&request->lock);
        if (cancelled) {MMShaderLibraryDestroy(parsed);return MSLFail(error,4,"Source request expired during AIR validation");}
        const MMShaderEntry *entry=MMShaderLibraryGetEntry(parsed,i);size_t size=0;
        const void *air=MMShaderLibraryAIR(parsed,i,&size);
        status=MMMetalValidateAIRFunction(air,size,entry->name,entry->stage,&failure);
        if (status!=MM_SHADER_OK)break;
    }
    MMShaderLibraryDestroy(parsed);
    if (status!=MM_SHADER_OK)return MSLFail(error,3,failure.message);
    *result=[[[NSData alloc] initWithBytes:library length:length] autorelease];
    if (!*result)return MSLFail(error,3,"Cannot retain compiled source library");
    return YES;
#endif
}

static void MSLRun(MMMSLRequest *request) {
    pthread_mutex_lock(&request->lock);BOOL cancelled=request->cancelled;pthread_mutex_unlock(&request->lock);
    NSError *error=nil,*warning=nil;NSData *result=nil;
    if (cancelled)error=MSLError(4,"Source request expired before entering the native compiler");
    else {
        request->context=MSLNativeCreate();
        if (!request->context)error=MSLError(1,"MTLCodeGenServiceCreate returned no native frontend context");
        else {
            pthread_mutex_lock(&request->lock);cancelled=request->cancelled;pthread_mutex_unlock(&request->lock);
            if (cancelled)error=MSLError(4,"Source request expired while creating the native compiler context");
            else {
            void *opaque=request;
            request->nativeCallback=(MSLCompletion)_Block_copy(^(uint32_t code,const void *bytes,size_t size,const char *message) {
                MSLNativeCompleted((MMMSLRequest *)opaque,code,bytes,size,message);
            });
            if (!request->nativeCallback)error=MSLError(1,"Cannot retain native source compiler completion block");
            else {
                request->nativeThread=pthread_self();
                MSLNativeBuild(request->context,UINT32_MAX,13,request->packet,request->packetSize,request->nativeCallback);
                pthread_mutex_lock(&request->lock);
                request->quarantined=!request->callbackSeen || request->callbacksActive!=0 || request->asynchronousCallback;
                cancelled=request->cancelled;pthread_mutex_unlock(&request->lock);
                if (request->quarantined) {
                    /* Keep the admission reference, context, packet, block and
                     * state indefinitely. A late native callback stays valid;
                     * the fixed admission table bounds the quarantine. */
                    pthread_mutex_lock(&request->lock);
                    request->error=[MSLError(5,"Native MSL frontend returned without a quiescent completion; request quarantined") retain];
                    request->finished=YES;pthread_cond_broadcast(&request->condition);pthread_mutex_unlock(&request->lock);
                    return;
                }
                if (cancelled)error=MSLError(4,"Native MSL request completed after its caller deadline");
                else MSLValidateReply(request,&result,&warning,&error);
            }
            }
            MSLNativeDestroy(request->context);request->context=NULL;
        }
    }
    pthread_mutex_lock(&request->lock);request->result=[result retain];request->warning=[warning retain];
    request->error=[error retain];request->finished=YES;pthread_cond_broadcast(&request->condition);pthread_mutex_unlock(&request->lock);
    MSLReleaseAdmission(request);
}

id<MTLLibrary> MaliMetalNewLibraryWithSource(MaliMetalDevice *device,NSString *source,
                                            MTLCompileOptions *options,NSError **error) {
    if (error)*error=nil;
    if (!device || ![device backendDevice]) {MSLFail(error,1,"Native device is unavailable for a source library");return nil;}
#ifndef MM_SHADER_ENABLE_AIR_COMPILER
    (void)source;(void)options;MSLFail(error,1,"Source compilation requires the actual AIR decoder provider");return nil;
#else
    const MMShaderCompiler *compiler=MMShaderGetNativeCompiler();
    if (!compiler || compiler->abi_version!=MM_SHADER_ABI_VERSION) {MSLFail(error,1,"AIR decoder provider is unavailable or has another ABI");return nil;}
    const void *sourceBytes=NULL;size_t sourceSize=0;
    if (!MSLUTF8(source,MM_MSL_MAX_SOURCE,&sourceBytes,&sourceSize)) {
        MSLFail(error,2,"Metal source must be bounded UTF-8 text without embedded NUL bytes");return nil;
    }
    MTLCompileOptions *settings=options?[(id)options copy]:[[MTLCompileOptions alloc] init];
    MSLString arguments={0};BOOL valid=MSLBuildOptions(settings,&arguments,error);[settings release];
    if (!valid) {free(arguments.bytes);return nil;}
    size_t afterSource=(16+sourceSize+1+3)&~(size_t)3;
    size_t packetSize=afterSource+arguments.length+1;
    MMMSLRequest *request=[[MMMSLRequest alloc] init];
    if (!request) {free(arguments.bytes);MSLFail(error,1,"Cannot allocate source compiler request");return nil;}
    request->packet=calloc(1,packetSize);request->packetSize=packetSize;
    if (!request->packet) {free(arguments.bytes);[request release];MSLFail(error,1,"Cannot allocate source compiler packet");return nil;}
    MSLWrite32(request->packet,(uint32_t)(sourceSize+1));MSLWrite32(request->packet+4,0);
    MSLWrite32(request->packet+8,(uint32_t)(arguments.length+1));MSLWrite32(request->packet+12,0);
    if (sourceSize)memcpy(request->packet+16,sourceBytes,sourceSize);
    if (arguments.length)memcpy(request->packet+afterSource,arguments.bytes,arguments.length);
    free(arguments.bytes);
    pthread_once(&MSLLoadOnce,MSLLoad);
    if (MSLLoadFailure[0] || !MSLQueue) {[request release];MSLFail(error,1,MSLLoadFailure);return nil;}
    if (!MSLAdmit(request)) {[request release];MSLFail(error,5,"Native MSL compiler outstanding/quarantine count or retained-byte budget is exhausted");return nil;}
    dispatch_async(MSLQueue,^{@autoreleasepool {MSLRun(request);}});
    struct timespec deadline;clock_gettime(CLOCK_REALTIME,&deadline);
    deadline.tv_sec+=MM_MSL_TIMEOUT_MS/1000;deadline.tv_nsec+=(MM_MSL_TIMEOUT_MS%1000)*1000000u;
    if (deadline.tv_nsec>=1000000000) {deadline.tv_sec++;deadline.tv_nsec-=1000000000;}
    pthread_mutex_lock(&request->lock);int waited=0;
    while (!request->finished && !waited)waited=pthread_cond_timedwait(&request->condition,&request->lock,&deadline);
    NSData *data=nil;NSError *failure=nil;
    if (request->finished) {data=[request->result retain];failure=[(request->error?request->error:request->warning) retain];}
    else {request->cancelled=YES;failure=[MSLError(4,waited==ETIMEDOUT?
        "Native MSL compilation exceeded 20 seconds; its request remains owned until the native call and callback are safe":
        "Native MSL compiler wait failed; its pending request remains owned") retain];}
    pthread_mutex_unlock(&request->lock);[request release];
    if (error)*error=[failure autorelease];else [failure release];
    if (!data)return nil;
    NSError *creationError=nil;
    id library=[[MaliMetalLibrary alloc] initWithBytes:[data bytes] length:[data length] device:device error:&creationError];
    [data release];if (!library && error)*error=creationError;
    return library;
#endif
}
#else
id<MTLLibrary> MaliMetalNewLibraryWithSource(MaliMetalDevice *device,NSString *source,
                                            MTLCompileOptions *options,NSError **error) {
    (void)device;(void)source;(void)options;
    if (error)*error=MSLError(1,"Source compilation requires the actual AIR decoder provider");
    return nil;
}
#endif
