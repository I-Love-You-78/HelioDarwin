/*
 * MaliMetalVulkan.c: Render pipeline execution and Valhall job construction
 */

#include "MaliMetalVulkan.h"
#include <string.h>
#include <stdlib.h>

typedef struct {
    float x, y, z, w;
    float u, v;
    uint32_t color;
} MaliVertex;

static inline uint32_t blend_bgra(uint32_t src, uint32_t dst)
{
    uint32_t sa = (src >> 24) & 0xff;
    if (sa == 0) return dst;
    if (sa == 255) return src;

    uint32_t da = (dst >> 24) & 0xff;
    uint32_t sr = (src >> 16) & 0xff;
    uint32_t sg = (src >> 8) & 0xff;
    uint32_t sb = src & 0xff;

    uint32_t dr = (dst >> 16) & 0xff;
    uint32_t dg = (dst >> 8) & 0xff;
    uint32_t db = dst & 0xff;

    uint32_t r = (sr * sa + dr * (255 - sa)) / 255;
    uint32_t g = (sg * sa + dg * (255 - sa)) / 255;
    uint32_t b = (sb * sa + db * (255 - sa)) / 255;
    uint32_t a = sa + (da * (255 - sa)) / 255;

    return (a << 24) | (r << 16) | (g << 8) | b;
}

void MaliMetalRasterizeCommand(const MaliMetalDrawCommand *cmd, void *dstPixels, uint32_t dstWidth, uint32_t dstHeight, uint32_t dstStride)
{
    if (!cmd || !dstPixels || dstWidth == 0 || dstHeight == 0) return;

    uint32_t *dst = (uint32_t *)dstPixels;
    uint32_t strideWords = dstStride ? (dstStride / 4) : dstWidth;

    /* If we have vertex buffer data, process 2D screen-space quads (standard CoreAnimation tiles) */
    if (cmd->vertexBufferPtr[0] && cmd->vertexCount >= 3) {
        const MaliVertex *v = (const MaliVertex *)((const uint8_t *)cmd->vertexBufferPtr[0] + cmd->vertexBufferOffset[0]);
        const uint32_t *srcTex = (const uint32_t *)cmd->fragmentTexturePtr[0];
        uint32_t texW = cmd->textureWidth[0];
        uint32_t texH = cmd->textureHeight[0];

        /* Determine bounding box */
        float minX = (float)dstWidth, minY = (float)dstHeight, maxX = 0.0f, maxY = 0.0f;
        for (NSUInteger i = 0; i < cmd->vertexCount; i++) {
            float vx = v[i].x;
            float vy = v[i].y;
            if (vx < minX) minX = vx;
            if (vx > maxX) maxX = vx;
            if (vy < minY) minY = vy;
            if (vy > maxY) maxY = vy;
        }

        /* Scissor clipping */
        int x0 = (int)minX; if (x0 < 0) x0 = 0;
        int y0 = (int)minY; if (y0 < 0) y0 = 0;
        int x1 = (int)(maxX + 0.5f); if (x1 > (int)dstWidth) x1 = (int)dstWidth;
        int y1 = (int)(maxY + 0.5f); if (y1 > (int)dstHeight) y1 = (int)dstHeight;

        if (cmd->scissorRect.width > 0 && cmd->scissorRect.height > 0) {
            int sx0 = (int)cmd->scissorRect.x;
            int sy0 = (int)cmd->scissorRect.y;
            int sx1 = sx0 + (int)cmd->scissorRect.width;
            int sy1 = sy0 + (int)cmd->scissorRect.height;
            if (x0 < sx0) x0 = sx0;
            if (y0 < sy0) y0 = sy0;
            if (x1 > sx1) x1 = sx1;
            if (y1 > sy1) y1 = sy1;
        }

        if (x0 >= x1 || y0 >= y1) return;

        /* Blit / composite texture or solid fill into destination rectangle */
        float spanX = maxX - minX > 0.001f ? (maxX - minX) : 1.0f;
        float spanY = maxY - minY > 0.001f ? (maxY - minY) : 1.0f;

        for (int y = y0; y < y1; y++) {
            uint32_t *row = dst + (size_t)y * strideWords;
            float normY = ((float)y - minY) / spanY;
            int ty = (int)(normY * (float)texH);
            if (ty < 0) ty = 0;
            if (ty >= (int)texH) ty = (int)texH - 1;

            for (int x = x0; x < x1; x++) {
                float normX = ((float)x - minX) / spanX;
                int tx = (int)(normX * (float)texW);
                if (tx < 0) tx = 0;
                if (tx >= (int)texW) tx = (int)texW - 1;

                uint32_t color = 0xffffffff;
                if (srcTex && texW > 0 && texH > 0) {
                    color = srcTex[(size_t)ty * texW + tx];
                } else if (cmd->vertexCount > 0) {
                    color = v[0].color ? v[0].color : 0xffffffff;
                }

                row[x] = blend_bgra(color, row[x]);
            }
        }
    }
}
