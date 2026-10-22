# MaliMetal (Metal ICD Driver)

This directory contains the userspace Metal Installable Client Driver (ICD) bundle (`MaliMetal.bundle`) for Apple's `Metal.framework` targeting the ARM Mali-G57 MC2 (Valhall architecture, MediaTek MT6789).

### Current Implementation Scope
To satisfy Apple `Metal.framework` and `CoreAnimation` / `RenderServer` during iOS 16 boot:
- **IOKit Hardware Mapping:** `MaliMetalDevice` connects to XNU `SweetMaliUserClient` via `IOServiceOpen` and maps physical Mali MMIO registers (`0x13000000`, 64KB) using `IOConnectMapMemory64`.
- **Valhall Register & Job Definitions:** `MaliValhallHw.h` defines physical registers, Job Slot MMIO, and 64-byte Valhall job descriptor structures.
- **2D Rasterizer Execution:** `MaliMetalVulkan.c` executes real 2D quad clipping, bounding box calculation, texture coordinate sampling, and 32-bit BGRA alpha blending into target frame surfaces during `[MTLCommandBuffer commit]`.
- **Protocol Implementation:** Implements `MTLDevice`, `MTLCommandQueue`, `MTLCommandBuffer`, `MTLRenderCommandEncoder`, `MTLBuffer`, `MTLTexture`, and `MTLRenderPipelineState`.

### Long-term Architectural Roadmap
Following the architecture of `darling-metal` and Mesa `panvk`:
1. Bridge Metal shading language / Apple Intermediate Representation (AIR) into SPIR-V.
2. Direct Valhall Job Slot descriptor submission for 3D graphics acceleration.

**Status:** In-memory Metal ICD with 2D software rasterizer and IOKit MMIO mapping (WIP bridge toward Mesa panvk).
