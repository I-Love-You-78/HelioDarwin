# zenlte hardware facts

Every address on this page is transcribed from the vendor kernel tree for this
exact board, `samsungexynos7420/android_kernel_samsung_universal7420` at
`lineage-19.1`, and is cited with the file and line it came from. Nothing here
is inherited from the sweet/topaz/x676c ports, whose addresses belong to
Qualcomm SM7150 and MediaTek MT6789 parts.

Device: Samsung Galaxy S6 Edge+, board name `zenlte`, model SM-G928F.
SoC: Samsung Exynos 7420.

(Do not confuse this with `hero2lte`, the Galaxy S7 Edge, SM-G935F, Exynos
8890, whose UEFI port is recorded in `hero2lte-ref`. It is a different SoC
with a different memory map; it is cited here only as independent evidence
that the GIC-400 frame addresses are right.)

## CPU

Four Cortex-A57 and four Cortex-A53, ARMv8.0-A. No PAN, no crypto extension,
no pointer authentication. 64-byte cache lines.

| cluster | cores | MPIDR Aff1:Aff0 | type |
|---|---|---|---|
| 0 | 4x Cortex-A57 | `0x000`-`0x003` | `'P'` |
| 1 | 4x Cortex-A53 | `0x100`-`0x103` | `'E'` |

`cluster-type` in the ADT must be the character `'P'` or `'E'`: that is what
`ml_parse_cpu_topology()` compares against (`osfmk/arm64/machine_routines.c`),
not a 0/1 index. Anything else leaves the core as `CLUSTER_TYPE_SMP`.

`MAX_CPU_CLUSTERS` must be 2, because `machine_routines.c` asserts
`cpu->cluster_id < MAX_CPU_CLUSTERS`.

PSCI version 0.2 over SMC; `cpu_on` is `0xC4000003`.

## Interrupt controller: GIC-400 (GICv2)

`arch/arm64/boot/dts/exynos7420.dtsi:464`, node `interrupt-controller@11001000`
with `compatible = "arm,cortex-a15-gic", "arm,cortex-a9-gic"` and four `reg`
entries:

| frame | address | size |
|---|---|---|
| distributor (GICD) | `0x11001000` | `0x1000` |
| CPU interface (GICC) | `0x11002000` | `0x1000` |
| virtual interface control (GICH) | `0x11004000` | `0x2000` |
| virtual CPU interface (GICV) | `0x11006000` | `0x2000` |

This is the single largest difference from every other board in the tree.
There are no redistributors and no `ICC_*` system registers, so:

- `ICC_SGI1R_EL1` does not exist here. Executing it raises an
  undefined-instruction exception; IPIs go through `GICD_SGIR` at `0xF00`
  instead.
- Interrupt acknowledge and EOI are memory-mapped `GICC_IAR` (`0x00C`) and
  `GICC_EOIR` (`0x010`), not `ICC_IAR1_EL1`/`ICC_EOIR1_EL1`.
- There is no `GICR_WAKER` to clear and no `GICD_CTLR.ARE_NS`.

Independent confirmation: the S7 Edge (Exynos 8890) port in `hero2lte-ref`
records the same four frame addresses and the same PPI layout
(`porting.toml:117`), so this is a family trait of these Samsung SoCs, not a
transcription slip.

## Timer

Architected timer PPIs are the standard `<13 14 11 10>`: secure physical,
non-secure physical, virtual, hypervisor. XNU drives the non-secure physical
timer, so the timer INTID is **14**.

Do not copy the S7 Edge's UEFI value of 27: that port used the *virtual* timer
for its own purposes, and XNU's `sleh_irq` path here expects the physical one.

Timebase is 24 MHz (`exynos7420.dtsi`, fixed-rate-clocks/oscclk).

IPI SGI is 1, matching `SWEET_IPI_SGI`.

## Memory

`arch/arm64/boot/dts/exynos7420-zenlte_common.dtsi:18`:

```
memory@40000000 { reg = <0x40000000 0xC0000000>; };
```

The window therefore ends exactly at 4 GiB, but it is **not** all usable. The
vendor reserved-memory node carves out, in ascending order:

| base | size | name |
|---|---|---|
| `0xE1000000` | `0x800000` | mipi-lli-shmem |
| `0xE1F00000` | `0x40000` | secdma |
| `0xE2000000` | `0x600000` | ion common |
| `0xE2600000` | `0x110000` | mfc_nfw |
| `0xE2800000` | `0x200000` | mfc_fw |
| `0xE2A00000` | `0xE400000` | ion video |
| `0xF0E00000` | `0x600000` | ion video_ext |
| `0xF2A00000` | `0x200000` | mali iram-vector |
| `0xF2C00000` | `0x100000` | sss debug |

`zenlte/tools/mkadt.py` refuses to emit an ADT whose framebuffer range
overlaps any of these, which caught a bogus default address during bring-up.

## Display

`exynos7420.dtsi:2825` and `:2897`:

| block | address | size | compatible |
|---|---|---|---|
| DECON0 | `0x13930000` | `0x10000` | `samsung,exynos5-decon_driver` |
| DECON1 | `0x13950000` | `0x10000` | same |
| DSIM0 | `0x13900000` | `0x1000` | `samsung,exynos5-dsim` |

Panel `s6e3hf2` (`exynos7420-universal7420.dts:936`):

| property | value |
|---|---|
| mode | 2 (MIPI command mode) |
| resolution | 1600 x 2560 |
| size | 70 x 121 mm |
| refresh | 60 Hz |
| DSI high-speed clock | 1100 MHz |
| DSI escape clock | 20 MHz |

### The panel console

`pexpert/arm/pe_serial.c` carries a framebuffer serial console that is
driven entirely by the Apple Device Tree, with no compile-time address: its
`fbcon_probe()` looks for a node named `sweet-fbcon`, reads `reg`, `width`,
`height` and `stride`, maps the range write-combining, and registers itself
as a serial console. Every `kprintf`, including the panic path, then lands on
the panel.

That makes the node the whole contract:

```
sweet-fbcon {
    reg    = <0xFB_ADDRESS 0xFB_SIZE>   /* absolute physical */
    width  = 1600
    height = 2560
    stride = 6400                       /* 1600 * 4, 32bpp */
}
```

The framebuffer physical address is chosen by the boot loader, not by the
vendor device tree, so it is a parameter: `mkadt.py --fb 0x...`. The default
is a placeholder that the overlap check will reject unless it happens to be
free.

Because the console used to keep a full-panel DRAM mirror, it also capped the
panel at 1080x2400 pixels and would have silently refused a 1600x2560 panel.
The mirror is gone: the character grid (now 200x160 columns, enough for a
100x80 layout at scale 2) is the only state, and a scroll repaints from it.

## UART

`exynos7420.dtsi:572`, node `serial_1: uart@14C20000`, which
`exynos7420-zenlte_common.dtsi:113` enables with the comment *"Enable UART
channel for debug port"*:

| property | value |
|---|---|
| base | `0x14C20000` |
| size | `0x100` |
| interrupt | SPI 456 |
| compatible | `samsung,exynos7420-uart` |
| pins | `gpd1-0` .. `gpd1-3`, function 2 (`exynos7420-pinctrl.dtsi:378`) |
| console name | `ttySAC1` (bootargs), 115200n8 |

The other channels, for reference: `serial_0` `0x13630000`, `serial_2`
`0x14C30000`, `serial_3` `0x14C40000`, `serial_4` `0x11460000`.

Earlier notes in this tree said `0x13810000`; that was wrong and is corrected
here.

## Storage: UFS 2.0

`bootargs` names `androidboot.boot_devices=15570000.ufs`, so the UFS host
controller is at `0x15570000`, IRQ 200, with the PHY at `0x15571800`.

## Power and peripherals

All Samsung parts, none of which has a counterpart in the sweet drivers:

| function | location |
|---|---|
| PMIC | S2MPS15 on hsi2c `0x13660000`, address `0x66` |
| sub-PMIC | S2MPB02 on hsi2c `0x13680000`, address `0x59` |
| charger | MAX77833 on hsi2c `0x13650000`, address `0x66` |
| MUIC | MAX77838 on hsi2c `0x13690000`, address `0x60` |
| touchscreen | hsi2c `0x14E10000`, address `0x20` |
| touchkey | i2c `0x20` |
| USB | `usb@15400000`, DWC3 |

The sweet drivers this kernel does **not** include address `0x18220000`
(RPMH), `0x0c440000` (SPMI), `0x0f523000` (cpufreq-hw), `0x04a84000` (GENI
I2C) and `0x05e05000`/`0x14013000` (MTK display). On an Exynos 7420 those
ranges are ordinary DRAM or unrelated blocks, which is why they are gated out
by construction rather than at run time.

## What the first hardware run has to confirm

1. The kernel reaches `kprintf` with the panel console registered, so the boot
   log appears on the screen at 1600x2560.
2. `pe_init_fiq()` survives: it only writes GIC-400 distributor and CPU
   interface registers, never a GICv3 system register.
3. `rtclock_intr` fires, proving the physical timer PPI 14 is delivered as a
   Group 1 IRQ through `sleh_irq`.
4. All 4 GiB is described correctly, with the carve-outs above left alone.
