/*
 * MaliValhallHw.h: ARM Mali-G57 MC2 (Valhall v9, Job-Manager) HW Register Map & Job Structures
 *
 * Register layout is the canonical ARM Mali Job-Manager map as published in the
 * Linux kbase driver (mali_kbase_gpu_regmap.h) and Mesa Panfrost. This is the
 * authoritative spec for Bifrost/Valhall-JM parts; MediaTek MT6789 integrates an
 * unmodified Mali-G57 MC2, so the same offsets apply. GPU MMIO base = 0x13000000.
 *
 * NOTE: these offsets only matter once the MFG power domain is actually up. On
 * MT6789 the GPU is powered by the AP-side gpufreqv2 path (GPUEB firmware is OFF),
 * i.e. PMIC bucks (VGPU/VSRAM_GPU) -> topckgen/MFG_TOP clocks -> MTCMOS domains.
 * Until that sequence succeeds the whole bank reads 0x00000000.
 */

#ifndef MALI_VALHALL_HW_H
#define MALI_VALHALL_HW_H

#include <stdint.h>

/* ---- GPU_CONTROL block (base 0x0000) ------------------------------------ */
#define MALI_REG_GPU_ID             0x0000u
#define MALI_REG_L2_FEATURES        0x0004u
#define MALI_REG_CORE_FEATURES      0x0008u
#define MALI_REG_TILER_FEATURES     0x000cu
#define MALI_REG_MEM_FEATURES       0x0010u
#define MALI_REG_MMU_FEATURES       0x0014u
#define MALI_REG_AS_PRESENT         0x0018u   /* number of MMU address spaces */
#define MALI_REG_JS_PRESENT         0x001cu   /* number of job slots */
#define MALI_REG_GPU_IRQ_RAWSTAT    0x0020u
#define MALI_REG_GPU_IRQ_CLEAR      0x0024u
#define MALI_REG_GPU_IRQ_MASK       0x0028u
#define MALI_REG_GPU_IRQ_STATUS     0x002cu
#define MALI_REG_GPU_COMMAND        0x0030u
#define MALI_REG_GPU_STATUS         0x0034u
#define MALI_REG_GPU_FAULTSTATUS    0x003cu
#define MALI_REG_GPU_FAULT_ADDR_LO  0x0040u
#define MALI_REG_GPU_FAULT_ADDR_HI  0x0044u
#define MALI_REG_PWR_KEY            0x0050u   /* unlock = 0x2968A819 */
#define MALI_REG_PWR_OVERRIDE0      0x0054u
#define MALI_REG_PWR_OVERRIDE1      0x0058u

/* Core-availability / power bitmaps (64-bit, LO/HI pairs) */
#define MALI_REG_SHADER_PRESENT_LO  0x0100u
#define MALI_REG_SHADER_PRESENT_HI  0x0104u
#define MALI_REG_TILER_PRESENT_LO   0x0110u
#define MALI_REG_L2_PRESENT_LO      0x0120u
#define MALI_REG_SHADER_READY_LO    0x0140u
#define MALI_REG_TILER_READY_LO     0x0150u
#define MALI_REG_L2_READY_LO        0x0160u
#define MALI_REG_SHADER_PWRON_LO    0x0180u
#define MALI_REG_SHADER_PWRON_HI    0x0184u
#define MALI_REG_TILER_PWRON_LO     0x0190u
#define MALI_REG_L2_PWRON_LO        0x01a0u
#define MALI_REG_SHADER_PWROFF_LO   0x01c0u
#define MALI_REG_TILER_PWROFF_LO    0x01d0u
#define MALI_REG_L2_PWROFF_LO       0x01e0u

/* GPU_COMMAND opcodes */
#define MALI_GPU_CMD_NOP            0x00u
#define MALI_GPU_CMD_SOFT_RESET     0x01u
#define MALI_GPU_CMD_HARD_RESET     0x02u
#define MALI_GPU_CMD_CLEAN_CACHES   0x07u
#define MALI_GPU_CMD_CLEAN_INV_CACHES 0x08u

/* GPU_IRQ bits */
#define MALI_GPU_IRQ_FAULT          (1u << 0)
#define MALI_GPU_IRQ_POWER_CHANGED  (1u << 9)
#define MALI_GPU_IRQ_POWER_CHANGED_ALL (1u << 10)
#define MALI_GPU_IRQ_RESET_COMPLETED (1u << 8)
#define MALI_GPU_IRQ_CLEAN_CACHES_COMPLETED (1u << 17)

/* ---- JOB_CONTROL block (base 0x1000) ------------------------------------ */
#define MALI_JOB_CONTROL_BASE       0x1000u
#define MALI_JOB_IRQ_RAWSTAT        0x1000u
#define MALI_JOB_IRQ_CLEAR          0x1004u
#define MALI_JOB_IRQ_MASK           0x1008u
#define MALI_JOB_IRQ_STATUS         0x100cu

/*
 * Job Slots. Slot 0 base = JOB_CONTROL_BASE + 0x800 = 0x1800 (absolute).
 * Each slot is 0x80 apart. (0: fragment, 1: vertex/tiler, 2: compute/other.)
 */
#define MALI_JS_OFFSET(slot)        (0x1800u + ((slot) * 0x80u))

/* Registers within a job slot (offset from the slot base) */
#define MALI_JS_HEAD_LO             0x0000u
#define MALI_JS_HEAD_HI             0x0004u
#define MALI_JS_TAIL_LO             0x0008u
#define MALI_JS_TAIL_HI             0x000cu
#define MALI_JS_AFFINITY_LO         0x0010u
#define MALI_JS_AFFINITY_HI         0x0014u
#define MALI_JS_CONFIG              0x0018u
#define MALI_JS_XAFFINITY           0x001cu
#define MALI_JS_COMMAND             0x0020u
#define MALI_JS_STATUS              0x0024u
#define MALI_JS_HEAD_NEXT_LO        0x0040u
#define MALI_JS_HEAD_NEXT_HI        0x0044u
#define MALI_JS_AFFINITY_NEXT_LO    0x0050u
#define MALI_JS_AFFINITY_NEXT_HI    0x0054u
#define MALI_JS_CONFIG_NEXT         0x0058u
#define MALI_JS_XAFFINITY_NEXT      0x005cu
#define MALI_JS_COMMAND_NEXT        0x0060u
#define MALI_JS_FLUSH_ID_NEXT       0x0070u

/* JS_COMMAND / JS_COMMAND_NEXT opcodes */
#define MALI_JS_COMMAND_NOP         0x00u
#define MALI_JS_COMMAND_START       0x01u
#define MALI_JS_COMMAND_SOFT_STOP   0x02u
#define MALI_JS_COMMAND_HARD_STOP   0x03u

/* ---- MMU block (base 0x2000) -------------------------------------------- */
#define MALI_MMU_IRQ_RAWSTAT        0x2000u
#define MALI_MMU_IRQ_CLEAR          0x2004u
#define MALI_MMU_IRQ_MASK           0x2008u
#define MALI_MMU_IRQ_STATUS         0x200cu

/* Address spaces. AS0 base = 0x2400, each AS is 0x40 apart. */
#define MALI_AS_OFFSET(as)          (0x2400u + ((as) * 0x40u))
#define MALI_AS_TRANSTAB_LO         0x0000u
#define MALI_AS_TRANSTAB_HI         0x0004u
#define MALI_AS_MEMATTR_LO          0x0008u
#define MALI_AS_MEMATTR_HI          0x000cu
#define MALI_AS_LOCKADDR_LO         0x0010u
#define MALI_AS_LOCKADDR_HI         0x0014u
#define MALI_AS_COMMAND             0x0018u
#define MALI_AS_FAULTSTATUS         0x001cu
#define MALI_AS_FAULTADDRESS_LO     0x0020u
#define MALI_AS_FAULTADDRESS_HI     0x0024u
#define MALI_AS_STATUS              0x0028u
#define MALI_AS_TRANSCFG_LO         0x0030u
#define MALI_AS_TRANSCFG_HI         0x0034u

/* AS_COMMAND opcodes */
#define MALI_AS_COMMAND_NOP         0x00u
#define MALI_AS_COMMAND_UPDATE      0x01u
#define MALI_AS_COMMAND_LOCK        0x02u
#define MALI_AS_COMMAND_UNLOCK      0x03u
#define MALI_AS_COMMAND_FLUSH_PT    0x04u
#define MALI_AS_COMMAND_FLUSH_MEM   0x05u

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
