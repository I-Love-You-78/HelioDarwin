#include "MaliMetalShaderVulkan.h"
#include <stdio.h>
#include <string.h>

static MMVKResult unsupported(MMVKError *error, const char *message) {
    if (error) {
        memset(error, 0, sizeof(*error));
        error->code = MMVK_ERROR_UNSUPPORTED;
        snprintf(error->message, sizeof(error->message), "%s", message);
    }
    return MMVK_ERROR_UNSUPPORTED;
}

static uint32_t backend_stages(uint32_t stages) {
    if (stages & ~(1u | 16u))
        return 0;
    return ((stages & 1u) ? MMVK_STAGE_VERTEX : 0) | ((stages & 16u) ? MMVK_STAGE_FRAGMENT : 0);
}

static MMVKFormat backend_format(uint32_t format) {
    switch (format) {
    case 37:
        return MMVK_FORMAT_RGBA8_UNORM;
    case 43:
        return MMVK_FORMAT_RGBA8_SRGB;
    case 44:
        return MMVK_FORMAT_BGRA8_UNORM;
    case 50:
        return MMVK_FORMAT_BGRA8_SRGB;
    default:
        return 0;
    }
}

MMVKResult MMShaderVulkanDescribe(const MMShaderRenderOutput *output,
                                  const MMVKRenderPipelineDescriptor *requested,
                                  MMShaderVulkanDescription *description, MMVKError *error) {
    if (!output || !requested || !description || output->abi_version != MM_SHADER_ABI_VERSION)
        return unsupported(error, "Missing translated render pipeline description");
    memset(description, 0, sizeof(*description));
    const MMShaderBackendPlan *plan = output->backend_plan;
    if (!plan)
        return unsupported(error, output->backend_plan_error
                                      ? output->backend_plan_error
                                      : "AIR compiler did not provide a supported execution plan");
    if (plan->binding_count > MM_SHADER_MAX_BINDINGS ||
        plan->push_count > MM_SHADER_MAX_PUSH_FIELDS || plan->vertex_binding_count > 31 ||
        plan->vertex_attribute_count > 31 || !plan->color_attachment_count ||
        plan->color_attachment_count > 8)
        return unsupported(error, "Translated execution plan exceeds its ABI bounds");
    if (requested->topology != plan->topology || plan->topology != 3)
        return unsupported(error, "Translated execution currently supports triangle-list topology");
    uint32_t requestedCount = requested->color_attachment_count ? requested->color_attachment_count : 1;
    if (requestedCount != plan->color_attachment_count)
        return unsupported(error,
                           "Render target count differs from the compiled shader attachments");

    MMVKRenderPipelineDescriptor *descriptor = &description->descriptor;
    descriptor->color_attachment_count = plan->color_attachment_count;
    for (uint32_t i = 0; i < plan->color_attachment_count; i++) {
        const MMShaderBackendColor *color = plan->colors + i;
        MMVKFormat format = color->format ? backend_format(color->format) : 0;
        MMVKFormat requestedFormat = requested->color_attachment_count
                                        ? requested->color_formats[i] : requested->color_format;
        if ((color->format && !format) || format != requestedFormat)
            return unsupported(error,
                               "Render target format differs from the compiled shader attachment");
        descriptor->color_formats[i] = format;
        descriptor->color_blends[i] = (MMVKBlend){color->enabled, color->src_rgb, color->dst_rgb,
            color->rgb_op, color->src_alpha, color->dst_alpha, color->alpha_op, color->write_mask};
    }
    for (uint32_t i = 0; i < output->module_count; ++i) {
        const MMShaderStageModule *module = output->modules + i;
        if (!module->words || module->word_count < 5 || module->words[0] != 0x07230203 ||
            module->word_count > SIZE_MAX / sizeof(uint32_t))
            return unsupported(error, "Compiler returned an invalid SPIR-V module span");
        MMVKShader shader = {module->words, module->word_count * sizeof(uint32_t),
                             module->entry_point};
        if (module->vulkan_stage == 1 && !descriptor->vertex.words)
            descriptor->vertex = shader;
        else if (module->vulkan_stage == 16 && !descriptor->fragment.words)
            descriptor->fragment = shader;
        else
            return unsupported(error,
                               "Translated execution requires one vertex and one fragment module");
    }
    if (!descriptor->vertex.words || !descriptor->fragment.words)
        return unsupported(error, "Translated execution requires both render stages");
    for (uint32_t i = 0; i < plan->binding_count; ++i) {
        const MMShaderBackendBinding *binding = plan->bindings + i;
        MMVKBindingLayout *native = description->bindings + i;
        native->set = binding->set;
        native->binding = binding->binding;
        native->stages = backend_stages(binding->vulkan_stages);
        if (!native->stages || binding->metal_stage > MM_SHADER_FRAGMENT)
            return unsupported(error, "Unsupported shader argument stage or slot");
        if (binding->vulkan_type == 1) {
            if (binding->metal_index >= 128 || binding->sampler_index >= 16)
                return unsupported(error, "Unsupported sampled texture or sampler index");
            native->type = MMVK_BIND_COMBINED_IMAGE_SAMPLER;
        } else if (binding->constant_bytes && binding->constant_size && binding->vulkan_type == 7)
            native->type = MMVK_BIND_STORAGE_BUFFER;
        else if (binding->vulkan_type == 6 && binding->metal_index < 31)
            native->type = MMVK_BIND_UNIFORM_BUFFER;
        else if (binding->vulkan_type == 7 && binding->metal_index < 31)
            native->type = MMVK_BIND_STORAGE_BUFFER;
        else
            return unsupported(error, "Unsupported translated resource descriptor");
    }
    for (uint32_t i = 0; i < plan->vertex_binding_count; ++i) {
        const MMShaderBackendVertexBinding *binding = plan->vertex_bindings + i;
        description->vertex_bindings[i] =
            (MMVKVertexBinding){binding->binding, binding->stride, binding->per_instance};
    }
    for (uint32_t i = 0; i < plan->vertex_attribute_count; ++i) {
        const MMShaderBackendVertexAttribute *attribute = plan->vertex_attributes + i;
        description->vertex_attributes[i] = (MMVKVertexAttribute){
            attribute->location, attribute->binding, attribute->format, attribute->offset};
    }
    descriptor->layout.bindings = description->bindings;
    descriptor->layout.binding_count = plan->binding_count;
    descriptor->layout.push_constant_size = plan->push_constant_size;
    descriptor->vertex_bindings = description->vertex_bindings;
    descriptor->vertex_binding_count = plan->vertex_binding_count;
    descriptor->vertex_attributes = description->vertex_attributes;
    descriptor->vertex_attribute_count = plan->vertex_attribute_count;
    descriptor->color_format = descriptor->color_formats[0];
    descriptor->blend = descriptor->color_blends[0];
    descriptor->topology = plan->topology;
    return MMVK_OK;
}

MMVKResult MMShaderVulkanCreateConstants(MMVKDevice *device, const MMShaderRenderOutput *output,
                                         MMShaderVulkanConstants *constants, MMVKError *error) {
    if (!device || !output || !output->backend_plan || !constants)
        return unsupported(error, "Missing compiler constant resource description");
    memset(constants, 0, sizeof(*constants));
    const MMShaderBackendPlan *plan = output->backend_plan;
    if (plan->binding_count > MM_SHADER_MAX_BINDINGS)
        return unsupported(error, "Too many compiler constant resources");
    for (uint32_t i = 0; i < plan->binding_count; ++i) {
        const MMShaderBackendBinding *binding = plan->bindings + i;
        if (!binding->constant_bytes)
            continue;
        if (binding->vulkan_type != 7 || !binding->constant_size ||
            binding->constant_size > binding->minimum_size || binding->minimum_size > SIZE_MAX) {
            MMShaderVulkanDestroyConstants(constants);
            return unsupported(error, "Invalid compiler constant blob span");
        }
        MMVKResult result = mmvk_buffer_create(device, binding->minimum_size, MMVK_BUFFER_STORAGE,
                                               &constants->buffers[i], error);
        if (result != MMVK_OK) {
            MMShaderVulkanDestroyConstants(constants);
            return result;
        }
        void *destination = mmvk_buffer_contents(constants->buffers[i]);
        memset(destination, 0, (size_t)binding->minimum_size);
        memcpy(destination, binding->constant_bytes, binding->constant_size);
        result = mmvk_buffer_flush(constants->buffers[i], 0, binding->minimum_size, error);
        if (result != MMVK_OK) {
            MMShaderVulkanDestroyConstants(constants);
            return result;
        }
    }
    return MMVK_OK;
}

void MMShaderVulkanDestroyConstants(MMShaderVulkanConstants *constants) {
    if (!constants)
        return;
    for (uint32_t i = 0; i < MM_SHADER_MAX_BINDINGS; ++i) {
        mmvk_buffer_destroy(constants->buffers[i]);
        constants->buffers[i] = NULL;
    }
}

MMVKResult MMShaderVulkanBindResources(MMVKCommands *commands, const MMShaderRenderOutput *output,
                                       const MMShaderVulkanBindings *resources, MMVKError *error) {
    if (!commands || !output || !output->backend_plan || !resources)
        return unsupported(error, "Missing compiled buffer binding plan or Metal argument tables");
    const MMShaderVulkanBuffer *vertex_buffers = resources->vertex_buffers;
    const MMShaderVulkanBuffer *fragment_buffers = resources->fragment_buffers;
    const MMShaderBackendPlan *plan = output->backend_plan;
    if (plan->binding_count > MM_SHADER_MAX_BINDINGS ||
        plan->push_count > MM_SHADER_MAX_PUSH_FIELDS || plan->vertex_binding_count > 31)
        return unsupported(error, "Translated execution plan exceeds its ABI bounds");
    /* Validate the entire draw before recording any resource bindings. */
    for (uint32_t i = 0; i < plan->binding_count; ++i) {
        const MMShaderBackendBinding *binding = plan->bindings + i;
        if (binding->vulkan_type == 1) {
            if (binding->metal_index >= 128 || binding->sampler_index >= 16 ||
                binding->metal_stage > MM_SHADER_FRAGMENT)
                return unsupported(error, "Invalid compiled texture/sampler slot");
            MMVKImage *image = (binding->metal_stage == MM_SHADER_VERTEX
                                    ? resources->vertex_textures
                                    : resources->fragment_textures)[binding->metal_index];
            MMVKSampler *sampler = (binding->metal_stage == MM_SHADER_VERTEX
                                        ? resources->vertex_samplers
                                        : resources->fragment_samplers)[binding->sampler_index];
            if (!image || !sampler)
                return unsupported(error, "A statically used texture or sampler is unbound");
            MMVKFormat format = mmvk_image_format(image);
            if (format != MMVK_FORMAT_RGBA8_UNORM && format != MMVK_FORMAT_BGRA8_UNORM)
                return unsupported(error,
                                   "Sampled sRGB or cross-class texture needs a shader variant");
            continue;
        }
        if (binding->constant_bytes) {
            MMVKBuffer *buffer = resources->constant_buffers[i];
            if (!buffer || binding->vulkan_type != 7 || !binding->minimum_size ||
                binding->minimum_size > mmvk_buffer_size(buffer))
                return unsupported(error, "A compiler constant buffer is missing or too short");
            continue;
        }
        if (binding->metal_index >= 31 || binding->metal_stage > MM_SHADER_FRAGMENT)
            return unsupported(error, "Invalid compiled Metal buffer slot");
        const MMShaderVulkanBuffer *buffer =
            (binding->metal_stage == MM_SHADER_VERTEX ? vertex_buffers : fragment_buffers) +
            binding->metal_index;
        if (!buffer->buffer)
            return unsupported(error, "A statically used shader buffer is unbound");
        if (!binding->alignment || buffer->offset % binding->alignment || buffer->offset % 16)
            return unsupported(error,
                               "Shader buffer offset needs a descriptor alignment/range variant");
        uint64_t buffer_size = mmvk_buffer_size(buffer->buffer);
        if (!binding->minimum_size || buffer->offset > buffer_size ||
            binding->minimum_size > buffer_size - buffer->offset)
            return unsupported(error, "Bound shader buffer is shorter than its proven byte window");
    }
    for (uint32_t i = 0; i < plan->vertex_binding_count; ++i) {
        const MMShaderBackendVertexBinding *binding = plan->vertex_bindings + i;
        if (binding->metal_index >= 31 || !vertex_buffers[binding->metal_index].buffer ||
            vertex_buffers[binding->metal_index].offset >=
                mmvk_buffer_size(vertex_buffers[binding->metal_index].buffer))
            return unsupported(error, "A compiled vertex stream is unbound or outside its buffer");
    }
    for (uint32_t i = 0; i < plan->push_count; ++i) {
        const MMShaderBackendPush *push = plan->pushes + i;
        if (push->size != 4 || !backend_stages(push->vulkan_stages) ||
            (push->kind != MM_SHADER_PUSH_BUFFER_OFFSET &&
             push->kind != MM_SHADER_PUSH_COLOR_WRITE_RULE))
            return unsupported(error, "Unsupported translated push-constant field");
    }
    for (uint32_t i = 0; i < plan->binding_count; ++i) {
        const MMShaderBackendBinding *binding = plan->bindings + i;
        if (binding->vulkan_type == 1) {
            MMVKImage *image = (binding->metal_stage == MM_SHADER_VERTEX
                                    ? resources->vertex_textures
                                    : resources->fragment_textures)[binding->metal_index];
            MMVKSampler *sampler = (binding->metal_stage == MM_SHADER_VERTEX
                                        ? resources->vertex_samplers
                                        : resources->fragment_samplers)[binding->sampler_index];
            MMVKResult result = mmvk_commands_bind_image_sampler(
                commands, binding->set, binding->binding, image, sampler, error);
            if (result != MMVK_OK)
                return result;
            continue;
        }
        if (binding->constant_bytes) {
            MMVKResult result = mmvk_commands_bind_buffer(commands, binding->set, binding->binding,
                                                          resources->constant_buffers[i], 0,
                                                          binding->minimum_size, error);
            if (result != MMVK_OK)
                return result;
            continue;
        }
        const MMShaderVulkanBuffer *buffer =
            (binding->metal_stage == MM_SHADER_VERTEX ? vertex_buffers : fragment_buffers) +
            binding->metal_index;
        MMVKResult result =
            mmvk_commands_bind_buffer(commands, binding->set, binding->binding, buffer->buffer,
                                      buffer->offset, binding->minimum_size, error);
        if (result != MMVK_OK)
            return result;
    }
    for (uint32_t i = 0; i < plan->vertex_binding_count; ++i) {
        const MMShaderBackendVertexBinding *binding = plan->vertex_bindings + i;
        const MMShaderVulkanBuffer *buffer = vertex_buffers + binding->metal_index;
        MMVKResult result = mmvk_commands_bind_vertex_buffer(commands, binding->binding,
                                                             buffer->buffer, buffer->offset, error);
        if (result != MMVK_OK)
            return result;
    }
    for (uint32_t i = 0; i < plan->push_count; ++i) {
        const MMShaderBackendPush *push = plan->pushes + i;
        /* The descriptor starts at the aligned Metal bind offset, so its
         * residual is zero even when the Metal buffer offset is nonzero. */
        uint32_t value = push->kind == MM_SHADER_PUSH_BUFFER_OFFSET ? 0 : push->value;
        /* MMVKPipelineLayout uses one range shared by both render stages.
         * Vulkan 01796 requires both flags even for a field used by one stage. */
        MMVKResult result =
            mmvk_commands_push_constants(commands, MMVK_STAGE_VERTEX | MMVK_STAGE_FRAGMENT,
                                         push->offset, &value, sizeof(value), error);
        if (result != MMVK_OK)
            return result;
    }
    return MMVK_OK;
}

MMVKResult MMShaderVulkanBindBuffers(MMVKCommands *commands, const MMShaderRenderOutput *output,
                                     const MMShaderVulkanBuffer vertex_buffers[31],
                                     const MMShaderVulkanBuffer fragment_buffers[31],
                                     MMVKError *error) {
    if (!vertex_buffers || !fragment_buffers)
        return unsupported(error, "Missing Metal buffer argument tables");
    MMShaderVulkanBindings resources = {0};
    memcpy(resources.vertex_buffers, vertex_buffers, sizeof(resources.vertex_buffers));
    memcpy(resources.fragment_buffers, fragment_buffers, sizeof(resources.fragment_buffers));
    return MMShaderVulkanBindResources(commands, output, &resources, error);
}
