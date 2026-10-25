/* SPDX-License-Identifier: MIT */
#import "MaliMetalSynchronization.h"
#import "MaliMetalResources.h"
#include <string.h>
#include <stdlib.h>
#include <mach/vm_region.h>
#include <time.h>
#include <stdio.h>

extern void *IOServiceMatching(const char *);
extern io_service_t IOServiceGetMatchingService(mach_port_t,void *);
extern kern_return_t IOObjectRelease(mach_port_t);
extern kern_return_t IOConnectCallScalarMethod(io_connect_t,uint32_t,const uint64_t *,uint32_t,uint64_t *,uint32_t *);
extern kern_return_t mach_port_type(mach_port_t,mach_port_t,uint32_t *);
extern kern_return_t mach_port_deallocate(mach_port_t,mach_port_t);
extern kern_return_t mach_vm_region(mach_port_t,mach_vm_address_t *,mach_vm_size_t *,
    int,int *,uint32_t *,mach_port_t *);

@interface NSObject (MaliMetalNativeSharedEvent)
- (mach_port_t)eventPort;
- (uint64_t)signaledValue;
- (void)setSignaledValue:(uint64_t)value;
- (BOOL)waitUntilSignaledValue:(uint64_t)value timeoutMS:(uint64_t)timeout;
- (MMVKBuffer *)maliBuffer;
- (void *)iosurface;
- (BOOL)maliPrepareImage:(BOOL)preserve error:(MMVKError *)error;
- (BOOL)maliReadbackImage:(MMVKError *)error;
- (MMVKResult)maliCompleteComputeExecution:(MMVKError *)error;
@end
@interface _MTLDevice (MaliMetalNativeSharedEventFactory)
- (id)newSharedEvent;
- (id)newSharedEventWithHandle:(id)handle;
@end

/* Collision-free readiness selector, separate from Apple's standard ABI.
 * Exact constants are coordinated with the IOSurface kernel implementation. */
#define MM_SHARED_READY_SELECTOR 0x53574501u
#define MM_SHARED_READY_MAGIC UINT64_C(0x4d4d534845563031)
#define MM_SHARED_READY_FEATURES UINT64_C(31)
static pthread_mutex_t SharedTransportLock=PTHREAD_MUTEX_INITIALIZER;
static io_connect_t SharedConnection;
static BOOL SharedReady;
static MMVKResult sharedError(MMVKError *e,MMVKResult result,int32_t native,const char *message) {
    if(e){memset(e,0,sizeof(*e));e->code=result;e->native_code=native;
        size_t n=strlen(message);if(n>=sizeof(e->message))n=sizeof(e->message)-1;
        memcpy(e->message,message,n);}
    return result;
}
static BOOL sharedPortValid(mach_port_t port) {
    uint32_t type=0;
    return port && mach_port_type(mach_task_self(),port,&type)==KERN_SUCCESS &&
        (type&0x00010000u) && !(type&0x00100000u); /* SEND, not DEAD_NAME */
}
static BOOL sharedMapValid(uint64_t address) {
    if(!address || (address&7u) || address>UINT64_MAX-8) return NO;
    mach_vm_address_t start=address;mach_vm_size_t size=0;mach_port_t object=0;
    vm_region_basic_info_data_64_t info={0};uint32_t count=VM_REGION_BASIC_INFO_COUNT_64;
    kern_return_t kr=mach_vm_region(mach_task_self(),&start,&size,VM_REGION_BASIC_INFO_64,
        (int *)&info,&count,&object);
    if(object)mach_port_deallocate(mach_task_self(),object);
    return kr==KERN_SUCCESS && count==VM_REGION_BASIC_INFO_COUNT_64 && start<=address &&
        address-start<=size && size-(address-start)>=8 &&
        (info.protection&1) && !(info.protection&2) && !(info.max_protection&2);
}
static BOOL sharedMapForPort(mach_port_t port,uint64_t *address) {
    uint64_t input=port,output[3]={0};uint32_t count=3;
    if(!sharedPortValid(port)||IOConnectCallScalarMethod(SharedConnection,38,&input,1,output,&count)!=KERN_SUCCESS||
       count!=3||!output[2]||!sharedMapValid(output[1]))return NO;
    if(address)*address=output[1];return YES;
}
BOOL MaliMetalSharedEventTransportReady(void) {
    pthread_mutex_lock(&SharedTransportLock);
    if(SharedReady){pthread_mutex_unlock(&SharedTransportLock);return YES;}
    io_service_t service=IOServiceGetMatchingService(0,IOServiceMatching("IOSurfaceRoot"));
    io_connect_t connection=0;
    if(!service){pthread_mutex_unlock(&SharedTransportLock);return NO;}
    kern_return_t kr=IOServiceOpen(service,mach_task_self(),0,&connection);IOObjectRelease(service);
    if(kr!=KERN_SUCCESS){pthread_mutex_unlock(&SharedTransportLock);return NO;}
    uint64_t ready[3]={0};uint32_t count=3;
    BOOL valid=IOConnectCallScalarMethod(connection,MM_SHARED_READY_SELECTOR,NULL,0,ready,&count)==KERN_SUCCESS &&
        count==3&&ready[0]==MM_SHARED_READY_MAGIC&&ready[1]==16384&&
        (ready[2]&MM_SHARED_READY_FEATURES)==MM_SHARED_READY_FEATURES;
    uint64_t created[2]={0};count=2;
    if(valid)valid=IOConnectCallScalarMethod(connection,36,NULL,0,created,&count)==KERN_SUCCESS&&
        count==2&&created[0]<=UINT32_MAX&&sharedPortValid((mach_port_t)created[0])&&sharedMapValid(created[1]);
    SharedConnection=connection;
    uint64_t imported=0;
    if(valid)valid=sharedMapForPort((mach_port_t)created[0],&imported)&&
        __atomic_load_n((uint64_t *)(uintptr_t)created[1],__ATOMIC_ACQUIRE)==0&&
        __atomic_load_n((uint64_t *)(uintptr_t)imported,__ATOMIC_ACQUIRE)==0;
    if(valid){uint64_t signal[2]={created[0],1};
        valid=IOConnectCallScalarMethod(connection,37,signal,2,NULL,NULL)==KERN_SUCCESS&&
            __atomic_load_n((uint64_t *)(uintptr_t)created[1],__ATOMIC_ACQUIRE)==1&&
            __atomic_load_n((uint64_t *)(uintptr_t)imported,__ATOMIC_ACQUIRE)==1;}
    if(valid){uint64_t wait[3]={created[0],1,0};
        valid=IOConnectCallScalarMethod(connection,45,wait,3,NULL,NULL)==KERN_SUCCESS;}
    if(created[0]&&created[0]<=UINT32_MAX)mach_port_deallocate(mach_task_self(),(mach_port_t)created[0]);
    if(valid)SharedReady=YES;
    else {SharedConnection=0;IOServiceClose(connection);}
    pthread_mutex_unlock(&SharedTransportLock);return valid;
}
BOOL MaliMetalIsNativeSharedEvent(id event) {
    Class cls=objc_getClass("_MTLSharedEvent");
    return event&&cls&&[event isKindOfClass:cls];
}
static BOOL sharedEventValid(id event) {
    return MaliMetalIsNativeSharedEvent(event)&&MaliMetalSharedEventTransportReady()&&
        sharedMapForPort([event eventPort],NULL);
}
MMVKResult MaliMetalSharedEventPublish(id event,uint64_t value,MMVKError *error) {
    if(!sharedEventValid(event))return sharedError(error,MMVK_ERROR_ARGUMENT,0,"Invalid native shared event transport/right");
    uint64_t input[2]={[event eventPort],value};
    __atomic_thread_fence(__ATOMIC_RELEASE);
    kern_return_t kr=IOConnectCallScalarMethod(SharedConnection,37,input,2,NULL,NULL);
    return kr==KERN_SUCCESS?MMVK_OK:sharedError(error,MMVK_ERROR_IO,kr,"IOSurface shared-event publication failed");
}
MMVKResult MaliMetalSharedEventWait(id event,uint64_t value,uint64_t timeout,MMVKError *error) {
    if(!sharedEventValid(event))return sharedError(error,MMVK_ERROR_ARGUMENT,0,"Invalid native shared event transport/right");
    uint64_t address=0;if(!sharedMapForPort([event eventPort],&address))
        return sharedError(error,MMVK_ERROR_IO,0,"Shared event has no valid read-only timeline");
    if(__atomic_load_n((uint64_t *)(uintptr_t)address,__ATOMIC_ACQUIRE)>=value)return MMVK_OK;
    uint64_t milliseconds=timeout==UINT64_MAX?UINT64_MAX:timeout/1000000ull+(timeout%1000000ull!=0);
    uint64_t input[3]={[event eventPort],value,milliseconds};
    kern_return_t kr=IOConnectCallScalarMethod(SharedConnection,45,input,3,NULL,NULL);
    if(kr==KERN_SUCCESS){
        if(__atomic_load_n((uint64_t *)(uintptr_t)address,__ATOMIC_ACQUIRE)<value)
            return sharedError(error,MMVK_ERROR_STATE,kr,"Shared-event wait returned before threshold publication");
        return MMVK_OK;
    }
    if((uint32_t)kr==0xe00002d6u)return sharedError(error,MMVK_TIMEOUT,kr,"Shared-event wait remains pending");
    return sharedError(error,MMVK_ERROR_IO,kr,"IOSurface shared-event wait failed");
}

@implementation MaliMetalEvent
- (instancetype)initWithDevice:(MaliMetalDevice *)device {
    self=[super init];
    if(!self) return nil;
    if(!device) { [self release];return nil; }
    _eventDevice=[device retain];
    MMVKError error={0};
    if(mmvk_event_create([device backendDevice],&_event,&error)!=MMVK_OK) {
        [self release];return nil;
    }
    return self;
}
- (void)dealloc {
    mmvk_event_destroy(_event);[_eventDevice release];[super dealloc];
}
- (id<MTLDevice>)device { return _eventDevice; }
- (MMVKEvent *)maliEvent { return _event; }
@end

@implementation MaliMetalFence
- (instancetype)initWithDevice:(MaliMetalDevice *)device {
    self=[super initWithDevice:device];
    if(!self) return nil;
    int result=pthread_mutex_init(&_generationLock,NULL);
    if(result) {
        [self release];return nil;
    }
    _generationLockInitialized=YES;
    return self;
}
- (void)dealloc {
    if(_generationLockInitialized) pthread_mutex_destroy(&_generationLock);
    [super dealloc];
}
- (BOOL)reserveUpdate:(uint64_t *)out {
    if(!out) return NO;
    pthread_mutex_lock(&_generationLock);
    BOOL ok=_generation!=UINT64_MAX;
    if(ok) *out=++_generation;
    pthread_mutex_unlock(&_generationLock);return ok;
}
- (uint64_t)waitValue {
    pthread_mutex_lock(&_generationLock);uint64_t value=_generation;
    pthread_mutex_unlock(&_generationLock);return value;
}
- (MMVKResult)failureForValue:(uint64_t)value error:(MMVKError *)error {
    pthread_mutex_lock(&_generationLock);
    MMVKResult result=MMVK_OK;
    if(_failedGeneration&&value>=_failedGeneration){
        result=_generationFailure.code;if(error)*error=_generationFailure;
    }
    pthread_mutex_unlock(&_generationLock);return result;
}
- (void)failUnsubmittedValue:(uint64_t)value result:(MMVKResult)result error:(const MMVKError *)error {
    if(!value||value<=mmvk_event_last_submitted([self maliEvent]))return;
    pthread_mutex_lock(&_generationLock);
    if(!_failedGeneration||value<_failedGeneration){
        _failedGeneration=value;memset(&_generationFailure,0,sizeof(_generationFailure));
        _generationFailure.code=result<0?result:MMVK_ERROR_STATE;
        _generationFailure.native_code=error?error->native_code:0;
        const char *cause=error&&error->message[0]?error->message:"producer command buffer failed";
        snprintf(_generationFailure.message,sizeof(_generationFailure.message),
            "Fence generation %llu was not submitted: %.120s",(unsigned long long)value,cause);
    }
    pthread_mutex_unlock(&_generationLock);
}
- (MMVKResult)waitForValue:(uint64_t)value timeout:(uint64_t)timeout error:(MMVKError *)error {
    if(!value)return MMVK_OK;
    uint64_t completed=0;MMVKResult result=mmvk_event_value([self maliEvent],&completed,error);
    if(result)return result;
    /* A later failure cannot revoke an earlier successful native prefix. */
    if(completed>=value)return MMVK_OK;
    result=[self failureForValue:value error:error];if(result)return result;
    result=mmvk_event_wait([self maliEvent],value,timeout,error);
    if(result==MMVK_TIMEOUT||result==MMVK_NOT_READY){
        MMVKResult failed=[self failureForValue:value error:error];if(failed)return failed;
    }
    return result;
}
@end

@implementation MaliMetalFenceCommand
- (instancetype)initWithFence:(MaliMetalFence *)fence update:(BOOL)update {
    self=[super init];if(self) {_fence=[fence retain];_update=update;}return self;
}
- (void)dealloc {[_fence release];[super dealloc];}
- (BOOL)prepareForSubmission {
    return [self prepareForSubmissionWithError:NULL]==MMVK_OK;
}
- (MMVKResult)prepareForSubmissionWithError:(MMVKError *)error {
    if(_prepared)return MMVK_OK;
    uint64_t previous=[_fence waitValue];
    MMVKResult failed=[_fence failureForValue:previous error:error];if(failed)return failed;
    if(_update){if(![_fence reserveUpdate:&_value])
        return sharedError(error,MMVK_ERROR_STATE,0,"Fence submission generation overflow");}
    else _value=previous;
    _prepared=YES;return MMVK_OK;
}
- (void)failUnsubmittedWithResult:(MMVKResult)result error:(const MMVKError *)error {
    if(_prepared&&_update)[_fence failUnsubmittedValue:_value result:result error:error];
}
- (MaliMetalFence *)fence {return _fence;}
- (uint64_t)value {return _value;}
- (BOOL)isUpdate {return _update;}
@end

static pthread_mutex_t MaliMetalFenceSubmissionLock=PTHREAD_MUTEX_INITIALIZER;
@implementation MaliMetalFenceSubmission
- (void)dealloc {
    MMVKError abandoned={.code=MMVK_ERROR_STATE};
    strcpy(abandoned.message,"Fence reservation owner retired before native submission");
    [self failUnsubmittedWithResult:MMVK_ERROR_STATE error:&abandoned];
    for(NSUInteger i=0;i<_count;i++) [_commands[i] release];
    free(_commands);[super dealloc];
}
- (MaliMetalFenceCommand *)addFence:(MaliMetalFence *)fence update:(BOOL)update {
    if(_prepared) return nil;
    MaliMetalFenceCommand *command=[[MaliMetalFenceCommand alloc] initWithFence:fence update:update];
    if(!command) return nil;
    MaliMetalFenceCommand **commands=realloc(_commands,(_count+1)*sizeof(*commands));
    if(!commands) {[command release];return nil;}
    _commands=commands;_commands[_count++]=command;return command;
}
- (BOOL)prepareForSubmission {
    return [self prepareForSubmissionWithError:NULL]==MMVK_OK;
}
- (MMVKResult)prepareForSubmissionWithError:(MMVKError *)error {
    pthread_mutex_lock(&MaliMetalFenceSubmissionLock);
    MMVKResult result=MMVK_OK;
    if(!_prepared) for(NSUInteger i=0;i<_count;i++)
        if((result=[_commands[i] prepareForSubmissionWithError:error])!=MMVK_OK)break;
    if(result==MMVK_OK)_prepared=YES;
    else [self failUnsubmittedWithResult:result error:error];
    pthread_mutex_unlock(&MaliMetalFenceSubmissionLock);return result;
}
- (void)failUnsubmittedWithResult:(MMVKResult)result error:(const MMVKError *)error {
    for(NSUInteger i=0;i<_count;i++)[_commands[i] failUnsubmittedWithResult:result error:error];
}
@end

@implementation MaliMetalDevice (Synchronization)
- (id<MTLEvent>)newEvent { return [[MaliMetalEvent alloc] initWithDevice:self]; }
- (id<MTLFence>)newFence { return [[MaliMetalFence alloc] initWithDevice:self]; }
- (id)newSharedEvent {
    if(!MaliMetalSharedEventTransportReady())return nil;
    id event=[super newSharedEvent];
    if(event&&!sharedEventValid(event)){[event release];return nil;}
    return event;
}
- (id)newSharedEventWithHandle:(id)handle {
    Class cls=objc_getClass("MTLSharedEventHandle");
    if(!MaliMetalSharedEventTransportReady()||!cls||!handle||![handle isKindOfClass:cls]||
       !sharedMapForPort([handle eventPort],NULL))return nil;
    id event=[super newSharedEventWithHandle:handle];
    if(event&&!sharedEventValid(event)){[event release];return nil;}
    return event;
}
@end

@implementation MaliMetalCommandBuffer (Synchronization)
- (void)encodeWaitForEvent:(id<MTLEvent>)event value:(uint64_t)value {
    [self appendSynchronizationWithEvent:(id)event value:value signal:NO];
}
- (void)encodeSignalEvent:(id<MTLEvent>)event value:(uint64_t)value {
    [self appendSynchronizationWithEvent:(id)event value:value signal:YES];
}
@end

@implementation MaliMetalEncoderSynchronization
- (instancetype)initWithBuffer:(MaliMetalCommandBuffer *)buffer {
    self=[super init];if(!self) return nil;
    _commandBuffer=(void *)buffer;
    _waitInsertion=[buffer synchronizationOperationCount];return self;
}
- (void)dealloc {
    for(NSUInteger i=0;i<_updateCount;i++) [_updates[i] release];
    free(_updates);[super dealloc];
}
- (BOOL)validateFence:(id)fence {
    MaliMetalCommandBuffer *buffer=(id)_commandBuffer;
    if(_ended||!fence||![fence isKindOfClass:[MaliMetalFence class]]||
       [fence device]!=[buffer device]) {
        [NSException raise:@"NSInvalidArgumentException" format:@"Invalid fence, device or ended encoder"];
        return NO;
    }
    return YES;
}
- (void)waitForFence:(id<MTLFence>)fence {
    if(![self validateFence:(id)fence]) return;
    for(NSUInteger i=0;i<_updateCount;i++) if(_updates[i]==(id)fence) {
        [NSException raise:@"NSInvalidArgumentException" format:@"Cannot wait after updating a fence in one encoder"];
        return;
    }
    [(MaliMetalCommandBuffer *)_commandBuffer insertFence:(id)fence update:NO
        atOperationIndex:_waitInsertion++];
}
- (void)updateFence:(id<MTLFence>)fence {
    if(![self validateFence:(id)fence]) return;
    for(NSUInteger i=0;i<_updateCount;i++) if(_updates[i]==(id)fence) return;
    MaliMetalFence **updates=realloc(_updates,(_updateCount+1)*sizeof(*updates));
    if(!updates) {
        [(MaliMetalCommandBuffer *)_commandBuffer recordBackendResult:MMVK_ERROR_MEMORY error:NULL];
        return;
    }
    _updates=updates;_updates[_updateCount++]=[(id)fence retain];
}
- (void)endEncoding {
    if(_ended) {
        [NSException raise:@"NSInternalInconsistencyException" format:@"Fence scope has ended"];
        return;
    }
    _ended=YES;
    MaliMetalCommandBuffer *buffer=(id)_commandBuffer;
    for(NSUInteger i=0;i<_updateCount;i++)
        [buffer insertFence:_updates[i] update:YES atOperationIndex:[buffer synchronizationOperationCount]];
}
@end

static void notifyAcceptedPrefix(MaliMetalDevice *device,MMVKSyncStream *stream,void (^didSubmit)(void)) {
    if(didSubmit&&mmvk_sync_stream_has_accepted_submissions(stream)){
        [device unlockExecution];didSubmit();[device lockExecution];
    }
}
MMVKResult MaliMetalSynchronizationBoundary(MaliMetalDevice *device, MMVKSyncStream **stream,
    MMVKCommands **commands, MaliMetalEvent *event, uint64_t value, BOOL signal,
    void (^didSubmit)(void),MMVKError *error)
{
    if(!device||!stream||!commands||!*commands||!event||
       ![event isKindOfClass:[MaliMetalEvent class]]||[event device]!=(id)device) {
        if(error) {memset(error,0,sizeof(*error));error->code=MMVK_ERROR_ARGUMENT;
            strcpy(error->message,"Invalid synchronization event or device");}
        return MMVK_ERROR_ARGUMENT;
    }
    MMVKResult result=MMVK_OK;
    if(!*stream) result=mmvk_sync_stream_create([device backendDevice],stream,error);
    if(result) return result;
    BOOL fenceSignal=signal&&[event isKindOfClass:[MaliMetalFence class]];
    if(fenceSignal){
        result=[(MaliMetalFence *)event failureForValue:value error:error];if(result)return result;
    }
    /* A fence update may wait for an earlier generation. First submit its
     * preceding work without the fence signal, so Scheduled can launch the
     * producer before this worker blocks on that predecessor. */
    result=mmvk_sync_stream_boundary(*stream,commands,signal&&!fenceSignal?[event maliEvent]:NULL,value,error);
    notifyAcceptedPrefix(device,*stream,didSubmit);
    if(result)return result;
    if(mmvk_device_is_quarantined([device backendDevice]))
        return sharedError(error,MMVK_ERROR_STATE,0,"adapter device is quarantined after prefix submission");
    if(fenceSignal){
        if(!value)return sharedError(error,MMVK_ERROR_ARGUMENT,0,"Fence updates require a reserved generation");
        result=[(MaliMetalFence *)event failureForValue:value error:error];if(result)return result;
        result=MaliMetalWaitForEvent(device,event,value-1,error);if(result)return result;
        result=[(MaliMetalFence *)event failureForValue:value error:error];if(result)return result;
        result=mmvk_sync_stream_boundary(*stream,commands,[event maliEvent],value,error);
        notifyAcceptedPrefix(device,*stream,didSubmit);
    }
    if(result||signal) return result;
    /* The backend has one native queue. Waiting on a future timeline value in
     * QueueSubmit would block a later producer on that queue. Delay the CPU
     * submission instead, allowing another Metal queue to submit its producer.
     * The global execution lock must not cover this wait. */
    result=MaliMetalWaitForEvent(device,event,value,error);
    if(result) return result;
    /* Other queues may have changed image generations while this wait ran.
     * The fresh segment has not recorded resources yet. */
    return mmvk_commands_full_barrier(*commands,error);
}

MMVKResult MaliMetalWaitForEvent(MaliMetalDevice *device, MaliMetalEvent *event,
                               uint64_t value, MMVKError *error)
{
    [device unlockExecution];
    MMVKResult result;
    do {
        if(mmvk_device_is_quarantined([device backendDevice])){
            result=sharedError(error,MMVK_ERROR_STATE,0,"adapter device is quarantined; dependency completion is unknown");break;
        }
        result=[event isKindOfClass:[MaliMetalFence class]]?
            [(MaliMetalFence *)event waitForValue:value timeout:1000000000ull error:error]:
            mmvk_event_wait([event maliEvent],value,1000000000ull,error);
        if(result>=0&&mmvk_device_is_quarantined([device backendDevice]))
            result=sharedError(error,MMVK_ERROR_STATE,0,"adapter device became quarantined during dependency wait");
    } while(result==MMVK_TIMEOUT||result==MMVK_NOT_READY);
    [device lockExecution];
    if(result==MMVK_OK&&mmvk_device_is_quarantined([device backendDevice]))
        result=sharedError(error,MMVK_ERROR_STATE,0,"adapter device became quarantined before dependency recording resumed");
    return result;
}

typedef struct {id resource;uint64_t generation,published;} MMExecutedWrite;
/* IOSurface publication always delegates to the allocation owner, including
 * for nested views. Keep one generation for that actual root, while the queue
 * continues retaining every frontend resource used by encoded operations. */
static MMVKImage *publicationImageRoot(id resource) {
    if(![resource respondsToSelector:@selector(maliReadbackImage:)]||
       ![resource respondsToSelector:@selector(maliImage)])return NULL;
    return mmvk_image_root([resource maliImage]);
}
static BOOL samePublicationResource(id first,id second) {
    if(first==second)return YES;
    MMVKImage *root=publicationImageRoot(first);
    return root&&root==publicationImageRoot(second);
}
@implementation MaliMetalExecutedResources
- (void)dealloc {
    MMExecutedWrite *writes=_writes;
    for(NSUInteger i=0;i<_writeCount;i++)[writes[i].resource release];
    for(NSUInteger i=0;i<_completionCount;i++)[_completions[i] release];
    free(_writes);free(_completions);[super dealloc];
}
- (MMVKResult)noteWrite:(id)resource error:(MMVKError *)error {
    if(!resource)return sharedError(error,MMVK_ERROR_ARGUMENT,0,"Missing executed resource write");
    if(_generation==UINT64_MAX)return sharedError(error,MMVK_ERROR_STATE,0,"Executed resource generation overflow");
    MMExecutedWrite *writes=_writes;
    for(NSUInteger i=0;i<_writeCount;i++)if(samePublicationResource(writes[i].resource,resource)){
        writes[i].generation=++_generation;return MMVK_OK;
    }
    if(_writeCount==SIZE_MAX/sizeof(*writes))return sharedError(error,MMVK_ERROR_MEMORY,0,"Executed resource capacity overflow");
    writes=realloc(writes,(_writeCount+1)*sizeof(*writes));
    if(!writes)return sharedError(error,MMVK_ERROR_MEMORY,0,"Executed resource allocation failed");
    _writes=writes;writes[_writeCount++]=(MMExecutedWrite){[resource retain],++_generation,0};return MMVK_OK;
}
- (MMVKResult)noteCompletion:(id)resource error:(MMVKError *)error {
    if(!resource||![resource respondsToSelector:@selector(maliCompleteComputeExecution:)])
        return sharedError(error,MMVK_ERROR_ARGUMENT,0,"Invalid executed completion record");
    for(NSUInteger i=0;i<_completionCount;i++)if(_completions[i]==resource)return MMVK_OK;
    if(_completionCount==SIZE_MAX/sizeof(*_completions))return sharedError(error,MMVK_ERROR_MEMORY,0,"Completion capacity overflow");
    id *records=realloc(_completions,(_completionCount+1)*sizeof(*records));
    if(!records)return sharedError(error,MMVK_ERROR_MEMORY,0,"Completion record allocation failed");
    _completions=records;_completions[_completionCount++]=[resource retain];return MMVK_OK;
}
- (MMVKResult)publishExecutedPrefix:(MMVKError *)error {
    for(NSUInteger i=0;i<_completionCount;i++){
        MMVKResult r=[_completions[i] maliCompleteComputeExecution:error];if(r)return r;
    }
    MMExecutedWrite *writes=_writes;
    for(NSUInteger i=0;i<_writeCount;i++){
        if(writes[i].generation==writes[i].published)continue;
        id resource=writes[i].resource;
        if([resource respondsToSelector:@selector(maliBuffer)]){
            if([resource respondsToSelector:@selector(storageMode)]&&[resource storageMode]!=MTLStorageModeShared)continue;
            MMVKBuffer *buffer=[resource maliBuffer];
            if(!buffer)return sharedError(error,MMVK_ERROR_STATE,0,"Executed buffer lost native allocation");
            MMVKResult r=mmvk_buffer_invalidate(buffer,0,mmvk_buffer_size(buffer),error);if(r)return r;
        }else if([resource respondsToSelector:@selector(maliReadbackImage:)]){
            if(![resource maliReadbackImage:error])return error&&error->code?error->code:
                sharedError(error,MMVK_ERROR_IO,0,"Executed texture publication failed");
        }else return sharedError(error,MMVK_ERROR_UNSUPPORTED,0,"Executed write has no host publication contract");
    }
    for(NSUInteger i=0;i<_writeCount;i++)writes[i].published=writes[i].generation;
    __atomic_thread_fence(__ATOMIC_RELEASE);return MMVK_OK;
}
- (MMVKResult)refreshConsumerResources:(id const *)resources count:(NSUInteger)count error:(MMVKError *)error {
    if(count&&!resources)return sharedError(error,MMVK_ERROR_ARGUMENT,0,"Missing consumer resource list");
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    for(NSUInteger i=0;i<count;i++){
        id resource=resources[i];
        if([resource respondsToSelector:@selector(maliBuffer)]&&
           [resource respondsToSelector:@selector(storageMode)]&&[resource storageMode]==MTLStorageModeShared){
            MMVKBuffer *buffer=[resource maliBuffer];
            if(buffer){MMVKResult r=mmvk_buffer_flush(buffer,0,mmvk_buffer_size(buffer),error);if(r)return r;}
        }else if([resource respondsToSelector:@selector(iosurface)]&&[resource iosurface]&&
                 [resource respondsToSelector:@selector(maliPrepareImage:error:)]){
            BOOL prepared=NO;
            for(NSUInteger previous=0;previous<i;previous++)
                if(samePublicationResource(resources[previous],resource)){prepared=YES;break;}
            if(prepared)continue;
            if(![resource maliPrepareImage:YES error:error])return error&&error->code?error->code:
                sharedError(error,MMVK_ERROR_IO,0,"Shared consumer IOSurface refresh failed");
        }
    }
    return MMVK_OK;
}
@end

MMVKResult MaliMetalSharedEventBoundary(MaliMetalDevice *device,MMVKSyncStream **stream,
    MMVKCommands **commands,id event,uint64_t value,BOOL signal,MaliMetalExecutedResources *visibility,
    void (^didSubmit)(void),MMVKResult (^refresh)(MMVKError *),MMVKError *error)
{
    if(!device||!stream||!commands||!*commands||!visibility||!sharedEventValid(event))
        return sharedError(error,MMVK_ERROR_ARGUMENT,0,"Invalid shared-event command boundary");
    MMVKResult result=MMVK_OK;
    if(!*stream)result=mmvk_sync_stream_create([device backendDevice],stream,error);
    if(result)return result;
    result=mmvk_sync_stream_boundary(*stream,commands,NULL,0,error);
    notifyAcceptedPrefix(device,*stream,didSubmit);
    if(result)return result;
    /* Actual submitted GPU segments retire before exposing CPU-visible data. */
    do {
        result=mmvk_sync_stream_drain(*stream,1000000000ull,error);
        /* Negative wait outcomes retain/quarantine the stream. DEVICE_LOST
         * may be an IOKit transport failure and is not DMA retirement proof. */
    }while(result==MMVK_TIMEOUT||result==MMVK_NOT_READY);
    if(result)return result;
    result=[visibility publishExecutedPrefix:error];if(result)return result;
    [device unlockExecution];
    if(signal)result=MaliMetalSharedEventPublish(event,value,error);
    else do{
        if(mmvk_device_is_quarantined([device backendDevice])){
            result=sharedError(error,MMVK_ERROR_STATE,0,"adapter device is quarantined during shared dependency wait");break;
        }
        result=MaliMetalSharedEventWait(event,value,1000000000ull,error);
        if(result>=0&&mmvk_device_is_quarantined([device backendDevice]))
            result=sharedError(error,MMVK_ERROR_STATE,0,"adapter device became quarantined during shared dependency wait");
    }while(result==MMVK_TIMEOUT||result==MMVK_NOT_READY);
    [device lockExecution];
    if(result)return result;
    if(mmvk_device_is_quarantined([device backendDevice]))
        return sharedError(error,MMVK_ERROR_STATE,0,"adapter device is quarantined before shared dependency recording resumed");
    if(!signal&&refresh){result=refresh(error);if(result)return result;}
    return mmvk_commands_full_barrier(*commands,error);
}
