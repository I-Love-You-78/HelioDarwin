/*
 * MaliMetalVulkan.h: Command translation and pipeline structures for MaliMetal
 * Bridges Metal render commands into Mali Valhall job streams or fallback rasterization.
 */

#ifndef MALI_METAL_VULKAN_H
#define MALI_METAL_VULKAN_H

#include "MaliMetalTypes.h"
#include "MaliValhallHw.h"

#define MALI_MAX_DRAW_COMMANDS  256
#define MALI_MAX_VERTEX_BUFFERS 8
#define MALI_MAX_TEXTURES       8

typedef struct {
    NSUInteger primitiveType;
    NSUInteger vertexStart;
    NSUInteger vertexCount;
    MTLViewport viewport;
    MTLScissorRect scissorRect;
    void *vertexBufferPtr[MALI_MAX_VERTEX_BUFFERS];
    NSUInteger vertexBufferOffset[MALI_MAX_VERTEX_BUFFERS];
    void *fragmentTexturePtr[MALI_MAX_TEXTURES];
    uint32_t textureWidth[MALI_MAX_TEXTURES];
    uint32_t textureHeight[MALI_MAX_TEXTURES];
} MaliMetalDrawCommand;

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Software Rasterizer / GPU Fallback for 2D UI Compositing (CoreAnimation)
 * Renders textured quads / solid rectangles into target 1080x2400 BGRA surface
 */
void MaliMetalRasterizeCommand(const MaliMetalDrawCommand *cmd, void *dstPixels, uint32_t dstWidth, uint32_t dstHeight, uint32_t dstStride);

#ifdef __cplusplus
}
#endif

#endif /* MALI_METAL_VULKAN_H */
