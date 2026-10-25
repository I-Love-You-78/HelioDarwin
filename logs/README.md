# Device boot logs

The latest [texture-copy/mipmap retry](boot-0199-run-40127381401-blit-sampler.log)
started at 01:01:09 on 2026-10-25, Asia/Yekaterinburg (UTC+5), with kernel
[40127381401](https://github.com/squeeeezy/HelioDarwin/actions/runs/40127381401)
and userspace `3323928`. Its original 106,442-byte console ends at the user's
manual return. The former missing texture-copy selector did not recur;
backboardd aborted while selecting `fixed_frag_lpf_cpf`, absent from the actual
library. The crash report identifies that name; binary analysis traces its
selection to the unavailable native FP16 branch. No kernel panic is recorded in this short capture; it does
not validate the delayed sampler or establish GPU output.

The preceding [integrated ABI 4 capture](boot-0198-run-40121803596-abi4.log) started
at 00:31:52 on 2026-10-25, Asia/Yekaterinburg (UTC+5). It used kernel artifact
[40121803596](https://github.com/squeeeezy/HelioDarwin/actions/runs/40121803596)
from `6ab5ae8` and userspace `015515c`. The original 217,245-byte console was
captured at the first rooted Android return. Native Vulkan 1.3 device creation
and the selected color tile-read feature were recorded. backboardd repeatedly
aborted on a missing Metal texture-copy selector; dependent services could not
query displays. The run ended with a kernel data abort in diagnostic stack
sampling, before its configured reset timer. GPU execution is not established.

Earlier completed captures follow. Boot-start times below are for
2026-10-24 in Asia/Yekaterinburg (UTC+5). These captures used kernel artifact
[40106851872](https://github.com/squeeeezy/HelioDarwin/actions/runs/40106851872),
built from kernel commit `9673976`.

| Console | Boot start | Userspace commit | Result and retention |
| --- | --- | --- | --- |
| [Native-profile export](boot-0197-run-40106851872-native-profile.log) | 22:16:28 | `0274263` | 123,736 bytes, manual return. The exact 120,379-byte enabled Vulkan profile was saved to a separate diagnostic file for offline validation; pipeline execution remained blocked. |
| [Profile-schema correction](boot-0197-run-40106851872-profile-schema.log) | 21:37:52 | `3c583d3` | 89,207 bytes, manual return. The schema rejection passed; the matching crash payload identified unsupported Whole/signed buffer-window planning in another pipeline. |
| [MRT/blending candidate](boot-0197-run-40106851872-mrt.log) | 21:13:11 | `17470ae` | 212,213 bytes, ending at manual reset. Backboardd aborted during pipeline creation; its separate crash payload identified malformed Vulkan format-property JSON. Seven previously affected processes were sampled alive; the common SIGBUS did not recur. |
| [UFS/fault diagnostic](boot-0197-run-40106851872-read-fault.log) | 20:33:11 | `9673976` | 262,132 bytes: the keep-head buffer filled, so later output is absent. UFS returned the expected metadata bytes. Backboardd's separate crash payload identified the unsupported MRT path. |

These are original binary pstore records despite their `.log` suffix.
Line endings and recorded bytes are preserved. Console writes can interleave
or contain damaged characters; files must not be normalized as ordinary text.
The generic `Metal failed to build render pipeline` banner omits the detailed
termination payload, so the cause above also uses the matching crash report.

These captures do not establish successful GPU job completion or rendering.
The metadata SIGBUS remains under investigation; absence in one boot is not
a confirmed repair. Older captures remain for comparison.
