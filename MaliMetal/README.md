# MaliMetal (Metal ICD Driver)

This directory contains the userspace Metal Installable Client Driver (ICD) bundle (`MaliMetal.bundle`) for Apple's `Metal.framework` targeting the ARM Mali-G57 MC2 (Valhall architecture, MediaTek MT6789).

### Current implementation

This is an experimental driver prototype. It is not a complete Metal driver or
a working Metal-to-Vulkan bridge. Loading it does not demonstrate UI rendering.
- **IOKit Hardware Mapping:** `MaliMetalDevice` connects to XNU `SweetMaliUserClient` via `IOServiceOpen` and maps physical Mali MMIO registers (`0x13000000`, 64KB) using `IOConnectMapMemory64`.
- **Valhall Register & Job Definitions:** `MaliValhallHw.h` defines physical registers, Job Slot MMIO, and 64-byte Valhall job descriptor structures.
- **CPU rectangle prototype:** `MaliMetalVulkan.c` assumes its own fixed vertex layout. It does not execute Metal shaders and is not a general CoreAnimation renderer. Reverse engineering of this iOS version shows 48-byte CoreAnimation vertices; this prototype uses a different layout.
- **Resources and command objects:** buffer and packed 2D texture allocation, IOSurface import, and partial command recording. Many protocol methods remain missing or stubbed. No working shader compiler, Vulkan backend, GPU address-space management, or GPU draw submission exists.

### Build and install

The legacy checked-in binaries were built for macOS. Build a fresh iOS artifact
from source instead of copying those files into an iOS root.

Requirements: Python with `ziglang`, LLVM `ld64.lld`, and `libSystem.B.dylib`
with its reexports extracted from the matching user-supplied iOS shared cache.
Place the libraries under `SYSROOT/usr/lib` and `SYSROOT/usr/lib/system`, with
`usr/lib/libSystem.dylib` pointing to or containing `libSystem.B.dylib`.
These proprietary input libraries are not included in this repository.

```powershell
python MaliMetal/build.py --sysroot PATH_TO_SYSROOT --wsl --linker /usr/bin/ld64.lld-18
```

The output is `MaliMetal/out/MaliMetal.bundle` with a `build.json` manifest.
The builder checks the arm64/iOS Mach-O platform. This compilation check does
not establish runtime correctness or Metal conformance. The current cross-build
uses limited declarations and dynamic symbol lookup, not a complete iOS SDK.

When preparing an **offline mounted root image**, install explicitly:

```sh
sudo python3 tools/sweet-root-tweaks.py MOUNTED_ROOT --malimetal-build MaliMetal/out
```

The installer verifies source hashes, binary SHA-256 and the iOS platform before
changing the image. Omitting the option leaves the existing bundle unchanged.

### Long-term Architectural Roadmap
Following the architecture of `darling-metal` and Mesa `panvk`:
1. Bridge Metal shading language / Apple Intermediate Representation (AIR) into SPIR-V.
2. Direct Valhall Job Slot descriptor submission for 3D graphics acceleration.

**Status:** CPU prototype and partial IOKit integration; GPU rendering and the
Metal-to-Vulkan path are unimplemented. An interactive lockscreen has not been
verified with this driver.
