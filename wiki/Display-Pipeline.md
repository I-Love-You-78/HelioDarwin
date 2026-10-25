# Display Pipeline

The physical X676C panel is an ili7835 command-mode display, 1080×2400 pixels.
LK initializes the panel and overlay path. XNU's `SweetFramebuffer.cpp` owns
subsequent framebuffer copying and DSI refresh. The shim does not run the older
secondary-core display pump.

## Scanout blocks

| Block | Physical base / configuration |
| --- | --- |
| DSI0 | `0x14013000` |
| DISP_MUTEX0 | `0x14001000` |
| OVL0 layer 1 | `0x14005000`; visible LK buffer `0xfeabe000`, stride 4320 bytes |
| OVL0_2L layer 0 | `0x14006000`; LK buffer `0xfe0c8000`, stride 4352 bytes |

The software vsync timer requests refresh at approximately 60 Hz. That cadence
does not prove sustained 60 FPS, tear-free presentation or GPU acceleration.
Framebuffer copies and cache cleaning still consume CPU time.

Patch 0200 services the inherited TOPRGU watchdog on a separate timer/workloop;
display-off and a busy DSI path cannot skip that service. Its configuration is
preserved. Runtime service gaps and display-off liveness remain validation gates.

## Serialized DSI refresh

Patch 0190 follows the inspected MediaTek DRM trigger ordering:

1. Acquire a nonblocking software gate so concurrent callers cannot overlap.
2. Read `DSI_INTSTA +0x0c`; busy is **bit 31**, not completion bit 0.
3. When idle, program mutex enable `0 -> 1`, pulse engine reset bit 0 in
   `DSI_CON_CTRL +0x10` while preserving other control bits, then write
   `DSI_START 0 -> 1`, with the required memory barriers.
4. Defer a busy transfer. Persistent busy for 100 ms enters bounded engine
   recovery instead of spinning indefinitely in the presentation callback.

The vsync path performs the push after copying/cleaning panel memory; `present`
does not issue a second competing push. In diagnostic run `39945002194`, all
sampled status values were `0x16`, recovery count remained zero, and the user
confirmed that the clock/interface appeared physically. This establishes the
corrected display path for that boot, not operation of the new MaliMetal driver.

## Native resolution and UIKit

The normal boot helper selects `sweetfb_native=1`: logical and physical buffers
are both 1080×2400, with no crop offset. At scale 3, UIKit's viewport is 360×800
points. The compatibility `sweetfb_native=0` mode uses a 1125×2436 canvas and
copies a centered panel window; that mode is not the native-resolution fix.

Native width alone does not select a valid UIKit status bar. The stock matcher
also consumes exclusion geometry and native/reference scale. SpringBoard's
shared display/device contexts supply that metadata to applications. The opt-in
native adapter supplies the actual panel cutout and corner radius and selects
the stock non-Jindo, geometry-aware `FixedSplit` policy. It keeps scanout,
scale, artwork identity and touch coordinates unchanged.

The native metadata adapter is compile-checked but not yet phone-validated.
Its offline installer now adds a guarded normal dependency to the exact supported
SpringBoard executable. It preserves the existing entitlement blobs and signature
metadata, updates ad-hoc page hashes, and retains an exact executable backup;
it does not depend on a DYLD environment variable reaching the process.
The 17:39:02 boot confirmed adapter loading, but full context validation retained
the original donor geometry. UIKit selects its screen-type-4 donor for native
360×800 reference bounds below 375×812. The input guard now accepts that exact
38/29-point inset pair alongside its other verified stock pairs; requested native
outputs and the full context matcher are unchanged. A successful native context,
normal status-bar provider and visible geometry remain runtime checks.
The 18:14:02 retry reported the requested native context with `match=1`, including
1080×2400 pixels, scale 3, the native cutout and safe area, and corner radius 39.
SpringBoard later aborted in MPS initialization, so visible provider selection,
orientation and rendered UI correctness are still unconfirmed.
Loading, policy selection, application contexts, orientation and the visible
status bar must be checked together. See [System Services and Posters](System-Services-and-Posters.md)
for exact geometry, API guards, installation and rollback, and [Input and HID](Input-and-HID.md)
for coordinate delivery.

## Source and diagnostics

- [DSI ordering and reset patch](https://github.com/squeeeezy/HelioDarwin/blob/x676c/xnu/patches/0190-x676c-serialize-dsi-and-use-toprgu-reset.patch)
- [Opt-in scanout diagnostics](https://github.com/squeeeezy/HelioDarwin/blob/x676c/xnu/patches/0188-x676c-trace-ui-scanout-and-diagnostic-reset.patch)
- [Native context adapter](https://github.com/squeeeezy/HelioDarwin/tree/x676c/tools/native-display)

Pixel hashes and sampled thumbnails establish buffer contents; they do not alone
prove that DSI transmitted those pixels. Preserve the raw [pstore record](Pstore-Debugging-on-MediaTek.md)
alongside the user's physical observation.
