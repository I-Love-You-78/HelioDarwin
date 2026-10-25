/* SPDX-License-Identifier: MIT
 * SweetMali XNU transport for Mesa 5a07217f + PanVK-kbase RC3 JM-v9.
 *
 * The kbase_* entry names below are the RC3 JM queue's internal interface.
 * They do not issue Linux kbase ioctls. All memory and JM operations cross the
 * versioned IOKit ABI. ABI v1 has one active job and no fence history, so this
 * initial backend serializes each actual hardware completion before returning
 * its atom token. Vulkan's CPU sync objects consume the cached terminal result.
 */
#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include "MaliKmodDarwin.h"
#include "kbase_kmod.h"
#include "pan_kmod_backend.h"
#include "util/os_time.h"

extern const struct pan_kmod_ops kbase_kmod_ops;
static _Thread_local uint32_t xnu_connection;

__attribute__((visibility("default"))) int32_t
panvk_xnu_bind_connection(uint32_t connection,uint32_t version,const char *mesa_pin);
uint32_t panvk_xnu_get_connection(void);

int32_t
panvk_xnu_bind_connection(uint32_t connection,uint32_t version,const char *mesa_pin)
{
   if (version!=MMK_ABI_VERSION || !mesa_pin ||
       strcmp(mesa_pin,"5a07217f034b3e50d8c7c7794f97a2df1742613b")) return EINVAL;
   MMKQuery q;
   int32_t kr=mmk_darwin_query(connection,&q);
   if (kr) return kr;
   uint32_t required=MMK_CAP_BO_ALLOC|MMK_CAP_JM_SUBMIT|MMK_CAP_EXPLICIT_SYNC;
   if(q.gpu_id!=0x90930010u || (q.capabilities&required)!=required) return ENODEV;
   xnu_connection=connection;
   return 0;
}

uint32_t panvk_xnu_get_connection(void) { return xnu_connection; }

struct xnu_bo {
   struct pan_kmod_bo base;
   MMKDarwinBO memory;
};
struct xnu_dev {
   struct pan_kmod_dev base;
   uint32_t connection;
   MMKQuery query;
   simple_mtx_t lock;
   struct xnu_bo *bos[MMK_MAX_BOS];
   unsigned bo_count;
   bool lost,owns_connection;
   uint64_t next_atom,completed_success;
};
static struct xnu_dev *xd(struct pan_kmod_dev *d)
{ return container_of(d,struct xnu_dev,base); }
static struct xnu_bo *xb(struct pan_kmod_bo *b)
{ return container_of(b,struct xnu_bo,base); }

static struct pan_kmod_dev *
xnu_create(int connection,uint32_t flags,const struct pan_kmod_driver *driver,
           const struct pan_kmod_allocator *allocator)
{
   struct xnu_dev *d=pan_kmod_alloc(allocator,sizeof(*d));
   if(!d)return NULL;
   d->connection=(uint32_t)connection;
   if(mmk_darwin_query(d->connection,&d->query) || d->query.gpu_id!=0x90930010u ||
      !d->query.thread_max_threads || !d->query.thread_max_workgroup_size ||
      !d->query.cpu_page_size || !d->query.shader_present){
      pan_kmod_free(allocator,d);errno=ENODEV;return NULL;
   }
   /* Mach ports must never reach close(2). Keep cache maintenance on IOKit,
    * which owns the BO mappings and their coherency policy. */
   flags=(flags&~PAN_KMOD_DEV_FLAG_OWNS_FD)|PAN_KMOD_DEV_FLAG_MMAP_SYNC_THROUGH_KERNEL;
   pan_kmod_dev_init(&d->base,-1,flags,driver,&kbase_kmod_ops,allocator);
   simple_mtx_init(&d->lock,mtx_plain);
   struct pan_kmod_dev_props *p=&d->base.props;
   const MMKQuery *q=&d->query;
   p->gpu_id=q->gpu_id;p->gpu_variant=q->core_features&0xff;
   p->shader_present=q->shader_present;p->tiler_features=q->tiler_features;
   p->mem_features=q->mem_features;p->mmu_features=q->mmu_features;
   memcpy(p->texture_features,q->texture_features,sizeof(p->texture_features));
   p->l2_features=q->l2_features;p->afbc_features=q->afbc_features;
   p->max_threads_per_core=q->thread_max_threads;
   p->max_threads_per_wg=q->thread_max_workgroup_size;
   p->max_tasks_per_core=MAX2(q->thread_features>>24,1);
   p->num_registers_per_core=q->thread_features&0xffff;
   /* This is Mesa's architectural v9 default when the hardware field is zero,
    * not a reported or assumed optional feature. */
   if(!p->num_registers_per_core)p->num_registers_per_core=p->max_threads_per_core*32;
   p->max_tls_instance_per_core=q->thread_tls_alloc?q->thread_tls_alloc:q->thread_max_threads;
   p->is_io_coherent=false;
   p->pgsize_bitmap=PAN_PGSIZE_4K;
   /* The initial all-resident submission path must not invalidate an unrelated
    * application's dirty cached mapping. Expose uncached HOST_VISIBLE memory
    * only until exact writable resource tracking is implemented. */
   p->supported_bo_flags=PAN_KMOD_BO_FLAG_EXECUTABLE|PAN_KMOD_BO_FLAG_NO_MMAP;
   p->allowed_group_priorities_mask=PAN_KMOD_GROUP_ALLOW_PRIORITY_MEDIUM;
   p->gpu_can_query_timestamp=false;p->timestamp_device_coherent=false;
   return &d->base;
}

struct pan_kmod_dev *
pan_kmod_xnu_dev_clone(struct pan_kmod_dev *physical,
                      const struct pan_kmod_allocator *allocator)
{
   uint32_t connection=0;
   int32_t kr=mmk_darwin_open_peer(xd(physical)->connection,&connection);
   if(kr){
      mesa_loge("SweetMali: logical device connection failed %#x",kr);
      errno=ENODEV;
      return NULL;
   }
   struct pan_kmod_dev *dev=xnu_create((int)connection,physical->flags,
                                      &physical->driver,allocator);
   if(!dev){
      int saved_errno=errno;
      mmk_darwin_close(connection);
      errno=saved_errno;
      return NULL;
   }
   xd(dev)->owns_connection=true;
   return dev;
}

static void xnu_destroy(struct pan_kmod_dev *base)
{
   struct xnu_dev *d=xd(base);
   /* Vulkan destroys every BO before its physical device. Do not tear down a
    * live GPU mapping behind outstanding references if a caller violates it. */
   if(d->bo_count){mesa_loge("SweetMali: device destroyed with %u live BOs",d->bo_count);return;}
   simple_mtx_destroy(&d->lock);pan_kmod_dev_cleanup(base);
   if(d->owns_connection){
      int32_t kr=mmk_darwin_close(d->connection);
      if(kr)mesa_loge("SweetMali: logical device close failed %#x",kr);
   }
   pan_kmod_dev_free(base,d);
}
static struct pan_kmod_bo *
xnu_alloc(struct pan_kmod_dev *base,struct pan_kmod_vm *vm,uint64_t size,uint32_t flags)
{
   struct xnu_dev *d=xd(base);
   /* Grow-on-fault is implemented as full physical commitment on this ABI.
    * Mesa may request its normal large tiler heap; allocation failure is real. */
   uint32_t allowed=base->props.supported_bo_flags|PAN_KMOD_BO_FLAG_ALLOC_ON_FAULT;
   if(!size||size>d->query.max_bo_size||(flags&~allowed)){
      mesa_loge("SweetMali: BO request unsupported size=%"PRIu64" flags=%#x allowed=%#x max=%"PRIu64,
                size,flags,allowed,d->query.max_bo_size);
      errno=ENOTSUP;return NULL;
   }
   struct xnu_bo *b=pan_kmod_dev_alloc(base,sizeof(*b));if(!b)return NULL;
   uint32_t bits=MMK_BO_READ|MMK_BO_WRITE;
   if(flags&PAN_KMOD_BO_FLAG_EXECUTABLE)bits|=MMK_BO_EXEC;
   simple_mtx_lock(&d->lock);
   unsigned live=d->bo_count;
   unsigned limit=MIN2(d->query.max_submit_bos,MMK_MAX_SUBMIT_BOS);
   const char *stage="kernel-allocation-or-map";
   int32_t kr;
   if(d->lost){
      stage="device-lost";kr=EIO;
   }else if(live>=limit){
      stage="live-BO-limit";kr=ENOSPC;
   }else{
      kr=mmk_darwin_alloc(d->connection,size,bits,&b->memory);
   }
   if(!kr){pan_kmod_bo_init(&b->base,base,vm,b->memory.info.size,flags,b->memory.info.handle);d->bos[d->bo_count++]=b;}
   simple_mtx_unlock(&d->lock);
   if(kr){
      mesa_loge("SweetMali: BO allocation failed stage=%s size=%"PRIu64" flags=%#x live=%u limit=%u result=%#x",
                stage,size,flags,live,limit,(uint32_t)kr);
      pan_kmod_dev_free(base,b);errno=kr==ENOSPC?ENOSPC:ENOMEM;return NULL;
   }
   return &b->base;
}
static void xnu_free(struct pan_kmod_bo *base)
{
   struct xnu_bo *b=xb(base);struct xnu_dev *d=xd(base->dev);
   simple_mtx_lock(&d->lock);
   int32_t kr=mmk_darwin_release(&b->memory);
   if(kr){simple_mtx_unlock(&d->lock);mesa_loge("SweetMali: BO release failed %#x; retaining mapping",kr);return;}
   for(unsigned n=0;n<d->bo_count;n++)if(d->bos[n]==b){d->bos[n]=d->bos[--d->bo_count];break;}
   simple_mtx_unlock(&d->lock);
   pan_kmod_bo_cleanup(base);
   pan_kmod_dev_free(base->dev,b);
}
static struct pan_kmod_bo *xnu_import(struct pan_kmod_dev *d,uint32_t handle,uint64_t size)
{(void)d;(void)handle;(void)size;errno=ENOTSUP;return NULL;}
static off_t xnu_mmap_offset(struct pan_kmod_bo *b)
{(void)b;errno=ENOTSUP;return -1;}
static void *xnu_mmap(struct pan_kmod_bo *b,int prot,int flags,void *host)
{(void)prot;(void)flags;if(host&&host!=xb(b)->memory.mapping){errno=ENOTSUP;return MAP_FAILED;}return xb(b)->memory.mapping;}
static int xnu_munmap(struct pan_kmod_bo *b,void *host,size_t size)
{if(host!=xb(b)->memory.mapping||size>b->size){errno=EINVAL;return -1;}return 0;}
static int xnu_flush(struct pan_kmod_dev *d)
{
   util_dynarray_foreach(&d->pending_bo_syncs.array,struct pan_kmod_deferred_bo_sync,sync){
      struct xnu_bo *b=xb(sync->bo);
      if(mmk_darwin_sync(&b->memory,sync->start,sync->size,MMK_SYNC_CPU_TO_GPU)){xd(d)->lost=true;errno=EIO;return -1;}
      if(sync->type==PAN_KMOD_BO_SYNC_CPU_CACHE_FLUSH_AND_INVALIDATE &&
         mmk_darwin_sync(&b->memory,sync->start,sync->size,MMK_SYNC_GPU_TO_CPU)){xd(d)->lost=true;errno=EIO;return -1;}
   }
   return 0;
}
static bool xnu_bo_wait(struct pan_kmod_bo *b,int64_t ns,bool read_only)
{(void)ns;(void)read_only;return !xd(b->dev)->lost;}
static struct pan_kmod_vm *xnu_vm_create(struct pan_kmod_dev *d,uint32_t flags,uint64_t start,uint64_t size)
{
   (void)start;(void)size;
   if(!(flags&PAN_KMOD_VM_FLAG_AUTO_VA)){errno=ENOTSUP;return NULL;}
   struct pan_kmod_vm *vm=pan_kmod_dev_alloc(d,sizeof(*vm));
   if(vm)pan_kmod_vm_init(vm,d,0,flags);return vm;
}
static void xnu_vm_destroy(struct pan_kmod_vm *vm)
{pan_kmod_vm_cleanup(vm);pan_kmod_dev_free(vm->dev,vm);}
static int xnu_vm_bind(struct pan_kmod_vm *vm,enum pan_kmod_vm_op_mode mode,struct pan_kmod_vm_op *ops,uint32_t count)
{
   if(mode==PAN_KMOD_VM_OP_MODE_ASYNC){errno=ENOTSUP;return -1;}
   for(uint32_t n=0;n<count;n++){
      struct pan_kmod_vm_op *op=&ops[n];
      if(pan_kmod_vm_op_check(vm,mode,op)||op->flags||op->signal.count||op->wait.count){errno=ENOTSUP;return -1;}
      if(op->type==PAN_KMOD_VM_OP_TYPE_MAP){
         if(!op->map.bo||op->map.bo->dev!=vm->dev||op->map.bo_offset<0||
            (uint64_t)op->map.bo_offset>op->map.bo->size||op->va.size>op->map.bo->size-(uint64_t)op->map.bo_offset){errno=EINVAL;return -1;}
         uint64_t va=xb(op->map.bo)->memory.info.gpu_va+op->map.bo_offset;
         if(op->va.start!=PAN_KMOD_VM_MAP_AUTO_VA&&op->va.start!=va){errno=ENOTSUP;return -1;}
         op->va.start=va;
      }else if(op->type!=PAN_KMOD_VM_OP_TYPE_UNMAP&&op->type!=PAN_KMOD_VM_OP_TYPE_SYNC_ONLY){errno=ENOTSUP;return -1;}
   }
   return 0;
}
static enum pan_kmod_vm_state xnu_vm_state(struct pan_kmod_vm *vm)
{return xd(vm->dev)->lost?PAN_KMOD_VM_FAULTY:PAN_KMOD_VM_USABLE;}
static uint64_t xnu_timestamp(const struct pan_kmod_dev *d)
{(void)d;errno=ENOTSUP;return 0;}

uint64_t kbase_kmod_job_submit_dep(struct pan_kmod_dev *base,uint64_t chain,uint32_t requirements,
    struct pan_kmod_bo **bos,uint32_t count,uint64_t dep0,uint8_t type0,uint64_t dep1,uint8_t type1,bool no_waiter)
{
   (void)bos;(void)count;(void)type0;(void)type1;(void)no_waiter;
   struct xnu_dev *d=xd(base);
   simple_mtx_lock(&d->lock);
   if(d->lost)goto fail;
   if(dep0>d->completed_success||dep1>d->completed_success||d->next_atom==UINT64_MAX)goto fail;
   /* The RC3 list contains transient batch descriptors but not every shader or
    * descriptor-referenced resource. Submit every live BO, bounded at alloc,
    * so cache maintenance and kernel lifetime checks cover all GPU references. */
   uint32_t handles[MMK_MAX_SUBMIT_BOS];
   for(unsigned n=0;n<d->bo_count;n++)handles[n]=d->bos[n]->memory.info.handle;
   uint32_t slot=(requirements&BASE_JD_REQ_FS)?0:1;
   uint64_t sequence=0;
   uint64_t deadline=os_time_get_nano()+MMK_MAX_WAIT_NS;
   int32_t kr;
   do{
      kr=mmk_darwin_submit(d->connection,chain,slot,handles,d->bo_count,0,&sequence);
      if(kr!=(int32_t)0xe00002d5u)break; /* kIOReturnBusy */
      os_time_sleep(1000);
   }while(os_time_get_nano()<deadline);
   if(kr){mesa_loge("SweetMali: JM submit failed %#x",kr);goto fail;}
   MMKWaitResult result={0};
   do{
      kr=mmk_darwin_wait(d->connection,sequence,MMK_MAX_WAIT_NS,&result);
   }while(!kr&&result.state==MMK_JOB_PENDING);
   uint64_t atom=++d->next_atom;
   if(!kr&&result.state==MMK_JOB_COMPLETE)d->completed_success=atom;
   else{
      mesa_loge("SweetMali: job %"PRIu64" failed kr=%#x state=%u JS=%#x AS=%#x addr=%"PRIx64,
         sequence,kr,result.state,result.exception,result.fault_status,result.fault_address);
      d->lost=true;
   }
   simple_mtx_unlock(&d->lock);return atom;
fail:d->lost=true;simple_mtx_unlock(&d->lock);errno=EIO;return 0;
}
uint64_t kbase_kmod_job_submit(struct pan_kmod_dev *d,uint64_t jc,uint32_t req,struct pan_kmod_bo **bos,uint32_t nbo,struct base_external_resource *ext,uint32_t ne)
{if(ext||ne){errno=ENOTSUP;return 0;}return kbase_kmod_job_submit_dep(d,jc,req,bos,nbo,0,0,0,0,false);}
bool kbase_kmod_job_submit_retry(struct pan_kmod_dev *d,uint64_t jc,uint32_t req,struct pan_kmod_bo **bos,uint32_t nbo,struct base_external_resource *ext,uint32_t ne,int attempts)
{(void)attempts;uint64_t atom=kbase_kmod_job_submit(d,jc,req,bos,nbo,ext,ne);bool ok=atom&&kbase_kmod_wait_atom(d,atom,-1);if(atom)kbase_kmod_atom_release(d,atom);return ok;}
bool kbase_kmod_wait_atom(struct pan_kmod_dev *base,uint64_t atom,int64_t ns)
{
   (void)ns;struct xnu_dev *d=xd(base);if(!atom){errno=EINVAL;return false;}
   /* All issued jobs run synchronously in increasing token order. A failure
    * latches lost and prevents later submissions, so this high-water mark is
    * permanent evidence of each successful completion, including old fences. */
   simple_mtx_lock(&d->lock);bool ok=atom<=d->completed_success;
   simple_mtx_unlock(&d->lock);if(!ok)errno=EIO;return ok;
}
void kbase_kmod_atom_release(struct pan_kmod_dev *base,uint64_t atom)
{(void)base;(void)atom;/* Synchronous submissions hold no in-flight atom storage. */}
void kbase_kmod_jm_clean_imports(struct pan_kmod_dev *d)
{(void)d;/* Every native/import BO is flushed by the kernel before submit. */}
struct pan_kmod_bo *kbase_kmod_bo_import_host(struct pan_kmod_dev *d,void *p,uint64_t n)
{(void)d;(void)p;(void)n;errno=ENOTSUP;return NULL;}
uint64_t kbase_kmod_page_size(void){return 4096;}
bool kbase_kmod_supports_dmabuf(const struct pan_kmod_dev *d){(void)d;return false;}
bool kbase_kmod_csf_has_error(const struct pan_kmod_dev *d)
{return container_of(d,const struct xnu_dev,base)->lost;}
int kbase_kmod_csf_export_cqs64_fence(struct pan_kmod_dev *d,const uint64_t *a,const uint64_t *v,unsigned n)
{(void)d;(void)a;(void)v;(void)n;errno=ENOTSUP;return -1;}
const struct drm_panthor_csif_info *kbase_kmod_get_csif_props(const struct pan_kmod_dev *d)
{(void)d;return NULL;}
uint32_t kbase_kmod_get_flush_id(const struct pan_kmod_dev *d){(void)d;return 0;}

const struct pan_kmod_ops kbase_kmod_ops={
   .dev_create=xnu_create,.dev_destroy=xnu_destroy,.bo_alloc=xnu_alloc,.bo_free=xnu_free,
   .bo_import=xnu_import,.bo_get_mmap_offset=xnu_mmap_offset,.bo_mmap=xnu_mmap,.bo_munmap=xnu_munmap,
   .flush_bo_map_syncs=xnu_flush,.bo_wait=xnu_bo_wait,.vm_create=xnu_vm_create,.vm_destroy=xnu_vm_destroy,
   .vm_bind=xnu_vm_bind,.vm_query_state=xnu_vm_state,.query_timestamp=xnu_timestamp,
};
