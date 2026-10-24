#ifndef MALI_METAL_COMPUTE_H
#define MALI_METAL_COMPUTE_H
#include "MaliMetalShader.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MM_SHADER_COMPUTE_ABI_VERSION 2u
enum {
    MM_SHADER_COMPUTE_BUFFER = 1,
    MM_SHADER_COMPUTE_CONSTANT = 2,
    MM_SHADER_COMPUTE_TEXTURE = 3,
    MM_SHADER_COMPUTE_SAMPLER = 4,
    MM_SHADER_COMPUTE_TABLE = 5,
    MM_SHADER_COMPUTE_COMBINED = 6,
    MM_SHADER_COMPUTE_STATUS = 7
};

typedef struct MMShaderComputeRequest {
    const void *library_bytes;
    size_t library_size;
    const char *entry_name;
    const char *device_profile_json;
    size_t device_profile_size;
    const MMShaderConstantValue *constants;
    size_t constant_count;
    uint32_t max_total_threads_per_threadgroup;
    uint32_t threadgroup_size_multiple_of_execution_width;
    uint32_t required_threads_per_threadgroup[3]; /* All zero = no requirement. */
    uint32_t max_call_stack_depth;                /* Zero = Metal default 1. */
    uint32_t timeout_ms;
} MMShaderComputeRequest;

typedef struct MMShaderComputeBinding {
    uint32_t set, binding, vulkan_type, source_kind, metal_index;
    uint32_t read, write;
    const uint8_t *constant_bytes;
    size_t constant_size;
} MMShaderComputeBinding;

typedef struct MMShaderComputeBuffer {
    uint64_t length, offset; /* Logical Metal length; native backing is padded to 16. */
    uint32_t bound;
    uint64_t gpu_address, device_address; /* Zero means no supplied address. */
    uint32_t attribute_stride, has_attribute_stride;
} MMShaderComputeBuffer;
typedef struct MMShaderComputeTexture {
    uint32_t bound, pixel_format;
    uint32_t texture_type, sample_count, usage, array_length, mipmap_level_count;
    uint32_t sparse, depth_twin, swizzle[4], parent_relative_level, parent_base_size[3];
    uint32_t own_format_views;
    uint64_t resource_id;
} MMShaderComputeTexture;
typedef struct MMShaderComputeSampler {
    uint32_t bound, min_filter, mag_filter, mip_filter, address[3], border_color;
    uint32_t normalized_coordinates, lod_average, max_anisotropy, compare_function, reduction_mode;
    float lod_min_clamp, lod_max_clamp;
} MMShaderComputeSampler;
typedef struct MMShaderComputeBindings {
    MMShaderComputeBuffer buffers[31];
    MMShaderComputeTexture textures[128];
    uint32_t threadgroup_lengths[31]; /* setThreadgroupMemoryLength, bytes. */
    MMShaderComputeSampler samplers[16];
    uint32_t stage_in_region_bound, stage_in_origin[3], stage_in_size[3];
    uint32_t imageblock_size[2];
} MMShaderComputeBindings;

enum { MM_SHADER_DISPATCH_THREADGROUPS = 0, MM_SHADER_DISPATCH_THREADS = 1,
       MM_SHADER_DISPATCH_INDIRECT = 2 };
typedef struct MMShaderComputeDispatchRequest {
    uint32_t kind;
    uint32_t grid[3]; /* Groups or threads according to kind, never silently rounded. */
    uint32_t threads_per_threadgroup[3];
    uint64_t indirect_length, indirect_offset; /* Logical source length, 12-byte arguments. */
} MMShaderComputeDispatchRequest;

typedef struct MMShaderSpecializationEntry {
    uint32_t id, offset;
    size_t size;
} MMShaderSpecializationEntry;
typedef struct MMShaderComputeDescriptor {
    uint32_t set, binding, vulkan_type, source_kind, metal_index;
    uint64_t offset, range;
    /* Canonical view of the actual bound texture; Vulkan numeric enums. */
    uint32_t view_type, view_format, view_aspect, view_layout, view_components[4];
    uint32_t view_level, view_has_level, view_twin, view_shader_swizzle;
    /* Exact canonical native sampler; one sampler is created for this use. */
    uint32_t sampler_min_filter, sampler_mag_filter, sampler_mip_filter, sampler_address[3];
    uint32_t sampler_compare_enabled, sampler_compare_op, sampler_border_color;
    float sampler_min_lod, sampler_max_lod, sampler_lod_bias, sampler_max_anisotropy;
    uint32_t sampler_reduction, sampler_has_border_components, sampler_border_components[4];
    const uint8_t *upload_bytes;
    size_t upload_size;
} MMShaderComputeDescriptor;
typedef struct MMShaderComputePush {
    uint32_t offset, size;
    const uint8_t *bytes;
} MMShaderComputePush;
typedef struct MMShaderComputeRegion {
    uint32_t group_count[3];
    const MMShaderSpecializationEntry *specialization;
    uint32_t specialization_count;
    const uint8_t *specialization_data;
    size_t specialization_size;
    const MMShaderComputePush *pushes;
    uint32_t push_count;
    uint32_t stage_flags, required_subgroup_size;
    uint32_t indirect_kind; /* 0=direct, 1=source buffer, 2=clamp scratch. */
    uint64_t indirect_offset;
} MMShaderComputeRegion;
typedef struct MMShaderComputeDispatchPlan {
    uint32_t abi_version, descriptor_count, region_count;
    const MMShaderComputeDescriptor *descriptors;
    const MMShaderComputeRegion *regions;
    void *private_data;
    void (*destroy)(struct MMShaderComputeDispatchPlan *plan);
    MMShaderStageModule stage; /* The settled shader variant, owned by this plan. */
    const MMShaderComputeBinding *bindings;
    uint32_t binding_count, push_constant_size;
    MMShaderStageModule indirect_clamp_stage;
    uint64_t indirect_source_offset, indirect_source_range;
    uint32_t indirect_residual;
} MMShaderComputeDispatchPlan;

typedef struct MMShaderComputeOutput {
    uint32_t abi_version;
    MMShaderStageModule stage; /* Generic kernel; local size is specialized per dispatch. */
    const MMShaderComputeBinding *bindings;
    uint32_t binding_count, push_constant_size;
    uint32_t max_total_threads_per_threadgroup, thread_execution_width;
    uint32_t static_threadgroup_memory_length;
    const char *metadata_json;
    size_t metadata_size;
    void *private_data;
    void (*destroy)(struct MMShaderComputeOutput *output);
    MMShaderStatus (*prepare_dispatch)(const struct MMShaderComputeOutput *,
                                       const MMShaderComputeBindings *,
                                       const MMShaderComputeDispatchRequest *,
                                       MMShaderComputeDispatchPlan *, MMShaderError *);
} MMShaderComputeOutput;

/* Only a real AIR kernel is accepted. MSL compilation is not provided.
 * Output owns all returned views until destroy. A dispatch plan independently
 * owns its specialization/push bytes until plan destroy. Keep the output alive
 * while preparing dispatches; retain native resources through GPU completion. */
MMShaderStatus MMShaderCompileCompute(const MMShaderCompiler *compiler,
                                      const MMShaderLibrary *library, uint32_t entry_index,
                                      const MMShaderComputeRequest *options,
                                      MMShaderComputeOutput *output, MMShaderError *error);
MMShaderStatus MMShaderPrepareComputeDispatch(const MMShaderComputeOutput *output,
                                              const MMShaderComputeBindings *bindings,
                                              const MMShaderComputeDispatchRequest *request,
                                              MMShaderComputeDispatchPlan *plan,
                                              MMShaderError *error);
void MMShaderComputeOutputDestroy(MMShaderComputeOutput *output);
void MMShaderComputeDispatchPlanDestroy(MMShaderComputeDispatchPlan *plan);

#ifdef __cplusplus
}
#endif
#endif
