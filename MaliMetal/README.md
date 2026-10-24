# MaliMetal

Experimental Metal implementation for iOS 16.7.16 (20H392) on the MT6789
Mali-G57 MC2, using a Vulkan backend and an XNU Job Manager transport.

## Implementation status

The source builds for arm64 iOS. End-to-end GPU rendering with this driver has
not been validated on the phone, and Metal or Vulkan conformance is not claimed.

- **Kernel transport:** versioned IOKit calls for pinned buffers, GPU address
  spaces, cache maintenance, Job Manager submission, completion and fault
  recovery. Register definitions and product errata were checked against the
  phone's vendor modules and Mesa v9 descriptions. The job header is 32 bytes
  with 128-byte job alignment. Userspace does not receive raw GPU MMIO.
- **Metal objects:** resources, queues and recorded operations call the Vulkan
  backend. Resources remain retained through completion. Failed or unsupported
  operations report their actual backend errors. Indexed drawing and compute
  dispatch use native allocations and exact binding plans.
- **Shader compiler:** an optional, separately built native AIR compiler parses
  Metal libraries and produces SPIR-V. The actual QuartzCore vertex/clear shader
  pair, selected textured draws and APL/dimming compute kernels were compiled
  and their SPIR-V validated offline. Typed resource bindings,
  vertex layouts and push constants come from compiler metadata. This evidence
  does not establish support for every QuartzCore shader or arbitrary MSL.
- **Vulkan backend:** buffer/image operations, command recording, render pipelines
  and specialized compute pipelines are implemented. The pinned native PanVK
  port builds as an arm64 iOS ICD with strict symbol resolution. Its execution
  on the phone and broader shader coverage remain unvalidated. Device creation
  fails when the required ICD or capabilities are unavailable.
- **Presentation:** IOSurface access preserves ownership, locking and row stride.
  The observed phone clock/interface uses the system's software rendering
  fallback and the corrected display path. It is not evidence of MaliMetal GPU
  execution.

`MaliMetalVulkan.c` contains Metal-to-Vulkan enum conversion.

The first integrated phone boot loaded PanVK, reported GPU identity and texture
features, and reached `util_cache_granularity`. The kernel trapped its `CTR_EL0` read;
patch 0193 configures the required user cache-type query on SWEET. Run
`39984534077` still contained the earlier startup constant, despite the updated
headers. Assembly now bypasses the restored compiler cache, and the assembled
register policy is checked before deployment. No GPU job execution has been
established by these boots.

## Dependencies and build

Build a fresh iOS artifact from the current source. Generated bundles and native
dependency binaries are kept outside version control.

The cross-build needs Python with `ziglang`, LLVM `ld64.lld`, Vulkan headers from
the pinned Mesa checkout, and matching iOS system libraries. Supply `libSystem`
and its reexports, `libobjc`, Foundation, CoreFoundation, Metal, IOKit and IOSurface
under their normal sysroot paths. `usr/lib/libSystem.dylib` must contain or point
to `libSystem.B.dylib`. These proprietary inputs must be supplied separately.

The native Vulkan dependency is prepared by `Vulkan/prepare_mesa.py` and built
with `Vulkan/build_mesa.py`. The port includes the pinned v9 Job Manager work and
an XNU transport overlay; upstream Mesa's G57 support alone is insufficient.
The build records revisions, actual transport inputs, kernel ABI, strict-link
audit and license notices beside `libpanvk_sweetmali.dylib`.

For native AIR compilation, build the pinned external compiler using
`Shader/build_air.py`. That helper records the compiler revision, Rust inputs,
toolchain and artifact hashes. Its output preserves the compiler and dependency
license notices; the compiler retains its own license.

```powershell
python MaliMetal/Shader/build_air.py --compiler-source PINNED_COMPILER_CHECKOUT --out AIR_BUILD
python MaliMetal/build.py --sysroot IOS_SYSROOT --vulkan-headers MESA_INCLUDE --air-provider AIR_BUILD --vulkan-icd PATH_TO_IOS_ICD --wsl --linker /usr/bin/ld64.lld-18
```

The AIR helper documents the supported Rust versions and its explicit option
for the older toolchain that was locally compile-checked. `PATH_TO_IOS_ICD` must
have its matching `panvk-sweetmali.json` and `LICENSES` directory alongside it.
Omitting the ICD prevents Vulkan device execution.

Output defaults to `MaliMetal/out/MaliMetal.bundle` with a `build.json` manifest.
The builder verifies the arm64/iOS platform, source and dependency hashes, and
resolution of the driver's own symbols. System symbols link strictly against
the matching supplied iOS libraries. Limited Darwin declarations are used rather
than a complete iOS SDK; Objective-C selector compatibility still needs runtime
validation. The bundle includes dependency provenance and license notices.

## Coverage limits

The implemented render path is bounded to one color attachment, single-sample
2D textures and supported triangle draws. AIR blending/framebuffer fetch, masked
color writes, multiple render targets, depth/stencil and broader texture or
resource variants remain unsupported. Runtime MSL source compilation is absent.
Compute translation supports verified direct-buffer and sampled-image forms;
other resource forms and unsupported shader variants return explicit errors.

IOSurface interoperability currently uses CPU staging and readback with ownership
and stride checks, not zero-copy imported GPU memory. The kernel serializes GPU
jobs and uses bounded completion polling. These choices establish implementation
boundaries, not performance or conformance guarantees.

## Offline installation

Install explicitly into an offline mounted root image:

```sh
sudo python3 tools/sweet-root-tweaks.py MOUNTED_ROOT --malimetal-build MaliMetal/out --malimetal-only
```

The installer checks the source digest, binary and component hashes, iOS
platform and generated bundle metadata. `--malimetal-only` installs the verified
bundle without applying daemon or preference changes. Omitting
`--malimetal-build` leaves the installed driver untouched.
