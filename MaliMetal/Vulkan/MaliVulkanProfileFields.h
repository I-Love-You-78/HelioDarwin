/* Generated from the pinned Khronos Vulkan core declarations. */
/* SPDX-License-Identifier: MIT */
#ifndef MMVK_PROFILE_FIELDS_H
#define MMVK_PROFILE_FIELDS_H

static const struct json_name VkSampleCountFlags_names[] = {
    { VK_SAMPLE_COUNT_1_BIT, "VK_SAMPLE_COUNT_1_BIT" },
    { VK_SAMPLE_COUNT_2_BIT, "VK_SAMPLE_COUNT_2_BIT" },
    { VK_SAMPLE_COUNT_4_BIT, "VK_SAMPLE_COUNT_4_BIT" },
    { VK_SAMPLE_COUNT_8_BIT, "VK_SAMPLE_COUNT_8_BIT" },
    { VK_SAMPLE_COUNT_16_BIT, "VK_SAMPLE_COUNT_16_BIT" },
    { VK_SAMPLE_COUNT_32_BIT, "VK_SAMPLE_COUNT_32_BIT" },
    { VK_SAMPLE_COUNT_64_BIT, "VK_SAMPLE_COUNT_64_BIT" },
    {0,NULL}
};

static const struct json_name VkShaderStageFlags_names[] = {
    { VK_SHADER_STAGE_VERTEX_BIT, "VK_SHADER_STAGE_VERTEX_BIT" },
    { VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, "VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT" },
    { VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, "VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT" },
    { VK_SHADER_STAGE_GEOMETRY_BIT, "VK_SHADER_STAGE_GEOMETRY_BIT" },
    { VK_SHADER_STAGE_FRAGMENT_BIT, "VK_SHADER_STAGE_FRAGMENT_BIT" },
    { VK_SHADER_STAGE_COMPUTE_BIT, "VK_SHADER_STAGE_COMPUTE_BIT" },
    { VK_SHADER_STAGE_ALL_GRAPHICS, "VK_SHADER_STAGE_ALL_GRAPHICS" },
    { VK_SHADER_STAGE_ALL, "VK_SHADER_STAGE_ALL" },
    { VK_SHADER_STAGE_RAYGEN_BIT_KHR, "VK_SHADER_STAGE_RAYGEN_BIT_KHR" },
    { VK_SHADER_STAGE_ANY_HIT_BIT_KHR, "VK_SHADER_STAGE_ANY_HIT_BIT_KHR" },
    { VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, "VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR" },
    { VK_SHADER_STAGE_MISS_BIT_KHR, "VK_SHADER_STAGE_MISS_BIT_KHR" },
    { VK_SHADER_STAGE_INTERSECTION_BIT_KHR, "VK_SHADER_STAGE_INTERSECTION_BIT_KHR" },
    { VK_SHADER_STAGE_CALLABLE_BIT_KHR, "VK_SHADER_STAGE_CALLABLE_BIT_KHR" },
    { VK_SHADER_STAGE_TASK_BIT_EXT, "VK_SHADER_STAGE_TASK_BIT_EXT" },
    { VK_SHADER_STAGE_MESH_BIT_EXT, "VK_SHADER_STAGE_MESH_BIT_EXT" },
    { VK_SHADER_STAGE_SUBPASS_SHADING_BIT_HUAWEI, "VK_SHADER_STAGE_SUBPASS_SHADING_BIT_HUAWEI" },
    { VK_SHADER_STAGE_CLUSTER_CULLING_BIT_HUAWEI, "VK_SHADER_STAGE_CLUSTER_CULLING_BIT_HUAWEI" },
    {0,NULL}
};

static const struct json_name VkSubgroupFeatureFlags_names[] = {
    { VK_SUBGROUP_FEATURE_BASIC_BIT, "VK_SUBGROUP_FEATURE_BASIC_BIT" },
    { VK_SUBGROUP_FEATURE_VOTE_BIT, "VK_SUBGROUP_FEATURE_VOTE_BIT" },
    { VK_SUBGROUP_FEATURE_ARITHMETIC_BIT, "VK_SUBGROUP_FEATURE_ARITHMETIC_BIT" },
    { VK_SUBGROUP_FEATURE_BALLOT_BIT, "VK_SUBGROUP_FEATURE_BALLOT_BIT" },
    { VK_SUBGROUP_FEATURE_SHUFFLE_BIT, "VK_SUBGROUP_FEATURE_SHUFFLE_BIT" },
    { VK_SUBGROUP_FEATURE_SHUFFLE_RELATIVE_BIT, "VK_SUBGROUP_FEATURE_SHUFFLE_RELATIVE_BIT" },
    { VK_SUBGROUP_FEATURE_CLUSTERED_BIT, "VK_SUBGROUP_FEATURE_CLUSTERED_BIT" },
    { VK_SUBGROUP_FEATURE_QUAD_BIT, "VK_SUBGROUP_FEATURE_QUAD_BIT" },
    { VK_SUBGROUP_FEATURE_ROTATE_BIT, "VK_SUBGROUP_FEATURE_ROTATE_BIT" },
    { VK_SUBGROUP_FEATURE_ROTATE_CLUSTERED_BIT, "VK_SUBGROUP_FEATURE_ROTATE_CLUSTERED_BIT" },
    { VK_SUBGROUP_FEATURE_PARTITIONED_BIT_EXT, "VK_SUBGROUP_FEATURE_PARTITIONED_BIT_EXT" },
    {0,NULL}
};

static const struct json_name VkResolveModeFlags_names[] = {
    { VK_RESOLVE_MODE_NONE, "VK_RESOLVE_MODE_NONE" },
    { VK_RESOLVE_MODE_SAMPLE_ZERO_BIT, "VK_RESOLVE_MODE_SAMPLE_ZERO_BIT" },
    { VK_RESOLVE_MODE_AVERAGE_BIT, "VK_RESOLVE_MODE_AVERAGE_BIT" },
    { VK_RESOLVE_MODE_MIN_BIT, "VK_RESOLVE_MODE_MIN_BIT" },
    { VK_RESOLVE_MODE_MAX_BIT, "VK_RESOLVE_MODE_MAX_BIT" },
    { VK_RESOLVE_MODE_EXTERNAL_FORMAT_DOWNSAMPLE_BIT_ANDROID, "VK_RESOLVE_MODE_EXTERNAL_FORMAT_DOWNSAMPLE_BIT_ANDROID" },
    { VK_RESOLVE_MODE_CUSTOM_BIT_EXT, "VK_RESOLVE_MODE_CUSTOM_BIT_EXT" },
    {0,NULL}
};

static const struct json_name VkDriverId_names[] = {
    { VK_DRIVER_ID_AMD_PROPRIETARY, "VK_DRIVER_ID_AMD_PROPRIETARY" },
    { VK_DRIVER_ID_AMD_OPEN_SOURCE, "VK_DRIVER_ID_AMD_OPEN_SOURCE" },
    { VK_DRIVER_ID_MESA_RADV, "VK_DRIVER_ID_MESA_RADV" },
    { VK_DRIVER_ID_NVIDIA_PROPRIETARY, "VK_DRIVER_ID_NVIDIA_PROPRIETARY" },
    { VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS, "VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS" },
    { VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA, "VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA" },
    { VK_DRIVER_ID_IMAGINATION_PROPRIETARY, "VK_DRIVER_ID_IMAGINATION_PROPRIETARY" },
    { VK_DRIVER_ID_QUALCOMM_PROPRIETARY, "VK_DRIVER_ID_QUALCOMM_PROPRIETARY" },
    { VK_DRIVER_ID_ARM_PROPRIETARY, "VK_DRIVER_ID_ARM_PROPRIETARY" },
    { VK_DRIVER_ID_GOOGLE_SWIFTSHADER, "VK_DRIVER_ID_GOOGLE_SWIFTSHADER" },
    { VK_DRIVER_ID_GGP_PROPRIETARY, "VK_DRIVER_ID_GGP_PROPRIETARY" },
    { VK_DRIVER_ID_BROADCOM_PROPRIETARY, "VK_DRIVER_ID_BROADCOM_PROPRIETARY" },
    { VK_DRIVER_ID_MESA_LLVMPIPE, "VK_DRIVER_ID_MESA_LLVMPIPE" },
    { VK_DRIVER_ID_MOLTENVK, "VK_DRIVER_ID_MOLTENVK" },
    { VK_DRIVER_ID_COREAVI_PROPRIETARY, "VK_DRIVER_ID_COREAVI_PROPRIETARY" },
    { VK_DRIVER_ID_JUICE_PROPRIETARY, "VK_DRIVER_ID_JUICE_PROPRIETARY" },
    { VK_DRIVER_ID_VERISILICON_PROPRIETARY, "VK_DRIVER_ID_VERISILICON_PROPRIETARY" },
    { VK_DRIVER_ID_MESA_TURNIP, "VK_DRIVER_ID_MESA_TURNIP" },
    { VK_DRIVER_ID_MESA_V3DV, "VK_DRIVER_ID_MESA_V3DV" },
    { VK_DRIVER_ID_MESA_PANVK, "VK_DRIVER_ID_MESA_PANVK" },
    { VK_DRIVER_ID_SAMSUNG_PROPRIETARY, "VK_DRIVER_ID_SAMSUNG_PROPRIETARY" },
    { VK_DRIVER_ID_MESA_VENUS, "VK_DRIVER_ID_MESA_VENUS" },
    { VK_DRIVER_ID_MESA_DOZEN, "VK_DRIVER_ID_MESA_DOZEN" },
    { VK_DRIVER_ID_MESA_NVK, "VK_DRIVER_ID_MESA_NVK" },
    { VK_DRIVER_ID_IMAGINATION_OPEN_SOURCE_MESA, "VK_DRIVER_ID_IMAGINATION_OPEN_SOURCE_MESA" },
    { VK_DRIVER_ID_MESA_HONEYKRISP, "VK_DRIVER_ID_MESA_HONEYKRISP" },
    { VK_DRIVER_ID_VULKAN_SC_EMULATION_ON_VULKAN, "VK_DRIVER_ID_VULKAN_SC_EMULATION_ON_VULKAN" },
    { VK_DRIVER_ID_MESA_KOSMICKRISP, "VK_DRIVER_ID_MESA_KOSMICKRISP" },
    { VK_DRIVER_ID_MESA_GFXSTREAM, "VK_DRIVER_ID_MESA_GFXSTREAM" },
    { VK_DRIVER_ID_APE_SOFT, "VK_DRIVER_ID_APE_SOFT" },
    { VK_DRIVER_ID_RESERVED_31, "VK_DRIVER_ID_RESERVED_31" },
    {0,NULL}
};

static const struct json_name VkShaderFloatControlsIndependence_names[] = {
    { VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_32_BIT_ONLY, "VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_32_BIT_ONLY" },
    { VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_ALL, "VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_ALL" },
    { VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_NONE, "VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_NONE" },
    {0,NULL}
};

static const struct json_name VkPointClippingBehavior_names[] = {
    { VK_POINT_CLIPPING_BEHAVIOR_ALL_CLIP_PLANES, "VK_POINT_CLIPPING_BEHAVIOR_ALL_CLIP_PLANES" },
    { VK_POINT_CLIPPING_BEHAVIOR_USER_CLIP_PLANES_ONLY, "VK_POINT_CLIPPING_BEHAVIOR_USER_CLIP_PLANES_ONLY" },
    {0,NULL}
};

static const struct json_name VkFormatFeatureFlags_names[] = {
    { VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT, "VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT" },
    { VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT, "VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT" },
    { VK_FORMAT_FEATURE_STORAGE_IMAGE_ATOMIC_BIT, "VK_FORMAT_FEATURE_STORAGE_IMAGE_ATOMIC_BIT" },
    { VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT, "VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT" },
    { VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT, "VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT" },
    { VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_ATOMIC_BIT, "VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_ATOMIC_BIT" },
    { VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT, "VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT" },
    { VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT, "VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT" },
    { VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT, "VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT" },
    { VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT, "VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT" },
    { VK_FORMAT_FEATURE_BLIT_SRC_BIT, "VK_FORMAT_FEATURE_BLIT_SRC_BIT" },
    { VK_FORMAT_FEATURE_BLIT_DST_BIT, "VK_FORMAT_FEATURE_BLIT_DST_BIT" },
    { VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT, "VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT" },
    { VK_FORMAT_FEATURE_TRANSFER_SRC_BIT, "VK_FORMAT_FEATURE_TRANSFER_SRC_BIT" },
    { VK_FORMAT_FEATURE_TRANSFER_DST_BIT, "VK_FORMAT_FEATURE_TRANSFER_DST_BIT" },
    { VK_FORMAT_FEATURE_MIDPOINT_CHROMA_SAMPLES_BIT, "VK_FORMAT_FEATURE_MIDPOINT_CHROMA_SAMPLES_BIT" },
    { VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_LINEAR_FILTER_BIT, "VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_LINEAR_FILTER_BIT" },
    { VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_SEPARATE_RECONSTRUCTION_FILTER_BIT, "VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_SEPARATE_RECONSTRUCTION_FILTER_BIT" },
    { VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_CHROMA_RECONSTRUCTION_EXPLICIT_BIT, "VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_CHROMA_RECONSTRUCTION_EXPLICIT_BIT" },
    { VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_CHROMA_RECONSTRUCTION_EXPLICIT_FORCEABLE_BIT, "VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_CHROMA_RECONSTRUCTION_EXPLICIT_FORCEABLE_BIT" },
    { VK_FORMAT_FEATURE_DISJOINT_BIT, "VK_FORMAT_FEATURE_DISJOINT_BIT" },
    { VK_FORMAT_FEATURE_COSITED_CHROMA_SAMPLES_BIT, "VK_FORMAT_FEATURE_COSITED_CHROMA_SAMPLES_BIT" },
    { VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_MINMAX_BIT, "VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_MINMAX_BIT" },
    { VK_FORMAT_FEATURE_VIDEO_DECODE_OUTPUT_BIT_KHR, "VK_FORMAT_FEATURE_VIDEO_DECODE_OUTPUT_BIT_KHR" },
    { VK_FORMAT_FEATURE_VIDEO_DECODE_DPB_BIT_KHR, "VK_FORMAT_FEATURE_VIDEO_DECODE_DPB_BIT_KHR" },
    { VK_FORMAT_FEATURE_ACCELERATION_STRUCTURE_VERTEX_BUFFER_BIT_KHR, "VK_FORMAT_FEATURE_ACCELERATION_STRUCTURE_VERTEX_BUFFER_BIT_KHR" },
    { VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_CUBIC_BIT_EXT, "VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_CUBIC_BIT_EXT" },
    { VK_FORMAT_FEATURE_FRAGMENT_DENSITY_MAP_BIT_EXT, "VK_FORMAT_FEATURE_FRAGMENT_DENSITY_MAP_BIT_EXT" },
    { VK_FORMAT_FEATURE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR, "VK_FORMAT_FEATURE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR" },
    { VK_FORMAT_FEATURE_VIDEO_ENCODE_INPUT_BIT_KHR, "VK_FORMAT_FEATURE_VIDEO_ENCODE_INPUT_BIT_KHR" },
    { VK_FORMAT_FEATURE_VIDEO_ENCODE_DPB_BIT_KHR, "VK_FORMAT_FEATURE_VIDEO_ENCODE_DPB_BIT_KHR" },
    {0,NULL}
};

static const struct json_name VkFormat_names[] = {
    { VK_FORMAT_R4G4_UNORM_PACK8, "VK_FORMAT_R4G4_UNORM_PACK8" },
    { VK_FORMAT_R4G4B4A4_UNORM_PACK16, "VK_FORMAT_R4G4B4A4_UNORM_PACK16" },
    { VK_FORMAT_B4G4R4A4_UNORM_PACK16, "VK_FORMAT_B4G4R4A4_UNORM_PACK16" },
    { VK_FORMAT_R5G6B5_UNORM_PACK16, "VK_FORMAT_R5G6B5_UNORM_PACK16" },
    { VK_FORMAT_B5G6R5_UNORM_PACK16, "VK_FORMAT_B5G6R5_UNORM_PACK16" },
    { VK_FORMAT_R5G5B5A1_UNORM_PACK16, "VK_FORMAT_R5G5B5A1_UNORM_PACK16" },
    { VK_FORMAT_B5G5R5A1_UNORM_PACK16, "VK_FORMAT_B5G5R5A1_UNORM_PACK16" },
    { VK_FORMAT_A1R5G5B5_UNORM_PACK16, "VK_FORMAT_A1R5G5B5_UNORM_PACK16" },
    { VK_FORMAT_R8_UNORM, "VK_FORMAT_R8_UNORM" },
    { VK_FORMAT_R8_SNORM, "VK_FORMAT_R8_SNORM" },
    { VK_FORMAT_R8_USCALED, "VK_FORMAT_R8_USCALED" },
    { VK_FORMAT_R8_SSCALED, "VK_FORMAT_R8_SSCALED" },
    { VK_FORMAT_R8_UINT, "VK_FORMAT_R8_UINT" },
    { VK_FORMAT_R8_SINT, "VK_FORMAT_R8_SINT" },
    { VK_FORMAT_R8_SRGB, "VK_FORMAT_R8_SRGB" },
    { VK_FORMAT_R8G8_UNORM, "VK_FORMAT_R8G8_UNORM" },
    { VK_FORMAT_R8G8_SNORM, "VK_FORMAT_R8G8_SNORM" },
    { VK_FORMAT_R8G8_USCALED, "VK_FORMAT_R8G8_USCALED" },
    { VK_FORMAT_R8G8_SSCALED, "VK_FORMAT_R8G8_SSCALED" },
    { VK_FORMAT_R8G8_UINT, "VK_FORMAT_R8G8_UINT" },
    { VK_FORMAT_R8G8_SINT, "VK_FORMAT_R8G8_SINT" },
    { VK_FORMAT_R8G8_SRGB, "VK_FORMAT_R8G8_SRGB" },
    { VK_FORMAT_R8G8B8_UNORM, "VK_FORMAT_R8G8B8_UNORM" },
    { VK_FORMAT_R8G8B8_SNORM, "VK_FORMAT_R8G8B8_SNORM" },
    { VK_FORMAT_R8G8B8_USCALED, "VK_FORMAT_R8G8B8_USCALED" },
    { VK_FORMAT_R8G8B8_SSCALED, "VK_FORMAT_R8G8B8_SSCALED" },
    { VK_FORMAT_R8G8B8_UINT, "VK_FORMAT_R8G8B8_UINT" },
    { VK_FORMAT_R8G8B8_SINT, "VK_FORMAT_R8G8B8_SINT" },
    { VK_FORMAT_R8G8B8_SRGB, "VK_FORMAT_R8G8B8_SRGB" },
    { VK_FORMAT_B8G8R8_UNORM, "VK_FORMAT_B8G8R8_UNORM" },
    { VK_FORMAT_B8G8R8_SNORM, "VK_FORMAT_B8G8R8_SNORM" },
    { VK_FORMAT_B8G8R8_USCALED, "VK_FORMAT_B8G8R8_USCALED" },
    { VK_FORMAT_B8G8R8_SSCALED, "VK_FORMAT_B8G8R8_SSCALED" },
    { VK_FORMAT_B8G8R8_UINT, "VK_FORMAT_B8G8R8_UINT" },
    { VK_FORMAT_B8G8R8_SINT, "VK_FORMAT_B8G8R8_SINT" },
    { VK_FORMAT_B8G8R8_SRGB, "VK_FORMAT_B8G8R8_SRGB" },
    { VK_FORMAT_R8G8B8A8_UNORM, "VK_FORMAT_R8G8B8A8_UNORM" },
    { VK_FORMAT_R8G8B8A8_SNORM, "VK_FORMAT_R8G8B8A8_SNORM" },
    { VK_FORMAT_R8G8B8A8_USCALED, "VK_FORMAT_R8G8B8A8_USCALED" },
    { VK_FORMAT_R8G8B8A8_SSCALED, "VK_FORMAT_R8G8B8A8_SSCALED" },
    { VK_FORMAT_R8G8B8A8_UINT, "VK_FORMAT_R8G8B8A8_UINT" },
    { VK_FORMAT_R8G8B8A8_SINT, "VK_FORMAT_R8G8B8A8_SINT" },
    { VK_FORMAT_R8G8B8A8_SRGB, "VK_FORMAT_R8G8B8A8_SRGB" },
    { VK_FORMAT_B8G8R8A8_UNORM, "VK_FORMAT_B8G8R8A8_UNORM" },
    { VK_FORMAT_B8G8R8A8_SNORM, "VK_FORMAT_B8G8R8A8_SNORM" },
    { VK_FORMAT_B8G8R8A8_USCALED, "VK_FORMAT_B8G8R8A8_USCALED" },
    { VK_FORMAT_B8G8R8A8_SSCALED, "VK_FORMAT_B8G8R8A8_SSCALED" },
    { VK_FORMAT_B8G8R8A8_UINT, "VK_FORMAT_B8G8R8A8_UINT" },
    { VK_FORMAT_B8G8R8A8_SINT, "VK_FORMAT_B8G8R8A8_SINT" },
    { VK_FORMAT_B8G8R8A8_SRGB, "VK_FORMAT_B8G8R8A8_SRGB" },
    { VK_FORMAT_A8B8G8R8_UNORM_PACK32, "VK_FORMAT_A8B8G8R8_UNORM_PACK32" },
    { VK_FORMAT_A8B8G8R8_SNORM_PACK32, "VK_FORMAT_A8B8G8R8_SNORM_PACK32" },
    { VK_FORMAT_A8B8G8R8_USCALED_PACK32, "VK_FORMAT_A8B8G8R8_USCALED_PACK32" },
    { VK_FORMAT_A8B8G8R8_SSCALED_PACK32, "VK_FORMAT_A8B8G8R8_SSCALED_PACK32" },
    { VK_FORMAT_A8B8G8R8_UINT_PACK32, "VK_FORMAT_A8B8G8R8_UINT_PACK32" },
    { VK_FORMAT_A8B8G8R8_SINT_PACK32, "VK_FORMAT_A8B8G8R8_SINT_PACK32" },
    { VK_FORMAT_A8B8G8R8_SRGB_PACK32, "VK_FORMAT_A8B8G8R8_SRGB_PACK32" },
    { VK_FORMAT_A2R10G10B10_UNORM_PACK32, "VK_FORMAT_A2R10G10B10_UNORM_PACK32" },
    { VK_FORMAT_A2R10G10B10_SNORM_PACK32, "VK_FORMAT_A2R10G10B10_SNORM_PACK32" },
    { VK_FORMAT_A2R10G10B10_USCALED_PACK32, "VK_FORMAT_A2R10G10B10_USCALED_PACK32" },
    { VK_FORMAT_A2R10G10B10_SSCALED_PACK32, "VK_FORMAT_A2R10G10B10_SSCALED_PACK32" },
    { VK_FORMAT_A2R10G10B10_UINT_PACK32, "VK_FORMAT_A2R10G10B10_UINT_PACK32" },
    { VK_FORMAT_A2R10G10B10_SINT_PACK32, "VK_FORMAT_A2R10G10B10_SINT_PACK32" },
    { VK_FORMAT_A2B10G10R10_UNORM_PACK32, "VK_FORMAT_A2B10G10R10_UNORM_PACK32" },
    { VK_FORMAT_A2B10G10R10_SNORM_PACK32, "VK_FORMAT_A2B10G10R10_SNORM_PACK32" },
    { VK_FORMAT_A2B10G10R10_USCALED_PACK32, "VK_FORMAT_A2B10G10R10_USCALED_PACK32" },
    { VK_FORMAT_A2B10G10R10_SSCALED_PACK32, "VK_FORMAT_A2B10G10R10_SSCALED_PACK32" },
    { VK_FORMAT_A2B10G10R10_UINT_PACK32, "VK_FORMAT_A2B10G10R10_UINT_PACK32" },
    { VK_FORMAT_A2B10G10R10_SINT_PACK32, "VK_FORMAT_A2B10G10R10_SINT_PACK32" },
    { VK_FORMAT_R16_UNORM, "VK_FORMAT_R16_UNORM" },
    { VK_FORMAT_R16_SNORM, "VK_FORMAT_R16_SNORM" },
    { VK_FORMAT_R16_USCALED, "VK_FORMAT_R16_USCALED" },
    { VK_FORMAT_R16_SSCALED, "VK_FORMAT_R16_SSCALED" },
    { VK_FORMAT_R16_UINT, "VK_FORMAT_R16_UINT" },
    { VK_FORMAT_R16_SINT, "VK_FORMAT_R16_SINT" },
    { VK_FORMAT_R16_SFLOAT, "VK_FORMAT_R16_SFLOAT" },
    { VK_FORMAT_R16G16_UNORM, "VK_FORMAT_R16G16_UNORM" },
    { VK_FORMAT_R16G16_SNORM, "VK_FORMAT_R16G16_SNORM" },
    { VK_FORMAT_R16G16_USCALED, "VK_FORMAT_R16G16_USCALED" },
    { VK_FORMAT_R16G16_SSCALED, "VK_FORMAT_R16G16_SSCALED" },
    { VK_FORMAT_R16G16_UINT, "VK_FORMAT_R16G16_UINT" },
    { VK_FORMAT_R16G16_SINT, "VK_FORMAT_R16G16_SINT" },
    { VK_FORMAT_R16G16_SFLOAT, "VK_FORMAT_R16G16_SFLOAT" },
    { VK_FORMAT_R16G16B16_UNORM, "VK_FORMAT_R16G16B16_UNORM" },
    { VK_FORMAT_R16G16B16_SNORM, "VK_FORMAT_R16G16B16_SNORM" },
    { VK_FORMAT_R16G16B16_USCALED, "VK_FORMAT_R16G16B16_USCALED" },
    { VK_FORMAT_R16G16B16_SSCALED, "VK_FORMAT_R16G16B16_SSCALED" },
    { VK_FORMAT_R16G16B16_UINT, "VK_FORMAT_R16G16B16_UINT" },
    { VK_FORMAT_R16G16B16_SINT, "VK_FORMAT_R16G16B16_SINT" },
    { VK_FORMAT_R16G16B16_SFLOAT, "VK_FORMAT_R16G16B16_SFLOAT" },
    { VK_FORMAT_R16G16B16A16_UNORM, "VK_FORMAT_R16G16B16A16_UNORM" },
    { VK_FORMAT_R16G16B16A16_SNORM, "VK_FORMAT_R16G16B16A16_SNORM" },
    { VK_FORMAT_R16G16B16A16_USCALED, "VK_FORMAT_R16G16B16A16_USCALED" },
    { VK_FORMAT_R16G16B16A16_SSCALED, "VK_FORMAT_R16G16B16A16_SSCALED" },
    { VK_FORMAT_R16G16B16A16_UINT, "VK_FORMAT_R16G16B16A16_UINT" },
    { VK_FORMAT_R16G16B16A16_SINT, "VK_FORMAT_R16G16B16A16_SINT" },
    { VK_FORMAT_R16G16B16A16_SFLOAT, "VK_FORMAT_R16G16B16A16_SFLOAT" },
    { VK_FORMAT_R32_UINT, "VK_FORMAT_R32_UINT" },
    { VK_FORMAT_R32_SINT, "VK_FORMAT_R32_SINT" },
    { VK_FORMAT_R32_SFLOAT, "VK_FORMAT_R32_SFLOAT" },
    { VK_FORMAT_R32G32_UINT, "VK_FORMAT_R32G32_UINT" },
    { VK_FORMAT_R32G32_SINT, "VK_FORMAT_R32G32_SINT" },
    { VK_FORMAT_R32G32_SFLOAT, "VK_FORMAT_R32G32_SFLOAT" },
    { VK_FORMAT_R32G32B32_UINT, "VK_FORMAT_R32G32B32_UINT" },
    { VK_FORMAT_R32G32B32_SINT, "VK_FORMAT_R32G32B32_SINT" },
    { VK_FORMAT_R32G32B32_SFLOAT, "VK_FORMAT_R32G32B32_SFLOAT" },
    { VK_FORMAT_R32G32B32A32_UINT, "VK_FORMAT_R32G32B32A32_UINT" },
    { VK_FORMAT_R32G32B32A32_SINT, "VK_FORMAT_R32G32B32A32_SINT" },
    { VK_FORMAT_R32G32B32A32_SFLOAT, "VK_FORMAT_R32G32B32A32_SFLOAT" },
    { VK_FORMAT_R64_UINT, "VK_FORMAT_R64_UINT" },
    { VK_FORMAT_R64_SINT, "VK_FORMAT_R64_SINT" },
    { VK_FORMAT_R64_SFLOAT, "VK_FORMAT_R64_SFLOAT" },
    { VK_FORMAT_R64G64_UINT, "VK_FORMAT_R64G64_UINT" },
    { VK_FORMAT_R64G64_SINT, "VK_FORMAT_R64G64_SINT" },
    { VK_FORMAT_R64G64_SFLOAT, "VK_FORMAT_R64G64_SFLOAT" },
    { VK_FORMAT_R64G64B64_UINT, "VK_FORMAT_R64G64B64_UINT" },
    { VK_FORMAT_R64G64B64_SINT, "VK_FORMAT_R64G64B64_SINT" },
    { VK_FORMAT_R64G64B64_SFLOAT, "VK_FORMAT_R64G64B64_SFLOAT" },
    { VK_FORMAT_R64G64B64A64_UINT, "VK_FORMAT_R64G64B64A64_UINT" },
    { VK_FORMAT_R64G64B64A64_SINT, "VK_FORMAT_R64G64B64A64_SINT" },
    { VK_FORMAT_R64G64B64A64_SFLOAT, "VK_FORMAT_R64G64B64A64_SFLOAT" },
    { VK_FORMAT_B10G11R11_UFLOAT_PACK32, "VK_FORMAT_B10G11R11_UFLOAT_PACK32" },
    { VK_FORMAT_E5B9G9R9_UFLOAT_PACK32, "VK_FORMAT_E5B9G9R9_UFLOAT_PACK32" },
    { VK_FORMAT_D16_UNORM, "VK_FORMAT_D16_UNORM" },
    { VK_FORMAT_X8_D24_UNORM_PACK32, "VK_FORMAT_X8_D24_UNORM_PACK32" },
    { VK_FORMAT_D32_SFLOAT, "VK_FORMAT_D32_SFLOAT" },
    { VK_FORMAT_S8_UINT, "VK_FORMAT_S8_UINT" },
    { VK_FORMAT_D16_UNORM_S8_UINT, "VK_FORMAT_D16_UNORM_S8_UINT" },
    { VK_FORMAT_D24_UNORM_S8_UINT, "VK_FORMAT_D24_UNORM_S8_UINT" },
    { VK_FORMAT_D32_SFLOAT_S8_UINT, "VK_FORMAT_D32_SFLOAT_S8_UINT" },
    { VK_FORMAT_BC1_RGB_UNORM_BLOCK, "VK_FORMAT_BC1_RGB_UNORM_BLOCK" },
    { VK_FORMAT_BC1_RGB_SRGB_BLOCK, "VK_FORMAT_BC1_RGB_SRGB_BLOCK" },
    { VK_FORMAT_BC1_RGBA_UNORM_BLOCK, "VK_FORMAT_BC1_RGBA_UNORM_BLOCK" },
    { VK_FORMAT_BC1_RGBA_SRGB_BLOCK, "VK_FORMAT_BC1_RGBA_SRGB_BLOCK" },
    { VK_FORMAT_BC2_UNORM_BLOCK, "VK_FORMAT_BC2_UNORM_BLOCK" },
    { VK_FORMAT_BC2_SRGB_BLOCK, "VK_FORMAT_BC2_SRGB_BLOCK" },
    { VK_FORMAT_BC3_UNORM_BLOCK, "VK_FORMAT_BC3_UNORM_BLOCK" },
    { VK_FORMAT_BC3_SRGB_BLOCK, "VK_FORMAT_BC3_SRGB_BLOCK" },
    { VK_FORMAT_BC4_UNORM_BLOCK, "VK_FORMAT_BC4_UNORM_BLOCK" },
    { VK_FORMAT_BC4_SNORM_BLOCK, "VK_FORMAT_BC4_SNORM_BLOCK" },
    { VK_FORMAT_BC5_UNORM_BLOCK, "VK_FORMAT_BC5_UNORM_BLOCK" },
    { VK_FORMAT_BC5_SNORM_BLOCK, "VK_FORMAT_BC5_SNORM_BLOCK" },
    { VK_FORMAT_BC6H_UFLOAT_BLOCK, "VK_FORMAT_BC6H_UFLOAT_BLOCK" },
    { VK_FORMAT_BC6H_SFLOAT_BLOCK, "VK_FORMAT_BC6H_SFLOAT_BLOCK" },
    { VK_FORMAT_BC7_UNORM_BLOCK, "VK_FORMAT_BC7_UNORM_BLOCK" },
    { VK_FORMAT_BC7_SRGB_BLOCK, "VK_FORMAT_BC7_SRGB_BLOCK" },
    { VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK, "VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK" },
    { VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK, "VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK" },
    { VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK, "VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK" },
    { VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK, "VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK" },
    { VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK, "VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK" },
    { VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK, "VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK" },
    { VK_FORMAT_EAC_R11_UNORM_BLOCK, "VK_FORMAT_EAC_R11_UNORM_BLOCK" },
    { VK_FORMAT_EAC_R11_SNORM_BLOCK, "VK_FORMAT_EAC_R11_SNORM_BLOCK" },
    { VK_FORMAT_EAC_R11G11_UNORM_BLOCK, "VK_FORMAT_EAC_R11G11_UNORM_BLOCK" },
    { VK_FORMAT_EAC_R11G11_SNORM_BLOCK, "VK_FORMAT_EAC_R11G11_SNORM_BLOCK" },
    { VK_FORMAT_ASTC_4x4_UNORM_BLOCK, "VK_FORMAT_ASTC_4x4_UNORM_BLOCK" },
    { VK_FORMAT_ASTC_4x4_SRGB_BLOCK, "VK_FORMAT_ASTC_4x4_SRGB_BLOCK" },
    { VK_FORMAT_ASTC_5x4_UNORM_BLOCK, "VK_FORMAT_ASTC_5x4_UNORM_BLOCK" },
    { VK_FORMAT_ASTC_5x4_SRGB_BLOCK, "VK_FORMAT_ASTC_5x4_SRGB_BLOCK" },
    { VK_FORMAT_ASTC_5x5_UNORM_BLOCK, "VK_FORMAT_ASTC_5x5_UNORM_BLOCK" },
    { VK_FORMAT_ASTC_5x5_SRGB_BLOCK, "VK_FORMAT_ASTC_5x5_SRGB_BLOCK" },
    { VK_FORMAT_ASTC_6x5_UNORM_BLOCK, "VK_FORMAT_ASTC_6x5_UNORM_BLOCK" },
    { VK_FORMAT_ASTC_6x5_SRGB_BLOCK, "VK_FORMAT_ASTC_6x5_SRGB_BLOCK" },
    { VK_FORMAT_ASTC_6x6_UNORM_BLOCK, "VK_FORMAT_ASTC_6x6_UNORM_BLOCK" },
    { VK_FORMAT_ASTC_6x6_SRGB_BLOCK, "VK_FORMAT_ASTC_6x6_SRGB_BLOCK" },
    { VK_FORMAT_ASTC_8x5_UNORM_BLOCK, "VK_FORMAT_ASTC_8x5_UNORM_BLOCK" },
    { VK_FORMAT_ASTC_8x5_SRGB_BLOCK, "VK_FORMAT_ASTC_8x5_SRGB_BLOCK" },
    { VK_FORMAT_ASTC_8x6_UNORM_BLOCK, "VK_FORMAT_ASTC_8x6_UNORM_BLOCK" },
    { VK_FORMAT_ASTC_8x6_SRGB_BLOCK, "VK_FORMAT_ASTC_8x6_SRGB_BLOCK" },
    { VK_FORMAT_ASTC_8x8_UNORM_BLOCK, "VK_FORMAT_ASTC_8x8_UNORM_BLOCK" },
    { VK_FORMAT_ASTC_8x8_SRGB_BLOCK, "VK_FORMAT_ASTC_8x8_SRGB_BLOCK" },
    { VK_FORMAT_ASTC_10x5_UNORM_BLOCK, "VK_FORMAT_ASTC_10x5_UNORM_BLOCK" },
    { VK_FORMAT_ASTC_10x5_SRGB_BLOCK, "VK_FORMAT_ASTC_10x5_SRGB_BLOCK" },
    { VK_FORMAT_ASTC_10x6_UNORM_BLOCK, "VK_FORMAT_ASTC_10x6_UNORM_BLOCK" },
    { VK_FORMAT_ASTC_10x6_SRGB_BLOCK, "VK_FORMAT_ASTC_10x6_SRGB_BLOCK" },
    { VK_FORMAT_ASTC_10x8_UNORM_BLOCK, "VK_FORMAT_ASTC_10x8_UNORM_BLOCK" },
    { VK_FORMAT_ASTC_10x8_SRGB_BLOCK, "VK_FORMAT_ASTC_10x8_SRGB_BLOCK" },
    { VK_FORMAT_ASTC_10x10_UNORM_BLOCK, "VK_FORMAT_ASTC_10x10_UNORM_BLOCK" },
    { VK_FORMAT_ASTC_10x10_SRGB_BLOCK, "VK_FORMAT_ASTC_10x10_SRGB_BLOCK" },
    { VK_FORMAT_ASTC_12x10_UNORM_BLOCK, "VK_FORMAT_ASTC_12x10_UNORM_BLOCK" },
    { VK_FORMAT_ASTC_12x10_SRGB_BLOCK, "VK_FORMAT_ASTC_12x10_SRGB_BLOCK" },
    { VK_FORMAT_ASTC_12x12_UNORM_BLOCK, "VK_FORMAT_ASTC_12x12_UNORM_BLOCK" },
    { VK_FORMAT_ASTC_12x12_SRGB_BLOCK, "VK_FORMAT_ASTC_12x12_SRGB_BLOCK" },
    {0,NULL}
};

static const struct json_name VkPhysicalDeviceType_names[] = {
    { VK_PHYSICAL_DEVICE_TYPE_OTHER, "VK_PHYSICAL_DEVICE_TYPE_OTHER" },
    { VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU, "VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU" },
    { VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, "VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU" },
    { VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU, "VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU" },
    { VK_PHYSICAL_DEVICE_TYPE_CPU, "VK_PHYSICAL_DEVICE_TYPE_CPU" },
    {0,NULL}
};

static void serialize_VkPhysicalDeviceFeatures(struct json_writer *j, const VkPhysicalDeviceFeatures *p)
{
    MMVK_JSON_BOOL(j, "robustBufferAccess", p->robustBufferAccess);
    MMVK_JSON_BOOL(j, "fullDrawIndexUint32", p->fullDrawIndexUint32);
    MMVK_JSON_BOOL(j, "imageCubeArray", p->imageCubeArray);
    MMVK_JSON_BOOL(j, "independentBlend", p->independentBlend);
    MMVK_JSON_BOOL(j, "geometryShader", p->geometryShader);
    MMVK_JSON_BOOL(j, "tessellationShader", p->tessellationShader);
    MMVK_JSON_BOOL(j, "sampleRateShading", p->sampleRateShading);
    MMVK_JSON_BOOL(j, "dualSrcBlend", p->dualSrcBlend);
    MMVK_JSON_BOOL(j, "logicOp", p->logicOp);
    MMVK_JSON_BOOL(j, "multiDrawIndirect", p->multiDrawIndirect);
    MMVK_JSON_BOOL(j, "drawIndirectFirstInstance", p->drawIndirectFirstInstance);
    MMVK_JSON_BOOL(j, "depthClamp", p->depthClamp);
    MMVK_JSON_BOOL(j, "depthBiasClamp", p->depthBiasClamp);
    MMVK_JSON_BOOL(j, "fillModeNonSolid", p->fillModeNonSolid);
    MMVK_JSON_BOOL(j, "depthBounds", p->depthBounds);
    MMVK_JSON_BOOL(j, "wideLines", p->wideLines);
    MMVK_JSON_BOOL(j, "largePoints", p->largePoints);
    MMVK_JSON_BOOL(j, "alphaToOne", p->alphaToOne);
    MMVK_JSON_BOOL(j, "multiViewport", p->multiViewport);
    MMVK_JSON_BOOL(j, "samplerAnisotropy", p->samplerAnisotropy);
    MMVK_JSON_BOOL(j, "textureCompressionETC2", p->textureCompressionETC2);
    MMVK_JSON_BOOL(j, "textureCompressionASTC_LDR", p->textureCompressionASTC_LDR);
    MMVK_JSON_BOOL(j, "textureCompressionBC", p->textureCompressionBC);
    MMVK_JSON_BOOL(j, "occlusionQueryPrecise", p->occlusionQueryPrecise);
    MMVK_JSON_BOOL(j, "pipelineStatisticsQuery", p->pipelineStatisticsQuery);
    MMVK_JSON_BOOL(j, "vertexPipelineStoresAndAtomics", p->vertexPipelineStoresAndAtomics);
    MMVK_JSON_BOOL(j, "fragmentStoresAndAtomics", p->fragmentStoresAndAtomics);
    MMVK_JSON_BOOL(j, "shaderTessellationAndGeometryPointSize", p->shaderTessellationAndGeometryPointSize);
    MMVK_JSON_BOOL(j, "shaderImageGatherExtended", p->shaderImageGatherExtended);
    MMVK_JSON_BOOL(j, "shaderStorageImageExtendedFormats", p->shaderStorageImageExtendedFormats);
    MMVK_JSON_BOOL(j, "shaderStorageImageMultisample", p->shaderStorageImageMultisample);
    MMVK_JSON_BOOL(j, "shaderStorageImageReadWithoutFormat", p->shaderStorageImageReadWithoutFormat);
    MMVK_JSON_BOOL(j, "shaderStorageImageWriteWithoutFormat", p->shaderStorageImageWriteWithoutFormat);
    MMVK_JSON_BOOL(j, "shaderUniformBufferArrayDynamicIndexing", p->shaderUniformBufferArrayDynamicIndexing);
    MMVK_JSON_BOOL(j, "shaderSampledImageArrayDynamicIndexing", p->shaderSampledImageArrayDynamicIndexing);
    MMVK_JSON_BOOL(j, "shaderStorageBufferArrayDynamicIndexing", p->shaderStorageBufferArrayDynamicIndexing);
    MMVK_JSON_BOOL(j, "shaderStorageImageArrayDynamicIndexing", p->shaderStorageImageArrayDynamicIndexing);
    MMVK_JSON_BOOL(j, "shaderClipDistance", p->shaderClipDistance);
    MMVK_JSON_BOOL(j, "shaderCullDistance", p->shaderCullDistance);
    MMVK_JSON_BOOL(j, "shaderFloat64", p->shaderFloat64);
    MMVK_JSON_BOOL(j, "shaderInt64", p->shaderInt64);
    MMVK_JSON_BOOL(j, "shaderInt16", p->shaderInt16);
    MMVK_JSON_BOOL(j, "shaderResourceResidency", p->shaderResourceResidency);
    MMVK_JSON_BOOL(j, "shaderResourceMinLod", p->shaderResourceMinLod);
    MMVK_JSON_BOOL(j, "sparseBinding", p->sparseBinding);
    MMVK_JSON_BOOL(j, "sparseResidencyBuffer", p->sparseResidencyBuffer);
    MMVK_JSON_BOOL(j, "sparseResidencyImage2D", p->sparseResidencyImage2D);
    MMVK_JSON_BOOL(j, "sparseResidencyImage3D", p->sparseResidencyImage3D);
    MMVK_JSON_BOOL(j, "sparseResidency2Samples", p->sparseResidency2Samples);
    MMVK_JSON_BOOL(j, "sparseResidency4Samples", p->sparseResidency4Samples);
    MMVK_JSON_BOOL(j, "sparseResidency8Samples", p->sparseResidency8Samples);
    MMVK_JSON_BOOL(j, "sparseResidency16Samples", p->sparseResidency16Samples);
    MMVK_JSON_BOOL(j, "sparseResidencyAliased", p->sparseResidencyAliased);
    MMVK_JSON_BOOL(j, "variableMultisampleRate", p->variableMultisampleRate);
    MMVK_JSON_BOOL(j, "inheritedQueries", p->inheritedQueries);
}

static void serialize_VkPhysicalDeviceVulkan11Features(struct json_writer *j, const VkPhysicalDeviceVulkan11Features *p)
{
    MMVK_JSON_BOOL(j, "storageBuffer16BitAccess", p->storageBuffer16BitAccess);
    MMVK_JSON_BOOL(j, "uniformAndStorageBuffer16BitAccess", p->uniformAndStorageBuffer16BitAccess);
    MMVK_JSON_BOOL(j, "storagePushConstant16", p->storagePushConstant16);
    MMVK_JSON_BOOL(j, "storageInputOutput16", p->storageInputOutput16);
    MMVK_JSON_BOOL(j, "multiview", p->multiview);
    MMVK_JSON_BOOL(j, "multiviewGeometryShader", p->multiviewGeometryShader);
    MMVK_JSON_BOOL(j, "multiviewTessellationShader", p->multiviewTessellationShader);
    MMVK_JSON_BOOL(j, "variablePointersStorageBuffer", p->variablePointersStorageBuffer);
    MMVK_JSON_BOOL(j, "variablePointers", p->variablePointers);
    MMVK_JSON_BOOL(j, "protectedMemory", p->protectedMemory);
    MMVK_JSON_BOOL(j, "samplerYcbcrConversion", p->samplerYcbcrConversion);
    MMVK_JSON_BOOL(j, "shaderDrawParameters", p->shaderDrawParameters);
}

static void serialize_VkPhysicalDeviceVulkan12Features(struct json_writer *j, const VkPhysicalDeviceVulkan12Features *p)
{
    MMVK_JSON_BOOL(j, "samplerMirrorClampToEdge", p->samplerMirrorClampToEdge);
    MMVK_JSON_BOOL(j, "drawIndirectCount", p->drawIndirectCount);
    MMVK_JSON_BOOL(j, "storageBuffer8BitAccess", p->storageBuffer8BitAccess);
    MMVK_JSON_BOOL(j, "uniformAndStorageBuffer8BitAccess", p->uniformAndStorageBuffer8BitAccess);
    MMVK_JSON_BOOL(j, "storagePushConstant8", p->storagePushConstant8);
    MMVK_JSON_BOOL(j, "shaderBufferInt64Atomics", p->shaderBufferInt64Atomics);
    MMVK_JSON_BOOL(j, "shaderSharedInt64Atomics", p->shaderSharedInt64Atomics);
    MMVK_JSON_BOOL(j, "shaderFloat16", p->shaderFloat16);
    MMVK_JSON_BOOL(j, "shaderInt8", p->shaderInt8);
    MMVK_JSON_BOOL(j, "descriptorIndexing", p->descriptorIndexing);
    MMVK_JSON_BOOL(j, "shaderInputAttachmentArrayDynamicIndexing", p->shaderInputAttachmentArrayDynamicIndexing);
    MMVK_JSON_BOOL(j, "shaderUniformTexelBufferArrayDynamicIndexing", p->shaderUniformTexelBufferArrayDynamicIndexing);
    MMVK_JSON_BOOL(j, "shaderStorageTexelBufferArrayDynamicIndexing", p->shaderStorageTexelBufferArrayDynamicIndexing);
    MMVK_JSON_BOOL(j, "shaderUniformBufferArrayNonUniformIndexing", p->shaderUniformBufferArrayNonUniformIndexing);
    MMVK_JSON_BOOL(j, "shaderSampledImageArrayNonUniformIndexing", p->shaderSampledImageArrayNonUniformIndexing);
    MMVK_JSON_BOOL(j, "shaderStorageBufferArrayNonUniformIndexing", p->shaderStorageBufferArrayNonUniformIndexing);
    MMVK_JSON_BOOL(j, "shaderStorageImageArrayNonUniformIndexing", p->shaderStorageImageArrayNonUniformIndexing);
    MMVK_JSON_BOOL(j, "shaderInputAttachmentArrayNonUniformIndexing", p->shaderInputAttachmentArrayNonUniformIndexing);
    MMVK_JSON_BOOL(j, "shaderUniformTexelBufferArrayNonUniformIndexing", p->shaderUniformTexelBufferArrayNonUniformIndexing);
    MMVK_JSON_BOOL(j, "shaderStorageTexelBufferArrayNonUniformIndexing", p->shaderStorageTexelBufferArrayNonUniformIndexing);
    MMVK_JSON_BOOL(j, "descriptorBindingUniformBufferUpdateAfterBind", p->descriptorBindingUniformBufferUpdateAfterBind);
    MMVK_JSON_BOOL(j, "descriptorBindingSampledImageUpdateAfterBind", p->descriptorBindingSampledImageUpdateAfterBind);
    MMVK_JSON_BOOL(j, "descriptorBindingStorageImageUpdateAfterBind", p->descriptorBindingStorageImageUpdateAfterBind);
    MMVK_JSON_BOOL(j, "descriptorBindingStorageBufferUpdateAfterBind", p->descriptorBindingStorageBufferUpdateAfterBind);
    MMVK_JSON_BOOL(j, "descriptorBindingUniformTexelBufferUpdateAfterBind", p->descriptorBindingUniformTexelBufferUpdateAfterBind);
    MMVK_JSON_BOOL(j, "descriptorBindingStorageTexelBufferUpdateAfterBind", p->descriptorBindingStorageTexelBufferUpdateAfterBind);
    MMVK_JSON_BOOL(j, "descriptorBindingUpdateUnusedWhilePending", p->descriptorBindingUpdateUnusedWhilePending);
    MMVK_JSON_BOOL(j, "descriptorBindingPartiallyBound", p->descriptorBindingPartiallyBound);
    MMVK_JSON_BOOL(j, "descriptorBindingVariableDescriptorCount", p->descriptorBindingVariableDescriptorCount);
    MMVK_JSON_BOOL(j, "runtimeDescriptorArray", p->runtimeDescriptorArray);
    MMVK_JSON_BOOL(j, "samplerFilterMinmax", p->samplerFilterMinmax);
    MMVK_JSON_BOOL(j, "scalarBlockLayout", p->scalarBlockLayout);
    MMVK_JSON_BOOL(j, "imagelessFramebuffer", p->imagelessFramebuffer);
    MMVK_JSON_BOOL(j, "uniformBufferStandardLayout", p->uniformBufferStandardLayout);
    MMVK_JSON_BOOL(j, "shaderSubgroupExtendedTypes", p->shaderSubgroupExtendedTypes);
    MMVK_JSON_BOOL(j, "separateDepthStencilLayouts", p->separateDepthStencilLayouts);
    MMVK_JSON_BOOL(j, "hostQueryReset", p->hostQueryReset);
    MMVK_JSON_BOOL(j, "timelineSemaphore", p->timelineSemaphore);
    MMVK_JSON_BOOL(j, "bufferDeviceAddress", p->bufferDeviceAddress);
    MMVK_JSON_BOOL(j, "bufferDeviceAddressCaptureReplay", p->bufferDeviceAddressCaptureReplay);
    MMVK_JSON_BOOL(j, "bufferDeviceAddressMultiDevice", p->bufferDeviceAddressMultiDevice);
    MMVK_JSON_BOOL(j, "vulkanMemoryModel", p->vulkanMemoryModel);
    MMVK_JSON_BOOL(j, "vulkanMemoryModelDeviceScope", p->vulkanMemoryModelDeviceScope);
    MMVK_JSON_BOOL(j, "vulkanMemoryModelAvailabilityVisibilityChains", p->vulkanMemoryModelAvailabilityVisibilityChains);
    MMVK_JSON_BOOL(j, "shaderOutputViewportIndex", p->shaderOutputViewportIndex);
    MMVK_JSON_BOOL(j, "shaderOutputLayer", p->shaderOutputLayer);
    MMVK_JSON_BOOL(j, "subgroupBroadcastDynamicId", p->subgroupBroadcastDynamicId);
}

static void serialize_VkPhysicalDeviceLimits(struct json_writer *j, const VkPhysicalDeviceLimits *p)
{
    json_unsigned(j, "maxImageDimension1D", p->maxImageDimension1D);
    json_unsigned(j, "maxImageDimension2D", p->maxImageDimension2D);
    json_unsigned(j, "maxImageDimension3D", p->maxImageDimension3D);
    json_unsigned(j, "maxImageDimensionCube", p->maxImageDimensionCube);
    json_unsigned(j, "maxImageArrayLayers", p->maxImageArrayLayers);
    json_unsigned(j, "maxTexelBufferElements", p->maxTexelBufferElements);
    json_unsigned(j, "maxUniformBufferRange", p->maxUniformBufferRange);
    json_unsigned(j, "maxStorageBufferRange", p->maxStorageBufferRange);
    json_unsigned(j, "maxPushConstantsSize", p->maxPushConstantsSize);
    json_unsigned(j, "maxMemoryAllocationCount", p->maxMemoryAllocationCount);
    json_unsigned(j, "maxSamplerAllocationCount", p->maxSamplerAllocationCount);
    json_unsigned(j, "bufferImageGranularity", p->bufferImageGranularity);
    json_unsigned(j, "sparseAddressSpaceSize", p->sparseAddressSpaceSize);
    json_unsigned(j, "maxBoundDescriptorSets", p->maxBoundDescriptorSets);
    json_unsigned(j, "maxPerStageDescriptorSamplers", p->maxPerStageDescriptorSamplers);
    json_unsigned(j, "maxPerStageDescriptorUniformBuffers", p->maxPerStageDescriptorUniformBuffers);
    json_unsigned(j, "maxPerStageDescriptorStorageBuffers", p->maxPerStageDescriptorStorageBuffers);
    json_unsigned(j, "maxPerStageDescriptorSampledImages", p->maxPerStageDescriptorSampledImages);
    json_unsigned(j, "maxPerStageDescriptorStorageImages", p->maxPerStageDescriptorStorageImages);
    json_unsigned(j, "maxPerStageDescriptorInputAttachments", p->maxPerStageDescriptorInputAttachments);
    json_unsigned(j, "maxPerStageResources", p->maxPerStageResources);
    json_unsigned(j, "maxDescriptorSetSamplers", p->maxDescriptorSetSamplers);
    json_unsigned(j, "maxDescriptorSetUniformBuffers", p->maxDescriptorSetUniformBuffers);
    json_unsigned(j, "maxDescriptorSetUniformBuffersDynamic", p->maxDescriptorSetUniformBuffersDynamic);
    json_unsigned(j, "maxDescriptorSetStorageBuffers", p->maxDescriptorSetStorageBuffers);
    json_unsigned(j, "maxDescriptorSetStorageBuffersDynamic", p->maxDescriptorSetStorageBuffersDynamic);
    json_unsigned(j, "maxDescriptorSetSampledImages", p->maxDescriptorSetSampledImages);
    json_unsigned(j, "maxDescriptorSetStorageImages", p->maxDescriptorSetStorageImages);
    json_unsigned(j, "maxDescriptorSetInputAttachments", p->maxDescriptorSetInputAttachments);
    json_unsigned(j, "maxVertexInputAttributes", p->maxVertexInputAttributes);
    json_unsigned(j, "maxVertexInputBindings", p->maxVertexInputBindings);
    json_unsigned(j, "maxVertexInputAttributeOffset", p->maxVertexInputAttributeOffset);
    json_unsigned(j, "maxVertexInputBindingStride", p->maxVertexInputBindingStride);
    json_unsigned(j, "maxVertexOutputComponents", p->maxVertexOutputComponents);
    json_unsigned(j, "maxTessellationGenerationLevel", p->maxTessellationGenerationLevel);
    json_unsigned(j, "maxTessellationPatchSize", p->maxTessellationPatchSize);
    json_unsigned(j, "maxTessellationControlPerVertexInputComponents", p->maxTessellationControlPerVertexInputComponents);
    json_unsigned(j, "maxTessellationControlPerVertexOutputComponents", p->maxTessellationControlPerVertexOutputComponents);
    json_unsigned(j, "maxTessellationControlPerPatchOutputComponents", p->maxTessellationControlPerPatchOutputComponents);
    json_unsigned(j, "maxTessellationControlTotalOutputComponents", p->maxTessellationControlTotalOutputComponents);
    json_unsigned(j, "maxTessellationEvaluationInputComponents", p->maxTessellationEvaluationInputComponents);
    json_unsigned(j, "maxTessellationEvaluationOutputComponents", p->maxTessellationEvaluationOutputComponents);
    json_unsigned(j, "maxGeometryShaderInvocations", p->maxGeometryShaderInvocations);
    json_unsigned(j, "maxGeometryInputComponents", p->maxGeometryInputComponents);
    json_unsigned(j, "maxGeometryOutputComponents", p->maxGeometryOutputComponents);
    json_unsigned(j, "maxGeometryOutputVertices", p->maxGeometryOutputVertices);
    json_unsigned(j, "maxGeometryTotalOutputComponents", p->maxGeometryTotalOutputComponents);
    json_unsigned(j, "maxFragmentInputComponents", p->maxFragmentInputComponents);
    json_unsigned(j, "maxFragmentOutputAttachments", p->maxFragmentOutputAttachments);
    json_unsigned(j, "maxFragmentDualSrcAttachments", p->maxFragmentDualSrcAttachments);
    json_unsigned(j, "maxFragmentCombinedOutputResources", p->maxFragmentCombinedOutputResources);
    json_unsigned(j, "maxComputeSharedMemorySize", p->maxComputeSharedMemorySize);
    MMVK_JSON_ARRAY(j, "maxComputeWorkGroupCount", p->maxComputeWorkGroupCount, 3);
    json_unsigned(j, "maxComputeWorkGroupInvocations", p->maxComputeWorkGroupInvocations);
    MMVK_JSON_ARRAY(j, "maxComputeWorkGroupSize", p->maxComputeWorkGroupSize, 3);
    json_unsigned(j, "subPixelPrecisionBits", p->subPixelPrecisionBits);
    json_unsigned(j, "subTexelPrecisionBits", p->subTexelPrecisionBits);
    json_unsigned(j, "mipmapPrecisionBits", p->mipmapPrecisionBits);
    json_unsigned(j, "maxDrawIndexedIndexValue", p->maxDrawIndexedIndexValue);
    json_unsigned(j, "maxDrawIndirectCount", p->maxDrawIndirectCount);
    json_float(j, "maxSamplerLodBias", p->maxSamplerLodBias);
    json_float(j, "maxSamplerAnisotropy", p->maxSamplerAnisotropy);
    json_unsigned(j, "maxViewports", p->maxViewports);
    MMVK_JSON_ARRAY(j, "maxViewportDimensions", p->maxViewportDimensions, 2);
    MMVK_JSON_ARRAY(j, "viewportBoundsRange", p->viewportBoundsRange, 2);
    json_unsigned(j, "viewportSubPixelBits", p->viewportSubPixelBits);
    json_unsigned(j, "minMemoryMapAlignment", p->minMemoryMapAlignment);
    json_unsigned(j, "minTexelBufferOffsetAlignment", p->minTexelBufferOffsetAlignment);
    json_unsigned(j, "minUniformBufferOffsetAlignment", p->minUniformBufferOffsetAlignment);
    json_unsigned(j, "minStorageBufferOffsetAlignment", p->minStorageBufferOffsetAlignment);
    json_signed(j, "minTexelOffset", p->minTexelOffset);
    json_unsigned(j, "maxTexelOffset", p->maxTexelOffset);
    json_signed(j, "minTexelGatherOffset", p->minTexelGatherOffset);
    json_unsigned(j, "maxTexelGatherOffset", p->maxTexelGatherOffset);
    json_float(j, "minInterpolationOffset", p->minInterpolationOffset);
    json_float(j, "maxInterpolationOffset", p->maxInterpolationOffset);
    json_unsigned(j, "subPixelInterpolationOffsetBits", p->subPixelInterpolationOffsetBits);
    json_unsigned(j, "maxFramebufferWidth", p->maxFramebufferWidth);
    json_unsigned(j, "maxFramebufferHeight", p->maxFramebufferHeight);
    json_unsigned(j, "maxFramebufferLayers", p->maxFramebufferLayers);
    json_flags(j, "framebufferColorSampleCounts", p->framebufferColorSampleCounts, VkSampleCountFlags_names);
    json_flags(j, "framebufferDepthSampleCounts", p->framebufferDepthSampleCounts, VkSampleCountFlags_names);
    json_flags(j, "framebufferStencilSampleCounts", p->framebufferStencilSampleCounts, VkSampleCountFlags_names);
    json_flags(j, "framebufferNoAttachmentsSampleCounts", p->framebufferNoAttachmentsSampleCounts, VkSampleCountFlags_names);
    json_unsigned(j, "maxColorAttachments", p->maxColorAttachments);
    json_flags(j, "sampledImageColorSampleCounts", p->sampledImageColorSampleCounts, VkSampleCountFlags_names);
    json_flags(j, "sampledImageIntegerSampleCounts", p->sampledImageIntegerSampleCounts, VkSampleCountFlags_names);
    json_flags(j, "sampledImageDepthSampleCounts", p->sampledImageDepthSampleCounts, VkSampleCountFlags_names);
    json_flags(j, "sampledImageStencilSampleCounts", p->sampledImageStencilSampleCounts, VkSampleCountFlags_names);
    json_flags(j, "storageImageSampleCounts", p->storageImageSampleCounts, VkSampleCountFlags_names);
    json_unsigned(j, "maxSampleMaskWords", p->maxSampleMaskWords);
    MMVK_JSON_BOOL(j, "timestampComputeAndGraphics", p->timestampComputeAndGraphics);
    json_float(j, "timestampPeriod", p->timestampPeriod);
    json_unsigned(j, "maxClipDistances", p->maxClipDistances);
    json_unsigned(j, "maxCullDistances", p->maxCullDistances);
    json_unsigned(j, "maxCombinedClipAndCullDistances", p->maxCombinedClipAndCullDistances);
    json_unsigned(j, "discreteQueuePriorities", p->discreteQueuePriorities);
    MMVK_JSON_ARRAY(j, "pointSizeRange", p->pointSizeRange, 2);
    MMVK_JSON_ARRAY(j, "lineWidthRange", p->lineWidthRange, 2);
    json_float(j, "pointSizeGranularity", p->pointSizeGranularity);
    json_float(j, "lineWidthGranularity", p->lineWidthGranularity);
    MMVK_JSON_BOOL(j, "strictLines", p->strictLines);
    MMVK_JSON_BOOL(j, "standardSampleLocations", p->standardSampleLocations);
    json_unsigned(j, "optimalBufferCopyOffsetAlignment", p->optimalBufferCopyOffsetAlignment);
    json_unsigned(j, "optimalBufferCopyRowPitchAlignment", p->optimalBufferCopyRowPitchAlignment);
    json_unsigned(j, "nonCoherentAtomSize", p->nonCoherentAtomSize);
}

static void serialize_VkPhysicalDeviceVulkan11Properties(struct json_writer *j, const VkPhysicalDeviceVulkan11Properties *p)
{
    MMVK_JSON_ARRAY(j, "deviceUUID", p->deviceUUID, VK_UUID_SIZE);
    MMVK_JSON_ARRAY(j, "driverUUID", p->driverUUID, VK_UUID_SIZE);
    MMVK_JSON_ARRAY(j, "deviceLUID", p->deviceLUID, VK_LUID_SIZE);
    json_unsigned(j, "deviceNodeMask", p->deviceNodeMask);
    MMVK_JSON_BOOL(j, "deviceLUIDValid", p->deviceLUIDValid);
    json_unsigned(j, "subgroupSize", p->subgroupSize);
    json_flags(j, "subgroupSupportedStages", p->subgroupSupportedStages, VkShaderStageFlags_names);
    json_flags(j, "subgroupSupportedOperations", p->subgroupSupportedOperations, VkSubgroupFeatureFlags_names);
    MMVK_JSON_BOOL(j, "subgroupQuadOperationsInAllStages", p->subgroupQuadOperationsInAllStages);
    json_enum(j, "pointClippingBehavior", p->pointClippingBehavior, VkPointClippingBehavior_names);
    json_unsigned(j, "maxMultiviewViewCount", p->maxMultiviewViewCount);
    json_unsigned(j, "maxMultiviewInstanceIndex", p->maxMultiviewInstanceIndex);
    MMVK_JSON_BOOL(j, "protectedNoFault", p->protectedNoFault);
    json_unsigned(j, "maxPerSetDescriptors", p->maxPerSetDescriptors);
    json_unsigned(j, "maxMemoryAllocationSize", p->maxMemoryAllocationSize);
}

static void serialize_VkPhysicalDeviceVulkan12Properties(struct json_writer *j, const VkPhysicalDeviceVulkan12Properties *p)
{
    json_enum(j, "driverID", p->driverID, VkDriverId_names);
    json_enum(j, "denormBehaviorIndependence", p->denormBehaviorIndependence, VkShaderFloatControlsIndependence_names);
    json_enum(j, "roundingModeIndependence", p->roundingModeIndependence, VkShaderFloatControlsIndependence_names);
    MMVK_JSON_BOOL(j, "shaderSignedZeroInfNanPreserveFloat16", p->shaderSignedZeroInfNanPreserveFloat16);
    MMVK_JSON_BOOL(j, "shaderSignedZeroInfNanPreserveFloat32", p->shaderSignedZeroInfNanPreserveFloat32);
    MMVK_JSON_BOOL(j, "shaderSignedZeroInfNanPreserveFloat64", p->shaderSignedZeroInfNanPreserveFloat64);
    MMVK_JSON_BOOL(j, "shaderDenormPreserveFloat16", p->shaderDenormPreserveFloat16);
    MMVK_JSON_BOOL(j, "shaderDenormPreserveFloat32", p->shaderDenormPreserveFloat32);
    MMVK_JSON_BOOL(j, "shaderDenormPreserveFloat64", p->shaderDenormPreserveFloat64);
    MMVK_JSON_BOOL(j, "shaderDenormFlushToZeroFloat16", p->shaderDenormFlushToZeroFloat16);
    MMVK_JSON_BOOL(j, "shaderDenormFlushToZeroFloat32", p->shaderDenormFlushToZeroFloat32);
    MMVK_JSON_BOOL(j, "shaderDenormFlushToZeroFloat64", p->shaderDenormFlushToZeroFloat64);
    MMVK_JSON_BOOL(j, "shaderRoundingModeRTEFloat16", p->shaderRoundingModeRTEFloat16);
    MMVK_JSON_BOOL(j, "shaderRoundingModeRTEFloat32", p->shaderRoundingModeRTEFloat32);
    MMVK_JSON_BOOL(j, "shaderRoundingModeRTEFloat64", p->shaderRoundingModeRTEFloat64);
    MMVK_JSON_BOOL(j, "shaderRoundingModeRTZFloat16", p->shaderRoundingModeRTZFloat16);
    MMVK_JSON_BOOL(j, "shaderRoundingModeRTZFloat32", p->shaderRoundingModeRTZFloat32);
    MMVK_JSON_BOOL(j, "shaderRoundingModeRTZFloat64", p->shaderRoundingModeRTZFloat64);
    json_unsigned(j, "maxUpdateAfterBindDescriptorsInAllPools", p->maxUpdateAfterBindDescriptorsInAllPools);
    MMVK_JSON_BOOL(j, "shaderUniformBufferArrayNonUniformIndexingNative", p->shaderUniformBufferArrayNonUniformIndexingNative);
    MMVK_JSON_BOOL(j, "shaderSampledImageArrayNonUniformIndexingNative", p->shaderSampledImageArrayNonUniformIndexingNative);
    MMVK_JSON_BOOL(j, "shaderStorageBufferArrayNonUniformIndexingNative", p->shaderStorageBufferArrayNonUniformIndexingNative);
    MMVK_JSON_BOOL(j, "shaderStorageImageArrayNonUniformIndexingNative", p->shaderStorageImageArrayNonUniformIndexingNative);
    MMVK_JSON_BOOL(j, "shaderInputAttachmentArrayNonUniformIndexingNative", p->shaderInputAttachmentArrayNonUniformIndexingNative);
    MMVK_JSON_BOOL(j, "robustBufferAccessUpdateAfterBind", p->robustBufferAccessUpdateAfterBind);
    MMVK_JSON_BOOL(j, "quadDivergentImplicitLod", p->quadDivergentImplicitLod);
    json_unsigned(j, "maxPerStageDescriptorUpdateAfterBindSamplers", p->maxPerStageDescriptorUpdateAfterBindSamplers);
    json_unsigned(j, "maxPerStageDescriptorUpdateAfterBindUniformBuffers", p->maxPerStageDescriptorUpdateAfterBindUniformBuffers);
    json_unsigned(j, "maxPerStageDescriptorUpdateAfterBindStorageBuffers", p->maxPerStageDescriptorUpdateAfterBindStorageBuffers);
    json_unsigned(j, "maxPerStageDescriptorUpdateAfterBindSampledImages", p->maxPerStageDescriptorUpdateAfterBindSampledImages);
    json_unsigned(j, "maxPerStageDescriptorUpdateAfterBindStorageImages", p->maxPerStageDescriptorUpdateAfterBindStorageImages);
    json_unsigned(j, "maxPerStageDescriptorUpdateAfterBindInputAttachments", p->maxPerStageDescriptorUpdateAfterBindInputAttachments);
    json_unsigned(j, "maxPerStageUpdateAfterBindResources", p->maxPerStageUpdateAfterBindResources);
    json_unsigned(j, "maxDescriptorSetUpdateAfterBindSamplers", p->maxDescriptorSetUpdateAfterBindSamplers);
    json_unsigned(j, "maxDescriptorSetUpdateAfterBindUniformBuffers", p->maxDescriptorSetUpdateAfterBindUniformBuffers);
    json_unsigned(j, "maxDescriptorSetUpdateAfterBindUniformBuffersDynamic", p->maxDescriptorSetUpdateAfterBindUniformBuffersDynamic);
    json_unsigned(j, "maxDescriptorSetUpdateAfterBindStorageBuffers", p->maxDescriptorSetUpdateAfterBindStorageBuffers);
    json_unsigned(j, "maxDescriptorSetUpdateAfterBindStorageBuffersDynamic", p->maxDescriptorSetUpdateAfterBindStorageBuffersDynamic);
    json_unsigned(j, "maxDescriptorSetUpdateAfterBindSampledImages", p->maxDescriptorSetUpdateAfterBindSampledImages);
    json_unsigned(j, "maxDescriptorSetUpdateAfterBindStorageImages", p->maxDescriptorSetUpdateAfterBindStorageImages);
    json_unsigned(j, "maxDescriptorSetUpdateAfterBindInputAttachments", p->maxDescriptorSetUpdateAfterBindInputAttachments);
    json_flags(j, "supportedDepthResolveModes", p->supportedDepthResolveModes, VkResolveModeFlags_names);
    json_flags(j, "supportedStencilResolveModes", p->supportedStencilResolveModes, VkResolveModeFlags_names);
    MMVK_JSON_BOOL(j, "independentResolveNone", p->independentResolveNone);
    MMVK_JSON_BOOL(j, "independentResolve", p->independentResolve);
    MMVK_JSON_BOOL(j, "filterMinmaxSingleComponentFormats", p->filterMinmaxSingleComponentFormats);
    MMVK_JSON_BOOL(j, "filterMinmaxImageComponentMapping", p->filterMinmaxImageComponentMapping);
    json_unsigned(j, "maxTimelineSemaphoreValueDifference", p->maxTimelineSemaphoreValueDifference);
    json_flags(j, "framebufferIntegerColorSampleCounts", p->framebufferIntegerColorSampleCounts, VkSampleCountFlags_names);
}

#endif
