/* SPDX-License-Identifier: MIT */
#ifndef MALI_METAL_RENDER_VULKAN_H
#define MALI_METAL_RENDER_VULKAN_H
#include "MaliMetalRender.h"
#include "MaliMetalShaderVulkan.h"
#include "../Vulkan/MaliVulkanGraphics.h"

typedef struct MMShaderVulkanRenderCache MMShaderVulkanRenderCache;
typedef struct MMShaderVulkanRenderExecution MMShaderVulkanRenderExecution;
MMVKResult MMShaderVulkanRenderCacheCreate(MMVKDevice *,MMShaderVulkanRenderCache **,MMVKError *);
void MMShaderVulkanRenderCacheDestroy(MMShaderVulkanRenderCache *);
MMVKResult MMShaderVulkanPrepareRender(MMShaderVulkanRenderCache *,const MMShaderRenderOutput *,
    const MMShaderVulkanBindings *,const MMShaderRenderBindings *,const MMShaderRenderDrawRequest *,
    const MMVKGraphicsState *,const MMVKPassLayout *,MMShaderVulkanRenderExecution **,MMVKError *);
MMVKResult MMShaderVulkanRecordRender(MMVKCommands *,MMShaderVulkanRenderExecution *,MMVKBuffer *indices,MMVKError *);
void MMShaderVulkanRenderExecutionDestroy(MMShaderVulkanRenderExecution *);
#endif
