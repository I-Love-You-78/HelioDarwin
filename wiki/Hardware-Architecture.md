# Hardware Architecture: MediaTek MT6789 (Helio G99)

## 1. Overview in Plain English

The **MediaTek Helio G99 (MT6789)** is an octa-core 64-bit ARM SoC fabricated on TSMC's 6nm FinFET process. For years, running foreign operating systems (especially Apple's Darwin/XNU kernel) on MediaTek chips was considered impossible because of:
1. **Aggressive MTCMOS Power Gating**: Unlike Qualcomm SoCs where peripheral blocks often remain powered by default firmware, MediaTek powers down almost all subsystems (GPU, Camera, Video, Display engines) at the silicon level. Accessing an unpowered subsystem register triggers an immediate, unrecoverable AXI bus hang.
2. **Proprietary SCPSYS Control**: Power domains are controlled by a dedicated System Power Manager (SPM) using multi-stage handshake sequences with microsecond delays.
3. **16 KB Memory Granule Requirements**: Darwin/iOS requires a strict 16 KB virtual memory page size. Fortunately, the MT6789 uses modern Cortex-A76 and Cortex-A55 cores that support `TGran16` natively in hardware.

---

## 2. Memory Map & Carveouts

The physical DRAM space begins at `0x40000000` (1 GiB offset) and spans up to 8 GiB (`0x2_40000000`):

| Region | Physical Address Range | Size | Description |
| :--- | :--- | :--- | :--- |
| **DRAM Base** | `0x40000000`–`0x44000000` | 64 MiB | ATF, TrustZone, and Secure OS carveouts |
| **Framebuffer** | `0x5c000000`–`0x5f000000` | 48 MiB | Continuous splash / Display DMA buffer |
| **XNU Kernel** | `0x60000000`–`0x80000000` | 512 MiB | Kernel text, data, and initial physical pools |
| **System RAM** | `0x80000000`–`0x2_40000000` | ~7 GiB | Free memory pool for iOS userland and Mach VM |

---

## 3. Peripheral MMIO Register Map

| Subsystem | Physical Address | Size | Description |
| :--- | :--- | :--- | :--- |
| **GIC-600 Dist** | `0x0c000000` | 64 KiB | ARM GICv3 Distributor |
| **GIC-600 Rdist**| `0x0c080000` | 1 MiB | ARM GICv3 Redistributors (stride `0x20000` per core) |
| **TOPCKGEN** | `0x10000000` | 4 KiB | Top Clock Generator & PLL control |
| **INFRACFG_AO** | `0x10001000` | 4 KiB | Always-On Infrastructure Configuration |
| **SCPSYS (SPM)** | `0x10006000` | 4 KiB | System Control Processor / MTCMOS power controller |
| **UART0** | `0x11002000` | 4 KiB | High-speed debug serial console (921600 baud) |
| **UFS Host** | `0x11270000` | 64 KiB | MediaTek UFS 2.2 Host Controller |
| **Mali-G57 GPU** | `0x13000000` | 4 MiB | ARM Mali Valhall core registers |
| **MFG_TOP_CFG** | `0x13fbf000` | 4 KiB | GPU clock gating and subsystem config |
| **DISP_MUTEX0** | `0x14001000` | 4 KiB | Display hardware sync mutex controller |
| **DSI0** | `0x14013000` | 4 KiB | MIPI Display Serial Interface controller |

---

## 4. MTCMOS Power Sequencing (SCPSYS)

In the MT6789, the GPU sits within the **MFG (Manufacturing / Graphics)** power domain. To power on the Mali-G57 without triggering a bus deadlock, the kernel must execute the following sequence:

1. **Enable PMIC Power Rails**:
   * `vgpu`: PMIC MT6366 Buck regulator (target ~0.75V).
   * `vsram_gpu`: PMIC SRAM supply (target ~0.75V).
2. **Sequence MTCMOS in SCPSYS (`0x10006000`)**:
   ```c
   /* Step 1: Assert PWR_ON */
   scpsys[SPM_MFG_PWR_CON] |= (1u << 0);
   IODelay(20);

   /* Step 2: Assert PWR_ON_2ND */
   scpsys[SPM_MFG_PWR_CON] |= (1u << 1);
   IODelay(20);

   /* Step 3: Clear SRAM_PDN (release memory power-down) */
   scpsys[SPM_MFG_PWR_CON] &= ~(0xful << 8);
   IODelay(20);

   /* Step 4: Clear PWR_ISO (release bus isolation) */
   scpsys[SPM_MFG_PWR_CON] &= ~(1u << 2);
   IODelay(20);

   /* Step 5: Assert PWR_RST_B (release hardware reset) */
   scpsys[SPM_MFG_PWR_CON] |= (1u << 3);
   IODelay(20);
   ```
3. **Un-gate Clocks in `MFG_TOP_CONFIG` (`0x13fbf000`)**:
   * Enable graphics clock distribution.
4. **Access Mali Registers at `0x13000000`**:
   * `GPU_ID` at `0x0000` reports `0x9093` (Mali-G57 Valhall r0p1, 2 cores).
