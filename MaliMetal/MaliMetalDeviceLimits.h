/* iOS 16.7 Metal's named _limits ivar. Runtime encoding is checked before use. */
#pragma once
#include <stddef.h>
#include <stdint.h>

#define MM_METAL_LIMIT_FIELDS(X) \
    X(uint32_t, maxFramebufferStorageBits, "I") \
    X(uint32_t, linearTextureArrayAlignmentBytes, "I") \
    X(uint32_t, linearTextureArrayAlignmentSlice, "I") \
    X(uint32_t, maxTileBuffers, "I") \
    X(uint32_t, maxTileTextures, "I") \
    X(uint32_t, maxTileSamplers, "I") \
    X(uint32_t, maxTileInlineDataSize, "I") \
    X(uint32_t, minTilePixels, "I") \
    X(uint32_t, maxColorAttachments, "I") \
    X(uint32_t, maxVertexAttributes, "I") \
    X(uint32_t, maxVertexBuffers, "I") \
    X(uint32_t, maxVertexTextures, "I") \
    X(uint32_t, maxVertexSamplers, "I") \
    X(uint32_t, maxVertexInlineDataSize, "I") \
    X(uint32_t, maxInterpolants, "I") \
    X(uint32_t, maxFragmentBuffers, "I") \
    X(uint32_t, maxFragmentTextures, "I") \
    X(uint32_t, maxFragmentSamplers, "I") \
    X(uint32_t, maxFragmentInlineDataSize, "I") \
    X(uint32_t, maxComputeBuffers, "I") \
    X(uint32_t, maxComputeTextures, "I") \
    X(uint32_t, maxComputeSamplers, "I") \
    X(uint32_t, maxComputeInlineDataSize, "I") \
    X(uint32_t, maxComputeLocalMemorySizes, "I") \
    X(uint32_t, maxTotalComputeThreadsPerThreadgroup, "I") \
    X(uint32_t, maxComputeThreadgroupMemory, "I") \
    X(float, maxLineWidth, "f") \
    X(float, maxPointSize, "f") \
    X(uint32_t, maxVisibilityQueryOffset, "I") \
    X(uint32_t, padmaxBufferLength, "I") \
    X(uint32_t, minConstantBufferAlignmentBytes, "I") \
    X(uint32_t, minBufferNoCopyAlignmentBytes, "I") \
    X(uint32_t, maxTextureWidth1D, "I") \
    X(uint32_t, maxTextureWidth2D, "I") \
    X(uint32_t, maxTextureHeight2D, "I") \
    X(uint32_t, maxTextureWidth3D, "I") \
    X(uint32_t, maxTextureHeight3D, "I") \
    X(uint32_t, maxTextureDepth3D, "I") \
    X(uint32_t, maxTextureDimensionCube, "I") \
    X(uint32_t, maxTextureLayers, "I") \
    X(uint32_t, linearTextureAlignmentBytes, "I") \
    X(uint32_t, iosurfaceTextureAlignmentBytes, "I") \
    X(uint32_t, iosurfaceReadOnlyTextureAlignmentBytes, "I") \
    X(uint32_t, deviceLinearTextureAlignmentBytes, "I") \
    X(uint32_t, deviceLinearReadOnlyTextureAlignmentBytes, "I") \
    X(uint32_t, maxFunctionConstantIndices, "I") \
    X(uint32_t, maxComputeThreadgroupMemoryAlignmentBytes, "I") \
    X(uint32_t, maxInterpolatedComponents, "I") \
    X(uint32_t, maxTessellationFactor, "I") \
    X(uint32_t, maxIndirectBuffers, "I") \
    X(uint32_t, maxIndirectTextures, "I") \
    X(uint32_t, maxIndirectSamplers, "I") \
    X(uint32_t, maxIndirectSamplersPerDevice, "I") \
    X(uint32_t, maxFenceInstances, "I") \
    X(uint32_t, maxViewportCount, "I") \
    X(uint32_t, maxCustomSamplePositions, "I") \
    X(uint32_t, maxVertexAmplificationFactor, "I") \
    X(uint32_t, maxVertexAmplificationCount, "I") \
    X(uint32_t, maxTextureBufferWidth, "I") \
    X(uint32_t, maxComputeAttributes, "I") \
    X(uint32_t, maxIOCommandsInFlight, "I") \
    X(uint32_t, maxPredicatedNestingDepth, "I") \
    X(uint32_t, maxConstantBufferArguments, "I") \
    X(uint64_t, maxBufferLength, "Q")

#define MM_LIMIT_DECLARE(type, name, encoding) type name;
typedef struct { MM_METAL_LIMIT_FIELDS(MM_LIMIT_DECLARE) } MMMetalDeviceLimits;
#undef MM_LIMIT_DECLARE
_Static_assert(sizeof(MMMetalDeviceLimits) == 264, "Metal limits layout");
_Static_assert(offsetof(MMMetalDeviceLimits, maxBufferLength) == 256, "Metal buffer limit offset");

#define MM_LIMIT_ENCODE(type, name, encoding) "\"" #name "\"" encoding
static const char MMMetalDeviceLimitsEncoding[] = "{?=" MM_METAL_LIMIT_FIELDS(MM_LIMIT_ENCODE) "}";
#undef MM_LIMIT_ENCODE
#undef MM_METAL_LIMIT_FIELDS
