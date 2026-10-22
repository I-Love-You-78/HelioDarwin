/*
 * MaliValhallHw.h: ARM Mali-G57 MC2 (Valhall v9) Hardware Register Map & Job Structures
 * Based on Mesa Panfrost / PanVK driver architecture for MediaTek MT6789.
 */

#ifndef MALI_VALHALL_HW_H
#define MALI_VALHALL_HW_H

#include <stdint.h>

/*
 * Mali GPU Register Offsets (Base: 0x13000000)
 */
#define MALI_REG_GPU_ID             0x0000u
#define MALI_REG_L2_FEATURES        0x0004u
#define MALI_REG_SUSPEND_SIZE       0x0008u
#define MALI_REG_TILER_FEATURES     0x000cu
#define MALI_REG_MEM_FEATURES       0x0010u
#define MALI_REG_MMU_FEATURES       0x0014u
#define MALI_REG_AS_PRESENT         0x0020u
#define MALI_REG_JS_PRESENT         0x0024u
#define MALI_REG_GPU_IRQ_RAWSTAT    0x0028u
#define MALI_REG_GPU_IRQ_CLEAR      0x002cu
#define MALI_REG_GPU_IRQ_MASK       0x0030u
#define MALI_REG_GPU_STATUS         0x0034u
#define MALI_REG_GPU_FAULT_ADDR_LO  0x0040u
#define MALI_REG_GPU_FAULT_ADDR_HI  0x0044u

/*
 * Job Slot Registers (3 Slots: 0: Fragment, 1: Non-fragment/Vertex, 2: System)
 * Slot n Base = 0x0800 + (n * 0x80)
 */
#define MALI_JS_OFFSET(slot)        (0x0800u + ((slot) * 0x80u))

#define MALI_JS_HEAD_NEXT_LO        0x0000u
#define MALI_JS_HEAD_NEXT_HI        0x0004u
#define MALI_JS_AFFINITY_NEXT_LO    0x0008u
#define MALI_JS_AFFINITY_NEXT_HI    0x000cu
#define MALI_JS_CONFIG_NEXT         0x0010u
#define MALI_JS_COMMAND_NEXT        0x0014u
#define MALI_JS_STATUS              0x0018u
#define MALI_JS_HEAD_LO             0x0020u
#define MALI_JS_HEAD_HI             0x0024u
#define MALI_JS_TAIL_LO             0x0028u
#define MALI_JS_TAIL_HI             0x002cu

/*
 * JS_COMMAND_NEXT Commands
 */
#define MALI_JS_COMMAND_NOP         0x00u
#define MALI_JS_COMMAND_START       0x01u
#define MALI_JS_COMMAND_SOFT_STOP   0x02u
#define MALI_JS_COMMAND_HARD_STOP   0x03u

/*
 * Valhall Job Descriptor Types
 */
#define MALI_JOB_TYPE_NULL          0x01u
#define MALI_JOB_TYPE_WRITEVALUE    0x02u
#define MALI_JOB_TYPE_CACHE_FLUSH   0x03u
#define MALI_JOB_TYPE_COMPUTE       0x04u
#define MALI_JOB_TYPE_VERTEX        0x05u
#define MALI_JOB_TYPE_TILER         0x06u
#define MALI_JOB_TYPE_FRAGMENT      0x07u

/*
 * 64-byte Hardware Job Descriptor Header (Panfrost / Valhall)
 */
struct __attribute__((packed)) mali_job_descriptor_header {
    uint8_t   job_type;
    uint8_t   index;
    uint16_t  flags;
    uint32_t  fault_pointer;
    uint64_t  next_job_address;
    uint64_t  dependencies[2];
};

/*
 * Minimal Fragment Job Payload
 */
struct __attribute__((packed)) mali_fragment_job_payload {
    uint64_t  framebuffer;
    uint64_t  polygon_list;
    uint32_t  min_tile_x;
    uint32_t  min_tile_y;
    uint32_t  max_tile_x;
    uint32_t  max_tile_y;
};

#endif /* MALI_VALHALL_HW_H */
