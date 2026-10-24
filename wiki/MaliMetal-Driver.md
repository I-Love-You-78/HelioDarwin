# MaliMetal Driver

MaliMetal implements a bounded Metal-to-Vulkan path for iOS 16.7.16 (`20H392`)
on the X676C Mali-G57 MC2. The source and native dependencies compile for arm64
iOS. Selected real CoreAnimation render and compute shaders translate to
validated SPIR-V. **Assembled GPU execution on the phone is not yet validated.**
Full Metal or Vulkan conformance is not claimed.

## First integrated device observation

Run `39972968304` loaded the native PanVK library and read Mali-G57 identity and
texture features. Backboardd then repeatedly received SIGILL while PanVK queried
`nonCoherentAtomSize`: `util_cache_granularity` reads `CTR_EL0`, but the kernel
left user access disabled. Patch 0193 configures the read-only hardware cache-type
query for SWEET; its configured X676C cluster contains only Cortex-A55 cores.
Run `39984534077` still assembled the old `SCTLR_EL1=0x3454593d`, while a fresh
local assembly of its archived source and headers produces `0x34d4d93d`.
Assembly now bypasses the restored compiler cache. CI and deployment check the
startup constant in the Mach-O before booting. The generic illegal-instruction
handler remains unchanged. These observations do not establish Vulkan device
creation, GPU memory mappings or GPU jobs.

Run `40083985422` passed the cache-register query and kept backboardd running.
It then failed at `vkCreateDevice`: the logical-device constructor used
`os_dupfd_cloexec(-1)`, although the XNU transport holds an IOKit connection.
The current port opens a separate client on the same accelerator with
[IOServiceOpen](https://developer.apple.com/documentation/iokit/1514515-ioserviceopen)
and closes that owned connection when the logical device is destroyed. The
physical-device query connection remains borrowed from its caller. Transport
initialization failures use `VK_ERROR_INITIALIZATION_FAILED`; allocation failures
retain their allocation error. This updated path is not yet device-validated.

The first retry of that path reached IOKit service discovery and returned
`kIOReturnUnsupported` (`0xe00002c7`). The kernel user client inherited
`IOUserClient::getService`, which returns `NULL`. Patch 0195 returns the attached
GPU provider, completing the connection-discovery contract. GPU execution still
needs validation with that kernel.

Run `40089627425` passed service discovery, then panicked during GPU-buffer
cache-mode setup. `pmap_sync_page_attributes_phys` shifted a 32-bit page number
before converting it to an address, dropping physical-address bits above 4 GiB.
Patch 0196 uses `ptoa_64` and `flush_dcache64`. The corrected local object uses
a 64-bit shift, and the following boot passed that cache-mode setup.

Run `40090751567` subsequently created the Vulkan device (`VkResult 0`), but
Metal aborted in `MTLAddDevice` because the frontend class did not declare
`MTLDevice`. That function also requires `MTLDeviceSPI`, then invokes
`initLimits` and `initFeatureQueries`. The current frontend declares both
protocols and provides its own native limits rather than selecting an Apple
GPU profile. The 264-byte limits layout and named field encoding are checked
against the loaded Metal runtime. A retry with that frontend passed the
registration checks and reached `CA::OGL::new_metal_context`, then crashed in
`MaliMetalObject setLabel:`. Its compiled accessor used offset zero because
the standalone `NSObject` declaration omitted `isa`; it tried to release the
packed object header. The declaration now reserves that word and synthesized
atomic copy accessors use offset eight. Derived frontend objects share the
corrected layout; ivars following the opaque `_MTLDevice` base still use runtime
offsets. The layout fix and GPU command execution remain hardware-unvalidated.

## Execution layers

| Layer | Implementation |
| --- | --- |
| Metal frontend | Objective-C device, resources, queues, command buffers and encoders. |
| Shader translation | C library parsing/binding interfaces and a pinned Rust AIR compiler adapter. |
| Vulkan backend | Real Vulkan resources, immutable draw/dispatch descriptor snapshots and pipelines. |
| PanVK ICD | Pinned Mesa plus v9 Job Manager work and an XNU transport overlay, built as an iOS dylib. |
| XNU | Versioned IOKit BO, GPU-VM, synchronization, submission, completion and recovery calls. |
| Hardware | Mali-G57 MC2, Valhall v9 Job Manager; GPU ID `0x90930010` observed from XNU. |

## Device discovery and capabilities

The kernel accelerator publishes `MetalPluginName` and `MetalPluginClassName`.
Stock Metal loads `/System/Library/Extensions/MaliMetal.bundle` and requires the
device class to inherit from its private `_MTLDevice` base. Registration requires
a usable native Vulkan device, enabled backend capabilities and an AIR compiler
profile. Failure prevents device creation and preserves the underlying error.

Private feature-query initialization follows the inspected 20H392 binary.
No complete Apple GPU family is advertised. Limits and supported operations
reflect the implemented frontend and enabled native device profile; upstream
Vulkan support alone is insufficient to advertise a Metal feature.

## Resources and commands

Buffers and single-sample 2D images use real Vulkan allocations. Shared buffers
and IOSurface-backed textures preserve host ownership, locking and row stride;
Private resources use native storage and do not expose CPU contents. IOSurface
interoperability currently uses staging/copy/readback, not imported zero-copy GPU
memory. Allocations and resources remain owned through actual fence completion.

Per-queue serial submission preserves command order. Render commands include
supported triangle draws and indexed draws with native index buffers. Compute
programs retain the compiled module/layout and create exact specialization
variants from compiler-produced dispatch plans. Nonuniform grids are represented
by exact interior/edge regions, rather than rounded-up extra invocations.
Unsupported operations produce explicit errors.

## AIR and native shader compilation

The parser reads precompiled Metal libraries, function metadata and constants.
The native compiler emits SPIR-V and typed plans for argument ranges, vertex
layouts, constants and dispatch specialization. Runtime MSL source compilation
is not implemented. Rust is confined to the AIR compiler adapter; the frontend,
Vulkan backend and kernel transport use Objective-C/C/C++.

Offline checks used actual CoreAnimation clear, upsample, a selected specialized
textured pipeline, and APL/dimming compute kernels. Those checks establish the
tested input/translation paths, not every shader variant in QuartzCore or every
future application pipeline. Compute resource support is currently bounded to
verified direct buffers, constants and read-only sampled-image forms.

## Kernel and PanVK boundary

The ICD uses XNU IOKit calls instead of Linux DRM or Android kbase ioctls. The
initial port does not advertise dma-buf/external-memory import or host-cached BO
memory. CPU allocations use Normal non-cacheable mappings; CPU pages are 16 KiB
and GPU pages are 4 KiB. Kernel-created GPU addresses are separate from CPU
virtual addresses.

One hardware job runs globally at a time. An independent workloop timer polls
JM/MMU/GPU status; a bounded job watchdog attempts reset and captures failures.
Failed recovery retains potentially DMA-visible pinned storage. Tiler heaps use
committed bounded allocations; grow-on-fault paging is not implemented. See
[GPU Power Sequence](MT6789-GPU-Power-Sequence.md) for rails, clocks, register
layout and remaining DVFS work.

## Build and provenance

Build the pinned dependencies separately and provide matching iOS libraries;
proprietary inputs and generated binaries are not included in Git. The main
builder verifies iOS platform, source and dependency hashes and strict linking.
It packages dependency provenance and license notices. The offline installer
verifies the current source digest and copied files before replacing the bundle.
See [build instructions](https://github.com/squeeeezy/HelioDarwin/blob/x676c/MaliMetal/README.md).

Pinned source inputs:

- [Mesa 5a07217f](https://gitlab.freedesktop.org/mesa/mesa/-/tree/5a07217f034b3e50d8c7c7794f97a2df1742613b)
- [v9 PanVK work fd6ed91e](https://github.com/zenithblue-oss/panvk-kbase-android/tree/fd6ed91eff1c8d3a83307a2348b959bb93797011)
- [AIR compiler a2c440b8](https://github.com/steelbrain/metal2vulkan/tree/a2c440b8bcdcf20d34aa1694b61ae22c2a7ea68b)

The native build preserves upstream license notices. Prior related work includes
[Darling](https://github.com/darlinghq/darling) and
[Indium](https://github.com/darlinghq/indium); their existence does not establish
runtime compatibility of this implementation.

## Remaining coverage and validation

Framebuffer fetch, AIR blend specialization, masked color writes, multiple render
targets, depth/stencil, MSAA, mipmaps, texture arrays/3D and broader shader/resource
forms remain unsupported. Advanced Metal interfaces and private SPI coverage
also need further work. Explicit compute barrier selectors and storage-image
write kernels are not part of the verified initial compute path.

Offline source/patch composition and compilation are separate from the next
device checks: stock Metal loading, GPU power/VM access, native pipeline creation,
real JM completion, correct image/readback and UI presentation. The clock/interface
observed after the DSI fix used system software rendering and does not prove this
driver's GPU execution.
