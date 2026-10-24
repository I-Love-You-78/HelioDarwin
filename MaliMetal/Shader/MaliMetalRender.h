/* SPDX-License-Identifier: MIT */
#ifndef MALI_METAL_RENDER_H
#define MALI_METAL_RENDER_H
#include "MaliMetalShader.h"

#define MM_SHADER_RENDER_DRAW_ABI_VERSION 1u

typedef struct MMShaderRenderBuffer {
    uint64_t length, offset, gpu_address, device_address;
    uint32_t bound, attribute_stride, has_attribute_stride;
} MMShaderRenderBuffer;

typedef struct MMShaderRenderTexture {
    uint32_t bound, texture_type, pixel_format, usage;
    uint32_t width, height, depth, array_length, mipmap_level_count, sample_count;
    uint32_t sparse, own_format_views, parent_relative_level, parent_base_size[3];
    uint32_t swizzle[4];
    uint64_t resource_id;
} MMShaderRenderTexture;

typedef struct MMShaderRenderSampler {
    uint32_t bound, min_filter, mag_filter, mip_filter;
    uint32_t s_address_mode, t_address_mode, r_address_mode, border_color;
    uint32_t normalized_coordinates, lod_average, max_anisotropy, compare_function, reduction_mode;
    float lod_min_clamp, lod_max_clamp;
    uint64_t resource_id;
} MMShaderRenderSampler;

typedef struct MMShaderRenderStageBindings {
    MMShaderRenderBuffer buffers[31];
    MMShaderRenderTexture textures[128];
    MMShaderRenderSampler samplers[16];
} MMShaderRenderStageBindings;

typedef struct MMShaderRenderBindings {
    MMShaderRenderStageBindings vertex, fragment;
    uint32_t render_width, render_height, render_target_array_length, viewport_count;
    uint32_t color_formats[8];
    float blend_color[4];
} MMShaderRenderBindings;

typedef struct MMShaderRenderDrawRequest {
    uint32_t kind, primitive_type, start, count, instance_count, base_instance;
    int32_t base_vertex;
    uint32_t index_type;
    uint64_t index_length, index_offset;
} MMShaderRenderDrawRequest;

enum {
    MM_SHADER_RENDER_BUFFER = 1,
    MM_SHADER_RENDER_TEXTURE = 2,
    MM_SHADER_RENDER_SAMPLER = 3,
    MM_SHADER_RENDER_COMBINED_IMAGE_SAMPLER = 4,
    MM_SHADER_RENDER_CONSTANT = 5,
    MM_SHADER_RENDER_BYTES = 6,
    MM_SHADER_RENDER_ZERO_BUFFER = 7
};

typedef struct MMShaderRenderImageView {
    uint32_t view_type, format, aspect, layout, components[4];
    uint32_t level, flags;
} MMShaderRenderImageView;

typedef struct MMShaderRenderNativeSampler {
    uint32_t min_filter, mag_filter, mipmap_mode, address_u, address_v, address_w;
    uint32_t compare_enabled, compare_op, border_color, reduction_mode;
    uint32_t border_components_enabled, border_components[4];
    float min_lod, max_lod, lod_bias, max_anisotropy;
} MMShaderRenderNativeSampler;

typedef struct MMShaderRenderDescriptor {
    uint32_t set, binding, array_element, vulkan_type, source_kind;
    uint32_t metal_stage, metal_index, sampler_stage, sampler_index;
    MMShaderRenderImageView view;
    MMShaderRenderNativeSampler sampler;
    uint64_t offset, range;
    const void *bytes;
    size_t byte_count;
} MMShaderRenderDescriptor;

typedef struct MMShaderRenderPush {
    uint32_t vulkan_stages, offset, size;
    const void *bytes;
} MMShaderRenderPush;

typedef struct MMShaderRenderSpecialization {
    uint32_t id, offset;
    size_t size;
} MMShaderRenderSpecialization;

typedef struct MMShaderRenderVertexBuffer {
    uint32_t binding, metal_index, stride;
    uint64_t offset;
} MMShaderRenderVertexBuffer;

typedef struct MMShaderRenderDrawPlan {
    uint32_t abi_version, descriptor_count;
    const MMShaderRenderDescriptor *descriptors;
    uint32_t push_count;
    const MMShaderRenderPush *pushes;
    uint32_t specialization_count;
    const MMShaderRenderSpecialization *specialization;
    const void *specialization_data;
    size_t specialization_size;
    uint32_t vertex_buffer_count;
    const MMShaderRenderVertexBuffer *vertex_buffers;
    uint32_t module_count;
    const MMShaderStageModule *modules;
    const MMShaderBackendPlan *backend_plan;
    uint8_t pipeline_key[32];
    uint32_t draw_kind, draw_arguments[5];
    uint32_t index_type;
    uint64_t index_offset;
    void *private_data;
    void (*destroy)(struct MMShaderRenderDrawPlan *plan);
} MMShaderRenderDrawPlan;

void MMShaderRenderDrawPlanDestroy(MMShaderRenderDrawPlan *plan);
#endif
