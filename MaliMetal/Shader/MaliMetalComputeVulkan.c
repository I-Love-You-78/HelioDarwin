#include "MaliMetalComputeVulkan.h"
#include <stdio.h>
#include <string.h>

static MMVKResult invalid(MMVKError *error, const char *message) {
    if (error) {
        memset(error, 0, sizeof(*error));
        error->code = MMVK_ERROR_UNSUPPORTED;
        snprintf(error->message, sizeof(error->message), "%s", message);
    }
    return MMVK_ERROR_UNSUPPORTED;
}

static MMVKBindingType binding_type(const MMShaderComputeBinding *binding) {
    switch (binding->source_kind) {
    case MM_SHADER_COMPUTE_BUFFER:
        if (binding->metal_index >= 31)
            return 0;
        if (binding->vulkan_type == 6)
            return MMVK_BIND_UNIFORM_BUFFER;
        if (binding->vulkan_type == 7)
            return MMVK_BIND_STORAGE_BUFFER;
        break;
    case MM_SHADER_COMPUTE_CONSTANT:
        if (binding->vulkan_type == 7 && binding->constant_bytes && binding->constant_size)
            return MMVK_BIND_STORAGE_BUFFER;
        break;
    case MM_SHADER_COMPUTE_TEXTURE:
        if (binding->metal_index < 128 && binding->vulkan_type == 2 && binding->read &&
            !binding->write)
            return MMVK_BIND_SAMPLED_IMAGE;
        break;
    }
    return 0;
}

static int valid_output(const MMShaderComputeOutput *output) {
    return output && output->abi_version == MM_SHADER_COMPUTE_ABI_VERSION &&
           output->binding_count <= MM_SHADER_MAX_BINDINGS &&
           (!output->binding_count || output->bindings);
}

MMVKResult MMShaderVulkanDescribeCompute(const MMShaderComputeOutput *output,
                                         MMShaderVulkanComputeDescription *description,
                                         MMVKError *error) {
    if (!valid_output(output) || !description)
        return invalid(error, "Missing compiled compute module or binding layout");
    memset(description, 0, sizeof(*description));
    const MMShaderStageModule *stage = &output->stage;
    if (stage->vulkan_stage != 32 || !stage->words || stage->word_count < 5 ||
        stage->word_count > SIZE_MAX / sizeof(uint32_t) || stage->words[0] != 0x07230203 ||
        !stage->entry_point)
        return invalid(error, "Invalid compiled compute SPIR-V span");
    for (uint32_t i = 0; i < output->binding_count; ++i) {
        const MMShaderComputeBinding *binding = output->bindings + i;
        MMVKBindingType type = binding_type(binding);
        if (!type)
            return invalid(error, "Unsupported translated compute descriptor source");
        for (uint32_t j = 0; j < i; ++j) {
            const MMShaderComputeBinding *previous = output->bindings + j;
            if (previous->set == binding->set && previous->binding == binding->binding)
                return invalid(error, "Duplicate translated compute descriptor");
        }
        description->bindings[i] =
            (MMVKBindingLayout){binding->set, binding->binding, MMVK_STAGE_COMPUTE, type};
    }
    description->shader =
        (MMVKShader){stage->words, stage->word_count * sizeof(uint32_t), stage->entry_point};
    description->layout = (MMVKPipelineLayout){description->bindings, output->binding_count,
                                               output->push_constant_size};
    return MMVK_OK;
}

MMVKResult MMShaderVulkanCreateComputeConstants(MMVKDevice *device,
                                                const MMShaderComputeOutput *output,
                                                MMShaderVulkanConstants *constants,
                                                MMVKError *error) {
    if (!device || !valid_output(output) || !constants)
        return invalid(error, "Missing compute constant resource description");
    memset(constants, 0, sizeof(*constants));
    for (uint32_t i = 0; i < output->binding_count; ++i) {
        const MMShaderComputeBinding *binding = output->bindings + i;
        if (binding->source_kind != MM_SHADER_COMPUTE_CONSTANT)
            continue;
        if (!binding_type(binding) || binding->constant_size > SIZE_MAX - 15) {
            MMShaderVulkanDestroyConstants(constants);
            return invalid(error, "Invalid compute constant blob");
        }
        size_t size = (binding->constant_size + 15) & ~(size_t)15;
        MMVKResult result =
            mmvk_buffer_create(device, size, MMVK_BUFFER_STORAGE, constants->buffers + i, error);
        if (result != MMVK_OK) {
            MMShaderVulkanDestroyConstants(constants);
            return result;
        }
        void *destination = mmvk_buffer_contents(constants->buffers[i]);
        if (!destination) {
            MMShaderVulkanDestroyConstants(constants);
            return invalid(error, "Compute constant buffer is not host writable");
        }
        memset(destination, 0, size);
        memcpy(destination, binding->constant_bytes, binding->constant_size);
        result = mmvk_buffer_flush(constants->buffers[i], 0, size, error);
        if (result != MMVK_OK) {
            MMShaderVulkanDestroyConstants(constants);
            return result;
        }
    }
    return MMVK_OK;
}

MMVKResult MMShaderVulkanComputeBindingFacts(const MMShaderVulkanComputeResources *resources,
                                             MMShaderComputeBindings *facts, MMVKError *error) {
    if (!resources || !facts)
        return invalid(error, "Missing compute argument tables");
    memset(facts, 0, sizeof(*facts));
    for (uint32_t i = 0; i < 31; ++i) {
        const MMShaderVulkanBuffer *buffer = resources->buffers + i;
        if (!buffer->buffer)
            continue;
        uint64_t length = resources->logical_lengths[i];
        if (!length || length > mmvk_buffer_size(buffer->buffer) || buffer->offset > length)
            return invalid(error, "Compute buffer exceeds its logical Metal length");
        facts->buffers[i] = (MMShaderComputeBuffer){length, buffer->offset, 1};
    }
    for (uint32_t i = 0; i < 128; ++i) {
        MMVKImage *image = resources->textures[i];
        if (!image)
            continue;
        MMVKFormat format = mmvk_image_format(image);
        uint32_t pixel_format = 0;
        switch (format) {
        case MMVK_FORMAT_RGBA8_UNORM:
            pixel_format = 70;
            break;
        case MMVK_FORMAT_RGBA8_SRGB:
            pixel_format = 71;
            break;
        case MMVK_FORMAT_BGRA8_UNORM:
            pixel_format = 80;
            break;
        case MMVK_FORMAT_BGRA8_SRGB:
            pixel_format = 81;
            break;
        }
        if (!pixel_format)
            return invalid(error, "Compute image requires an unbuilt format variant");
        facts->textures[i] = (MMShaderComputeTexture){1, pixel_format};
    }
    memcpy(facts->threadgroup_lengths, resources->threadgroup_lengths,
           sizeof(facts->threadgroup_lengths));
    return MMVK_OK;
}

static const MMShaderComputeBinding *find_binding(const MMShaderComputeOutput *output,
                                                  const MMShaderComputeDescriptor *descriptor,
                                                  uint32_t *index) {
    for (uint32_t i = 0; i < output->binding_count; ++i) {
        const MMShaderComputeBinding *binding = output->bindings + i;
        if (binding->set == descriptor->set && binding->binding == descriptor->binding) {
            *index = i;
            return binding;
        }
    }
    return NULL;
}

MMVKResult MMShaderVulkanBindComputeResources(MMVKCommands *commands,
                                              const MMShaderComputeOutput *output,
                                              const MMShaderVulkanComputeResources *resources,
                                              const MMShaderComputeDispatchPlan *plan,
                                              MMVKError *error) {
    if (!commands || !valid_output(output) || !resources || !plan ||
        plan->abi_version != MM_SHADER_COMPUTE_ABI_VERSION ||
        plan->descriptor_count != output->binding_count ||
        (plan->descriptor_count && !plan->descriptors))
        return invalid(error, "Missing instantiated compute descriptors");
    MMVKBuffer *buffers[MM_SHADER_MAX_BINDINGS] = {0};
    MMVKImage *images[MM_SHADER_MAX_BINDINGS] = {0};
    uint64_t seen = 0;
    /* Validate all spans before recording any native resource bindings. */
    for (uint32_t i = 0; i < plan->descriptor_count; ++i) {
        const MMShaderComputeDescriptor *descriptor = plan->descriptors + i;
        uint32_t index = 0;
        const MMShaderComputeBinding *binding = find_binding(output, descriptor, &index);
        if (!binding || !binding_type(binding) || (seen & ((uint64_t)1 << index)) ||
            descriptor->vulkan_type != binding->vulkan_type ||
            descriptor->source_kind != binding->source_kind ||
            descriptor->metal_index != binding->metal_index)
            return invalid(error, "Compute dispatch descriptor differs from its compiled layout");
        seen |= (uint64_t)1 << index;
        switch (binding->source_kind) {
        case MM_SHADER_COMPUTE_BUFFER:
            buffers[i] = resources->buffers[binding->metal_index].buffer;
            break;
        case MM_SHADER_COMPUTE_CONSTANT:
            buffers[i] = resources->constant_buffers[index];
            break;
        case MM_SHADER_COMPUTE_TEXTURE:
            images[i] = resources->textures[binding->metal_index];
            if (!images[i] || (mmvk_image_format(images[i]) != MMVK_FORMAT_RGBA8_UNORM &&
                               mmvk_image_format(images[i]) != MMVK_FORMAT_BGRA8_UNORM))
                return invalid(error, "A used compute image is missing or needs a format variant");
            continue;
        }
        if (!buffers[i] || !descriptor->range ||
            descriptor->offset > mmvk_buffer_size(buffers[i]) ||
            descriptor->range > mmvk_buffer_size(buffers[i]) - descriptor->offset)
            return invalid(error, "Instantiated compute descriptor exceeds its native buffer");
    }
    for (uint32_t i = 0; i < plan->descriptor_count; ++i) {
        const MMShaderComputeDescriptor *descriptor = plan->descriptors + i;
        MMVKResult result =
            images[i] ? mmvk_commands_bind_image(commands, descriptor->set, descriptor->binding,
                                                 images[i], error)
                      : mmvk_commands_bind_buffer(commands, descriptor->set, descriptor->binding,
                                                  buffers[i], descriptor->offset, descriptor->range,
                                                  error);
        if (result != MMVK_OK)
            return result;
    }
    return MMVK_OK;
}

MMVKResult MMShaderVulkanPushComputeRegion(MMVKCommands *commands,
                                           const MMShaderComputeOutput *output,
                                           const MMShaderComputeRegion *region, MMVKError *error) {
    if (!commands || !valid_output(output) || !region || (region->push_count && !region->pushes))
        return invalid(error, "Missing compute dispatch region push constants");
    for (uint32_t i = 0; i < region->push_count; ++i) {
        const MMShaderComputePush *push = region->pushes + i;
        if (!push->bytes || !push->size || push->offset % 4 || push->size % 4 ||
            push->offset > output->push_constant_size ||
            push->size > output->push_constant_size - push->offset)
            return invalid(error, "Compute push write exceeds the compiled layout");
    }
    for (uint32_t i = 0; i < region->push_count; ++i) {
        const MMShaderComputePush *push = region->pushes + i;
        MMVKResult result = mmvk_commands_push_constants(commands, MMVK_STAGE_COMPUTE, push->offset,
                                                         push->bytes, push->size, error);
        if (result != MMVK_OK)
            return result;
    }
    return MMVK_OK;
}
