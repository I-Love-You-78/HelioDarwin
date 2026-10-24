#include "MaliMetalVulkan.h"
#include "Vulkan/MaliVulkanResources.h"
MMVKFormat MaliMetalPixelFormat(MTLPixelFormat format) {
    return mmvk_format_from_metal((uint32_t)format);
}
/* MTLVertexFormat scalar/vector types to VkFormat. Packed/normalized formats
 * outside this table must be rejected instead of interpreting their bytes. */
int MaliMetalVertexFormat(NSUInteger format, uint32_t *out) {
    uint32_t value;
    switch (format) {
    case 25:
        value = 76;
        break; /* half2 -> R16G16_SFLOAT */
    case 26:
        value = 90;
        break;
    case 27:
        value = 97;
        break;
    case 28:
        value = 100;
        break; /* float */
    case 29:
        value = 103;
        break;
    case 30:
        value = 106;
        break;
    case 31:
        value = 109;
        break;
    case 32:
        value = 99;
        break; /* int */
    case 33:
        value = 102;
        break;
    case 34:
        value = 105;
        break;
    case 35:
        value = 108;
        break;
    case 36:
        value = 98;
        break; /* uint */
    case 37:
        value = 101;
        break;
    case 38:
        value = 104;
        break;
    case 39:
        value = 107;
        break;
    case 7:
        value = 16;
        break; /* uchar2 normalized */
    case 8:
        value = 23;
        break;
    case 9:
        value = 37;
        break;
    default:
        return 0;
    }
    if (out)
        *out = value;
    return 1;
}
int MaliMetalBlendFactor(NSUInteger factor, uint32_t *out) {
    /* Both APIs assign 0..9 to zero/one/src/dst color+alpha; saturated source
     * alpha differs. Constant color/alpha indices also differ. */
    uint32_t value;
    if (factor <= 9) {
        static const uint32_t factors[] = {0, 1, 2, 3, 6, 7, 4, 5, 8, 9};
        value = factors[factor];
    } else if (factor == 10)
        value = 14;
    else if (factor >= 11 && factor <= 14)
        value = (uint32_t)factor - 1;
    else
        return 0;
    if (out)
        *out = value;
    return 1;
}
int MaliMetalBlendOperation(NSUInteger operation, uint32_t *out) {
    if (operation > 4)
        return 0;
    if (out)
        *out = (uint32_t)operation;
    return 1;
}
