/* SPDX-License-Identifier: MIT */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#import "MaliMetalTypes.h"
#include "Vulkan/MaliVulkanBackend.h"
@class MaliMetalDevice;

BOOL MaliMetalMPSCommonBackendEnabled(void);
/* Called after native feature-query initialization, before Metal publication.
 * A successful registration uses the complete native MPSDevice_Common object. */
MMVKResult MaliMetalRegisterMPSCommonBackend(MaliMetalDevice *device, MMVKError *error);
#ifdef __cplusplus
}
#endif
