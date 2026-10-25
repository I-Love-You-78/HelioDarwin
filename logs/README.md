# Device boot logs

Latest GPU-completion capture: [boot-0205-run-40224078640-ui-texture-layout.log](boot-0205-run-40224078640-ui-texture-layout.log).

The 00:26:36 run on 2026-10-26 used kernel `40224078640` (`1c09edc`)
and userspace `3e97131`. The 141,245-byte console records ten successful native
JM terminal results, including fragment jobs, after hardware completion polling
and cache handling. Their captured JS/MMU/GPU status fields are zero. This is
phone evidence of GPU job execution; it does not establish correct pixels,
numerical FP16, frame rate or complete Metal/Vulkan support.

The next renderer failure is now precise: backboardd reaches the adapter's
128-live-BO limit before native allocation, refusing requests of 64, 69,632 and
135,168 bytes with ENOSPC. This is not an observed device-memory allocation
failure. Released surface owners now show falling wired-byte snapshots; no
surface NoMemory failure is retained in this console. A bounded suballocation
remedy is under development, with kernel memory limits preserved.

PosterBoard still traps while initializing/publishing a configuration with nil
paths. Both helper registrations are accepted, but its provider query is denied
by the real host-entitlement policy: zero results are not evidence of absence.
Native PosterBoard initially discovers nine providers. The helper's standalone
legacy registration replaces two valid records without their capability
metadata; PosterBoard rejects them and loses its defaults. The custom repair
job is being corrected; original providers and user data must be preserved.

The console reaches the 600-second reset and the new vendor retry loop, followed
by Android B return around 00:39. The user still reported only the Sakura image;
a complete rendered UI and wallpaper remain unconfirmed.

Previous signed-cache capture: [boot-0202-run-40219933214-ui-signed-cache.log](boot-0202-run-40219933214-ui-signed-cache.log).

The 23:17:49 run on 2026-10-25 used kernel `40219933214` and userspace
`0de001f`. Its original 163,421-byte console records the pinned 2,760-entry
trust set ready and successful real membership calls from `lsd` and `installd`.
PosterBoard and AegirPoster subsequently ran. Native geometry again read back
`match=1`; the previous D8F18H rejection did not recur. This does not establish
completed GPU rendering: backboardd aborted with `buffer memory binding:
VkResult -2`, a label then shared by allocation, mapping and binding failures.
Separate helper logs identify the missing
`newTextureLayoutWithDescriptor:isHeapOrBufferBacked:` selector. Later surface
creation failed with NoMemory. No new driverkitd per-process-limit report was
retained; the exact mmap branch and memory-failure cause remain unproven.

The user saw Sakura with artifacts and a spinner, then black screens, and
returned manually to Android. The console reaches sync and the single diagnostic
reset write; its subsequent `reset returned` print also occurs in earlier
automatic-return captures, so it does not identify the reset failure by itself.
Successful GPU job completions were not individually logged in this build.
Buffer swaps and a visible spinner alone do not identify the frame producer.

The [19:51:55 MPS/watchdog run](boot-0200-run-40200328350-ui-mps-common.log)
on 2026-10-25 used kernel `40200328350` and userspace `ec1806d`. The 79,206-byte
original console reaches the 600-second sync/reset sequence and automatic
Android return. TOPRGU reports an enabled 30-second timeout and initial service
gaps near one second. Native MPS Common cache construction/readback passed in
backboardd; no prior SpringBoard abort appears. The sampled shell instead waits
on LaunchServices initialization. Journal recovery's MobileGestalt identity query
entered the missing Wi-Fi-controller fallback; the poster query timed out.
Separate logs retain native geometry `match=1`. A complete GPU frame, Gaussian
pixels and wallpaper delivery remain unconfirmed; physical display-off was not
exercised in this capture.

The [18:14:02 draw/geometry retry](boot-0199-run-40127381401-ui-render-geometry.log)
on 2026-10-25 used kernel `40127381401` and userspace `4c3a670`. Its original
65,739-byte console was captured after self-reset; Android reported Watchdog.
No panic or intentional diagnostic-reset marker is recorded. Separate adapter
logs confirmed native context and corner `match=1`, then identified SpringBoard's
MPS GaussianBlur device assertion. The console records display-off before those
aborts. Source inspection found inherited-watchdog service tied to display
pushes; patch 0200 separates it. Exact inherited timeout and reset subtype were
not captured. The poster registration outcome remained unresolved.

The [17:39:02 integrated UI trial](boot-0199-run-40127381401-ui-five.log) on
2026-10-25 used kernel artifact `40127381401` and userspace `34ed402`. Its original
139,081-byte console ends at manual return. The separately captured profile
reported FP16 enabled; backboardd then aborted on the internal
`MaliMetalRenderDraw device` selector. Native-display constructor loading was
observed, but its context readback retained donor geometry. The poster helper
reached LaunchServices registration; the capture ended before its phase timeout.
These observations do not establish a completed GPU frame or correct wallpaper.

The preceding [texture-copy/mipmap retry](boot-0199-run-40127381401-blit-sampler.log)
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
