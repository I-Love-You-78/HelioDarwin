#include "MaliMetalCompute.h"
#include <stdio.h>
#include <string.h>

static MMShaderStatus compute_error(MMShaderError *error, MMShaderStatus status,
                                    const char *message) {
    if (error) {
        memset(error, 0, sizeof(*error));
        error->status = status;
        snprintf(error->message, sizeof(error->message), "%s", message);
    }
    return status;
}

MMShaderStatus MMShaderCompileCompute(const MMShaderCompiler *compiler,
                                      const MMShaderLibrary *library, uint32_t entry_index,
                                      const MMShaderComputeRequest *options,
                                      MMShaderComputeOutput *output, MMShaderError *error) {
    if (error)
        memset(error, 0, sizeof(*error));
    if (output)
        memset(output, 0, sizeof(*output));
    const MMShaderEntry *entry = MMShaderLibraryGetEntry(library, entry_index);
    if (!entry || entry->stage != MM_SHADER_KERNEL || !options || !output)
        return compute_error(error, MM_SHADER_INVALID_ARGUMENT,
                             "Compute compilation requires an actual kernel entry and options");
    if (!options->device_profile_json || !options->device_profile_size)
        return compute_error(error, MM_SHADER_INVALID_ARGUMENT,
                             "Compute compilation requires an enabled Vulkan device profile");
    if (!compiler)
        compiler = MMShaderGetNativeCompiler();
    if (!compiler || compiler->abi_version != MM_SHADER_ABI_VERSION || !compiler->compile_compute)
        return compute_error(error, MM_SHADER_COMPILER_UNAVAILABLE,
                             "Native AIR compute compiler ABI2 is unavailable");
    MMShaderComputeRequest request = *options;
    request.library_bytes = MMShaderLibraryBytes(library, &request.library_size);
    request.entry_name = entry->name;
    MMShaderStatus status = compiler->compile_compute(&request, output, error);
    if (status != MM_SHADER_OK) {
        MMShaderComputeOutputDestroy(output);
        return status;
    }
    if (output->abi_version != MM_SHADER_COMPUTE_ABI_VERSION || output->stage.vulkan_stage != 32 ||
        !output->stage.words || output->stage.word_count < 5 || !output->stage.entry_point ||
        output->stage.word_count > SIZE_MAX / sizeof(uint32_t) ||
        output->stage.words[0] != 0x07230203 || output->binding_count > MM_SHADER_MAX_BINDINGS ||
        (output->binding_count && !output->bindings) || !output->destroy ||
        !output->prepare_dispatch || !output->max_total_threads_per_threadgroup ||
        !output->thread_execution_width) {
        MMShaderComputeOutputDestroy(output);
        return compute_error(error, MM_SHADER_COMPILE_FAILED,
                             "Native compute compiler returned an incomplete kernel artifact");
    }
    return MM_SHADER_OK;
}

MMShaderStatus MMShaderPrepareComputeDispatch(const MMShaderComputeOutput *output,
                                              const MMShaderComputeBindings *bindings,
                                              const MMShaderComputeDispatchRequest *request,
                                              MMShaderComputeDispatchPlan *plan,
                                              MMShaderError *error) {
    if (error)
        memset(error, 0, sizeof(*error));
    if (plan)
        memset(plan, 0, sizeof(*plan));
    if (!output || output->abi_version != MM_SHADER_COMPUTE_ABI_VERSION ||
        !output->prepare_dispatch || !bindings || !request || !plan)
        return compute_error(error, MM_SHADER_INVALID_ARGUMENT, "Missing compute dispatch state");
    MMShaderStatus status = output->prepare_dispatch(output, bindings, request, plan, error);
    if (status != MM_SHADER_OK) {
        MMShaderComputeDispatchPlanDestroy(plan);
        return status;
    }
    if (plan->abi_version != MM_SHADER_COMPUTE_ABI_VERSION || !plan->destroy ||
        plan->descriptor_count != output->binding_count ||
        (plan->descriptor_count && !plan->descriptors) || (plan->region_count && !plan->regions)) {
        MMShaderComputeDispatchPlanDestroy(plan);
        return compute_error(error, MM_SHADER_COMPILE_FAILED,
                             "Native compute binder returned an incomplete dispatch plan");
    }
    return status;
}

void MMShaderComputeOutputDestroy(MMShaderComputeOutput *output) {
    if (!output)
        return;
    if (output->destroy)
        output->destroy(output);
    memset(output, 0, sizeof(*output));
}

void MMShaderComputeDispatchPlanDestroy(MMShaderComputeDispatchPlan *plan) {
    if (!plan)
        return;
    if (plan->destroy)
        plan->destroy(plan);
    memset(plan, 0, sizeof(*plan));
}
