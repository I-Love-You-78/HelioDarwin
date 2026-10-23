#ifndef MALI_METAL_COMPUTE_H
#define MALI_METAL_COMPUTE_H
#include "MaliMetalShader.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MM_SHADER_COMPUTE_ABI_VERSION 1u
enum {
    MM_SHADER_COMPUTE_BUFFER = 1,
    MM_SHADER_COMPUTE_CONSTANT = 2,
    MM_SHADER_COMPUTE_TEXTURE = 3
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
} MMShaderComputeBuffer;
typedef struct MMShaderComputeTexture {
    uint32_t bound, pixel_format; /* Actual MTLPixelFormat; 2D/one level/one layer. */
} MMShaderComputeTexture;
typedef struct MMShaderComputeBindings {
    MMShaderComputeBuffer buffers[31];
    MMShaderComputeTexture textures[128];
    uint32_t threadgroup_lengths[31]; /* setThreadgroupMemoryLength, bytes. */
} MMShaderComputeBindings;

enum { MM_SHADER_DISPATCH_THREADGROUPS = 0, MM_SHADER_DISPATCH_THREADS = 1 };
typedef struct MMShaderComputeDispatchRequest {
    uint32_t kind;
    uint32_t grid[3]; /* Groups or threads according to kind, never silently rounded. */
    uint32_t threads_per_threadgroup[3];
} MMShaderComputeDispatchRequest;

typedef struct MMShaderSpecializationEntry {
    uint32_t id, offset;
    size_t size;
} MMShaderSpecializationEntry;
typedef struct MMShaderComputeDescriptor {
    uint32_t set, binding, vulkan_type, source_kind, metal_index;
    uint64_t offset, range;
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
} MMShaderComputeRegion;
typedef struct MMShaderComputeDispatchPlan {
    uint32_t abi_version, descriptor_count, region_count;
    const MMShaderComputeDescriptor *descriptors;
    const MMShaderComputeRegion *regions;
    void *private_data;
    void (*destroy)(struct MMShaderComputeDispatchPlan *plan);
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
