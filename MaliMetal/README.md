# MaliMetal (Metal ICD Driver Skeleton)

This directory contains the userspace Metal Installable Client Driver (ICD) bundle (MaliMetal.bundle) for Apple's Metal.framework on ARM Mali-G57 MC2 (Valhall).

### Current Implementation Scope (v1.0 Skeleton)
To satisfy Apple Metal.framework and CoreAnimation / RenderServer validation checks during early iOS 16 boot:
- Implements core Objective-C protocols: MTLDevice, MTLCommandQueue, MTLCommandBuffer, MTLRenderPipelineState.
- Provides memory-backed MTLBuffer and MTLTexture (1080×2400 BGRA surface) allocated via calloc.
- Satisfies CAMetalContextCreate and IOAcceleratorES client binding so userland compositors do not abort due to missing GPU drivers.

### Long-term Architectural Roadmap
Following the prior art of [darlinghq/indium](https://github.com/darlinghq/indium) and [darling-metal](https://github.com/darlinghq/darling-metal), future milestones will bridge these command encoders and Apple Intermediate Representation (AIR) shaders directly into **Vulkan 1.3 / SPIR-V** executed via Mesa's panvk Valhall driver.

**Status:** In-memory ICD Driver Skeleton (WIP bridge toward Mesa panvk).
