/* SPDX-License-Identifier: MIT */
#ifndef MALI_VULKAN_PROFILE_H
#define MALI_VULKAN_PROFILE_H
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>
/* All inputs are actual queried properties and the exact enabled feature
 * chains passed to vkCreateDevice. The returned JSON is caller-owned. */
char *mmvk_profile_create(VkPhysicalDevice physical,
    const VkPhysicalDeviceFeatures *enabled,
    const VkPhysicalDeviceVulkan11Features *enabled11,
    const VkPhysicalDeviceVulkan12Features *enabled12,
    const VkPhysicalDeviceProperties *properties,
    const VkPhysicalDeviceVulkan11Properties *properties11,
    const VkPhysicalDeviceVulkan12Properties *properties12,
    PFN_vkGetPhysicalDeviceFormatProperties get_format,
    PFN_vkGetPhysicalDeviceImageFormatProperties2 get_image_format);
/* Disabled unless MALIMETAL_NATIVE_PROFILE_PATH names an absolute output path. */
void mmvk_profile_export(const char *json);
#endif
