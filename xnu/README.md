# XNU kernel build

The [workflow](../.github/workflows/xnu.yml) builds XNU using
[blacktop/darwin-xnu-build](https://github.com/blacktop/darwin-xnu-build) at the
pinned revision recorded in that workflow. Its defaults are macOS source release
`13.5` (XNU 8796 generation), `DEVELOPMENT` and the `SWEET` machine configuration.
The target kernel is arm64; the runner is selected by the workflow's `macos-14`
label and supplies Apple build tools.

Numbered `patches/*.patch` and supporting shell files are copied to the build
script's patch directory before compilation. X676C bring-up is layered on the
existing SWEET port; selecting that machine configuration does not identify the
physical SoC as the earlier Qualcomm target.

Download one explicit successful run:

```sh
gh run download RUN_ID -R squeeeezy/HelioDarwin -n xnu-kernel -D xnu/out
```

The deployment helper expects `kernel.development.sweet` for the default
configuration. CI also publishes the source/generated-header build tree for
local object checks. `x676c/tools/xnu-cc.py LOG BUILDTREE [SOURCE ...]` reconstructs
the original build root from the log, with `XNU_CC_DXB` available as an explicit
override. `tools/xnu-cc.py` forwards to the same implementation. Local object
compilation is not a substitute for the complete kernel link.
