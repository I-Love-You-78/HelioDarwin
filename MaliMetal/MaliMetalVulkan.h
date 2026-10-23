/*
 * Fixed-layout CPU rectangle prototype. This is not a Vulkan implementation
 * or an implementation of arbitrary Metal vertex and fragment shaders.
 */

#ifndef MALI_METAL_VULKAN_H
#define MALI_METAL_VULKAN_H

#include "MaliMetalTypes.h"

#define MALI_MAX_DRAW_COMMANDS  256
#define MALI_MAX_VERTEX_BUFFERS 8
#define MALI_MAX_TEXTURES       8

typedef struct {
    NSUInteger primitiveType;
    NSUInteger vertexStart;
    NSUInteger vertexCount;
    MTLViewport viewport;
    MTLScissorRect scissorRect;
    uint8_t hasScissorRect;
    void *vertexBufferPtr[MALI_MAX_VERTEX_BUFFERS];
    NSUInteger vertexBufferOffset[MALI_MAX_VERTEX_BUFFERS];
    NSUInteger vertexBufferLength[MALI_MAX_VERTEX_BUFFERS];
    void *fragmentTexturePtr[MALI_MAX_TEXTURES];
    uint32_t textureWidth[MALI_MAX_TEXTURES];
    uint32_t textureHeight[MALI_MAX_TEXTURES];
    uint32_t textureBytesPerRow[MALI_MAX_TEXTURES];
} MaliMetalDrawCommand;

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Experimental rectangle rasterizer for its own fixed vertex layout.
 * CoreAnimation's general shader and vertex layouts are not implemented.
 */
void MaliMetalRasterizeCommand(const MaliMetalDrawCommand *cmd, void *dstPixels, uint32_t dstWidth, uint32_t dstHeight, uint32_t dstStride);

#ifdef __cplusplus
}
#endif

#endif /* MALI_METAL_VULKAN_H */
