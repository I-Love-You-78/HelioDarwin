/*
 * Experimental CPU rectangle rasterizer; no Vulkan calls or GPU job submission.
 */

#include "MaliMetalVulkan.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <limits.h>

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
    if (!cmd || !dstPixels || dstWidth == 0 || dstHeight == 0 ||
        dstWidth > INT_MAX || dstHeight > INT_MAX) return;

    uint32_t *dst = (uint32_t *)dstPixels;
    uint32_t strideWords = dstStride ? (dstStride / 4) : dstWidth;
    if (strideWords < dstWidth || (dstStride & 3)) return;

    /* This prototype consumes MaliVertex, not arbitrary Metal shader inputs. */
    if (cmd->vertexBufferPtr[0] && cmd->vertexCount >= 3) {
        NSUInteger offset = cmd->vertexBufferOffset[0];
        NSUInteger length = cmd->vertexBufferLength[0];
        if (offset > length) return;
        NSUInteger available = (length - offset) / sizeof(MaliVertex);
        if (cmd->vertexStart > available || cmd->vertexCount > available - cmd->vertexStart) return;
        const MaliVertex *v = (const MaliVertex *)((const uint8_t *)cmd->vertexBufferPtr[0] + offset) + cmd->vertexStart;
        const uint32_t *srcTex = (const uint32_t *)cmd->fragmentTexturePtr[0];
        uint32_t texW = cmd->textureWidth[0];
        uint32_t texH = cmd->textureHeight[0];
        uint32_t texStride = cmd->textureBytesPerRow[0] / 4;
        if (srcTex && (texW == 0 || texH == 0 || texW > INT_MAX || texH > INT_MAX ||
            (cmd->textureBytesPerRow[0] & 3) || texStride < texW)) return;

        /* Determine bounding box */
        float minX = (float)dstWidth, minY = (float)dstHeight, maxX = 0.0f, maxY = 0.0f;
        for (NSUInteger i = 0; i < cmd->vertexCount; i++) {
            float vx = v[i].x;
            float vy = v[i].y;
            if (!isfinite(vx) || !isfinite(vy)) return;
            if (vx < minX) minX = vx;
            if (vx > maxX) maxX = vx;
            if (vy < minY) minY = vy;
            if (vy > maxY) maxY = vy;
        }

        /* Scissor clipping */
        if (maxX <= 0 || maxY <= 0 || minX >= dstWidth || minY >= dstHeight) return;
        int x0 = minX > 0 ? (int)minX : 0;
        int y0 = minY > 0 ? (int)minY : 0;
        int x1 = maxX < dstWidth ? (int)(maxX + 0.5f) : (int)dstWidth;
        int y1 = maxY < dstHeight ? (int)(maxY + 0.5f) : (int)dstHeight;

        if (cmd->hasScissorRect) {
            if (cmd->scissorRect.x >= dstWidth || cmd->scissorRect.y >= dstHeight ||
                cmd->scissorRect.width == 0 || cmd->scissorRect.height == 0) return;
            int sx0 = (int)cmd->scissorRect.x;
            int sy0 = (int)cmd->scissorRect.y;
            NSUInteger sw = cmd->scissorRect.width;
            NSUInteger sh = cmd->scissorRect.height;
            if (sw > dstWidth - cmd->scissorRect.x) sw = dstWidth - cmd->scissorRect.x;
            if (sh > dstHeight - cmd->scissorRect.y) sh = dstHeight - cmd->scissorRect.y;
            int sx1 = sx0 + (int)sw;
            int sy1 = sy0 + (int)sh;
            if (x0 < sx0) x0 = sx0;
            if (y0 < sy0) y0 = sy0;
            if (x1 > sx1) x1 = sx1;
            if (y1 > sy1) y1 = sy1;
        }

        if (x0 >= x1 || y0 >= y1) return;

        /* Blit / composite texture or solid fill into destination rectangle */
        double spanX = (double)maxX - minX;
        double spanY = (double)maxY - minY;
        if (spanX <= 0 || spanY <= 0) return;

        for (int y = y0; y < y1; y++) {
            uint32_t *row = dst + (size_t)y * strideWords;
            double normY = ((double)y - minY) / spanY;
            int ty = (int)(normY * texH);
            if (ty < 0) ty = 0;
            if (ty >= (int)texH) ty = (int)texH - 1;

            for (int x = x0; x < x1; x++) {
                double normX = ((double)x - minX) / spanX;
                int tx = (int)(normX * texW);
                if (tx < 0) tx = 0;
                if (tx >= (int)texW) tx = (int)texW - 1;

                uint32_t color = 0xffffffff;
                if (srcTex && texW > 0 && texH > 0) {
                    color = srcTex[(size_t)ty * texStride + tx];
                } else if (cmd->vertexCount > 0) {
                    color = v[0].color;
                }

                row[x] = blend_bgra(color, row[x]);
            }
        }
    }
}
