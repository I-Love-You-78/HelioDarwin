#include "MaliMetalComputeVulkan.h"
#include "../Vulkan/MaliVulkanInternal.h"
#include <stdlib.h>
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
        if (binding->metal_index < 128 && binding->vulkan_type == 2)
            return MMVK_BIND_SAMPLED_IMAGE;
        if (binding->metal_index < 128 && binding->vulkan_type == 3)
            return MMVK_BIND_STORAGE_IMAGE;
        break;
    case MM_SHADER_COMPUTE_SAMPLER:
        if (binding->vulkan_type == 0) return MMVK_BIND_SAMPLER;
        break;
    case MM_SHADER_COMPUTE_COMBINED:
        if (binding->metal_index < 128 && binding->vulkan_type == 1) return MMVK_BIND_COMBINED_IMAGE_SAMPLER;
        break;
    case MM_SHADER_COMPUTE_TABLE:
    case MM_SHADER_COMPUTE_STATUS:
        if (binding->vulkan_type == 6) return MMVK_BIND_UNIFORM_BUFFER;
        if (binding->vulkan_type == 7) return MMVK_BIND_STORAGE_BUFFER;
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
        if (mmvk_buffer_is_aliasable(buffer->buffer)) return invalid(error, "Compute buffer is aliasable");
        facts->buffers[i] = (MMShaderComputeBuffer){.length=length,.offset=buffer->offset,.bound=1};
    }
    for (uint32_t i = 0; i < 128; ++i) {
        MMVKImage *image = resources->textures[i];
        if (!image)
            continue;
        if (mmvk_image_is_aliasable(image)) return invalid(error, "Compute image is aliasable");
        const MMVKImageDescriptorEx *desc = mmvk_image_descriptor(image);
        const MMVKImageDescriptorEx *root = mmvk_image_descriptor(mmvk_image_root(image));
        uint32_t pixel = resources->metal_pixel_formats[i];
        if (!pixel) {
            switch(mmvk_image_format(image)) {
            case MMVK_FORMAT_RGBA8_UNORM: pixel=70; break;
            case MMVK_FORMAT_RGBA8_SRGB: pixel=71; break;
            case MMVK_FORMAT_BGRA8_UNORM: pixel=80; break;
            case MMVK_FORMAT_BGRA8_SRGB: pixel=81; break;
            default: return invalid(error,"Compute texture requires its actual Metal pixel format");
            }
        }
        if (!desc || !root || mmvk_format_from_metal(pixel) != desc->format)
            return invalid(error,"Compute texture Metal format differs from its native view");
        MMShaderComputeTexture *t = facts->textures + i;
        t->bound=1; t->pixel_format=pixel; t->texture_type=desc->type; t->sample_count=desc->samples;
        t->usage=((desc->usage&MMVK_IMAGE_SAMPLED)?1u:0u)|((desc->usage&MMVK_IMAGE_STORAGE)?2u:0u)|((desc->usage&MMVK_IMAGE_RENDER_TARGET)?4u:0u);
        t->array_length=(desc->type==5||desc->type==6)?desc->array_layers/6:desc->array_layers;
        t->mipmap_level_count=desc->mip_levels;
        t->own_format_views=!root->mutable_format;
        t->parent_relative_level=image->range.baseMipLevel;
        t->parent_base_size[0]=root->width; t->parent_base_size[1]=root->height; t->parent_base_size[2]=root->depth;
        const uint32_t *metal_swizzle=mmvk_image_metal_swizzle(image);
        if(!metal_swizzle)return invalid(error,"Compute image lacks immutable Metal swizzle facts");
        memcpy(t->swizzle,metal_swizzle,sizeof(t->swizzle));
    }
    memcpy(facts->samplers,resources->sampler_facts,sizeof(facts->samplers));
    facts->stage_in_region_bound=resources->stage_in_region_bound;
    memcpy(facts->stage_in_origin,resources->stage_in_origin,sizeof(facts->stage_in_origin));
    memcpy(facts->stage_in_size,resources->stage_in_size,sizeof(facts->stage_in_size));
    memcpy(facts->imageblock_size,resources->imageblock_size,sizeof(facts->imageblock_size));
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
    MMShaderComputeOutput settled;
    if (output && plan) {
        settled=*output; settled.stage=plan->stage; settled.bindings=plan->bindings;
        settled.binding_count=plan->binding_count; settled.push_constant_size=plan->push_constant_size;
        output=&settled;
    }
    if (!commands || !valid_output(output) || !resources || !plan ||
        plan->abi_version != MM_SHADER_COMPUTE_ABI_VERSION ||
        plan->descriptor_count != output->binding_count ||
        (plan->descriptor_count && !plan->descriptors))
        return invalid(error, "Missing instantiated compute descriptors");
    MMVKBuffer *buffers[MM_SHADER_MAX_BINDINGS] = {0};
    MMVKImage *images[MM_SHADER_MAX_BINDINGS] = {0};
    MMVKSampler *samplers[MM_SHADER_MAX_BINDINGS] = {0};
    uint64_t seen = 0;
    /* Validate all spans before recording any native resource bindings. */
    for (uint32_t i = 0; i < plan->descriptor_count; ++i) {
        const MMShaderComputeDescriptor *descriptor = plan->descriptors + i;
        uint32_t index = 0;
        const MMShaderComputeBinding *binding = find_binding(output, descriptor, &index);
        if (!binding || !binding_type(binding) || (seen & ((uint64_t)1 << index)) ||
            descriptor->vulkan_type != binding->vulkan_type ||
            descriptor->source_kind != binding->source_kind)
            return invalid(error, "Compute dispatch descriptor differs from its compiled layout");
        seen |= (uint64_t)1 << index;
        switch (binding->source_kind) {
        case MM_SHADER_COMPUTE_BUFFER:
            buffers[i] = resources->buffers[binding->metal_index].buffer;
            break;
        case MM_SHADER_COMPUTE_CONSTANT:
            buffers[i] = resources->dispatch_buffers[index] ? resources->dispatch_buffers[index] : resources->constant_buffers[index];
            break;
        case MM_SHADER_COMPUTE_TABLE:
        case MM_SHADER_COMPUTE_STATUS:
            buffers[i] = resources->dispatch_buffers[index];
            break;
        case MM_SHADER_COMPUTE_SAMPLER:
            samplers[i]=resources->dispatch_samplers[index];
            if(!samplers[i])return invalid(error,"Missing canonical compute sampler");
            continue;
        case MM_SHADER_COMPUTE_COMBINED:
            samplers[i]=resources->dispatch_samplers[index];
            if(!samplers[i])return invalid(error,"Missing canonical combined sampler");
            /* fall through */
        case MM_SHADER_COMPUTE_TEXTURE:
            if(descriptor->metal_index>=128)return invalid(error,"Invalid planned compute texture slot");
            images[i] = resources->dispatch_images[index];
            if (!images[i] || mmvk_image_is_aliasable(images[i]))
                return invalid(error,"Missing canonical compute image view");
            continue;
        }
        if (!buffers[i] || mmvk_buffer_is_aliasable(buffers[i]) || !descriptor->range ||
            descriptor->offset > mmvk_buffer_size(buffers[i]) ||
            descriptor->range > mmvk_buffer_size(buffers[i]) - descriptor->offset)
            return invalid(error, "Instantiated compute descriptor exceeds its native buffer");
    }
    for (uint32_t i = 0; i < plan->descriptor_count; ++i) {
        const MMShaderComputeDescriptor *descriptor = plan->descriptors + i;
        MMVKResult result =
            images[i] && samplers[i] ? mmvk_commands_bind_image_sampler(commands,descriptor->set,descriptor->binding,images[i],samplers[i],error)
                      : images[i] ? mmvk_commands_bind_image(commands, descriptor->set, descriptor->binding,
                                                 images[i], error)
                      : samplers[i] ? mmvk_commands_bind_sampler(commands,descriptor->set,descriptor->binding,samplers[i],error)
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
struct MMShaderVulkanComputeExecution {
    MMShaderComputeOutput output;
    const MMShaderComputeDispatchPlan *plan;
    MMShaderVulkanComputeResources resources;
    MMVKPipeline **pipelines;
    MMVKPipeline *clamp_pipeline;
    MMVKBuffer *scratch, *status;
};

static MMVKResult compute_upload(MMVKDevice *device, const uint8_t *bytes, size_t length,
                                 uint32_t usage, MMVKBuffer **out, MMVKError *error) {
    if (!bytes || !length || length > SIZE_MAX-15)
        return invalid(error,"Invalid compute upload span");
    size_t padded=(length+15)&~(size_t)15;
    MMVKResult result=mmvk_buffer_create(device,padded,usage,out,error);
    if(result!=MMVK_OK)return result;
    void *destination=mmvk_buffer_contents(*out);
    if(!destination)return invalid(error,"Compute upload is not host visible");
    memset(destination,0,padded);memcpy(destination,bytes,length);
    return mmvk_buffer_flush(*out,0,padded,error);
}

static MMVKResult compute_view(MMVKImage *source,const MMShaderComputeDescriptor *d,
                                MMVKImage **out,MMVKError *error) {
    if(!source||mmvk_image_is_aliasable(source)||d->view_twin)
        return invalid(error,"Missing compute image or unavailable depth twin");
    const MMVKImageDescriptorEx *x=mmvk_image_descriptor(source);
    if(!x||!d->view_aspect||
       (d->view_aspect&~mmvk_format_aspects((MMVKFormat)(MMVK_FORMAT_NATIVE_BASE+d->view_format))))
        return invalid(error,"Compute view has incompatible aspects");
    uint32_t type;
    switch(d->view_type){
    case VK_IMAGE_VIEW_TYPE_1D:type=0;break;
    case VK_IMAGE_VIEW_TYPE_1D_ARRAY:type=1;break;
    case VK_IMAGE_VIEW_TYPE_2D:type=x->samples>1?4:2;break;
    case VK_IMAGE_VIEW_TYPE_2D_ARRAY:type=x->samples>1?8:3;break;
    case VK_IMAGE_VIEW_TYPE_CUBE:type=5;break;
    case VK_IMAGE_VIEW_TYPE_CUBE_ARRAY:type=6;break;
    case VK_IMAGE_VIEW_TYPE_3D:type=7;break;
    default:return invalid(error,"Unknown canonical compute image view type");
    }
    if(d->view_has_level&&d->view_level>=x->mip_levels)
        return invalid(error,"Compute image view level exceeds its Metal texture");
    MMVKImageViewDescriptor v={0};
    v.format=(MMVKFormat)(MMVK_FORMAT_NATIVE_BASE+d->view_format);
    /* Preserve named legacy formats when the canonical view uses that format. */
    if(mmvk_format(x->format)==(VkFormat)d->view_format)v.format=x->format;
    v.type=type;v.base_level=d->view_has_level?d->view_level:0;
    v.level_count=d->view_has_level?1:x->mip_levels;v.layer_count=x->array_layers;
    /* Metal swizzles are applied after the compiler's format mapping. Native
     * source components already include that mapping and cannot be composed a
     * second time (notably Alpha8 and storage aliases). Shader-applied swizzles
     * take the canonical mapping alone. The helper preserves constants and
     * canonicalizes channels from themselves to Vulkan IDENTITY. */
    MMVKResult result=mmvk_image_compose_swizzle(source,d->view_components,
        d->view_shader_swizzle!=0,v.swizzle,error);
    if(result!=MMVK_OK)return result;
    v.aspect=d->view_aspect;
    v.usage=d->vulkan_type==3?MMVK_IMAGE_STORAGE:MMVK_IMAGE_SAMPLED;
    return mmvk_image_create_view(source,&v,out,error);
}

static MMVKResult compute_sampler(MMVKDevice *device,const MMShaderComputeDescriptor *d,
                                   MMVKSampler **out,MMVKError *error){
    if(d->sampler_has_border_components)
        return invalid(error,"Native sampler border mapping is unavailable");
    MMVKSamplerDescriptor s={0};
    s.min_filter=d->sampler_min_filter;s.mag_filter=d->sampler_mag_filter;s.mip_filter=d->sampler_mip_filter;
    s.address_u=d->sampler_address[0];s.address_v=d->sampler_address[1];s.address_w=d->sampler_address[2];
    /* The compiler emulates Metal pixel coordinates; its Vulkan sampler is normalized. */
    s.normalized_coordinates=1;s.compare_enabled=d->sampler_compare_enabled;s.compare_op=d->sampler_compare_op;
    s.border_color=d->sampler_border_color;s.min_lod=d->sampler_min_lod;s.max_lod=d->sampler_max_lod;
    s.lod_bias=d->sampler_lod_bias;s.max_anisotropy=d->sampler_max_anisotropy;
    /* The strict creator validates this enum, comparison compatibility and the
     * actually enabled samplerFilterMinmax feature before chaining the native
     * reduction descriptor. Never replace min/max with weighted average. */
    s.reduction_mode=d->sampler_reduction;
    return mmvk_sampler_create(device,&s,out,error);
}

void MMShaderVulkanDestroyComputeExecution(MMShaderVulkanComputeExecution *execution){
    if(!execution)return;
    for(uint32_t i=0;i<execution->output.binding_count;i++){
        MMVKBuffer *b=execution->resources.dispatch_buffers[i];
        if(b&&b!=execution->status)mmvk_buffer_destroy(b);
        mmvk_image_destroy(execution->resources.dispatch_images[i]);
        mmvk_sampler_destroy(execution->resources.dispatch_samplers[i]);
    }
    if(execution->pipelines)for(uint32_t i=0;i<execution->plan->region_count;i++)
        mmvk_pipeline_destroy(execution->pipelines[i]);
    free(execution->pipelines);mmvk_pipeline_destroy(execution->clamp_pipeline);
    mmvk_buffer_destroy(execution->scratch);mmvk_buffer_destroy(execution->status);free(execution);
}

MMVKResult MMShaderVulkanCreateComputeExecution(MMVKDevice *device,const MMShaderComputeOutput *output,
    const MMShaderVulkanComputeResources *resources,const MMShaderComputeDispatchPlan *plan,
    MMShaderVulkanComputeExecution **out,MMVKError *error){
    if(out)*out=NULL;
    if(!device||!valid_output(output)||!resources||!plan||!out||plan->abi_version!=MM_SHADER_COMPUTE_ABI_VERSION||
       plan->binding_count>MM_SHADER_MAX_BINDINGS||plan->descriptor_count!=plan->binding_count||
       (plan->binding_count&&(!plan->bindings||!plan->descriptors))||(plan->region_count&&!plan->regions)||plan->region_count>4096)
        return invalid(error,"Missing settled compute execution plan");
    MMShaderVulkanComputeExecution *e=calloc(1,sizeof(*e));
    if(!e)return mmvk_errorf(error,MMVK_ERROR_MEMORY,0,"Compute execution allocation failed");
    e->output=*output;e->output.stage=plan->stage;e->output.bindings=plan->bindings;
    e->output.binding_count=plan->binding_count;e->output.push_constant_size=plan->push_constant_size;
    e->plan=plan;e->resources=*resources;
    memset(e->resources.dispatch_buffers,0,sizeof(e->resources.dispatch_buffers));
    memset(e->resources.dispatch_images,0,sizeof(e->resources.dispatch_images));
    memset(e->resources.dispatch_samplers,0,sizeof(e->resources.dispatch_samplers));
    MMShaderVulkanComputeDescription description;
    MMVKResult result=MMShaderVulkanDescribeCompute(&e->output,&description,error);
    if(result!=MMVK_OK)goto fail;
    if(plan->region_count){
        e->pipelines=calloc(plan->region_count,sizeof(*e->pipelines));
        if(!e->pipelines){result=mmvk_errorf(error,MMVK_ERROR_MEMORY,0,"Compute region allocation failed");goto fail;}
    }
    for(uint32_t r=0;r<plan->region_count;r++){
        const MMShaderComputeRegion *region=plan->regions+r;
        result=mmvk_compute_pipeline_create_specialized(device,&description.shader,&description.layout,
            (const MMVKSpecializationEntry *)region->specialization,region->specialization_count,
            region->specialization_data,region->specialization_size,region->stage_flags,
            region->required_subgroup_size,e->pipelines+r,error);
        if(result!=MMVK_OK)goto fail;
        if(region->indirect_kind){
            if(!resources->indirect_buffer||resources->indirect_length>mmvk_buffer_size(resources->indirect_buffer)||
               mmvk_buffer_is_aliasable(resources->indirect_buffer)){
                result=invalid(error,"Missing native compute indirect source");goto fail;
            }
            if(region->indirect_kind==1&&(region->indirect_offset>resources->indirect_length||
                12>resources->indirect_length-region->indirect_offset)){
                result=invalid(error,"Indirect region exceeds logical source");goto fail;
            }
        }
    }
    uint8_t initial[32]={0};
    result=compute_upload(device,initial,sizeof(initial),MMVK_BUFFER_STORAGE,&e->status,error);
    if(result!=MMVK_OK)goto fail;
    uint64_t seen=0;
    for(uint32_t i=0;i<plan->descriptor_count;i++){
        const MMShaderComputeDescriptor *d=plan->descriptors+i;
        uint32_t index=0;const MMShaderComputeBinding *b=find_binding(&e->output,d,&index);
        if(!b||b->vulkan_type!=d->vulkan_type||b->source_kind!=d->source_kind||
           (seen&((uint64_t)1<<index))){
            result=invalid(error,"Compute descriptor differs from settled layout");goto fail;
        }
        seen|=(uint64_t)1<<index;
        switch(d->source_kind){
        case MM_SHADER_COMPUTE_CONSTANT:
            result=compute_upload(device,b->constant_bytes,b->constant_size,MMVK_BUFFER_STORAGE,e->resources.dispatch_buffers+index,error);
            break;
        case MM_SHADER_COMPUTE_TABLE:
            if(d->range!=d->upload_size){result=invalid(error,"Compute table range differs from uploaded bytes");break;}
            result=compute_upload(device,d->upload_bytes,d->upload_size,d->vulkan_type==6?MMVK_BUFFER_UNIFORM:MMVK_BUFFER_STORAGE,e->resources.dispatch_buffers+index,error);
            break;
        case MM_SHADER_COMPUTE_STATUS:e->resources.dispatch_buffers[index]=e->status;break;
        case MM_SHADER_COMPUTE_COMBINED:
        case MM_SHADER_COMPUTE_TEXTURE:
            if(d->metal_index>=128){result=invalid(error,"Compute texture index exceeds table");break;}
            result=compute_view(resources->textures[d->metal_index],d,e->resources.dispatch_images+index,error);
            if(result!=MMVK_OK||d->source_kind!=MM_SHADER_COMPUTE_COMBINED)break;
            /* fall through */
        case MM_SHADER_COMPUTE_SAMPLER:
            result=compute_sampler(device,d,e->resources.dispatch_samplers+index,error);break;
        default:break;
        }
        if(result!=MMVK_OK)goto fail;
    }
    if(plan->indirect_clamp_stage.word_count){
        if(!resources->indirect_buffer||plan->indirect_source_offset>resources->indirect_length||
           plan->indirect_source_range>resources->indirect_length-plan->indirect_source_offset){
            result=invalid(error,"Compute indirect clamp exceeds logical source");goto fail;
        }
        MMVKBindingLayout clamp_bindings[3]={{0,0,MMVK_STAGE_COMPUTE,MMVK_BIND_STORAGE_BUFFER},
            {0,1,MMVK_STAGE_COMPUTE,MMVK_BIND_STORAGE_BUFFER},{0,2,MMVK_STAGE_COMPUTE,MMVK_BIND_STORAGE_BUFFER}};
        MMVKPipelineLayout clamp_layout={clamp_bindings,3,4};
        MMVKShader shader={plan->indirect_clamp_stage.words,plan->indirect_clamp_stage.word_count*sizeof(uint32_t),plan->indirect_clamp_stage.entry_point};
        result=mmvk_compute_pipeline_create(device,&shader,&clamp_layout,&e->clamp_pipeline,error);
        if(result!=MMVK_OK)goto fail;
        result=mmvk_buffer_create(device,16,MMVK_BUFFER_STORAGE|64u,&e->scratch,error);
        if(result!=MMVK_OK)goto fail;
    }
    *out=e;return MMVK_OK;
fail:MMShaderVulkanDestroyComputeExecution(e);return result;
}

static MMVKResult compute_indirect(MMVKCommands *commands,MMVKBuffer *buffer,uint64_t offset,MMVKError *error){
    if(mmvk_recording(commands,error)!=MMVK_OK)return MMVK_ERROR_STATE;
    if(commands->in_render||!commands->pipeline||commands->pipeline->point!=VK_PIPELINE_BIND_POINT_COMPUTE||
       !buffer||buffer->dev!=commands->dev||mmvk_buffer_is_aliasable(buffer)||!(buffer->usage&64u)||
       offset%4||offset>buffer->size||12>buffer->size-offset)
        return invalid(error,"Invalid native indirect compute dispatch");
    PFN_vkCmdDispatchIndirect dispatch=(PFN_vkCmdDispatchIndirect)commands->dev->GetDeviceProcAddr(commands->dev->device,"vkCmdDispatchIndirect");
    if(!dispatch)return invalid(error,"Native vkCmdDispatchIndirect is unavailable");
    MMVKResult result=mmvk_bind_snapshot(commands,error);
    if(result!=MMVK_OK)return result;
    mmvk_memory_barrier(commands);dispatch(commands->cmd,buffer->buffer,offset);return MMVK_OK;
}

MMVKResult MMShaderVulkanComputeMemoryBarrier(MMVKCommands *commands,MMVKError *error){
    if(mmvk_recording(commands,error)!=MMVK_OK)return MMVK_ERROR_STATE;
    if(commands->in_render)return invalid(error,"Compute barrier is inside a render pass");
    mmvk_memory_barrier(commands);return MMVK_OK;
}

/* Track this recorded region's settled shader writes, rather than the frontend
 * encoder's output list, which can contain resources used by future operations.
 * This runs only after a dispatch command records successfully. Native tracking
 * canonicalizes image aliases to their root and deduplicates repeated regions. */
static MMVKResult compute_mark_writes(MMVKCommands *commands,
    const MMShaderVulkanComputeExecution *e,MMVKError *error){
    for(uint32_t i=0;i<e->plan->descriptor_count;i++){
        const MMShaderComputeDescriptor *d=e->plan->descriptors+i;
        uint32_t index=0;
        const MMShaderComputeBinding *binding=find_binding(&e->output,d,&index);
        if(!binding)return invalid(error,"Recorded compute descriptor has no settled binding");
        if(!binding->write)continue;
        MMVKResult result;
        switch(d->source_kind){
        case MM_SHADER_COMPUTE_BUFFER:
            if(d->vulkan_type!=7||binding->metal_index>=31)
                return invalid(error,"Writable compute buffer is not a storage descriptor");
            result=mmvk_commands_mark_buffer_written(commands,
                e->resources.buffers[binding->metal_index].buffer,error);
            break;
        case MM_SHADER_COMPUTE_TEXTURE:
            if(d->vulkan_type!=3)
                return invalid(error,"Writable compute image is not a storage descriptor");
            result=mmvk_commands_mark_image_written(commands,e->resources.dispatch_images[index],error);
            break;
        case MM_SHADER_COMPUTE_STATUS:
            result=mmvk_commands_mark_buffer_written(commands,e->status,error);
            break;
        default:
            return invalid(error,"Writable compute descriptor has no native write tracker");
        }
        if(result!=MMVK_OK)return result;
    }
    return MMVK_OK;
}

MMVKResult MMShaderVulkanRecordComputeExecution(MMVKCommands *commands,MMShaderVulkanComputeExecution *e,MMVKError *error){
    if(!e||!commands)return invalid(error,"Missing native compute execution");
    if(commands->dev!=e->status->dev)return invalid(error,"Compute execution belongs to a different native device");
    MMVKResult result;
    if(e->clamp_pipeline){
        result=mmvk_commands_bind_pipeline(commands,e->clamp_pipeline,error);
        if(result!=MMVK_OK)return result;
        result=mmvk_commands_bind_buffer(commands,0,0,e->resources.indirect_buffer,e->plan->indirect_source_offset,e->plan->indirect_source_range,error);
        if(result!=MMVK_OK)return result;
        result=mmvk_commands_bind_buffer(commands,0,1,e->scratch,0,12,error);
        if(result!=MMVK_OK)return result;
        result=mmvk_commands_bind_buffer(commands,0,2,e->status,0,32,error);
        if(result!=MMVK_OK)return result;
        uint32_t residual=e->plan->indirect_residual;
        result=mmvk_commands_push_constants(commands,MMVK_STAGE_COMPUTE,0,&residual,4,error);
        if(result!=MMVK_OK)return result;
        result=mmvk_commands_dispatch(commands,1,1,1,error);
        if(result!=MMVK_OK)return result;
        /* The prepass reads the indirect source and writes only these two
         * internal buffers; failed recording never marks either one. */
        result=mmvk_commands_mark_buffer_written(commands,e->scratch,error);
        if(result!=MMVK_OK)return result;
        result=mmvk_commands_mark_buffer_written(commands,e->status,error);
        if(result!=MMVK_OK)return result;
    }
    for(uint32_t r=0;r<e->plan->region_count;r++){
        const MMShaderComputeRegion *region=e->plan->regions+r;
        result=mmvk_commands_bind_pipeline(commands,e->pipelines[r],error);
        if(result!=MMVK_OK)return result;
        result=MMShaderVulkanBindComputeResources(commands,&e->output,&e->resources,e->plan,error);
        if(result!=MMVK_OK)return result;
        result=MMShaderVulkanPushComputeRegion(commands,&e->output,region,error);
        if(result!=MMVK_OK)return result;
        if(region->indirect_kind)result=compute_indirect(commands,region->indirect_kind==2?e->scratch:e->resources.indirect_buffer,region->indirect_offset,error);
        else result=mmvk_commands_dispatch(commands,region->group_count[0],region->group_count[1],region->group_count[2],error);
        if(result!=MMVK_OK)return result;
        if(region->indirect_kind||(region->group_count[0]&&region->group_count[1]&&region->group_count[2])){
            result=compute_mark_writes(commands,e,error);
            if(result!=MMVK_OK)return result;
        }
    }
    return MMVK_OK;
}

MMVKResult MMShaderVulkanCompleteComputeExecution(MMShaderVulkanComputeExecution *e,MMVKError *error){
    if(!e||!e->status)return invalid(error,"Missing native compute completion state");
    MMVKResult result=mmvk_buffer_invalidate(e->status,0,32,error);
    if(result!=MMVK_OK)return result;
    const uint32_t *words=mmvk_buffer_contents(e->status);
    if(!words)return invalid(error,"Compute execution status is not host readable");
    if(words[0])return mmvk_errorf(error,MMVK_ERROR_SHADER,(int32_t)words[0],
        "Compute execution status reports 0x%x (indirect overflow or shader resource fault)",words[0]);
    return MMVK_OK;
}
