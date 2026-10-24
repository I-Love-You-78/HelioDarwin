/* SPDX-License-Identifier: MIT */
#include "MaliMetalRenderVulkan.h"
#include "../Vulkan/MaliVulkanResources.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct render_variant {
    struct render_variant *next;
    uint32_t references;
    uint8_t key[32];
    uint32_t state[32],pass[14];
    MMVKSpecializationEntry *entries;
    uint32_t entry_count;
    void *data;
    size_t data_size;
    MMVKPipeline *pipeline;
} RenderVariant;
struct MMShaderVulkanRenderCache {MMVKDevice *device;pthread_mutex_t lock;RenderVariant *variants;};
struct MMShaderVulkanRenderExecution {
    MMShaderRenderDrawPlan plan;
    RenderVariant *variant;
    MMShaderVulkanBindings resources;
    MMVKBuffer *buffers[MM_SHADER_MAX_BINDINGS];
    MMVKImage *images[MM_SHADER_MAX_BINDINGS];
    MMVKSampler *samplers[MM_SHADER_MAX_BINDINGS];
};
static MMVKResult failure(MMVKError *e,MMVKResult code,const char *text){
    if(e){memset(e,0,sizeof(*e));e->code=code;snprintf(e->message,sizeof(e->message),"%s",text);}return code;
}
static void variant_release(RenderVariant *v){
    if(v&&__atomic_sub_fetch(&v->references,1,__ATOMIC_ACQ_REL)==0){mmvk_pipeline_destroy(v->pipeline);free(v->entries);free(v->data);free(v);}
}
MMVKResult MMShaderVulkanRenderCacheCreate(MMVKDevice *d,MMShaderVulkanRenderCache **out,MMVKError *e){
    if(!d||!out)return failure(e,MMVK_ERROR_ARGUMENT,"Missing render cache device or destination");
    *out=calloc(1,sizeof(**out));if(!*out)return failure(e,MMVK_ERROR_MEMORY,"Cannot allocate render cache");
    (*out)->device=d;pthread_mutex_init(&(*out)->lock,NULL);return MMVK_OK;
}
void MMShaderVulkanRenderCacheDestroy(MMShaderVulkanRenderCache *c){
    if(!c)return;RenderVariant *v=c->variants;while(v){RenderVariant *next=v->next;variant_release(v);v=next;}
    pthread_mutex_destroy(&c->lock);free(c);
}
static void state_words(const MMVKGraphicsState *s,uint32_t out[32]){
    const uint32_t values[]={s->topology,s->cull_mode,s->front_face,s->polygon_mode,s->primitive_restart,s->depth_clamp,s->rasterizer_discard,
        s->depth_bias,s->depth_test,s->depth_write,s->depth_compare,s->stencil_test,
        s->front.fail_op,s->front.pass_op,s->front.depth_fail_op,s->front.compare_op,s->front.read_mask,s->front.write_mask,
        s->back.fail_op,s->back.pass_op,s->back.depth_fail_op,s->back.compare_op,s->back.read_mask,s->back.write_mask,
        s->sample_count,s->alpha_to_coverage,s->alpha_to_one,s->sample_shading,s->sample_mask[0],s->sample_mask[1],s->tile_image_color_reads};
    memset(out,0,32*sizeof(*out));memcpy(out,values,sizeof(values));
}
static void pass_words(const MMVKPassLayout *p,uint32_t out[14]){
    memset(out,0,14*sizeof(*out));out[0]=p->color_count;out[1]=p->sample_count;
    for(uint32_t n=0;n<8;n++)out[n+2]=(uint32_t)p->color_formats[n];out[10]=(uint32_t)p->depth_stencil_format;
    out[11]=p->resolve_mask;out[12]=p->depth_resolve_mode;out[13]=p->stencil_resolve_mode;
}
static int same_specialization(const RenderVariant *v,const MMShaderRenderDrawPlan *p){
    if(v->entry_count!=p->specialization_count||v->data_size!=p->specialization_size||
       (v->data_size&&memcmp(v->data,p->specialization_data,v->data_size)))return 0;
    for(uint32_t n=0;n<v->entry_count;n++)if(v->entries[n].id!=p->specialization[n].id||
        v->entries[n].offset!=p->specialization[n].offset||v->entries[n].size!=p->specialization[n].size)return 0;
    return 1;
}
static MMVKResult pipeline_variant(MMShaderVulkanRenderCache *c,const MMShaderRenderDrawPlan *p,
    const MMVKGraphicsState *state,const MMVKPassLayout *pass,RenderVariant **out,MMVKError *e){
    MMVKGraphicsState effective=*state;
    effective.sample_count=p->backend_plan->raster_samples;
    effective.alpha_to_coverage=p->backend_plan->alpha_to_coverage;
    effective.alpha_to_one=p->backend_plan->alpha_to_one;
    effective.rasterizer_discard=p->backend_plan->rasterizer_discard;
    effective.sample_shading=p->backend_plan->sample_shading;
    effective.tile_image_color_reads=p->backend_plan->tile_image_color_reads;
    if(p->backend_plan->has_sample_mask)effective.sample_mask[0]=p->backend_plan->sample_mask;
    state=&effective;
    uint32_t words[32],layout[14];state_words(state,words);pass_words(pass,layout);
    pthread_mutex_lock(&c->lock);
    for(RenderVariant *v=c->variants;v;v=v->next)if(!memcmp(v->key,p->pipeline_key,32)&&
        !memcmp(v->state,words,sizeof(words))&&!memcmp(v->pass,layout,sizeof(layout))&&same_specialization(v,p)){
        __atomic_add_fetch(&v->references,1,__ATOMIC_RELAXED);*out=v;pthread_mutex_unlock(&c->lock);return MMVK_OK;
    }
    RenderVariant *v=calloc(1,sizeof(*v));MMVKResult result=MMVK_OK;
    if(!v){pthread_mutex_unlock(&c->lock);return failure(e,MMVK_ERROR_MEMORY,"Cannot allocate render variant");}
    v->references=1;memcpy(v->key,p->pipeline_key,32);memcpy(v->state,words,sizeof(words));memcpy(v->pass,layout,sizeof(layout));
    v->entry_count=p->specialization_count;v->data_size=p->specialization_size;
    v->entries=calloc(v->entry_count?v->entry_count:1,sizeof(*v->entries));v->data=malloc(v->data_size?v->data_size:1);
    if(!v->entries||!v->data){result=failure(e,MMVK_ERROR_MEMORY,"Cannot allocate render specialization");goto done;}
    for(uint32_t n=0;n<v->entry_count;n++)v->entries[n]=(MMVKSpecializationEntry){p->specialization[n].id,p->specialization[n].offset,p->specialization[n].size};
    if(v->data_size)memcpy(v->data,p->specialization_data,v->data_size);
    MMShaderBackendPlan adjusted=*p->backend_plan;
    for(uint32_t n=0;n<adjusted.color_attachment_count;n++)if(n>=pass->color_count||!pass->color_formats[n]){
        if((adjusted.color_outputs&(1u<<n))&&adjusted.colors[n].write_mask){result=failure(e,MMVK_ERROR_SHADER,"Canonical shader still writes an absent render attachment");goto done;}
        memset(adjusted.colors+n,0,sizeof(adjusted.colors[n]));
    }
    adjusted.color_attachment_count=pass->color_count;
    MMShaderRenderOutput output={.abi_version=MM_SHADER_ABI_VERSION,.module_count=p->module_count,.modules=p->modules,.backend_plan=&adjusted};
    MMVKRenderPipelineDescriptor requested={.topology=p->backend_plan->topology,.color_attachment_count=pass->color_count};
    memcpy(requested.color_formats,pass->color_formats,sizeof(requested.color_formats));
    MMShaderVulkanDescription description;
    result=MMShaderVulkanDescribe(&output,&requested,&description,e);if(result)goto done;
    MMVKGraphicsPipelineDescriptor native={.base=description.descriptor,.state=*state,.pass=*pass,
        .vertex={v->entries,v->entry_count,v->data,v->data_size},.fragment={v->entries,v->entry_count,v->data,v->data_size}};
    result=mmvk_graphics_pipeline_create(c->device,&native,&v->pipeline,e);if(result)goto done;
    v->next=c->variants;c->variants=v;__atomic_add_fetch(&v->references,1,__ATOMIC_RELAXED);*out=v;
done:if(result)variant_release(v);pthread_mutex_unlock(&c->lock);return result;
}
static MMVKImage *source_image(const MMShaderVulkanBindings *r,const MMShaderRenderDescriptor *d){
    if(d->metal_stage>1||d->metal_index>=128)return NULL;
    return (d->metal_stage?r->fragment_textures:r->vertex_textures)[d->metal_index];
}
static uint32_t metal_view_type(uint32_t type){
    static const uint32_t types[]={0,2,7,5,1,3,6};return type<7?types[type]:UINT32_MAX;
}
static MMVKResult make_image_view(MMVKImage *image,const MMShaderRenderImageView *view,uint32_t usage,MMVKImage **out,MMVKError *e){
    if(!image||mmvk_image_is_aliasable(image)||(view->flags&~2u))return failure(e,MMVK_ERROR_UNSUPPORTED,"Render image is missing, aliasable or needs unsupported view flags");
    const MMVKImageDescriptorEx *facts=mmvk_image_descriptor(image);uint32_t type=metal_view_type(view->view_type);
    if(facts&&facts->samples>1){if(type==2)type=4;else if(type==3)type=8;}
    if(type==UINT32_MAX||!facts)return failure(e,MMVK_ERROR_UNSUPPORTED,"Unknown render image view type");
    MMVKImageViewDescriptor native={.aspect=view->aspect,.usage=usage,.format=(MMVKFormat)(MMVK_FORMAT_NATIVE_BASE+view->format),.type=type,
        .base_level=view->level==UINT32_MAX?0:view->level,.level_count=view->level==UINT32_MAX?facts->mip_levels:1,
        .layer_count=facts->array_layers};
    if(mmvk_format_native_code(facts->format)==view->format)native.format=facts->format;
    MMVKResult composed=mmvk_image_compose_swizzle(image,view->components,(view->flags&2u)!=0,native.swizzle,e);
    if(composed)return composed;
    return mmvk_image_create_view(image,&native,out,e);
}
static MMVKResult make_sampler(MMVKDevice *device,const MMShaderRenderNativeSampler *s,MMVKSampler **out,MMVKError *e){
    if(s->border_components_enabled)return failure(e,MMVK_ERROR_UNSUPPORTED,"Render sampler requires mapped-border native support");
    MMVKSamplerDescriptor native={.min_filter=s->min_filter,.mag_filter=s->mag_filter,.mip_filter=s->mipmap_mode,
        .address_u=s->address_u,.address_v=s->address_v,.address_w=s->address_w,.normalized_coordinates=1,
        .compare_enabled=s->compare_enabled,.compare_op=s->compare_op,.border_color=s->border_color,
        .min_lod=s->min_lod,.max_lod=s->max_lod,.lod_bias=s->lod_bias,.max_anisotropy=s->max_anisotropy,
        .reduction_mode=s->reduction_mode};
    return mmvk_sampler_create(device,&native,out,e);
}
MMVKResult MMShaderVulkanPrepareRender(MMShaderVulkanRenderCache *cache,const MMShaderRenderOutput *output,
    const MMShaderVulkanBindings *resources,const MMShaderRenderBindings *bindings,const MMShaderRenderDrawRequest *request,
    const MMVKGraphicsState *state,const MMVKPassLayout *pass,MMShaderVulkanRenderExecution **out,MMVKError *e){
    if(!cache||!output||output->abi_version!=MM_SHADER_ABI_VERSION||!output->prepare_draw||!resources||!bindings||!request||!state||!pass||!out)
        return failure(e,MMVK_ERROR_ARGUMENT,"Missing canonical render execution input");
    *out=NULL;MMShaderVulkanRenderExecution *x=calloc(1,sizeof(*x));if(!x)return failure(e,MMVK_ERROR_MEMORY,"Cannot allocate render execution");
    MMShaderError error={0};MMShaderStatus status=output->prepare_draw(output,bindings,request,&x->plan,&error);
    MMVKResult result=status==MM_SHADER_OK?MMVK_OK:failure(e,MMVK_ERROR_SHADER,error.message);if(result)goto done;
    if(x->plan.abi_version!=MM_SHADER_RENDER_DRAW_ABI_VERSION||!x->plan.backend_plan||x->plan.descriptor_count>MM_SHADER_MAX_BINDINGS){result=failure(e,MMVK_ERROR_SHADER,"Invalid canonical render draw ABI or descriptor count");goto done;}
    x->resources=*resources;
    result=pipeline_variant(cache,&x->plan,state,pass,&x->variant,e);if(result)goto done;
    for(uint32_t n=0;n<x->plan.descriptor_count;n++){
        const MMShaderRenderDescriptor *d=x->plan.descriptors+n;
        switch(d->source_kind){
        case MM_SHADER_RENDER_BUFFER:
            if(d->metal_stage>1||d->metal_index>=31){result=failure(e,MMVK_ERROR_SHADER,"Render buffer slot is invalid");goto done;}break;
        case MM_SHADER_RENDER_CONSTANT:case MM_SHADER_RENDER_BYTES:case MM_SHADER_RENDER_ZERO_BUFFER:
            if(!d->range||d->range>SIZE_MAX||d->byte_count>d->range||(d->byte_count&&!d->bytes)){result=failure(e,MMVK_ERROR_SHADER,"Render synthetic buffer span is invalid");goto done;}
            result=mmvk_buffer_create(cache->device,d->range,MMVK_BUFFER_UNIFORM|MMVK_BUFFER_STORAGE,&x->buffers[n],e);if(result)goto done;
            memset(mmvk_buffer_contents(x->buffers[n]),0,(size_t)d->range);
            if(d->byte_count)memcpy(mmvk_buffer_contents(x->buffers[n]),d->bytes,d->byte_count);
            result=mmvk_buffer_flush(x->buffers[n],0,d->range,e);if(result)goto done;break;
        case MM_SHADER_RENDER_TEXTURE:case MM_SHADER_RENDER_COMBINED_IMAGE_SAMPLER:
            result=make_image_view(source_image(resources,d),&d->view,d->vulkan_type==3?MMVK_IMAGE_STORAGE:MMVK_IMAGE_SAMPLED,&x->images[n],e);if(result)goto done;
            if(d->source_kind==MM_SHADER_RENDER_TEXTURE)break;
            /* fall through */
        case MM_SHADER_RENDER_SAMPLER:
            result=make_sampler(cache->device,&d->sampler,&x->samplers[n],e);if(result)goto done;break;
        default:result=failure(e,MMVK_ERROR_UNSUPPORTED,"Unknown canonical render descriptor source");goto done;
        }
    }
    *out=x;return MMVK_OK;
done:MMShaderVulkanRenderExecutionDestroy(x);return result;
}
MMVKResult MMShaderVulkanRecordRender(MMVKCommands *commands,MMShaderVulkanRenderExecution *x,MMVKBuffer *indices,MMVKError *e){
    if(!commands||!x||!x->variant)return failure(e,MMVK_ERROR_ARGUMENT,"Missing render execution");
    MMVKResult r=mmvk_commands_bind_pipeline(commands,x->variant->pipeline,e);if(r)return r;
    for(uint32_t n=0;n<x->plan.descriptor_count;n++){
        const MMShaderRenderDescriptor *d=x->plan.descriptors+n;MMVKBuffer *buffer=NULL;
        switch(d->source_kind){
        case MM_SHADER_RENDER_BUFFER:buffer=(d->metal_stage?x->resources.fragment_buffers:x->resources.vertex_buffers)[d->metal_index].buffer;
            r=mmvk_commands_bind_buffer(commands,d->set,d->binding,buffer,d->offset,d->range,e);break;
        case MM_SHADER_RENDER_CONSTANT:case MM_SHADER_RENDER_BYTES:case MM_SHADER_RENDER_ZERO_BUFFER:
            r=mmvk_commands_bind_buffer(commands,d->set,d->binding,x->buffers[n],0,d->range,e);break;
        case MM_SHADER_RENDER_TEXTURE:r=mmvk_commands_bind_image(commands,d->set,d->binding,x->images[n],e);break;
        case MM_SHADER_RENDER_SAMPLER:r=mmvk_commands_bind_sampler(commands,d->set,d->binding,x->samplers[n],e);break;
        case MM_SHADER_RENDER_COMBINED_IMAGE_SAMPLER:r=mmvk_commands_bind_image_sampler(commands,d->set,d->binding,x->images[n],x->samplers[n],e);break;
        default:r=failure(e,MMVK_ERROR_UNSUPPORTED,"Unknown render descriptor source");break;
        }
        if(r)return r;
    }
    for(uint32_t n=0;n<x->plan.vertex_buffer_count;n++){
        const MMShaderRenderVertexBuffer *v=x->plan.vertex_buffers+n;
        if(v->metal_index>=31)return failure(e,MMVK_ERROR_SHADER,"Render vertex slot is invalid");
        r=mmvk_commands_bind_vertex_buffer(commands,v->binding,x->resources.vertex_buffers[v->metal_index].buffer,v->offset,e);if(r)return r;
    }
    for(uint32_t n=0;n<x->plan.push_count;n++){
        const MMShaderRenderPush *p=x->plan.pushes+n;
        r=mmvk_commands_push_constants(commands,MMVK_STAGE_VERTEX|MMVK_STAGE_FRAGMENT,p->offset,p->bytes,p->size,e);if(r)return r;
    }
    const uint32_t *a=x->plan.draw_arguments;
    if(x->plan.draw_kind==0)return mmvk_commands_draw(commands,a[0],a[1],a[2],a[3],e);
    if(x->plan.draw_kind==1){r=mmvk_commands_bind_index_buffer(commands,indices,x->plan.index_offset,x->plan.index_type,e);
        if(r)return r;return mmvk_commands_draw_indexed(commands,a[0],a[1],a[2],(int32_t)a[3],a[4],e);}
    return failure(e,MMVK_ERROR_UNSUPPORTED,"Unknown canonical render command");
}
void MMShaderVulkanRenderExecutionDestroy(MMShaderVulkanRenderExecution *x){
    if(!x)return;for(uint32_t n=0;n<MM_SHADER_MAX_BINDINGS;n++){mmvk_buffer_destroy(x->buffers[n]);mmvk_image_destroy(x->images[n]);mmvk_sampler_destroy(x->samplers[n]);}
    variant_release(x->variant);MMShaderRenderDrawPlanDestroy(&x->plan);free(x);
}
