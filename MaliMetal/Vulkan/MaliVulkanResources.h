/* SPDX-License-Identifier: MIT */
#ifndef MALI_VULKAN_RESOURCES_H
#define MALI_VULKAN_RESOURCES_H
#include "MaliVulkanBackend.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Native format tags are internal values, never Vulkan capability claims. Each
 * create/view operation queries the enabled device's exact format and usage. */
#define MMVK_FORMAT_NATIVE_BASE 0x10000u
#define MMVK_FORMAT_ALPHA8 ((MMVKFormat)0x20001u)
#define MMVK_FORMAT_EXTENDED_BASE 0x30000u
typedef struct MMVKHeap MMVKHeap;
typedef enum { MMVK_STORAGE_SHARED=0, MMVK_STORAGE_PRIVATE=2, MMVK_STORAGE_MEMORYLESS=3 } MMVKStorage;
typedef struct {
    uint32_t width,height,depth,mip_levels,array_layers,samples;
    uint32_t type; /* Metal texture type: 0/1=1D, 2/3=2D, 4=MS2D, 5/6=cube, 7=3D, 8=MS2D-array. */
    uint32_t usage,mutable_format;
    MMVKFormat format;
    uint32_t transient; /* pass-local attachment; no sampling, host or transfer usage. */
} MMVKImageDescriptorEx;
typedef struct {
    MMVKFormat format;
    uint32_t type,base_level,level_count,base_layer,layer_count;
    uint32_t swizzle[4]; /* FINAL Vulkan mapping, identity=0, zero=1, one=2, R/G/B/A=3..6; no implicit format swizzle. */
    uint32_t usage; /* zero inherits parent usage, otherwise a subset. */
    uint32_t aspect; /* zero inherits format aspects; sampled depth/stencil can select one. */
    uint32_t has_metal_swizzle,metal_swizzle[4]; /* Metal zero/one/R/G/B/A=0..5; otherwise inherits source facts. */
} MMVKImageViewDescriptor;
typedef struct {
    uint32_t level,layer,x,y,z,width,height,depth;
    uint32_t aspect; /* Vulkan aspect bit; zero chooses the sole format aspect. */
} MMVKImageRegion;
typedef struct { uint64_t size,alignment; uint32_t memory_type_bits; } MMVKSizeAlign;
enum { MMVK_REPR_NATIVE=0,MMVK_REPR_ALPHA_IN_RED=1,MMVK_REPR_XR_WORDS=2,MMVK_REPR_XR_PLANES=3 };
enum { MMVK_REPR_CANONICAL_DECODE=1u,MMVK_REPR_CANONICAL_ENCODE=2u,MMVK_REPR_HALF_SHADOW_BLEND=4u,MMVK_REPR_PLANE0_HOST_RMW=8u,MMVK_REPR_CANONICAL_MIP_FILTER=16u };
typedef struct {
    uint32_t kind,metal_format,native_format,host_texel_bytes,native_texel_bytes,plane_count,srgb,requirements;
} MMVKFormatRepresentation;
/* Logical XR formats retain their identity. Their raw unsigned backing image is
 * never a promise of Metal sampling/render/blend conversion by native Vulkan. */
MMVKResult mmvk_format_representation(MMVKFormat,MMVKFormatRepresentation *,MMVKError *);
uint32_t mmvk_format_host_texel_size(MMVKFormat);
int mmvk_format_is_extended(MMVKFormat);
int mmvk_format_is_two_plane(MMVKFormat);
MMVKResult mmvk_image_require_native_color_semantics(const MMVKImage *,MMVKError *);
MMVKFormat mmvk_format_from_metal(uint32_t metal_format);
uint32_t mmvk_format_native_code(MMVKFormat);
uint32_t mmvk_format_texel_size(MMVKFormat);
uint32_t mmvk_format_aspects(MMVKFormat);
typedef enum { MMVK_RESOURCE_BUFFER=1, MMVK_RESOURCE_TEXTURE=2, MMVK_RESOURCE_SAMPLER=3 } MMVKResourceKind;
/* IDs are monotonic typed identities, not GPU descriptor handles. Registry
 * acquisition verifies device/kind/liveness and retains the native object.
 * Release acquired references with mmvk_resource_release(). Address acquisition
 * uses only actual queried BDA intervals and the logical user length. As with
 * other native backend objects, caller keeps MMVKDevice alive through every
 * acquired reference and releases references before destroying that device. */
uint64_t mmvk_buffer_resource_id(const MMVKBuffer *);
uint64_t mmvk_image_resource_id(const MMVKImage *);
uint64_t mmvk_sampler_resource_id(const MMVKSampler *);
uint64_t mmvk_buffer_device_address(const MMVKBuffer *);
uint64_t mmvk_buffer_logical_size(const MMVKBuffer *);
MMVKResult mmvk_buffer_set_logical_size(MMVKBuffer *,uint64_t,MMVKError *);
MMVKResult mmvk_resource_acquire(MMVKDevice *,uint64_t,MMVKResourceKind,void **,MMVKError *);
MMVKResult mmvk_resource_acquire_address(MMVKDevice *,uint64_t address,uint64_t length,MMVKBuffer **,uint64_t *offset,MMVKError *);
void mmvk_resource_release(void *,MMVKResourceKind);
typedef struct {
    uint32_t min_filter,mag_filter,mip_filter,s_address_mode,t_address_mode,r_address_mode;
    uint32_t normalized_coordinates,compare_function,border_color,max_anisotropy;
    uint32_t lod_average,support_argument_buffers,reduction_mode;
    float lod_min_clamp,lod_max_clamp;
} MMVKMetalSamplerDescriptor;
/* Exact Metal facts remain independent of any per-use Vulkan translation. */
MMVKResult mmvk_sampler_create_metal(MMVKDevice *,const MMVKMetalSamplerDescriptor *,MMVKSampler **,MMVKError *);
const MMVKMetalSamplerDescriptor *mmvk_sampler_metal_descriptor(const MMVKSampler *);
MMVKResult mmvk_sampler_create_logical(MMVKDevice *,const MMVKSamplerDescriptor *,MMVKSampler **,MMVKError *);
int mmvk_sampler_has_native_state(const MMVKSampler *);
int mmvk_image_is_transient(const MMVKImage *);
MMVKResult mmvk_image_validate_attachment_actions(const MMVKImage *,uint32_t load,uint32_t store,uint32_t stencil_load,uint32_t stencil_store,int resolve_destination,MMVKError *);
/* Synchronous helpers preserve the first observed wait failure. Every
 * nonterminal wait error retains the submission until a signaled fence. Device
 * loss or an unproven failed submit transfers all native ownership to persistent
 * quarantine; device_destroy MUST consult resources_device_quarantined before
 * destroying native device/ICD state. No generic device-loss bit proves DMA idle. */
int mmvk_resources_device_quarantined(const MMVKDevice *);
MMVKResult mmvk_buffer_initialize_zero(MMVKBuffer *,MMVKError *);
MMVKResult mmvk_buffer_create_storage(MMVKDevice *,uint64_t,uint32_t,MMVKStorage,
                                      MMVKHeap *,MMVKBuffer **,MMVKError *);
MMVKResult mmvk_buffer_size_align(MMVKDevice *,uint64_t,uint32_t,MMVKSizeAlign *,MMVKError *);
MMVKResult mmvk_image_create_extended(MMVKDevice *,const MMVKImageDescriptorEx *,MMVKHeap *,MMVKImage **,MMVKError *);
MMVKResult mmvk_image_size_align(MMVKDevice *,const MMVKImageDescriptorEx *,MMVKSizeAlign *,MMVKError *);
MMVKResult mmvk_image_create_view(MMVKImage *,const MMVKImageViewDescriptor *,MMVKImage **,MMVKError *);
const uint32_t *mmvk_image_metal_swizzle(const MMVKImage *);
/* Compose the actual Metal swizzle AFTER canonical format decoding. The result
 * is a final native mapping passed unchanged to image_create_view. */
MMVKResult mmvk_image_compose_swizzle(const MMVKImage *,const uint32_t format_components[4],int shader_swizzle,uint32_t result[4],MMVKError *);
MMVKImage *mmvk_image_root(MMVKImage *);
const MMVKImageDescriptorEx *mmvk_image_descriptor(const MMVKImage *);
const MMVKSamplerDescriptor *mmvk_sampler_descriptor(const MMVKSampler *);
MMVKResult mmvk_commands_copy_image_region(MMVKCommands *,MMVKImage *,const MMVKImageRegion *,MMVKImage *,const MMVKImageRegion *,MMVKError *);
MMVKResult mmvk_commands_copy_buffer_image(MMVKCommands *,MMVKBuffer *,uint64_t,uint64_t,uint64_t,MMVKImage *,const MMVKImageRegion *,int to_image,MMVKError *);
MMVKResult mmvk_commands_generate_mipmaps(MMVKCommands *,MMVKImage *,MMVKError *);
/* Synchronous staging packs only the requested texels. Strides may include
 * padding; capacity is mandatory and checked before touching host memory.
 * Caller serializes CPU access against GPU work. Timed out transfers drain
 * their private submission before releasing temporary buffers. */
typedef struct { void *bytes; size_t capacity,bytes_per_row,bytes_per_image; } MMVKHostPlane;
/* Explicit IOSurface-style plane import/export includes color AND alpha. Metal
 * getBytes/replaceRegion still address plane0 alone through the ordinary APIs. */
MMVKResult mmvk_image_upload_planes(MMVKImage *,const MMVKImageRegion *,const MMVKHostPlane planes[2],MMVKError *);
MMVKResult mmvk_image_readback_planes(MMVKImage *,const MMVKImageRegion *,const MMVKHostPlane planes[2],uint64_t timeout,MMVKError *);
MMVKResult mmvk_image_upload_region(MMVKImage *,const MMVKImageRegion *,const void *,size_t capacity,size_t row,size_t image,MMVKError *);
MMVKResult mmvk_image_readback_region(MMVKImage *,const MMVKImageRegion *,void *,size_t capacity,size_t row,size_t image,uint64_t timeout,MMVKError *);
/* Heaps own one native memory allocation. Resource allocations honor both native
 * alignment and bufferImageGranularity. A resource retains its heap. Marking it
 * aliasable releases its interval; callers must complete all GPU uses first and
 * must never use that resource again. GPU binding paths must reject aliasable
 * resources through mmvk_*_is_aliasable. No sparse/external memory is imported. */
MMVKResult mmvk_heap_create(MMVKDevice *,uint64_t,MMVKStorage,MMVKHeap **,MMVKError *);
void mmvk_heap_retain(MMVKHeap *);
void mmvk_heap_destroy(MMVKHeap *);
uint64_t mmvk_heap_size(const MMVKHeap *);
uint64_t mmvk_heap_used_size(MMVKHeap *);
uint64_t mmvk_heap_max_available(MMVKHeap *,uint64_t alignment);
uint64_t mmvk_buffer_heap_offset(const MMVKBuffer *);
uint64_t mmvk_image_heap_offset(const MMVKImage *);
MMVKResult mmvk_buffer_make_aliasable(MMVKBuffer *,MMVKError *);
MMVKResult mmvk_image_make_aliasable(MMVKImage *,MMVKError *);
int mmvk_buffer_is_aliasable(const MMVKBuffer *);
int mmvk_image_is_aliasable(const MMVKImage *);
#ifdef __cplusplus
}
#endif
#endif
