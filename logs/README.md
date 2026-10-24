# Device boot logs

Latest completed captures are listed first. Boot-start times below are for
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

Neither capture establishes successful GPU job completion or rendering.
The metadata SIGBUS remains under investigation; absence in one boot is not
a confirmed repair. Older captures remain for comparison.
