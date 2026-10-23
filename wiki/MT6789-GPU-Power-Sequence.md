# MT6789 (Helio G99) Mali-G57 — Power & Clock Bring-up (Reverse-Engineered)

> **Source of truth.** Every register/offset below was reverse-engineered from the
> device's own stock binaries pulled off an Infinix X676C / sibling X676B
> (`mtk_gpufreq_mt6789.ko`, `clk-mt6789.ko`, `clk-mt6789-mfgcfg.ko`, `clk-common.ko`)
> in IDA, cross-checked against live `/proc/gpufreqv2` and `/proc/iomem`. It is **not**
> guessed from other SoCs. Where something is not yet confirmed it is marked TODO.

## 0. Why the GPU read all-zero

On MT6789 the Mali-G57 register bank at `0x13000000` reads `0x00000000` for everything
(incl. `GPU_ID` at offset `0x0`) until the **MFG power domain is actually powered**.
The chip exposes GPU DVFS through the **`gpufreqv2`** driver; on this unit
`/proc/gpufreqv2/gpufreq_status` reports **`GPUEB Support: Off`**, i.e. the GPU-embedded
controller firmware is NOT used — the **application processor drives MTCMOS/BUCK/CG
directly**. So XNU must replicate that AP-direct sequence; poking the wrong registers
(the earlier bring-up wrote `SPM+0x338` and `MFG_TOP` power bits) does nothing.

Confirmed facts from the live device:
- `ShaderPresent = 0x5` → Mali-G57 **MC2** (2 shader cores).
- Working OPP: 390–1003 MHz, **VGPU 0.575–0.725 V**, VSRAM_GPU **0.75 V**.
- `/dev/mem` is disabled, so register values can't be dumped from Android userspace —
  XNU's own boot log is the measurement channel.

## 1. Register bases (from /proc/iomem + DT)

| Block | Phys base | Notes |
| :-- | :-- | :-- |
| Mali-G57 GPU | `0x13000000` | 16 KB; kbase Job-Manager layout (see §6) |
| SPM (sleep/power) | `0x10006000` | MTCMOS `*_PWR_CON`, `PWR_STATUS` |
| infracfg_ao | `0x10001000` | bus-protection set/clr/sta |
| topckgen | `0x10000000` | MFG clock mux (CLK_CFG) |
| apmixedsys | `0x1000C000` | MFGPLL |
| MFG_TOP / mfgcfg | `0x13fbf000` | external CG + bg3d subsys CG; **confirmed** as `syscon@13fbf000` in device-tree |
| PWRAP (→MT6366 PMIC) | `0x10026000` | VGPU / VSRAM_GPU bucks |

## 2. Power-on order (`__gpufreq_power_control`, ON path)

1. `infra_bcrm[0x4C] |= 0x1800`  (bus request; base TODO)
2. regulator **VGPU** enable  (PWRAP → MT6366)
3. regulator **VSRAM_GPU** enable
4. **MFG0** MTCMOS on
5. **MFG1** MTCMOS on → release MFG bus-protection (infracfg_ao)
6. enable MFG **reference-clock mux**
7. **MFG2** MTCMOS on
8. **MFG3** MTCMOS on
9. clock_control: mux→CLOCK_MAIN, poll stable, enable **bg3d subsys CG**, **external CG**

Power-off is the exact reverse.

## 3. MTCMOS domains (SPM `0x10006000`)

| Domain | PWR_CON | PWR_STATUS bit |
| :-- | :-- | :-- |
| MFG0 (async/bus) | `+0x308` | `0x4` |
| MFG1 (core) | `+0x30C` | `0x8` |
| MFG2 (shader 0) | `+0x310` | `0x10` |
| MFG3 (shader 1) | `+0x314` | `0x20` |

`PWR_STATUS = +0x16C`, `PWR_STATUS_2ND = +0x170`.
PWR_CON bits: `0`=RST_B, `1`=ISO, `2`=PWR_ON, `3`=PWR_ON_2ND, `4`=CLK_DIS,
`8`=SRAM_PDN, `12`=SRAM_PDN_ACK.

Per-domain power-on (verified against `__gpufreq_mfg[0-3]_control`):
```
con |= (1<<2);                        // PWR_ON
con |= (1<<3);                        // PWR_ON_2ND
wait ((PWR_STATUS & bit) && (PWR_STATUS_2ND & bit));
udelay(~10);
con &= ~(1<<4);                       // clear CLK_DIS
con &= ~(1<<1);                       // clear ISO
con |=  (1<<0);                       // RST_B
con &= ~(1<<8);                       // clear SRAM_PDN
wait ((con & (1<<12)) == 0);          // SRAM_PDN_ACK clear
```

### MFG1 bus-protection release (infracfg_ao `0x10001000`, after MFG1 up)
```
infracfg[0x718] = 0x80;
infracfg[0x2A4] = 0x600000;
infracfg[0x718] = 0x60;
infracfg[0x2AC] = 0x200000;
```

## 4. Clocks

### External CG (`__gpufreq_external_cg_control`, MFG_TOP `0x13fbf000`)
```
mfg[0x20] |= 0x03C00000;
mfg[0xB0] &= ~0x0600;
mfg[0xB4] |= 0x0010;
mfg[0xB0]  = (mfg[0xB0] & ~0x00080100) | 0x80000;
mfg[0x24] |= 1;
mfg[0xF60]|= 1;
```

### bg3d subsys CG (`clk-mt6789-mfgcfg.ko`, SETCLR gate, parent `mfg_pll_ck`)
`mtk_gate_regs = { sta=0x0, clr=0x8, set=0x4 }`, bg3d shift = **0**.
- enable:  `mfgcfg[0x8] = (1<<0);`   (write bit to CLR)
- status:  `mfgcfg[0x0] & (1<<0) == 0`  → clock on

### MFG clock mux (`clk-mt6789.ko`, topckgen `0x10000000`)
`mfg_ref_sel` / `mfg_pll_sel` live in **CLK_CFG** at `mux_ofs=0x50`, `set=0x54`,
`clr=0x58`, update via CLK_CFG_UPDATE `0x04`; 4 parents. Exact `mux_shift`/parent-index
TODO (confirm via `clk-common` mux op or boot diag).

### MFGPLL (`clk-mt6789.ko`, apmixedsys `0x1000C000`)
`mtk_pll_data`: CON0 `reg=+0x268`, `pwr_reg=+0x274`. Full enable (pwr→en→pcw→lock)
TODO. For first bring-up the boot default parent may already give a readable clock.

## 5. Current status (XNU side)

- `SweetFramebuffer.cpp` power-on **rewritten** to the exact §2/§3 MTCMOS sequence +
  §4 external CG, with `PWR_STATUS`/`MFG_CON` diagnostics logged.
- bg3d subsys CG added (mfgcfg `syscon@13fbf000`, confirmed).
- Pending (to confirm via next boot log, since XNU can read these regs):
  - Whether MTCMOS + bg3d CG + boot-default mux already make `GPU_ID` readable.
  - MFGPLL enable + mux parent-select (only for real DVFS frequency, not register access).
  - PMIC VGPU encoding over PWRAP/MT6366 (interface confirmed; exact MT6366 regs not re-verified).

## 6. Mali-G57 register map (kbase Job-Manager, base `0x13000000`)

GPU_CONTROL: `GPU_ID 0x0`, `L2_FEATURES 0x4`, `TILER_FEATURES 0xC`, `MEM_FEATURES 0x10`,
`MMU_FEATURES 0x14`, `AS_PRESENT 0x18`, `JS_PRESENT 0x1C`, `GPU_IRQ_RAWSTAT 0x20`,
`GPU_IRQ_CLEAR 0x24`, `GPU_IRQ_MASK 0x28`, `GPU_IRQ_STATUS 0x2C`, `GPU_COMMAND 0x30`,
`GPU_STATUS 0x34`, `GPU_FAULTSTATUS 0x3C`, `GPU_FAULTADDRESS 0x40/0x44`,
`PWR_KEY 0x50` (= `0x2968A819`), core `*_PRESENT/_READY/_PWRON` at `0x100..0x1E0`.
JOB_CONTROL base `0x1000` (`JOB_IRQ_* 0x1000..0x100C`); job slot *n* at `0x1800 + n*0x80`.
MMU base `0x2000`; address space *n* at `0x2400 + n*0x40`.
Full definitions in [`MaliMetal/MaliValhallHw.h`](../MaliMetal/MaliValhallHw.h).

## 7. What's next for Metal→Vulkan (depends on this)

The Metal ICD is meaningless until the GPU answers. Order after power-on lands:
1. **GPU init** — `PWR_KEY` unlock → `SHADER/TILER/L2_PWRON` → poll `*_READY` →
   `SOFT_RESET` via `GPU_COMMAND` → read real `GPU_ID`/features.
2. **MMU/address spaces** — program AS0 (LPAE page tables, `TRANSTAB`/`MEMATTR`/
   `TRANSCFG`, `AS_COMMAND UPDATE`); GPU-VA allocator for buffers & descriptors.
3. **Job submission** — build Valhall job-descriptor chains in GPU memory; write
   `JS_HEAD_NEXT` + `JS_COMMAND_NEXT=START`; completion via `JOB_IRQ` (add handler).
4. **Metal→this** — `MTLDevice/Queue/Pipeline/Drawable` → state + shaders. Hardest part
   is the shader compiler: **Metal AIR → SPIR-V → Mali Valhall ISA** (Mesa panvk/NIR).
   Until then `MaliMetalVulkan.c` stays a CPU rasterizer (honest software fallback).
