/* SPDX-License-Identifier: MIT */
#include "MaliVulkanBackend.h"
#include "MaliKmodDarwin.h"
#include "../Shader/MaliMetalShaderVulkan.h"
#include "MaliVulkanProfile.h"
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define INSTANCE_FUNCS(X) \
 X(DestroyInstance) X(EnumeratePhysicalDevices) X(GetPhysicalDeviceProperties) \
 X(GetPhysicalDeviceFeatures) X(GetPhysicalDeviceFeatures2) X(GetPhysicalDeviceProperties2) \
 X(GetPhysicalDeviceImageFormatProperties2) X(GetPhysicalDeviceMemoryProperties) \
 X(GetPhysicalDeviceQueueFamilyProperties) X(GetPhysicalDeviceFormatProperties) \
 X(CreateDevice) X(GetDeviceProcAddr)
#define DEVICE_FUNCS(X) \
 X(DestroyDevice) X(DeviceWaitIdle) X(GetDeviceQueue) X(CreateBuffer) \
 X(DestroyBuffer) X(GetBufferMemoryRequirements) X(AllocateMemory) X(FreeMemory) \
 X(BindBufferMemory) X(MapMemory) X(UnmapMemory) X(FlushMappedMemoryRanges) \
 X(InvalidateMappedMemoryRanges) X(CreateImage) X(DestroyImage) \
 X(GetImageMemoryRequirements) X(BindImageMemory) X(CreateImageView) X(DestroyImageView) X(CreateSampler) X(DestroySampler) \
 X(CreateCommandPool) X(DestroyCommandPool) X(AllocateCommandBuffers) \
 X(BeginCommandBuffer) X(EndCommandBuffer) X(CmdPipelineBarrier) X(CmdFillBuffer) \
 X(CmdCopyBuffer) X(CmdClearColorImage) X(CmdCopyBufferToImage) X(CmdCopyImageToBuffer) \
 X(CreateFence) X(DestroyFence) X(GetFenceStatus) X(WaitForFences) X(QueueSubmit) \
 X(CreateShaderModule) X(DestroyShaderModule) X(CreateDescriptorSetLayout) \
 X(DestroyDescriptorSetLayout) X(CreatePipelineLayout) X(DestroyPipelineLayout) \
 X(CreateComputePipelines) X(CreateGraphicsPipelines) X(DestroyPipeline) \
 X(CreateDescriptorPool) X(DestroyDescriptorPool) X(AllocateDescriptorSets) \
 X(UpdateDescriptorSets) X(CmdBindDescriptorSets) X(CmdBindPipeline) X(CmdDispatch) \
 X(CmdPushConstants) X(CreateRenderPass) X(DestroyRenderPass) X(CreateFramebuffer) \
 X(DestroyFramebuffer) X(CmdBeginRenderPass) X(CmdEndRenderPass) \
 X(CmdSetViewport) X(CmdSetScissor) X(CmdBindVertexBuffers) X(CmdDraw) X(CmdBindIndexBuffer) X(CmdDrawIndexed)
#define DECLARE(name) PFN_vk##name name;
struct MMVKDevice {
    void *icd;
    VkInstance instance; VkPhysicalDevice physical; VkDevice device; VkQueue queue;
    uint32_t family; MMVKCapabilities caps;
    VkPhysicalDeviceProperties properties;
    VkPhysicalDeviceFeatures enabled_features;
    VkPhysicalDeviceVulkan11Features enabled11;
    VkPhysicalDeviceVulkan12Features enabled12;
    VkPhysicalDeviceVulkan11Properties properties11;
    VkPhysicalDeviceVulkan12Properties properties12;
    char *shader_profile;
    VkPhysicalDeviceMemoryProperties memory;
    pthread_mutex_t queue_lock;
    PFN_vkGetInstanceProcAddr GetInstanceProcAddr;
    INSTANCE_FUNCS(DECLARE) DEVICE_FUNCS(DECLARE)
};
#undef DECLARE
struct MMVKBuffer { MMVKDevice *dev; VkBuffer buffer; VkDeviceMemory memory;
    void *map; uint64_t size,allocation_size; uint32_t usage,coherent; };
struct MMVKImage { MMVKDevice *dev; VkImage image; VkImageView view; VkDeviceMemory memory;
    MMVKImageDescriptor desc; VkImageLayout layout; uint64_t generation,allocation_size; };
struct MMVKSampler { MMVKDevice *dev; VkSampler sampler; MMVKSamplerDescriptor desc; };
struct MMVKPipeline { MMVKDevice *dev; VkPipeline pipeline; VkPipelineLayout layout;
    VkPipelineBindPoint point; VkDescriptorSetLayout sets[4]; uint32_t set_count;
    MMVKBindingLayout bindings[64]; uint32_t binding_count,push_size; VkRenderPass render_pass;
    MMVKFormat format; uint32_t local_size[3]; };
struct MMVKComputeProgram { MMVKDevice *dev; MMVKPipeline *definition;
    VkShaderModule module; uint32_t *words; size_t size; char *entry; };
struct MMVKCommands { MMVKDevice *dev; VkCommandPool pool; VkCommandBuffer cmd;
    VkDescriptorPool descriptors; VkDescriptorSet sets[4]; MMVKPipeline *pipeline;
    VkDescriptorBufferInfo pending_buffers[64]; VkDescriptorImageInfo pending_images[64];
    uint64_t bound_bindings;
    uint32_t submitted,in_render,viewport,scissor,descriptor_snapshots;
    VkFramebuffer framebuffers[16]; VkRenderPass render_passes[16];
    MMVKFormat render_format; MMVKImage *render_target; uint32_t framebuffer_count;
    MMVKBuffer *index_buffer; uint64_t index_offset; uint32_t index_element_size;
    struct { MMVKImage *image; uint64_t generation; } images[64]; uint32_t image_count;
};
struct MMVKFence { MMVKDevice *dev; VkFence fence; };

static MMVKResult errorf(MMVKError *e,MMVKResult code,int32_t native,const char *fmt,...)
{
    if (e) { e->code=code; e->native_code=native;
        va_list ap; va_start(ap,fmt); vsnprintf(e->message,sizeof(e->message),fmt,ap); va_end(ap); }
    return code;
}
static MMVKResult vkresult(MMVKError *e,VkResult r,const char *operation)
{
    MMVKResult code;
    switch (r) {
    case VK_SUCCESS: code=MMVK_OK; break;
    case VK_NOT_READY: code=MMVK_NOT_READY; break;
    case VK_TIMEOUT: code=MMVK_TIMEOUT; break;
    case VK_ERROR_OUT_OF_HOST_MEMORY: case VK_ERROR_OUT_OF_DEVICE_MEMORY: code=MMVK_ERROR_MEMORY; break;
    case VK_ERROR_DEVICE_LOST: code=MMVK_ERROR_DEVICE_LOST; break;
    case VK_ERROR_FEATURE_NOT_PRESENT: case VK_ERROR_FORMAT_NOT_SUPPORTED:
    case VK_ERROR_EXTENSION_NOT_PRESENT: code=MMVK_ERROR_UNSUPPORTED; break;
    case VK_ERROR_INCOMPATIBLE_DRIVER: case VK_ERROR_INITIALIZATION_FAILED: code=MMVK_ERROR_DRIVER_UNAVAILABLE; break;
    default: code=MMVK_ERROR_IO; break;
    }
    return errorf(e,code,r,"%s: VkResult %d",operation,r);
}
const char *mmvk_result_string(MMVKResult r)
{
    switch (r) { case MMVK_OK:return "success";case MMVK_NOT_READY:return "not ready";
    case MMVK_TIMEOUT:return "timeout";case MMVK_ERROR_ARGUMENT:return "invalid argument";
    case MMVK_ERROR_MEMORY:return "out of memory";case MMVK_ERROR_UNSUPPORTED:return "unsupported";
    case MMVK_ERROR_DRIVER_UNAVAILABLE:return "SweetMali PanVK ICD unavailable";
    case MMVK_ERROR_DEVICE_LOST:return "GPU device lost";case MMVK_ERROR_IO:return "driver I/O error";
    case MMVK_ERROR_SHADER:return "invalid shader";case MMVK_ERROR_STATE:return "invalid command state"; }
    return "unknown result";
}
static VkFormat format(MMVKFormat f)
{
    switch(f) {case MMVK_FORMAT_RGBA8_UNORM:return VK_FORMAT_R8G8B8A8_UNORM;
    case MMVK_FORMAT_BGRA8_UNORM:return VK_FORMAT_B8G8R8A8_UNORM;
    case MMVK_FORMAT_RGBA8_SRGB:return VK_FORMAT_R8G8B8A8_SRGB;
    case MMVK_FORMAT_BGRA8_SRGB:return VK_FORMAT_B8G8R8A8_SRGB;default:return VK_FORMAT_UNDEFINED;}
}
static VkShaderStageFlags stages(uint32_t s)
{
    return ((s&MMVK_STAGE_VERTEX)?VK_SHADER_STAGE_VERTEX_BIT:0)|
        ((s&MMVK_STAGE_FRAGMENT)?VK_SHADER_STAGE_FRAGMENT_BIT:0)|
        ((s&MMVK_STAGE_COMPUTE)?VK_SHADER_STAGE_COMPUTE_BIT:0);
}
static int memory_type(MMVKDevice *d,uint32_t bits,VkMemoryPropertyFlags required)
{
    for (uint32_t i=0;i<d->memory.memoryTypeCount;i++)
        if ((bits&(1u<<i)) && (d->memory.memoryTypes[i].propertyFlags&required)==required) return (int)i;
    return -1;
}
MMVKResult mmvk_device_create(uint32_t connection,const char *path,MMVKDevice **out,MMVKError *e)
{
    if (!out || !connection) return errorf(e,MMVK_ERROR_ARGUMENT,0,"device requires an accelerator connection");
    *out=NULL;
    MMKQuery q;
    int32_t kr=mmk_darwin_query(connection,&q);
    if (kr) return errorf(e,MMVK_ERROR_DRIVER_UNAVAILABLE,kr,"kernel ABI %u query failed",MMK_ABI_VERSION);
    uint32_t required=MMK_CAP_BO_ALLOC|MMK_CAP_JM_SUBMIT|MMK_CAP_EXPLICIT_SYNC;
    if (q.gpu_id!=0x90930010u || (q.capabilities&required)!=required)
        return errorf(e,MMVK_ERROR_UNSUPPORTED,0,"GPU %#x or kernel BO/JM/sync capabilities unsupported",q.gpu_id);
    MMVKDevice *d=calloc(1,sizeof(*d));
    if (!d) return errorf(e,MMVK_ERROR_MEMORY,0,"device allocation failed");
    pthread_mutex_init(&d->queue_lock,NULL);
    d->icd=dlopen(path?path:"/System/Library/Extensions/MaliMetal.bundle/libpanvk_sweetmali.dylib",RTLD_NOW|RTLD_LOCAL);
    if (!d->icd) { errorf(e,MMVK_ERROR_DRIVER_UNAVAILABLE,0,"PanVK ICD: %s",dlerror()); goto fail; }
    /* This port-specific entry point verifies the kernel ABI and source pin.
     * An arbitrary Vulkan loader, software renderer, or Android .so cannot pass. */
    int32_t (*bind)(uint32_t,uint32_t,const char *)=dlsym(d->icd,"panvk_xnu_bind_connection");
    if (!bind || (kr=bind(connection,MMK_ABI_VERSION,"5a07217f034b3e50d8c7c7794f97a2df1742613b"))) {
        errorf(e,MMVK_ERROR_DRIVER_UNAVAILABLE,kr,"ICD lacks compatible SweetMali XNU transport"); goto fail;
    }
    d->GetInstanceProcAddr=(PFN_vkGetInstanceProcAddr)dlsym(d->icd,"vk_icdGetInstanceProcAddr");
    if (!d->GetInstanceProcAddr) { errorf(e,MMVK_ERROR_DRIVER_UNAVAILABLE,0,"ICD has no vk_icdGetInstanceProcAddr");goto fail; }
    PFN_vkCreateInstance create=(PFN_vkCreateInstance)d->GetInstanceProcAddr(NULL,"vkCreateInstance");
    if (!create) { errorf(e,MMVK_ERROR_DRIVER_UNAVAILABLE,0,"ICD has no vkCreateInstance");goto fail; }
    VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.pApplicationName="MaliMetal",.apiVersion=VK_API_VERSION_1_3};
    VkInstanceCreateInfo ci={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};
    VkResult r=create(&ci,NULL,&d->instance);
    if (r) {vkresult(e,r,"vkCreateInstance");goto fail;}
#define LOAD_INSTANCE(name) d->name=(PFN_vk##name)d->GetInstanceProcAddr(d->instance,"vk" #name); if (!d->name) {errorf(e,MMVK_ERROR_DRIVER_UNAVAILABLE,0,"missing vk" #name);goto fail;}
    INSTANCE_FUNCS(LOAD_INSTANCE)
#undef LOAD_INSTANCE
    uint32_t count=0;
    r=d->EnumeratePhysicalDevices(d->instance,&count,NULL);
    if (r || count!=1) { errorf(e,MMVK_ERROR_DRIVER_UNAVAILABLE,r,"expected one SweetMali device; got %u",count);goto fail; }
    r=d->EnumeratePhysicalDevices(d->instance,&count,&d->physical);
    if (r) {vkresult(e,r,"vkEnumeratePhysicalDevices");goto fail;}
    d->GetPhysicalDeviceProperties(d->physical,&d->properties);
    if (d->properties.vendorID!=0x13b5 || d->properties.deviceType!=VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
        errorf(e,MMVK_ERROR_UNSUPPORTED,0,"ICD did not expose the ARM integrated GPU");goto fail;
    }
    uint32_t families=0;
    d->GetPhysicalDeviceQueueFamilyProperties(d->physical,&families,NULL);
    if (!families || families>64) {errorf(e,MMVK_ERROR_UNSUPPORTED,0,"invalid queue family count");goto fail;}
    VkQueueFamilyProperties family[64];
    d->GetPhysicalDeviceQueueFamilyProperties(d->physical,&families,family);
    for (d->family=0;d->family<families;d->family++)
        if (family[d->family].queueCount && (family[d->family].queueFlags&(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT))==(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT)) break;
    if (d->family==families) {errorf(e,MMVK_ERROR_UNSUPPORTED,0,"no graphics/compute queue");goto fail;}
    float priority=1;
    VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=d->family,.queueCount=1,.pQueuePriorities=&priority};
    if(d->properties.apiVersion<VK_API_VERSION_1_2){errorf(e,MMVK_ERROR_UNSUPPORTED,0,"AIR translation requires Vulkan 1.2");goto fail;}
    VkPhysicalDeviceVulkan12Features available12={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceVulkan11Features available11={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,.pNext=&available12};
    VkPhysicalDeviceFeatures2 available={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,.pNext=&available11};
    d->GetPhysicalDeviceFeatures2(d->physical,&available);
    /* Enable the actual scalar, buffer and sampling features needed by the
     * bounded Metal path. Unused geometry/tessellation/preraster features stay
     * explicitly disabled, so RC3 does not commit their large scratch arenas. */
#define CORE_FEATURE(name) d->enabled_features.name=available.features.name
    CORE_FEATURE(robustBufferAccess);CORE_FEATURE(fullDrawIndexUint32);CORE_FEATURE(shaderInt16);CORE_FEATURE(shaderInt64);
    CORE_FEATURE(shaderFloat64);CORE_FEATURE(samplerAnisotropy);CORE_FEATURE(imageCubeArray);
    CORE_FEATURE(textureCompressionETC2);CORE_FEATURE(textureCompressionASTC_LDR);CORE_FEATURE(textureCompressionBC);
    CORE_FEATURE(shaderStorageImageExtendedFormats);CORE_FEATURE(shaderStorageImageReadWithoutFormat);
    CORE_FEATURE(shaderStorageImageWriteWithoutFormat);
#undef CORE_FEATURE
    d->enabled11.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    d->enabled11.pNext=&d->enabled12;
#define FEATURE11(name) d->enabled11.name=available11.name
    FEATURE11(storageBuffer16BitAccess);FEATURE11(uniformAndStorageBuffer16BitAccess);
    FEATURE11(storagePushConstant16);FEATURE11(storageInputOutput16);FEATURE11(shaderDrawParameters);
#undef FEATURE11
    d->enabled12.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
#define FEATURE12(name) d->enabled12.name=available12.name
    FEATURE12(storageBuffer8BitAccess);FEATURE12(uniformAndStorageBuffer8BitAccess);
    FEATURE12(storagePushConstant8);FEATURE12(shaderBufferInt64Atomics);FEATURE12(shaderSharedInt64Atomics);
    FEATURE12(shaderFloat16);FEATURE12(shaderInt8);FEATURE12(uniformBufferStandardLayout);
    FEATURE12(scalarBlockLayout);FEATURE12(hostQueryReset);FEATURE12(timelineSemaphore);
#undef FEATURE12
    VkPhysicalDeviceFeatures2 enabled={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext=&d->enabled11,.features=d->enabled_features};
    VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.pNext=&enabled,
        .queueCreateInfoCount=1,.pQueueCreateInfos=&qi};
    r=d->CreateDevice(d->physical,&di,NULL,&d->device);
    if (r) {vkresult(e,r,"vkCreateDevice");goto fail;}
#define LOAD_DEVICE(name) d->name=(PFN_vk##name)d->GetDeviceProcAddr(d->device,"vk" #name); if (!d->name) {errorf(e,MMVK_ERROR_DRIVER_UNAVAILABLE,0,"missing vk" #name);goto fail;}
    DEVICE_FUNCS(LOAD_DEVICE)
#undef LOAD_DEVICE
    d->GetDeviceQueue(d->device,d->family,0,&d->queue);
    d->GetPhysicalDeviceMemoryProperties(d->physical,&d->memory);
    d->properties12.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES;
    d->properties11.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES;
    d->properties11.pNext=&d->properties12;
    VkPhysicalDeviceProperties2 props2={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,.pNext=&d->properties11};
    d->GetPhysicalDeviceProperties2(d->physical,&props2);
    d->shader_profile=mmvk_profile_create(d->physical,&d->enabled_features,&d->enabled11,&d->enabled12,
        &d->properties,&d->properties11,&d->properties12,d->GetPhysicalDeviceFormatProperties,d->GetPhysicalDeviceImageFormatProperties2);
    if(!d->shader_profile){errorf(e,MMVK_ERROR_MEMORY,0,"actual device shader profile construction failed");goto fail;}
    const VkPhysicalDeviceLimits *l=&d->properties.limits;
    d->caps=(MMVKCapabilities){.size=sizeof(d->caps),.api_version=d->properties.apiVersion,.gpu_id=q.gpu_id,
        .capabilities=MMVK_CAP_BUFFER|MMVK_CAP_COMPUTE|MMVK_CAP_RENDER|MMVK_CAP_IMAGE_COPY,
        .max_image_dimension_2d=l->maxImageDimension2D,.max_vertex_buffers=l->maxVertexInputBindings,
        .max_vertex_attributes=l->maxVertexInputAttributes,.max_push_constant_size=l->maxPushConstantsSize,
        .max_bound_descriptor_sets=l->maxBoundDescriptorSets,.max_buffer_size=q.max_bo_size};
    d->caps.max_per_stage_sampled_images=l->maxPerStageDescriptorSampledImages;
    d->caps.max_per_stage_samplers=l->maxPerStageDescriptorSamplers;
    d->caps.max_compute_workgroup_invocations=l->maxComputeWorkGroupInvocations;
    d->caps.max_compute_shared_memory_size=l->maxComputeSharedMemorySize;
    if(d->enabled12.shaderFloat16)d->caps.capabilities|=MMVK_CAP_SHADER_FLOAT16;
    if(d->enabled_features.robustBufferAccess)d->caps.capabilities|=MMVK_CAP_INDEXED_RENDER;
    if(d->enabled_features.fullDrawIndexUint32)d->caps.capabilities|=MMVK_CAP_INDEX_UINT32;
    d->caps.subgroup_size=d->properties11.subgroupSize;
    d->caps.max_inline_uniform_bytes=l->maxUniformBufferRange;
    d->caps.max_varying_components=l->maxVertexOutputComponents<l->maxFragmentInputComponents?
        l->maxVertexOutputComponents:l->maxFragmentInputComponents;
    d->caps.cpu_page_size=q.cpu_page_size;
    d->caps.min_uniform_buffer_offset_alignment=l->minUniformBufferOffsetAlignment;
    d->caps.min_storage_buffer_offset_alignment=l->minStorageBufferOffsetAlignment;
    memcpy(d->caps.max_compute_workgroup_count,l->maxComputeWorkGroupCount,sizeof(l->maxComputeWorkGroupCount));
    memcpy(d->caps.max_compute_workgroup_size,l->maxComputeWorkGroupSize,sizeof(l->maxComputeWorkGroupSize));
    snprintf(d->caps.device_name,sizeof(d->caps.device_name),"%s",d->properties.deviceName);
    *out=d;
    return vkresult(e,VK_SUCCESS,"device created");
fail:
    mmvk_device_destroy(d);
    return e?e->code:MMVK_ERROR_DRIVER_UNAVAILABLE;
}
void mmvk_device_destroy(MMVKDevice *d)
{
    if (!d) return;
    if (d->device && d->DestroyDevice) {
        if(d->DeviceWaitIdle)d->DeviceWaitIdle(d->device);
        d->DestroyDevice(d->device,NULL);
    }
    if (d->instance && d->DestroyInstance)d->DestroyInstance(d->instance,NULL);
    if (d->icd)dlclose(d->icd);
    free(d->shader_profile);
    pthread_mutex_destroy(&d->queue_lock);
    free(d);
}
const MMVKCapabilities *mmvk_device_capabilities(const MMVKDevice *d){return d?&d->caps:NULL;}
const char *mmvk_device_shader_profile_json(const MMVKDevice *d,size_t *size)
{if(size)*size=d&&d->shader_profile?strlen(d->shader_profile):0;return d?d->shader_profile:NULL;}

MMVKResult mmvk_buffer_create(MMVKDevice *d,uint64_t size,uint32_t usage,MMVKBuffer **out,MMVKError *e)
{
    if(!d||!out||!size||size>d->caps.max_buffer_size||!usage||usage&~63u)return errorf(e,MMVK_ERROR_ARGUMENT,0,"invalid buffer descriptor");
    *out=NULL;
    MMVKBuffer *b=calloc(1,sizeof(*b));
    if(!b)return errorf(e,MMVK_ERROR_MEMORY,0,"buffer allocation failed");
    b->dev=d;b->size=size;b->usage=usage;
    VkBufferUsageFlags flags=0;
    if(usage&MMVK_BUFFER_VERTEX)flags|=VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if(usage&MMVK_BUFFER_INDEX)flags|=VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if(usage&MMVK_BUFFER_UNIFORM)flags|=VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if(usage&MMVK_BUFFER_STORAGE)flags|=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if(usage&MMVK_BUFFER_TRANSFER_SRC)flags|=VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if(usage&MMVK_BUFFER_TRANSFER_DST)flags|=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VkBufferCreateInfo ci={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=size,.usage=flags,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};
    VkResult r=d->CreateBuffer(d->device,&ci,NULL,&b->buffer);
    if(r)goto fail;
    VkMemoryRequirements req;d->GetBufferMemoryRequirements(d->device,b->buffer,&req);
    int type=memory_type(d,req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if(type<0)type=memory_type(d,req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    if(type<0){mmvk_buffer_destroy(b);return errorf(e,MMVK_ERROR_UNSUPPORTED,0,"no host-visible buffer memory");}
    b->coherent=(d->memory.memoryTypes[type].propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)!=0;
    b->allocation_size=req.size;
    VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=(uint32_t)type};
    r=d->AllocateMemory(d->device,&ai,NULL,&b->memory);if(r)goto fail;
    r=d->BindBufferMemory(d->device,b->buffer,b->memory,0);if(r)goto fail;
    r=d->MapMemory(d->device,b->memory,0,VK_WHOLE_SIZE,0,&b->map);if(r)goto fail;
    *out=b;return vkresult(e,VK_SUCCESS,"buffer created");
fail: mmvk_buffer_destroy(b);return vkresult(e,r,"buffer creation");
}
void mmvk_buffer_destroy(MMVKBuffer *b)
{
    if(!b)return;MMVKDevice *d=b->dev;
    if(b->map)d->UnmapMemory(d->device,b->memory);
    if(b->buffer)d->DestroyBuffer(d->device,b->buffer,NULL);
    if(b->memory)d->FreeMemory(d->device,b->memory,NULL);
    free(b);
}
void *mmvk_buffer_contents(MMVKBuffer *b){return b?b->map:NULL;}
uint64_t mmvk_buffer_size(const MMVKBuffer *b){return b?b->size:0;}
uint64_t mmvk_buffer_allocation_size(const MMVKBuffer *b){return b?b->allocation_size:0;}
uint64_t mmvk_image_allocation_size(const MMVKImage *i){return i?i->allocation_size:0;}
MMVKFormat mmvk_image_format(const MMVKImage *i){return i?i->desc.format:0;}
static MMVKResult buffer_sync(MMVKBuffer *b,uint64_t off,uint64_t len,int invalidate,MMVKError *e)
{
    if(!b||off>b->size||!len||len>b->size-off)return errorf(e,MMVK_ERROR_ARGUMENT,0,"invalid mapped buffer range");
    VkMappedMemoryRange range={.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=b->memory,.offset=0,.size=VK_WHOLE_SIZE};
    VkResult r=VK_SUCCESS;
    if(!b->coherent)r=invalidate?b->dev->InvalidateMappedMemoryRanges(b->dev->device,1,&range):b->dev->FlushMappedMemoryRanges(b->dev->device,1,&range);
    return vkresult(e,r,invalidate?"invalidate mapped memory":"flush mapped memory");
}
MMVKResult mmvk_buffer_flush(MMVKBuffer *b,uint64_t o,uint64_t l,MMVKError *e){return buffer_sync(b,o,l,0,e);}
MMVKResult mmvk_buffer_invalidate(MMVKBuffer *b,uint64_t o,uint64_t l,MMVKError *e){return buffer_sync(b,o,l,1,e);}

MMVKResult mmvk_image_create(MMVKDevice *d,const MMVKImageDescriptor *desc,MMVKImage **out,MMVKError *e)
{
    if(!d||!desc||!out||!desc->width||!desc->height||desc->width>d->caps.max_image_dimension_2d||desc->height>d->caps.max_image_dimension_2d||!desc->usage||desc->usage&~31u||!format(desc->format))return errorf(e,MMVK_ERROR_ARGUMENT,0,"invalid image descriptor");
    *out=NULL;MMVKImage *i=calloc(1,sizeof(*i));
    if(!i)return errorf(e,MMVK_ERROR_MEMORY,0,"image allocation failed");
    i->dev=d;i->desc=*desc;i->layout=VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageUsageFlags usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if(desc->usage&MMVK_IMAGE_RENDER_TARGET)usage|=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if(desc->usage&MMVK_IMAGE_SAMPLED)usage|=VK_IMAGE_USAGE_SAMPLED_BIT;
    if(desc->usage&MMVK_IMAGE_STORAGE)usage|=VK_IMAGE_USAGE_STORAGE_BIT;
    VkFormatProperties props;d->GetPhysicalDeviceFormatProperties(d->physical,format(desc->format),&props);
    VkFormatFeatureFlags required=VK_FORMAT_FEATURE_TRANSFER_SRC_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    if(usage&VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)required|=VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
    if(usage&VK_IMAGE_USAGE_SAMPLED_BIT)required|=VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if(usage&VK_IMAGE_USAGE_STORAGE_BIT)required|=VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
    if((props.optimalTilingFeatures&required)!=required){free(i);return errorf(e,MMVK_ERROR_UNSUPPORTED,0,"image format/usage unsupported");}
    VkImageCreateInfo ci={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,.format=format(desc->format),
        .extent={desc->width,desc->height,1},.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,
        .usage=usage,.sharingMode=VK_SHARING_MODE_EXCLUSIVE,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED};
    VkResult r=d->CreateImage(d->device,&ci,NULL,&i->image);if(r)goto fail;
    VkMemoryRequirements req;d->GetImageMemoryRequirements(d->device,i->image,&req);
    i->allocation_size=req.size;
    int type=memory_type(d,req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if(type<0)type=memory_type(d,req.memoryTypeBits,0);
    if(type<0){mmvk_image_destroy(i);return errorf(e,MMVK_ERROR_UNSUPPORTED,0,"no image memory type");}
    VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=(uint32_t)type};
    r=d->AllocateMemory(d->device,&ai,NULL,&i->memory);if(r)goto fail;
    r=d->BindImageMemory(d->device,i->image,i->memory,0);if(r)goto fail;
    /* A view is only valid when the image has a view-compatible usage. */
    if(usage&(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT)){
        VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=i->image,.viewType=VK_IMAGE_VIEW_TYPE_2D,.format=format(desc->format),.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
        r=d->CreateImageView(d->device,&vi,NULL,&i->view);if(r)goto fail;
    }
    *out=i;return vkresult(e,VK_SUCCESS,"image created");
fail:mmvk_image_destroy(i);return vkresult(e,r,"image creation");
}
void mmvk_image_destroy(MMVKImage *i)
{
    if(!i)return;MMVKDevice *d=i->dev;
    if(i->view)d->DestroyImageView(d->device,i->view,NULL);
    if(i->image)d->DestroyImage(d->device,i->image,NULL);
    if(i->memory)d->FreeMemory(d->device,i->memory,NULL);
    free(i);
}

static MMVKResult recording(MMVKCommands *c,MMVKError *e)
{return (!c||c->submitted)?errorf(e,MMVK_ERROR_STATE,0,"command list is not recording"):MMVK_OK;}
static void memory_barrier(MMVKCommands *c)
{
    VkMemoryBarrier b={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_HOST_WRITE_BIT,.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_HOST_READ_BIT};
    c->dev->CmdPipelineBarrier(c->cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT|VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT|VK_PIPELINE_STAGE_HOST_BIT,0,1,&b,0,NULL,0,NULL);
}
static MMVKResult image_access(MMVKCommands *c,MMVKImage *i,MMVKError *e)
{
    if(recording(c,e))return MMVK_ERROR_STATE;
    if(!i||i->dev!=c->dev||c->in_render)return errorf(e,MMVK_ERROR_ARGUMENT,0,"invalid image access");
    for(uint32_t n=0;n<c->image_count;n++)if(c->images[n].image==i){memory_barrier(c);return MMVK_OK;}
    if(c->image_count==64)return errorf(e,MMVK_ERROR_UNSUPPORTED,0,"command image limit exceeded");
    c->images[c->image_count].image=i;c->images[c->image_count++].generation=i->generation;
    VkImageMemoryBarrier b={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.oldLayout=i->layout,.newLayout=VK_IMAGE_LAYOUT_GENERAL,
        .srcAccessMask=i->layout==VK_IMAGE_LAYOUT_UNDEFINED?0:VK_ACCESS_MEMORY_WRITE_BIT,
        .dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,
        .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .image=i->image,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    c->dev->CmdPipelineBarrier(c->cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,NULL,0,NULL,1,&b);
    return MMVK_OK;
}
MMVKResult mmvk_commands_create(MMVKDevice *d,MMVKCommands **out,MMVKError *e)
{
    if(!d||!out)return errorf(e,MMVK_ERROR_ARGUMENT,0,"invalid command list destination");
    *out=NULL;MMVKCommands *c=calloc(1,sizeof(*c));
    if(!c)return errorf(e,MMVK_ERROR_MEMORY,0,"command list allocation failed");c->dev=d;
    VkCommandPoolCreateInfo pi={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=d->family,.flags=VK_COMMAND_POOL_CREATE_TRANSIENT_BIT};
    VkResult r=d->CreateCommandPool(d->device,&pi,NULL,&c->pool);if(r)goto fail;
    VkCommandBufferAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=c->pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
    r=d->AllocateCommandBuffers(d->device,&ai,&c->cmd);if(r)goto fail;
    VkCommandBufferBeginInfo bi={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    r=d->BeginCommandBuffer(c->cmd,&bi);if(r)goto fail;
    *out=c;return vkresult(e,VK_SUCCESS,"command list created");
fail:mmvk_commands_destroy(c);return vkresult(e,r,"command list creation");
}
void mmvk_commands_destroy(MMVKCommands *c)
{
    if(!c)return;MMVKDevice *d=c->dev;
    for(uint32_t n=0;n<c->framebuffer_count;n++){
        d->DestroyFramebuffer(d->device,c->framebuffers[n],NULL);
        d->DestroyRenderPass(d->device,c->render_passes[n],NULL);
    }
    if(c->descriptors)d->DestroyDescriptorPool(d->device,c->descriptors,NULL);
    if(c->pool)d->DestroyCommandPool(d->device,c->pool,NULL);
    free(c);
}
MMVKResult mmvk_commands_fill_buffer(MMVKCommands *c,MMVKBuffer *b,uint64_t offset,uint64_t length,uint32_t value,MMVKError *e)
{
    if(recording(c,e))return MMVK_ERROR_STATE;
    if(c->in_render||!b||b->dev!=c->dev||!(b->usage&MMVK_BUFFER_TRANSFER_DST)||!length||(offset|length)&3u||offset>b->size||length>b->size-offset)return errorf(e,MMVK_ERROR_ARGUMENT,0,"invalid buffer fill");
    memory_barrier(c);c->dev->CmdFillBuffer(c->cmd,b->buffer,offset,length,value);return MMVK_OK;
}
MMVKResult mmvk_commands_copy_buffer(MMVKCommands *c,MMVKBuffer *s,uint64_t so,MMVKBuffer *d,uint64_t offset,uint64_t length,MMVKError *e)
{
    if(recording(c,e))return MMVK_ERROR_STATE;
    if(c->in_render||!s||!d||s->dev!=c->dev||d->dev!=c->dev||!(s->usage&MMVK_BUFFER_TRANSFER_SRC)||!(d->usage&MMVK_BUFFER_TRANSFER_DST)||!length||so>s->size||length>s->size-so||offset>d->size||length>d->size-offset||((so|offset|length)&3u)||
       (s==d&&so<offset+length&&offset<so+length))return errorf(e,MMVK_ERROR_ARGUMENT,0,"invalid buffer copy");
    VkBufferCopy copy={so,offset,length};memory_barrier(c);c->dev->CmdCopyBuffer(c->cmd,s->buffer,d->buffer,1,&copy);return MMVK_OK;
}
MMVKResult mmvk_commands_clear_image(MMVKCommands *c,MMVKImage *i,const float rgba[4],MMVKError *e)
{
    if(!rgba)return errorf(e,MMVK_ERROR_ARGUMENT,0,"missing clear color");
    MMVKResult r=image_access(c,i,e);if(r)return r;
    VkClearColorValue color;memcpy(color.float32,rgba,sizeof(color.float32));
    VkImageSubresourceRange range={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    c->dev->CmdClearColorImage(c->cmd,i->image,VK_IMAGE_LAYOUT_GENERAL,&color,1,&range);return MMVK_OK;
}
MMVKResult mmvk_commands_prepare_image(MMVKCommands *c,MMVKImage *i,MMVKError *e)
{return image_access(c,i,e);}
MMVKResult mmvk_commands_submit(MMVKCommands *c,MMVKFence **out,MMVKError *e)
{
    if(recording(c,e))return MMVK_ERROR_STATE;
    if(!out||c->in_render)return errorf(e,MMVK_ERROR_STATE,0,"cannot submit an open render pass");
    *out=NULL;MMVKFence *f=calloc(1,sizeof(*f));
    if(!f)return errorf(e,MMVK_ERROR_MEMORY,0,"fence allocation failed");f->dev=c->dev;
    memory_barrier(c);
    VkResult r=c->dev->EndCommandBuffer(c->cmd);c->submitted=1;
    if(r){free(f);return vkresult(e,r,"vkEndCommandBuffer");}
    VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    r=c->dev->CreateFence(c->dev->device,&fi,NULL,&f->fence);
    if(r){free(f);return vkresult(e,r,"vkCreateFence");}
    VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&c->cmd};
    pthread_mutex_lock(&c->dev->queue_lock);
    for(uint32_t n=0;n<c->image_count;n++)if(c->images[n].image->generation!=c->images[n].generation){
        pthread_mutex_unlock(&c->dev->queue_lock);mmvk_fence_destroy(f);return errorf(e,MMVK_ERROR_STATE,0,"image changed since command recording");}
    r=c->dev->QueueSubmit(c->dev->queue,1,&submit,f->fence);
    if(!r)for(uint32_t n=0;n<c->image_count;n++){c->images[n].image->layout=VK_IMAGE_LAYOUT_GENERAL;c->images[n].image->generation++;}
    pthread_mutex_unlock(&c->dev->queue_lock);
    if(r){c->dev->DestroyFence(c->dev->device,f->fence,NULL);free(f);return vkresult(e,r,"vkQueueSubmit");}
    *out=f;return vkresult(e,VK_SUCCESS,"submitted");
}
MMVKResult mmvk_fence_wait(MMVKFence *f,uint64_t timeout,MMVKError *e)
{if(!f)return errorf(e,MMVK_ERROR_ARGUMENT,0,"missing fence");return vkresult(e,f->dev->WaitForFences(f->dev->device,1,&f->fence,VK_TRUE,timeout),"vkWaitForFences");}
MMVKResult mmvk_fence_status(MMVKFence *f,MMVKError *e)
{if(!f)return errorf(e,MMVK_ERROR_ARGUMENT,0,"missing fence");return vkresult(e,f->dev->GetFenceStatus(f->dev->device,f->fence),"vkGetFenceStatus");}
void mmvk_fence_destroy(MMVKFence *f){if(f){f->dev->DestroyFence(f->dev->device,f->fence,NULL);free(f);}}

static MMVKResult image_host_copy(MMVKImage *i,void *pixels,size_t stride,uint64_t timeout,int download,MMVKError *e)
{
    if(!i||!pixels||stride<(uint64_t)i->desc.width*4||stride>SIZE_MAX/i->desc.height)return errorf(e,MMVK_ERROR_ARGUMENT,0,"invalid image copy stride");
    uint64_t row=(uint64_t)i->desc.width*4,size=row*i->desc.height;
    MMVKBuffer *b=NULL;MMVKCommands *c=NULL;MMVKFence *f=NULL;
    MMVKResult result=mmvk_buffer_create(i->dev,size,download?MMVK_BUFFER_TRANSFER_DST:MMVK_BUFFER_TRANSFER_SRC,&b,e);
    if(result)goto done;
    if(!download){for(uint32_t y=0;y<i->desc.height;y++)memcpy((char *)b->map+y*row,(char *)pixels+y*stride,row);result=mmvk_buffer_flush(b,0,size,e);if(result)goto done;}
    result=mmvk_commands_create(i->dev,&c,e);if(result)goto done;
    result=image_access(c,i,e);if(result)goto done;
    VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={i->desc.width,i->desc.height,1}};
    if(download)i->dev->CmdCopyImageToBuffer(c->cmd,i->image,VK_IMAGE_LAYOUT_GENERAL,b->buffer,1,&copy);
    else i->dev->CmdCopyBufferToImage(c->cmd,b->buffer,i->image,VK_IMAGE_LAYOUT_GENERAL,1,&copy);
    result=mmvk_commands_submit(c,&f,e);if(result)goto done;
    result=mmvk_fence_wait(f,timeout,e);
    /* Temporary resources cannot be freed while a timed-out submission is live.
     * Drain this private operation before returning the original timeout. */
    if(result==MMVK_TIMEOUT){MMVKResult drain=mmvk_fence_wait(f,UINT64_MAX,NULL);if(drain!=MMVK_OK){result=drain;errorf(e,drain,0,"image transfer drain failed");}}
    if(result)goto done;
    if(download){result=mmvk_buffer_invalidate(b,0,size,e);if(result)goto done;for(uint32_t y=0;y<i->desc.height;y++)memcpy((char *)pixels+y*stride,(char *)b->map+y*row,row);}
done:mmvk_fence_destroy(f);mmvk_commands_destroy(c);mmvk_buffer_destroy(b);return result;
}
MMVKResult mmvk_image_upload(MMVKImage *i,const void *p,size_t s,MMVKError *e){return image_host_copy(i,(void *)p,s,UINT64_MAX,0,e);}
MMVKResult mmvk_image_readback(MMVKImage *i,void *p,size_t s,uint64_t t,MMVKError *e){return image_host_copy(i,p,s,t,1,e);}

static VkDescriptorType descriptor_type(MMVKBindingType type)
{
    switch(type){case MMVK_BIND_UNIFORM_BUFFER:return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case MMVK_BIND_STORAGE_BUFFER:return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case MMVK_BIND_COMBINED_IMAGE_SAMPLER:return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    case MMVK_BIND_SAMPLED_IMAGE:return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    case MMVK_BIND_STORAGE_IMAGE:return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    case MMVK_BIND_SAMPLER:return VK_DESCRIPTOR_TYPE_SAMPLER;
    default:return VK_DESCRIPTOR_TYPE_MAX_ENUM;}
}
static MMVKResult pipeline_layout(MMVKPipeline *p,const MMVKPipelineLayout *l,MMVKError *e)
{
    if(!l||l->binding_count>64||(l->binding_count&&!l->bindings)||l->push_constant_size>p->dev->caps.max_push_constant_size||(l->push_constant_size&3))return errorf(e,MMVK_ERROR_ARGUMENT,0,"invalid pipeline layout");
    uint32_t total[5]={0},per_stage[3][5]={{0}};
    for(uint32_t n=0;n<l->binding_count;n++){
        const MMVKBindingLayout *b=&l->bindings[n];
        if(b->set>=4||b->set>=p->dev->caps.max_bound_descriptor_sets||!b->stages||b->stages&~7u||descriptor_type(b->type)==VK_DESCRIPTOR_TYPE_MAX_ENUM)return errorf(e,MMVK_ERROR_UNSUPPORTED,0,"pipeline descriptor type/set unsupported");
        for(uint32_t k=0;k<n;k++)if(l->bindings[k].set==b->set&&l->bindings[k].binding==b->binding)return errorf(e,MMVK_ERROR_ARGUMENT,0,"duplicate pipeline binding");
        uint32_t classes[5]={b->type==MMVK_BIND_UNIFORM_BUFFER,b->type==MMVK_BIND_STORAGE_BUFFER,
            b->type==MMVK_BIND_COMBINED_IMAGE_SAMPLER||b->type==MMVK_BIND_SAMPLED_IMAGE,
            b->type==MMVK_BIND_STORAGE_IMAGE,b->type==MMVK_BIND_COMBINED_IMAGE_SAMPLER||b->type==MMVK_BIND_SAMPLER};
        for(unsigned type=0;type<5;type++){total[type]+=classes[type];for(unsigned stage=0;stage<3;stage++)if(b->stages&(1u<<stage))per_stage[stage][type]+=classes[type];}
        if(p->set_count<=b->set)p->set_count=b->set+1;
    }
    const VkPhysicalDeviceLimits *limits=&p->dev->properties.limits;
    const uint32_t total_limits[5]={limits->maxDescriptorSetUniformBuffers,limits->maxDescriptorSetStorageBuffers,
        limits->maxDescriptorSetSampledImages,limits->maxDescriptorSetStorageImages,limits->maxDescriptorSetSamplers};
    const uint32_t stage_limits[5]={limits->maxPerStageDescriptorUniformBuffers,limits->maxPerStageDescriptorStorageBuffers,
        limits->maxPerStageDescriptorSampledImages,limits->maxPerStageDescriptorStorageImages,limits->maxPerStageDescriptorSamplers};
    for(unsigned type=0;type<5;type++){
        if(total[type]>total_limits[type])return errorf(e,MMVK_ERROR_UNSUPPORTED,0,"pipeline descriptor count exceeds actual device limits");
        for(unsigned stage=0;stage<3;stage++)if(per_stage[stage][type]>stage_limits[type])return errorf(e,MMVK_ERROR_UNSUPPORTED,0,"per-stage descriptor count exceeds actual device limits");
    }
    if(l->binding_count)memcpy(p->bindings,l->bindings,l->binding_count*sizeof(*l->bindings));p->binding_count=l->binding_count;p->push_size=l->push_constant_size;
    for(uint32_t set=0;set<p->set_count;set++){
        VkDescriptorSetLayoutBinding bindings[64];uint32_t count=0;
        for(uint32_t n=0;n<l->binding_count;n++)if(l->bindings[n].set==set){
            const MMVKBindingLayout *b=&l->bindings[n];bindings[count++]=(VkDescriptorSetLayoutBinding){.binding=b->binding,.descriptorType=descriptor_type(b->type),.descriptorCount=1,.stageFlags=stages(b->stages)};}
        VkDescriptorSetLayoutCreateInfo ci={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=count,.pBindings=bindings};
        VkResult r=p->dev->CreateDescriptorSetLayout(p->dev->device,&ci,NULL,&p->sets[set]);if(r)return vkresult(e,r,"vkCreateDescriptorSetLayout");
    }
    VkPushConstantRange push={.stageFlags=p->point==VK_PIPELINE_BIND_POINT_COMPUTE?VK_SHADER_STAGE_COMPUTE_BIT:VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,.offset=0,.size=p->push_size};
    VkPipelineLayoutCreateInfo ci={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=p->set_count,.pSetLayouts=p->sets,.pushConstantRangeCount=p->push_size?1u:0u,.pPushConstantRanges=&push};
    return vkresult(e,p->dev->CreatePipelineLayout(p->dev->device,&ci,NULL,&p->layout),"vkCreatePipelineLayout");
}
static MMVKResult shader_module(MMVKDevice *d,const MMVKShader *s,VkShaderModule *out,MMVKError *e)
{
    if(!s||!s->words||s->size<20||(s->size&3)||s->words[0]!=0x07230203u||!s->entry||!s->entry[0])return errorf(e,MMVK_ERROR_SHADER,0,"invalid SPIR-V module/entry");
    VkShaderModuleCreateInfo ci={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=s->size,.pCode=s->words};
    return vkresult(e,d->CreateShaderModule(d->device,&ci,NULL,out),"vkCreateShaderModule");
}
static MMVKResult compute_entry(const MMVKShader *s,uint32_t *entry,MMVKError *e)
{
    if(!s||!s->words||s->size<20||(s->size&3)||s->words[0]!=0x07230203u||!s->entry)
        return errorf(e,MMVK_ERROR_SHADER,0,"invalid compute SPIR-V");
    size_t words=s->size/4;uint32_t function=0;
    for(size_t n=5;n<words;){
        uint32_t count=s->words[n]>>16,opcode=s->words[n]&0xffff;
        if(!count||count>words-n)return errorf(e,MMVK_ERROR_SHADER,0,"malformed SPIR-V instruction");
        if(opcode==15&&count>=4&&s->words[n+1]==5){ /* OpEntryPoint GLCompute */
            const char *name=(const char *)&s->words[n+3];size_t capacity=(count-3)*4;
            const char *end=memchr(name,0,capacity);
            if(!end)return errorf(e,MMVK_ERROR_SHADER,0,"unterminated SPIR-V entry point");
            if(strlen(s->entry)==(size_t)(end-name)&&!memcmp(name,s->entry,(size_t)(end-name)))function=s->words[n+2];
        }
        n+=count;
    }
    if(!function)return errorf(e,MMVK_ERROR_SHADER,0,"compute entry point absent from SPIR-V");
    *entry=function;return MMVK_OK;
}
static MMVKResult constant_u32(const MMVKShader *s,uint32_t id,
    const MMVKSpecializationEntry *entries,uint32_t count,const void *data,uint32_t *out,MMVKError *e)
{
    size_t words=s->size/4;uint32_t type=0,value=0,specializable=0,found=0;
    for(size_t n=5;n<words;){
        uint32_t wc=s->words[n]>>16,opcode=s->words[n]&0xffff;
        if((opcode==43||opcode==50)&&wc==4&&s->words[n+2]==id){
            type=s->words[n+1];value=s->words[n+3];specializable=opcode==50;found=1;break;
        }
        n+=wc;
    }
    if(!found)return errorf(e,MMVK_ERROR_UNSUPPORTED,0,"local size uses an unsupported specialization expression");
    uint32_t width=0;
    for(size_t n=5;n<words;){uint32_t wc=s->words[n]>>16;
        if((s->words[n]&0xffff)==21&&wc==4&&s->words[n+1]==type){width=s->words[n+2];break;}n+=wc;}
    if(width!=32)return errorf(e,MMVK_ERROR_SHADER,0,"compute local size must be a 32-bit integer");
    if(specializable){
        for(size_t n=5;n<words;){uint32_t wc=s->words[n]>>16;
            if((s->words[n]&0xffff)==71&&wc==4&&s->words[n+1]==id&&s->words[n+2]==1){ /* SpecId */
                for(uint32_t k=0;k<count;k++)if(entries[k].id==s->words[n+3]){
                    if(entries[k].size!=4)return errorf(e,MMVK_ERROR_ARGUMENT,0,"local-size specialization must contain four bytes");
                    memcpy(&value,(const char *)data+entries[k].offset,4);break;
                }
                break;
            }n+=wc;
        }
    }
    *out=value;return MMVK_OK;
}
static MMVKResult compute_local_size(MMVKDevice *d,const MMVKShader *s,
    const MMVKSpecializationEntry *entries,uint32_t count,const void *data,uint32_t size[3],MMVKError *e)
{
    uint32_t function=0;MMVKResult result=compute_entry(s,&function,e);if(result)return result;
    size_t words=s->size/4;uint32_t found=0,ids[3]={0},builtin=0;
    for(size_t n=5;n<words;){
        uint32_t wc=s->words[n]>>16,opcode=s->words[n]&0xffff;
        if(opcode==16&&wc==6&&s->words[n+1]==function&&s->words[n+2]==17){memcpy(size,&s->words[n+3],3*sizeof(*size));found=1;}
        if(opcode==331&&wc==6&&s->words[n+1]==function&&s->words[n+2]==38){memcpy(ids,&s->words[n+3],sizeof(ids));found=2;}
        if(opcode==71&&wc==4&&s->words[n+2]==11&&s->words[n+3]==25){
            if(builtin&&builtin!=s->words[n+1])return errorf(e,MMVK_ERROR_UNSUPPORTED,0,"ambiguous WorkgroupSize built-in");
            builtin=s->words[n+1];
        }n+=wc;
    }
    /* Vulkan 1.2 AIR lowering uses a BuiltIn WorkgroupSize composite. It takes
     * precedence over an optional literal LocalSize execution mode. */
    if(builtin){
        found=0;
        for(size_t n=5;n<words;){uint32_t wc=s->words[n]>>16,opcode=s->words[n]&0xffff;
            if((opcode==44||opcode==51)&&wc==6&&s->words[n+2]==builtin){memcpy(ids,&s->words[n+3],sizeof(ids));found=2;break;}
            n+=wc;
        }
    }
    if(!found)return errorf(e,MMVK_ERROR_UNSUPPORTED,0,"compute WorkgroupSize cannot be resolved");
    if(found==2)for(unsigned n=0;n<3;n++){result=constant_u32(s,ids[n],entries,count,data,&size[n],e);if(result)return result;}
    uint64_t invocations=1;
    for(unsigned n=0;n<3;n++){
        if(!size[n]||size[n]>d->caps.max_compute_workgroup_size[n]||
           invocations>d->caps.max_compute_workgroup_invocations/size[n])
            return errorf(e,MMVK_ERROR_UNSUPPORTED,0,"compute local size exceeds actual device limits");
        invocations*=size[n];
    }
    return MMVK_OK;
}
MMVKResult mmvk_compute_program_create(MMVKDevice *d,const MMVKShader *s,const MMVKPipelineLayout *l,MMVKComputeProgram **out,MMVKError *e)
{
    if(!d||!out)return errorf(e,MMVK_ERROR_ARGUMENT,0,"invalid compute program destination");*out=NULL;
    uint32_t entry;MMVKResult result=compute_entry(s,&entry,e);if(result)return result;
    MMVKComputeProgram *p=calloc(1,sizeof(*p));if(!p)return errorf(e,MMVK_ERROR_MEMORY,0,"compute program allocation failed");p->dev=d;
    p->definition=calloc(1,sizeof(*p->definition));p->words=malloc(s->size);p->entry=strdup(s->entry);
    if(p->definition){p->definition->dev=d;p->definition->point=VK_PIPELINE_BIND_POINT_COMPUTE;}
    if(!p->definition||!p->words||!p->entry){mmvk_compute_program_destroy(p);return errorf(e,MMVK_ERROR_MEMORY,0,"compute program data allocation failed");}
    memcpy(p->words,s->words,s->size);p->size=s->size;
    result=pipeline_layout(p->definition,l,e);if(result){mmvk_compute_program_destroy(p);return result;}
    result=shader_module(d,s,&p->module,e);if(result){mmvk_compute_program_destroy(p);return result;}
    *out=p;return MMVK_OK;
}
void mmvk_compute_program_destroy(MMVKComputeProgram *p)
{
    if(!p)return;
    if(p->module)p->dev->DestroyShaderModule(p->dev->device,p->module,NULL);
    mmvk_pipeline_destroy(p->definition);free(p->entry);free(p->words);free(p);
}
MMVKResult mmvk_compute_program_create_variant(MMVKComputeProgram *program,const MMVKSpecializationEntry *entries,uint32_t count,
    const void *data,size_t data_size,uint32_t flags,uint32_t subgroup,MMVKPipeline **out,MMVKError *e)
{
    if(!program||!out||count>128||(count&&!entries)||(data_size&&!data))return errorf(e,MMVK_ERROR_ARGUMENT,0,"invalid compute specialization data");*out=NULL;
    if(flags||subgroup)return errorf(e,MMVK_ERROR_UNSUPPORTED,0,"subgroup-size control and shader stage flags are not enabled");
    VkSpecializationMapEntry native[128];
    for(uint32_t n=0;n<count;n++){
        if(!entries[n].size||entries[n].offset>data_size||entries[n].size>data_size-entries[n].offset)
            return errorf(e,MMVK_ERROR_ARGUMENT,0,"specialization entry exceeds its data");
        for(uint32_t k=0;k<n;k++)if(entries[k].id==entries[n].id)return errorf(e,MMVK_ERROR_ARGUMENT,0,"duplicate specialization ID");
        native[n]=(VkSpecializationMapEntry){entries[n].id,entries[n].offset,entries[n].size};
    }
    MMVKPipeline *p=calloc(1,sizeof(*p));if(!p)return errorf(e,MMVK_ERROR_MEMORY,0,"compute variant allocation failed");
    p->dev=program->dev;p->point=VK_PIPELINE_BIND_POINT_COMPUTE;
    MMVKShader shader={program->words,program->size,program->entry};
    MMVKResult result=compute_local_size(p->dev,&shader,entries,count,data,p->local_size,e);if(result)goto done;
    MMVKPipelineLayout layout={program->definition->bindings,program->definition->binding_count,program->definition->push_size};
    result=pipeline_layout(p,&layout,e);if(result)goto done;
    VkSpecializationInfo specialization={.mapEntryCount=count,.pMapEntries=native,.dataSize=data_size,.pData=data};
    VkComputePipelineCreateInfo ci={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.layout=p->layout,
        .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,
                .module=program->module,.pName=program->entry,.pSpecializationInfo=count?&specialization:NULL}};
    result=vkresult(e,p->dev->CreateComputePipelines(p->dev->device,VK_NULL_HANDLE,1,&ci,NULL,&p->pipeline),"vkCreateComputePipelines specialization");
done:if(result)mmvk_pipeline_destroy(p);else *out=p;return result;
}
MMVKResult mmvk_compute_pipeline_create_specialized(MMVKDevice *d,const MMVKShader *s,const MMVKPipelineLayout *l,
    const MMVKSpecializationEntry *entries,uint32_t count,const void *data,size_t data_size,uint32_t flags,uint32_t subgroup,MMVKPipeline **out,MMVKError *e)
{
    if(out)*out=NULL;MMVKComputeProgram *program=NULL;
    MMVKResult result=mmvk_compute_program_create(d,s,l,&program,e);if(result)return result;
    result=mmvk_compute_program_create_variant(program,entries,count,data,data_size,flags,subgroup,out,e);
    mmvk_compute_program_destroy(program);return result;
}
MMVKResult mmvk_compute_pipeline_create(MMVKDevice *d,const MMVKShader *s,const MMVKPipelineLayout *l,MMVKPipeline **out,MMVKError *e)
{
    return mmvk_compute_pipeline_create_specialized(d,s,l,NULL,0,NULL,0,0,0,out,e);
}
MMVKResult mmvk_pipeline_compute_workgroup_size(const MMVKPipeline *p,uint32_t size[3])
{
    if(!p||!size||p->point!=VK_PIPELINE_BIND_POINT_COMPUTE)return MMVK_ERROR_ARGUMENT;
    memcpy(size,p->local_size,sizeof(p->local_size));return MMVK_OK;
}
static VkResult render_pass(MMVKDevice *d,MMVKFormat f,VkRenderPass *out)
{
    VkAttachmentDescription a={.format=format(f),.samples=VK_SAMPLE_COUNT_1_BIT,.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,.initialLayout=VK_IMAGE_LAYOUT_GENERAL,.finalLayout=VK_IMAGE_LAYOUT_GENERAL};
    VkAttachmentReference ref={0,VK_IMAGE_LAYOUT_GENERAL};
    VkSubpassDescription sub={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,.colorAttachmentCount=1,.pColorAttachments=&ref};
    VkRenderPassCreateInfo ci={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.attachmentCount=1,.pAttachments=&a,.subpassCount=1,.pSubpasses=&sub};
    return d->CreateRenderPass(d->device,&ci,NULL,out);
}
MMVKResult mmvk_render_pipeline_create(MMVKDevice *d,const MMVKRenderPipelineDescriptor *desc,MMVKPipeline **out,MMVKError *e)
{
    if(!d||!desc||!out||!format(desc->color_format)||desc->vertex_binding_count>d->caps.max_vertex_buffers||desc->vertex_binding_count>32||desc->vertex_attribute_count>d->caps.max_vertex_attributes||desc->vertex_attribute_count>32||
        (desc->vertex_binding_count&&!desc->vertex_bindings)||(desc->vertex_attribute_count&&!desc->vertex_attributes)||desc->topology>5||desc->blend.write_mask&~15u)return errorf(e,MMVK_ERROR_ARGUMENT,0,"invalid render pipeline descriptor");
    *out=NULL;MMVKPipeline *p=calloc(1,sizeof(*p));if(!p)return errorf(e,MMVK_ERROR_MEMORY,0,"pipeline allocation failed");
    p->dev=d;p->point=VK_PIPELINE_BIND_POINT_GRAPHICS;p->format=desc->color_format;
    VkShaderModule modules[2]={0};
    MMVKResult result=pipeline_layout(p,&desc->layout,e);if(result)goto done;
    result=shader_module(d,&desc->vertex,&modules[0],e);if(result)goto done;
    result=shader_module(d,&desc->fragment,&modules[1],e);if(result)goto done;
    VkVertexInputBindingDescription bindings[32];VkVertexInputAttributeDescription attrs[32];
    for(uint32_t n=0;n<desc->vertex_binding_count;n++){
        const MMVKVertexBinding *b=&desc->vertex_bindings[n];
        if(b->binding>=d->caps.max_vertex_buffers||b->stride>d->properties.limits.maxVertexInputBindingStride||b->per_instance>1){result=errorf(e,MMVK_ERROR_UNSUPPORTED,0,"vertex binding unsupported");goto done;}
        for(uint32_t k=0;k<n;k++)if(bindings[k].binding==b->binding){result=errorf(e,MMVK_ERROR_ARGUMENT,0,"duplicate vertex binding");goto done;}
        bindings[n]=(VkVertexInputBindingDescription){b->binding,b->stride,b->per_instance?VK_VERTEX_INPUT_RATE_INSTANCE:VK_VERTEX_INPUT_RATE_VERTEX};
    }
    for(uint32_t n=0;n<desc->vertex_attribute_count;n++){
        const MMVKVertexAttribute *a=&desc->vertex_attributes[n];VkFormatProperties props;
        d->GetPhysicalDeviceFormatProperties(d->physical,(VkFormat)a->format,&props);
        uint32_t found=0;for(uint32_t k=0;k<desc->vertex_binding_count;k++)if(bindings[k].binding==a->binding)found=1;
        if(!found||a->location>=d->caps.max_vertex_attributes||a->offset>d->properties.limits.maxVertexInputAttributeOffset||!(props.bufferFeatures&VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT)){result=errorf(e,MMVK_ERROR_UNSUPPORTED,0,"vertex attribute unsupported");goto done;}
        for(uint32_t k=0;k<n;k++)if(attrs[k].location==a->location){result=errorf(e,MMVK_ERROR_ARGUMENT,0,"duplicate vertex location");goto done;}
        attrs[n]=(VkVertexInputAttributeDescription){a->location,a->binding,(VkFormat)a->format,a->offset};
    }
    const MMVKBlend *blend=&desc->blend;
    if(blend->enabled&&(blend->src_rgb>18||blend->dst_rgb>18||blend->src_alpha>18||blend->dst_alpha>18||blend->rgb_op>4||blend->alpha_op>4)){result=errorf(e,MMVK_ERROR_UNSUPPORTED,0,"blend mode unsupported");goto done;}
    if(blend->enabled&&!d->enabled_features.dualSrcBlend&&(blend->src_rgb>=15||blend->dst_rgb>=15||blend->src_alpha>=15||blend->dst_alpha>=15)){result=errorf(e,MMVK_ERROR_UNSUPPORTED,0,"dual-source blend is not enabled");goto done;}
    VkResult r=render_pass(d,desc->color_format,&p->render_pass);if(r){result=vkresult(e,r,"vkCreateRenderPass");goto done;}
    VkPipelineShaderStageCreateInfo ss[2]={{.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=modules[0],.pName=desc->vertex.entry},
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=modules[1],.pName=desc->fragment.entry}};
    VkPipelineVertexInputStateCreateInfo vi={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,.vertexBindingDescriptionCount=desc->vertex_binding_count,.pVertexBindingDescriptions=bindings,.vertexAttributeDescriptionCount=desc->vertex_attribute_count,.pVertexAttributeDescriptions=attrs};
    VkPipelineInputAssemblyStateCreateInfo ia={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,.topology=(VkPrimitiveTopology)desc->topology};
    VkPipelineViewportStateCreateInfo vp={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,.viewportCount=1,.scissorCount=1};
    VkPipelineRasterizationStateCreateInfo rs={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,.polygonMode=VK_POLYGON_MODE_FILL,.cullMode=VK_CULL_MODE_NONE,.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE,.lineWidth=1};
    VkPipelineMultisampleStateCreateInfo ms={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState ba={.blendEnable=!!blend->enabled,.srcColorBlendFactor=(VkBlendFactor)blend->src_rgb,.dstColorBlendFactor=(VkBlendFactor)blend->dst_rgb,.colorBlendOp=(VkBlendOp)blend->rgb_op,
        .srcAlphaBlendFactor=(VkBlendFactor)blend->src_alpha,.dstAlphaBlendFactor=(VkBlendFactor)blend->dst_alpha,.alphaBlendOp=(VkBlendOp)blend->alpha_op,.colorWriteMask=blend->write_mask};
    VkPipelineColorBlendStateCreateInfo bs={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,.attachmentCount=1,.pAttachments=&ba};
    VkDynamicState dynamic[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds={.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,.dynamicStateCount=2,.pDynamicStates=dynamic};
    VkGraphicsPipelineCreateInfo ci={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,.stageCount=2,.pStages=ss,.pVertexInputState=&vi,.pInputAssemblyState=&ia,
        .pViewportState=&vp,.pRasterizationState=&rs,.pMultisampleState=&ms,.pColorBlendState=&bs,.pDynamicState=&ds,.layout=p->layout,.renderPass=p->render_pass};
    result=vkresult(e,d->CreateGraphicsPipelines(d->device,VK_NULL_HANDLE,1,&ci,NULL,&p->pipeline),"vkCreateGraphicsPipelines");
done:for(uint32_t n=0;n<2;n++)if(modules[n])d->DestroyShaderModule(d->device,modules[n],NULL);
    if(result)mmvk_pipeline_destroy(p);else *out=p;return result;
}
MMVKResult mmvk_render_pipeline_create_translated(MMVKDevice *d,const struct MMShaderRenderOutput *o,const MMVKRenderPipelineDescriptor *desc,MMVKPipeline **out,MMVKError *e)
{
    if(out)*out=NULL;
    MMShaderVulkanDescription converted;
    MMVKResult result=MMShaderVulkanDescribe(o,desc,&converted,e);
    if(result)return result;
    return mmvk_render_pipeline_create(d,&converted.descriptor,out,e);
}
void mmvk_pipeline_destroy(MMVKPipeline *p)
{
    if(!p)return;MMVKDevice *d=p->dev;
    if(p->pipeline)d->DestroyPipeline(d->device,p->pipeline,NULL);
    if(p->layout)d->DestroyPipelineLayout(d->device,p->layout,NULL);
    for(uint32_t n=0;n<4;n++)if(p->sets[n])d->DestroyDescriptorSetLayout(d->device,p->sets[n],NULL);
    if(p->render_pass)d->DestroyRenderPass(d->device,p->render_pass,NULL);
    free(p);
}
MMVKResult mmvk_sampler_create(MMVKDevice *d,const MMVKSamplerDescriptor *desc,MMVKSampler **out,MMVKError *e)
{
    if(!d||!desc||!out)return errorf(e,MMVK_ERROR_ARGUMENT,0,"invalid sampler destination");*out=NULL;
    /* The first AIR texture plan proves ordinary normalized sampling. Reject
     * modes whose runtime variant/coordinate dependencies are not implemented. */
    if(desc->min_filter>1||desc->mag_filter>1||desc->mip_filter>1||desc->address_u>3||desc->address_v>3||desc->address_w>3||
       desc->normalized_coordinates!=1||desc->compare_enabled||desc->border_color>5||
       !isfinite(desc->min_lod)||!isfinite(desc->max_lod)||!isfinite(desc->lod_bias)||!isfinite(desc->max_anisotropy)||
       desc->max_anisotropy>1||desc->max_anisotropy<0||desc->min_lod<0||desc->max_lod<desc->min_lod||fabsf(desc->lod_bias)>d->properties.limits.maxSamplerLodBias)
        return errorf(e,MMVK_ERROR_UNSUPPORTED,0,"sampler mode requires unsupported AIR runtime dependencies");
    MMVKSampler *s=calloc(1,sizeof(*s));if(!s)return errorf(e,MMVK_ERROR_MEMORY,0,"sampler allocation failed");s->dev=d;s->desc=*desc;
    VkSamplerCreateInfo ci={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,.magFilter=(VkFilter)desc->mag_filter,.minFilter=(VkFilter)desc->min_filter,
        .mipmapMode=(VkSamplerMipmapMode)desc->mip_filter,.addressModeU=(VkSamplerAddressMode)desc->address_u,.addressModeV=(VkSamplerAddressMode)desc->address_v,
        .addressModeW=(VkSamplerAddressMode)desc->address_w,.mipLodBias=desc->lod_bias,.maxAnisotropy=1,.minLod=desc->min_lod,.maxLod=desc->max_lod,.borderColor=(VkBorderColor)desc->border_color};
    VkResult r=d->CreateSampler(d->device,&ci,NULL,&s->sampler);
    if(r){free(s);return vkresult(e,r,"vkCreateSampler");}*out=s;return MMVK_OK;
}
void mmvk_sampler_destroy(MMVKSampler *s){if(s){s->dev->DestroySampler(s->dev->device,s->sampler,NULL);free(s);}}
MMVKResult mmvk_commands_bind_pipeline(MMVKCommands *c,MMVKPipeline *p,MMVKError *e)
{
    if(recording(c,e))return MMVK_ERROR_STATE;
    if(!p||p->dev!=c->dev||(c->in_render&&(p->point!=VK_PIPELINE_BIND_POINT_GRAPHICS||p->format!=c->render_format)))return errorf(e,MMVK_ERROR_ARGUMENT,0,"pipeline type or render format mismatch");
    if(c->pipeline==p)return MMVK_OK;
    c->pipeline=p;c->bound_bindings=0;
    c->dev->CmdBindPipeline(c->cmd,p->point,p->pipeline);return MMVK_OK;
}
MMVKResult mmvk_commands_bind_buffer(MMVKCommands *c,uint32_t set,uint32_t binding,MMVKBuffer *b,uint64_t offset,uint64_t length,MMVKError *e)
{
    if(recording(c,e))return MMVK_ERROR_STATE;
    if(!c->pipeline||!b||b->dev!=c->dev||offset>b->size||!length||length>b->size-offset)return errorf(e,MMVK_ERROR_ARGUMENT,0,"invalid descriptor buffer");
    MMVKPipeline *p=c->pipeline;
    for(uint32_t n=0;n<p->binding_count;n++)if(p->bindings[n].set==set&&p->bindings[n].binding==binding){
        VkDescriptorType type=descriptor_type(p->bindings[n].type);
        if(type!=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER&&type!=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            return errorf(e,MMVK_ERROR_ARGUMENT,0,"descriptor binding is not a buffer");
        uint64_t alignment=type==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER?c->dev->properties.limits.minUniformBufferOffsetAlignment:c->dev->properties.limits.minStorageBufferOffsetAlignment;
        uint64_t max=type==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER?c->dev->properties.limits.maxUniformBufferRange:c->dev->properties.limits.maxStorageBufferRange;
        uint32_t usage=type==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER?MMVK_BUFFER_UNIFORM:MMVK_BUFFER_STORAGE;
        if(!(b->usage&usage)||length>max||(alignment&&offset%alignment))return errorf(e,MMVK_ERROR_ARGUMENT,0,"descriptor buffer usage/alignment/range mismatch");
        c->pending_buffers[n]=(VkDescriptorBufferInfo){b->buffer,offset,length};
        c->bound_bindings|=UINT64_C(1)<<n;return MMVK_OK;
    }
    return errorf(e,MMVK_ERROR_ARGUMENT,0,"descriptor binding absent from pipeline");
}
static MMVKResult prepare_descriptor_image(MMVKCommands *c,MMVKImage *i,uint32_t usage,MMVKError *e)
{
    if(recording(c,e))return MMVK_ERROR_STATE;
    if(!c->pipeline||!i||i->dev!=c->dev||!(i->desc.usage&usage)||!i->view||i==c->render_target)
        return errorf(e,MMVK_ERROR_ARGUMENT,0,"invalid descriptor image usage or feedback loop");
    /* Texture uploads/clears transition to GENERAL before render encoding.
     * In-pass image transitions are refused; the frontend prepares snapshots
     * before opening the pass. Queue submission serializes their visibility. */
    if(c->in_render){
        uint32_t found=0;for(uint32_t n=0;n<c->image_count;n++)if(c->images[n].image==i)found=1;
        if(!found&&i->layout!=VK_IMAGE_LAYOUT_GENERAL)return errorf(e,MMVK_ERROR_STATE,0,"sampled image must be prepared before the render pass");
        if(!found){if(c->image_count==64)return errorf(e,MMVK_ERROR_UNSUPPORTED,0,"command image limit exceeded");c->images[c->image_count].image=i;c->images[c->image_count++].generation=i->generation;}
    }else{MMVKResult r=image_access(c,i,e);if(r)return r;}
    return MMVK_OK;
}
MMVKResult mmvk_commands_bind_image_sampler(MMVKCommands *c,uint32_t set,uint32_t binding,MMVKImage *i,MMVKSampler *s,MMVKError *e)
{
    if(recording(c,e))return MMVK_ERROR_STATE;
    if(!s||s->dev!=c->dev)return errorf(e,MMVK_ERROR_ARGUMENT,0,"invalid combined image sampler");
    MMVKResult result=prepare_descriptor_image(c,i,MMVK_IMAGE_SAMPLED,e);if(result)return result;
    for(uint32_t n=0;n<c->pipeline->binding_count;n++)if(c->pipeline->bindings[n].set==set&&c->pipeline->bindings[n].binding==binding){
        if(c->pipeline->bindings[n].type!=MMVK_BIND_COMBINED_IMAGE_SAMPLER)return errorf(e,MMVK_ERROR_ARGUMENT,0,"binding is not a combined image sampler");
        c->pending_images[n]=(VkDescriptorImageInfo){s->sampler,i->view,VK_IMAGE_LAYOUT_GENERAL};c->bound_bindings|=UINT64_C(1)<<n;return MMVK_OK;
    }
    return errorf(e,MMVK_ERROR_ARGUMENT,0,"sampled image binding absent from pipeline");
}
MMVKResult mmvk_commands_bind_image(MMVKCommands *c,uint32_t set,uint32_t binding,MMVKImage *i,MMVKError *e)
{
    if(recording(c,e))return MMVK_ERROR_STATE;
    if(!c->pipeline)return errorf(e,MMVK_ERROR_STATE,0,"image binding requires a pipeline");
    for(uint32_t n=0;n<c->pipeline->binding_count;n++)if(c->pipeline->bindings[n].set==set&&c->pipeline->bindings[n].binding==binding){
        MMVKBindingType type=c->pipeline->bindings[n].type;
        if(type!=MMVK_BIND_SAMPLED_IMAGE&&type!=MMVK_BIND_STORAGE_IMAGE)return errorf(e,MMVK_ERROR_ARGUMENT,0,"binding is not a separate image");
        MMVKResult result=prepare_descriptor_image(c,i,type==MMVK_BIND_SAMPLED_IMAGE?MMVK_IMAGE_SAMPLED:MMVK_IMAGE_STORAGE,e);if(result)return result;
        c->pending_images[n]=(VkDescriptorImageInfo){VK_NULL_HANDLE,i->view,VK_IMAGE_LAYOUT_GENERAL};c->bound_bindings|=UINT64_C(1)<<n;return MMVK_OK;
    }
    return errorf(e,MMVK_ERROR_ARGUMENT,0,"image binding absent from pipeline");
}
MMVKResult mmvk_commands_bind_sampler(MMVKCommands *c,uint32_t set,uint32_t binding,MMVKSampler *s,MMVKError *e)
{
    if(recording(c,e))return MMVK_ERROR_STATE;
    if(!c->pipeline||!s||s->dev!=c->dev)return errorf(e,MMVK_ERROR_ARGUMENT,0,"sampler binding requires a pipeline and sampler");
    for(uint32_t n=0;n<c->pipeline->binding_count;n++)if(c->pipeline->bindings[n].set==set&&c->pipeline->bindings[n].binding==binding){
        if(c->pipeline->bindings[n].type!=MMVK_BIND_SAMPLER)return errorf(e,MMVK_ERROR_ARGUMENT,0,"binding is not a separate sampler");
        c->pending_images[n]=(VkDescriptorImageInfo){s->sampler,VK_NULL_HANDLE,VK_IMAGE_LAYOUT_UNDEFINED};c->bound_bindings|=UINT64_C(1)<<n;return MMVK_OK;
    }
    return errorf(e,MMVK_ERROR_ARGUMENT,0,"sampler binding absent from pipeline");
}
static MMVKResult bind_snapshot(MMVKCommands *c,MMVKError *e)
{
    MMVKPipeline *p=c->pipeline;if(!p->set_count)return MMVK_OK;
    uint64_t required=p->binding_count==64?UINT64_MAX:((UINT64_C(1)<<p->binding_count)-1);
    if((c->bound_bindings&required)!=required)return errorf(e,MMVK_ERROR_STATE,0,"draw/dispatch has unbound descriptors");
    if(c->descriptor_snapshots==256)return errorf(e,MMVK_ERROR_UNSUPPORTED,0,"command descriptor snapshot limit exceeded");
    if(!c->descriptors){
        VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,16384},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,16384},{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,16384},
            {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,16384},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,16384},{VK_DESCRIPTOR_TYPE_SAMPLER,16384}};
        VkDescriptorPoolCreateInfo ci={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1024,.poolSizeCount=6,.pPoolSizes=sizes};
        VkResult r=c->dev->CreateDescriptorPool(c->dev->device,&ci,NULL,&c->descriptors);if(r)return vkresult(e,r,"vkCreateDescriptorPool");
    }
    VkDescriptorSetAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=c->descriptors,.descriptorSetCount=p->set_count,.pSetLayouts=p->sets};
    VkResult r=c->dev->AllocateDescriptorSets(c->dev->device,&ai,c->sets);if(r)return vkresult(e,r,"vkAllocateDescriptorSets");
    VkWriteDescriptorSet writes[64];
    for(uint32_t n=0;n<p->binding_count;n++)writes[n]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet=c->sets[p->bindings[n].set],.dstBinding=p->bindings[n].binding,.descriptorCount=1,
        .descriptorType=descriptor_type(p->bindings[n].type),.pBufferInfo=&c->pending_buffers[n],.pImageInfo=&c->pending_images[n]};
    /* Every draw receives fresh immutable sets. Update them before recording a
     * bind, then retain the pool for the full command-buffer/fence lifetime. */
    c->dev->UpdateDescriptorSets(c->dev->device,p->binding_count,writes,0,NULL);
    c->dev->CmdBindDescriptorSets(c->cmd,p->point,p->layout,0,p->set_count,c->sets,0,NULL);
    c->descriptor_snapshots++;return MMVK_OK;
}
MMVKResult mmvk_commands_push_constants(MMVKCommands *c,uint32_t stage,uint32_t offset,const void *data,uint32_t size,MMVKError *e)
{
    if(recording(c,e))return MMVK_ERROR_STATE;
    if(!c->pipeline||!data||!stage||stage&~7u||!size||(offset|size)&3u||offset>c->pipeline->push_size||size>c->pipeline->push_size-offset)return errorf(e,MMVK_ERROR_ARGUMENT,0,"invalid push constants");
    uint32_t range_stages=c->pipeline->point==VK_PIPELINE_BIND_POINT_COMPUTE?MMVK_STAGE_COMPUTE:(MMVK_STAGE_VERTEX|MMVK_STAGE_FRAGMENT);
    if(stage!=range_stages)return errorf(e,MMVK_ERROR_ARGUMENT,0,"push stages must match the overlapping pipeline range");
    c->dev->CmdPushConstants(c->cmd,c->pipeline->layout,stages(stage),offset,size,data);return MMVK_OK;
}
MMVKResult mmvk_commands_dispatch(MMVKCommands *c,uint32_t x,uint32_t y,uint32_t z,MMVKError *e)
{
    if(recording(c,e))return MMVK_ERROR_STATE;
    if(c->in_render||!c->pipeline||c->pipeline->point!=VK_PIPELINE_BIND_POINT_COMPUTE||x>c->dev->caps.max_compute_workgroup_count[0]||y>c->dev->caps.max_compute_workgroup_count[1]||z>c->dev->caps.max_compute_workgroup_count[2])return errorf(e,MMVK_ERROR_ARGUMENT,0,"invalid compute dispatch");
    MMVKResult result=bind_snapshot(c,e);if(result)return result;
    memory_barrier(c);c->dev->CmdDispatch(c->cmd,x,y,z);return MMVK_OK;
}
MMVKResult mmvk_commands_begin_render(MMVKCommands *c,MMVKImage *i,uint32_t clear,const float rgba[4],MMVKError *e)
{
    if(recording(c,e))return MMVK_ERROR_STATE;
    if(!i||i->dev!=c->dev||!(i->desc.usage&MMVK_IMAGE_RENDER_TARGET)||c->in_render||c->framebuffer_count==16)return errorf(e,MMVK_ERROR_ARGUMENT,0,"invalid render target or open pass");
    MMVKResult result=clear?mmvk_commands_clear_image(c,i,rgba,e):image_access(c,i,e);if(result)return result;
    VkRenderPass pass;VkResult r=render_pass(c->dev,i->desc.format,&pass);if(r)return vkresult(e,r,"vkCreateRenderPass");
    VkFramebufferCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,.renderPass=pass,.attachmentCount=1,.pAttachments=&i->view,.width=i->desc.width,.height=i->desc.height,.layers=1};
    VkFramebuffer fb;r=c->dev->CreateFramebuffer(c->dev->device,&fi,NULL,&fb);
    if(r){c->dev->DestroyRenderPass(c->dev->device,pass,NULL);return vkresult(e,r,"vkCreateFramebuffer");}
    c->render_passes[c->framebuffer_count]=pass;c->framebuffers[c->framebuffer_count++]=fb;
    VkRenderPassBeginInfo bi={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=pass,.framebuffer=fb,.renderArea={{0,0},{i->desc.width,i->desc.height}}};
    memory_barrier(c);
    c->dev->CmdBeginRenderPass(c->cmd,&bi,VK_SUBPASS_CONTENTS_INLINE);c->in_render=1;c->render_format=i->desc.format;c->render_target=i;
    mmvk_commands_set_viewport(c,0,0,(float)i->desc.width,(float)i->desc.height,0,1,NULL);
    mmvk_commands_set_scissor(c,0,0,i->desc.width,i->desc.height,NULL);return MMVK_OK;
}
MMVKResult mmvk_commands_set_viewport(MMVKCommands *c,float x,float y,float w,float h,float near,float far,MMVKError *e)
{
    if(recording(c,e))return MMVK_ERROR_STATE;
    const VkPhysicalDeviceLimits *l=&c->dev->properties.limits;
    float y_low=fminf(y,y+h),y_high=fmaxf(y,y+h);
    if(!isfinite(x)||!isfinite(y)||!isfinite(w)||!isfinite(h)||!isfinite(x+w)||!isfinite(y+h)||!isfinite(near)||!isfinite(far)||w<=0||h==0||w>l->maxViewportDimensions[0]||fabsf(h)>l->maxViewportDimensions[1]||
       x<l->viewportBoundsRange[0]||x+w>l->viewportBoundsRange[1]||y_low<l->viewportBoundsRange[0]||y_high>l->viewportBoundsRange[1]||
       (h<0&&c->dev->caps.api_version<VK_API_VERSION_1_1)||near<0||near>1||far<0||far>1)return errorf(e,MMVK_ERROR_ARGUMENT,0,"invalid viewport");
    VkViewport v={x,y,w,h,near,far};c->dev->CmdSetViewport(c->cmd,0,1,&v);c->viewport=1;return MMVK_OK;
}
MMVKResult mmvk_commands_set_scissor(MMVKCommands *c,int32_t x,int32_t y,uint32_t w,uint32_t h,MMVKError *e)
{
    if(recording(c,e))return MMVK_ERROR_STATE;
    if(x<0||y<0||w>INT32_MAX-(uint32_t)x||h>INT32_MAX-(uint32_t)y)return errorf(e,MMVK_ERROR_ARGUMENT,0,"invalid scissor");
    VkRect2D rect={{x,y},{w,h}};c->dev->CmdSetScissor(c->cmd,0,1,&rect);c->scissor=1;return MMVK_OK;
}
MMVKResult mmvk_commands_bind_vertex_buffer(MMVKCommands *c,uint32_t binding,MMVKBuffer *b,uint64_t offset,MMVKError *e)
{
    if(recording(c,e))return MMVK_ERROR_STATE;
    if(!b||b->dev!=c->dev||binding>=c->dev->caps.max_vertex_buffers||offset>=b->size||!(b->usage&MMVK_BUFFER_VERTEX))return errorf(e,MMVK_ERROR_ARGUMENT,0,"invalid vertex buffer binding");
    c->dev->CmdBindVertexBuffers(c->cmd,binding,1,&b->buffer,&offset);return MMVK_OK;
}
MMVKResult mmvk_commands_draw(MMVKCommands *c,uint32_t start,uint32_t count,uint32_t instances,uint32_t first_instance,MMVKError *e)
{
    if(recording(c,e))return MMVK_ERROR_STATE;
    if(!c->in_render||!c->pipeline||c->pipeline->point!=VK_PIPELINE_BIND_POINT_GRAPHICS||c->pipeline->format!=c->render_format||!c->viewport||!c->scissor)return errorf(e,MMVK_ERROR_STATE,0,"draw requires a compatible render pass and pipeline");
    MMVKResult result=bind_snapshot(c,e);if(result)return result;
    c->dev->CmdDraw(c->cmd,count,instances,start,first_instance);return MMVK_OK;
}
MMVKResult mmvk_commands_bind_index_buffer(MMVKCommands *c,MMVKBuffer *b,uint64_t offset,uint32_t type,MMVKError *e)
{
    if(recording(c,e))return MMVK_ERROR_STATE;
    if(!b||b->dev!=c->dev||!(b->usage&MMVK_BUFFER_INDEX)||type>1)
        return errorf(e,MMVK_ERROR_ARGUMENT,0,"invalid index buffer or index type");
    if(!(c->dev->caps.capabilities&MMVK_CAP_INDEXED_RENDER)||(type==1&&!(c->dev->caps.capabilities&MMVK_CAP_INDEX_UINT32)))
        return errorf(e,MMVK_ERROR_UNSUPPORTED,0,"actual device robustness or full uint32 index range is unavailable");
    uint32_t element=type==0?2u:4u;
    if(offset>=b->size||offset%element)return errorf(e,MMVK_ERROR_ARGUMENT,0,"index buffer offset is out of bounds or unaligned");
    c->dev->CmdBindIndexBuffer(c->cmd,b->buffer,offset,(VkIndexType)type);
    c->index_buffer=b;c->index_offset=offset;c->index_element_size=element;return MMVK_OK;
}
MMVKResult mmvk_commands_draw_indexed(MMVKCommands *c,uint32_t count,uint32_t instances,uint32_t first,int32_t base,uint32_t first_instance,MMVKError *e)
{
    if(recording(c,e))return MMVK_ERROR_STATE;
    if(!c->in_render||!c->pipeline||c->pipeline->point!=VK_PIPELINE_BIND_POINT_GRAPHICS||c->pipeline->format!=c->render_format||!c->viewport||!c->scissor||!c->index_buffer)
        return errorf(e,MMVK_ERROR_STATE,0,"indexed draw requires a compatible pass, pipeline and index buffer");
    uint64_t available=(c->index_buffer->size-c->index_offset)/c->index_element_size;
    if(first>available||count>available-first)return errorf(e,MMVK_ERROR_ARGUMENT,0,"indexed draw exceeds the bound index buffer");
    MMVKResult result=bind_snapshot(c,e);if(result)return result;
    c->dev->CmdDrawIndexed(c->cmd,count,instances,first,base,first_instance);return MMVK_OK;
}
MMVKResult mmvk_commands_end_render(MMVKCommands *c,MMVKError *e)
{
    if(recording(c,e))return MMVK_ERROR_STATE;
    if(!c->in_render)return errorf(e,MMVK_ERROR_STATE,0,"no open render pass");
    c->dev->CmdEndRenderPass(c->cmd);c->in_render=0;c->render_target=NULL;return MMVK_OK;
}
