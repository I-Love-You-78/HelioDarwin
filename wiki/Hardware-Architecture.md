# Hardware Architecture: MediaTek MT6789 (Helio G99)

## 1. Overview in Plain English

The **MediaTek Helio G99 (MT6789)** is an octa-core 64-bit ARM SoC on a TSMC 6 nm
process. Three things make bringing up a foreign OS on it hard:
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

## 4. MTCMOS Power Sequencing

The GPU sits in the MFG power domain. On this MT6789 the AP drives power directly
(`/proc/gpufreqv2` reports `GPUEB Support: Off`). The exact sequence, register offsets
and bit layout were reverse-engineered from the stock `mtk_gpufreq_mt6789.ko` and
`clk-mt6789*.ko`; see **[MT6789 GPU Power Sequence](MT6789-GPU-Power-Sequence.md)** for
the authoritative, verified map. Summary:

- The per-domain `*_PWR_CON` registers are in the **SPM** bank at `0x10006000`
  (`MFG0=+0x308`, `MFG1=+0x30c`, `MFG2=+0x310`, `MFG3=+0x314`), **not** in MFG_TOP; status
  is read from SPM `PWR_STATUS 0x16c` / `PWR_STATUS_2ND 0x170`.
- Order: VGPU → VSRAM_GPU rails → MFG0 → MFG1 (+ infracfg bus-protect release) →
  reference clock → MFG2 → MFG3 → bg3d subsys CG + MFG_TOP external CG.
- PMIC rails from the device OPP table: VGPU 0.575–0.725 V, VSRAM_GPU 0.75 V.

> An earlier bring-up wrote the power bits into MFG_TOP (`0x13fbf000 + 0x308…`) and used
> ~0.8 V; that was wrong and is corrected above. The Mali bank reads `0x0` until the MFG
> domain is actually powered — the vendor product ID `0x9093` comes from the device's own
> Mali driver sysfs, and XNU has not yet read it live.
