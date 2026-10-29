# ZENLTE: Samsung Exynos 7420 in HelioDarwin

What this directory is: the Exynos 7420 (Galaxy S6 / S6 Edge / S6 Edge+ /
Note 5) support layered onto HelioDarwin, the iOS 16 (XNU 8796) bring-up that
already runs on a Mali-G57 phone and has a working Metal-over-Vulkan stack.

Status: **bring-up, zero hardware results.** Nothing here has run on the S6
Edge+. Each step below has an acceptance gate; do not read a pass at one gate
as progress at the next.

## Why this base and not the others

| Project | What it does | Why it is or is not the base here |
| --- | --- | --- |
| **HelioDarwin** | iOS 16 userland on MT6789, Mali-G57, **Metal over Vulkan (PanVK)**, 206 XNU patches, real hardware | **Chosen.** Only public tree that has both goals: an iOS 16 XNU path and a Mali Metal path |
| UniXNU | Adreno-only Metal (Turnip + IR3). GICv3 only | Useful for its GICv3/4K-page documentation; its GPU path cannot serve Mali |
| HakiOS | iOS 16.5 userland, Adreno, GICv3, ARMv8.2 PAN | Its userland ideas transfer; its kernel needs PAN |
| darwin-on-arm/xnu | XNU ~2050 era, 32-bit ARM targets, unmaintained | Historical only |

## What is different on Exynos 7420

| Subsystem | HelioDarwin (MT6789) | Exynos 7420 | Consequence |
| --- | --- | --- | --- |
| CPU | A55 + A76, ARMv8.2 | **A57 ×4 + A53 ×4, ARMv8.0, no PAN** | Must build `-march=armv8-a`; no PAN path |
| Interrupts | GICv3 (`ICC_IAR1_EL1`, `GICR_*`) | **GIC-400** (`0x11001000`, `GICC_IAR` at `0x11002000`) | **`0300-zenlte-gicv2-interrupt-path.patch`** |
| Timer | vendor PPI remap | PPI 13/14/11/10, 24 MHz | INTID 14 for the physical timer |
| GPU | Mali-G57 (Valhall) | **Mali-T760 (Midgard)** | PanVK Midgard support is far behind Valhall: see `docs/mali-t760.md` |
| Storage | UFS | Exynos UFS at `0x15570000`, 4 frames + PHY | New driver, not `ufshcd`-shaped |
| Display | vendor DSI | DECON0 `0x13930000` + DSIM `0x13900000` | New driver |
| POST | TOPRGU reset | different watchdog | Recovery path must be re-established |

## The one patch in this directory

`xnu/0300-zenlte-gicv2-interrupt-path.patch`

- Adds the `ZENLTE` machine configuration (ARMv8.0, plain arm64, no PAC).
- Adds `pexpert/pexpert/arm64/zenlte.h`: GIC-400 frame addresses, register
  offsets, timer INTID 14, and the accessors the two changed files use.
- `pe_init_fiq()`: programs the GIC-400 distributor and the banked CPU
  interface (Group 1, no split EOI, priority mask, PPI/SGI enable).
- `sleh_irq()`: acknowledges through `GICC_IAR`, ends through `GICC_EOIR`,
  dispatches the timer PPI, SGIs and everything else.

Verification performed on this machine, not claimed on hardware:

```sh
cp -r <xnu-8796.121.2> pverify && cd pverify && git init -q . && git add -A && \
  git -c user.email=a@b -c user.name=t commit -qm base
git apply --check -v <this dir>/0300-zenlte-gicv2-interrupt-path.patch   # clean
git apply <this dir>/0300-zenlte-gicv2-interrupt-path.patch              # clean
```

The patch was generated from a real edited tree, so it is not hand-counted
diff text: `git apply` accepts it and reproduces the edited files byte for
byte.

## Build

Use the repository's own CI, which runs XNU on `macos-14` with Apple's toolchain
and the pinned `blacktop/darwin-xnu-build`, then publishes a `xnu-kernel`
artifact. Dispatch it with the ZENLTE machine configuration:

```sh
gh workflow run xnu.yml -R <your-fork> -f machine_config=ZENLTE \
  -f kernel_config=DEVELOPMENT -f keep_going=true
gh run download <run-id> -R <your-fork> -n xnu-kernel -D xnu/out
```

Do not promise a local macOS build: `Mu-Basetools`/XNU toolchain support for
macOS ARM hosts is not in the pinned build script, and this machine has no
Xcode (see "Blockers" in the parent README).

## Gates, in order

1. **Patch applies and the kernel links** in CI for `machine_config=ZENLTE`.
   Acceptance: a `kernel.development.zenlte` artifact exists.
2. **The shim boots the S6 Edge+ into a framebuffer** with an Exynos 7420 boot
   image. Acceptance: a screenshot or `serial=3` console line from the shim.
3. **`pe_init_fiq` survives** and the timer ticks. Acceptance: a boot log line
   after `pe_init_fiq`, and `rtclock_intr` counts rising.
4. **DDR map is right.** The vendor map describes 3 GiB at `0x40000000`; the
   top GiB of the 4 GiB package is undescribed. Acceptance: the shim reports a
   memory size that matches what the boot loader leaves mapped.

Only after 1-4 does storage (`0x15570000`), display and the Mali work become
meaningful.
