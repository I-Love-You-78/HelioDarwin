# ZENLTE: Samsung Exynos 7420 in HelioDarwin

This directory is the Exynos 7420 support (Galaxy S6 / S6 Edge / S6 Edge+ /
Note 5) layered onto HelioDarwin, the iOS 16 (XNU 8796) bring-up that already
runs on a Mali-G57 phone and has a working Metal-over-Vulkan stack.

Status: **bring-up, zero hardware results.** Nothing here has run on the S6
Edge+. The kernel builds in CI; it has never executed on the phone. Each gate
below is separate; passing one is not progress at the next.

Hardware facts, with a file and line for every address, are in
[HARDWARE.md](HARDWARE.md). Read that before touching a register.

## Why this base and not the others

| Project | What it does | Why it is or is not the base here |
| --- | --- | --- |
| **HelioDarwin** | iOS 16 userland on MT6789, Mali-G57, **Metal over Vulkan (PanVK)**, 206 XNU patches, real hardware | **Chosen.** Only public tree with both goals: an iOS 16 XNU path and a Mali Metal path |
| UniXNU | Adreno-only Metal (Turnip + IR3). GICv3 only | Useful for its GICv3/4K-page documentation; its GPU path cannot serve Mali |
| HakiOS | iOS 16.5 userland, Adreno, GICv3, ARMv8.2 PAN | Its userland ideas transfer; its kernel needs PAN |
| darwin-on-arm/xnu | XNU ~2050 era, 32-bit ARM targets, unmaintained | Historical only |

## What is different on Exynos 7420

| Subsystem | HelioDarwin (MT6789) | Exynos 7420 | Consequence |
| --- | --- | --- | --- |
| CPU | A55 + A76, ARMv8.2 | **A57 x4 + A53 x4, ARMv8.0, no PAN** | Built `-march=armv8-a`; two clusters, `MAX_CPU_CLUSTERS` 2 |
| Interrupts | GICv3 (`ICC_IAR1_EL1`, `GICR_*`) | **GIC-400** (`GICD 0x11001000`, `GICC 0x11002000`) | No `ICC_*` registers at all; see below |
| IPI | `ICC_SGI1R_EL1` | **`GICD_SGIR`** (`0xF00`) | `ICC_SGI1R_EL1` traps as undefined here |
| Timer | vendor PPI remap | PPI 13/14/11/10, 24 MHz | INTID 14 for the physical timer |
| GPU | Mali-G57 (Valhall) | **Mali-T760 (Midgard)** | PanVK Midgard support is far behind Valhall: see `docs/mali-t760.md` |
| Storage | UFS | Exynos UFS at `0x15570000`, 4 frames + PHY | New driver, not `ufshcd`-shaped |
| Display | vendor DSI | DECON0 `0x13930000` + DSIM `0x13900000`, panel 1600x2560 | New driver |
| Debug UART | unavailable, uses the panel | **`serial_1` at `0x14C20000`**, pins gpd1-0..3 | The vendor dts calls it the debug port |

## ZENLTE is its own machine config, not a SWEET variant

The sweet/topaz/x676c ports are Qualcomm SM7150 and MediaTek MT6789 boards.
Their drivers poke fixed physical addresses that belong to unrelated hardware
on an Exynos: RPMH at `0x18220000`, SPMI at `0x0c440000`, cpufreq-hw at
`0x0f523000`, GENI I2C at `0x04a84000`, the MTK display at `0x14013000`.

The tree already draws the line that keeps them out. Twelve files are gated
on `ARM64_BOARD_CONFIG_SWEET` - `sweet_rpmh`, `sweet_spmi`, `sweet_pwrap`,
`sweet_cpufreq`, `sweet_i2c1`, `sweet_ufs`, `sweet_usb`, `sweet_touch`,
`sweet_keys`, `SweetBattery`, `SweetFramebuffer`, `sweet_amfi` - and eight are
gated on plain `SWEET` (the userspace-facing IOKit services iOS needs:
`SweetSurface`, `SweetPlatformExpert`, `SweetKeyStore`, `SweetAMFIService`,
`SweetCPU`, `SweetBusy`, `SweetEntitlements`, `SweetWatchdog`).

So `ZENLTE.h` defines `SWEET` and deliberately does **not** define
`ARM64_BOARD_CONFIG_SWEET`. No vendor driver compiles in, and nothing has to
be filtered at run time.

## The patch

`xnu/patches/0300-zenlte-platform.patch` (12 files; generated with
`git diff` from a real edited tree, never hand-counted):

- `makedefs/MakeInc.def`: the `ZENLTE` machine config, `-march=armv8-a`, and
  plain `arm64` instead of `arm64e` (compiling arm64e would turn on
  `__has_feature(ptrauth_calls)` while `HAS_APPLE_PAC` stays 0, and the two
  disagree about `struct machine_thread` and `struct pmap`).
- `pexpert/pexpert/arm64/ZENLTE.h`: GIC-400 frames, register offsets, timer
  INTID 14, IPI SGI 1, and the `SWEET`/no-`ARM64_BOARD_CONFIG_SWEET` split.
- `pe_init_fiq()`: programs the GIC-400 distributor and banked CPU interface
  (Group 1, no split EOI, priority mask, PPI and SGI enable).
- `sleh_irq()`: acknowledges through `GICC_IAR`, ends through `GICC_EOIR`.
- `SweetCPU::signalCPU()`: `GICD_SGIR` instead of `ICC_SGI1R_EL1`, which is a
  GICv3 system register and raises an undefined-instruction exception here.
- `sweet_early.[ch]`, `arm_vm_init.c`: `ZENLTE_NO_EARLY_CONSOLE` switches off
  the MT6789 early console, whose framebuffer and pstore blocks are
  `0xfe000000` and `0x48000000` - ordinary DRAM on this SoC. The stubs gain
  `sweet_early_relocate` and `sweet_early_panic`, which
  `panic_trap_to_debugger()` and `arm_vm_init()` reference under
  `defined(SWEET)`.
- `pe_serial.c`: the framebuffer console no longer keeps a full-panel DRAM
  mirror. On 1600x2560 that mirror was 16MB of kernel BSS for a console, and
  it also silently rejected any panel bigger than 1080x2400. The character
  grid is now the only state and a scroll repaints from it.

The three staging scripts (`0019`, `0020`, `0026`) already carry the
`SWEET || ZENLTE` gate on the branch. They must NOT be in the patch: dxb runs
them from `patches/*.sh`, they do not exist in the XNU tree, and including
them made the whole patch fail and be skipped.

## The panel console

`pexpert/arm/pe_serial.c` has a framebuffer serial console driven entirely by
the Apple Device Tree. `fbcon_probe()` looks for a node named `sweet-fbcon`,
reads `reg`, `width`, `height`, `stride`, maps it write-combining, and
registers as a serial console - so every `kprintf`, panics included, lands on
the panel. No compile-time address is involved:

```
sweet-fbcon {
    reg    = <0xFB_ADDRESS 0xFB_SIZE>   /* absolute physical */
    width  = 1600
    height = 2560
    stride = 6400
}
```

The ADT for this board is generated by `tools/mkadt.py`, which takes the
framebuffer address from the boot loader (`--fb 0x...`) and refuses to emit a
tree whose framebuffer range overlaps a vendor carve-out.

```sh
python3 zenlte/tools/mkadt.py out.adt --fb 0x<address> --dump
```

## Build

CI runs XNU on `macos-14` with Apple's toolchain and the pinned
`blacktop/darwin-xnu-build`, then publishes an artifact:

```sh
gh workflow run xnu.yml -R <your-fork> -f machine_config=ZENLTE \
  -f kernel_config=DEVELOPMENT -f keep_going=true
gh run download <run-id> -R <your-fork> -n xnu-kernel -D xnu/out
```

A local macOS build is not available: the pinned build script has no
Mu-Basetools support for a macOS host, and this machine has no Xcode.

## Gates, in order

1. **Patch applies and the kernel links** in CI for `machine_config=ZENLTE`.
   Acceptance: a `kernel.development.zenlte` artifact exists.
2. **The shim boots the phone into a framebuffer.** Acceptance: a screenshot,
   or a `serial=3` line from the shim.
3. **The kernel's own boot log reaches the panel** at 1600x2560. Acceptance:
   readable `kprintf` output on the screen, not the shim's splash.
4. **`pe_init_fiq` survives** and the timer ticks. Acceptance: a log line
   after `pe_init_fiq`, and `rtclock_intr` counts rising.
5. **The DDR map is right.** The window is `0x40000000`+`0xC0000000`, ending
   at 4 GiB, minus the carve-outs listed in HARDWARE.md. Acceptance: the
   kernel reports the memory it was actually given.

Only after these do storage (`0x15570000`), display and the Mali work become
meaningful.
