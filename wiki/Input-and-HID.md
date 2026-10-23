# Input and HID

X676C input support has been implemented and compiled locally to ARM64 objects. **Physical touch, button delivery and iOS gestures are not yet validated with this implementation.** Hardware identity and wiring below are established by Android's active driver binding, input metadata, device tree and vendor driver behavior.

## Touchscreen

The actual controller is **FocalTech FT5452J**, chip ID `0x5452`, using Android module `ft3519_common`. The module filename does not identify an FT3519 controller. Android binds `mtk-tpd` at I2C device `0-0038`; merely loading a Goodix module does not make Goodix the active controller.

| Connection / limit | X676C value |
| --- | --- |
| Controller bus | I2C0, base `0x11e00000`, address `0x38` |
| APDMA channel | `0x10217080` |
| IRQ | GPIO9 / EINT9, falling edge in Android |
| Reset | GPIO152 |
| Power enable | GPIO62 |
| I2C pin functions | SCL0 on GPIO130; SDA0 on GPIO131 |
| Contact limit | Five slots |
| Native coordinates | X `0..1079`, Y `0..2399` |

The MT6789 I2C controller uses slave-address register `0x94` and a 16-byte FIFO. The driver reads each 32-byte touch report in one combined pointer-write/read APDMA transaction, with a contiguous bounce buffer, cache synchronization and bounded error recovery. FT5452J reports start at register `1`, with six-byte contact records; they do not use the earlier Topaz coordinate scaling. Controller selection requires the expected chip ID. No touch firmware upload is performed by XNU.

The touch thread polls at 10 ms while contacts are active, uses GPIO9 as a report hint, and also reads periodically to recover a missed edge. This does not claim that the Android IRQ route has been installed as a native XNU interrupt handler.

## Physical buttons

| Button | Hardware source | iOS event |
| --- | --- | --- |
| Power / side | MT6366 PMIC `TOPSTATUS` register `0x28`, active-low bit 1 | Consumer Power keyboard event |
| Volume up | Keypad `MEM1` bit 0, active low | Consumer Volume Increment keyboard event |
| Volume down | Keypad `MEM1` bit 1, active low | Consumer Volume Decrement keyboard event |

Android exposes power through `mtk-pmic-keys`, with PMIC interrupt 48 for press and 50 for release. It exposes the volume buttons separately through `mtk-kpd`. XNU currently polls those hardware states every 20 ms. Matrix bit 8 is not used as a substitute power button.

`sweet_pwrap.c` provides serialized, bounded MT6789 SWINF reads/writes shared with GPU power setup. Input reads the power debounce register; it does not reprogram PMIC regulators, interrupt routing or long-press reset configuration. See [GPU Power Sequence](MT6789-GPU-Power-Sequence.md) for the shared bus and regulator details.

## Startup and iOS delivery

The active `rd=ufs` path now starts touch and keys from `bsd/dev/ufs/ufs_disk.c` after UFS bring-up. Previously those startup calls existed only on the older `rd=ufsroot0` path, so publishing HID services did not start the hardware readers. Each input initializer has a once guard; touch hardware access is delayed by five seconds by default.

`SweetHIDTouch` publishes a built-in, display-integrated digitizer service. Each hardware scan becomes one collection containing all active and lifted finger paths, with normalized coordinates and aggregate event state. `SweetHIDButtons` publishes the consumer-control service. Events use `IOHIDEventServiceUserClient` and its serialized event queue, following the [Apple IOHIDFamily collection contract](https://github.com/apple-oss-distributions/IOHIDFamily/blob/IOHIDFamily-1915.140.3/IOHIDFamily/IOHIDEventDriver.cpp).

State is retained until a HID client is ready, and incomplete event allocation is reported as an error. There is no synthetic unlock swipe or periodic Home-button generator. Coordinates must remain consistent with the [logical display profile](Display-Pipeline.md); a framebuffer or UI geometry change requires checking that mapping too.

## Controls and validation boundary

- `sweettouch=0` disables touch; `sweetkeys=0` disables button polling.
- `sweettouchdelay=N` changes the touch startup delay in seconds.
- `sweetfb_stayon=1` deliberately keeps the physical display on, so lack of panel blanking alone does not prove a missing power event.

The five affected translation units, including startup and shared PWRAP access, compile to ARM64 objects. That verifies source integration, not controller transfers or end-to-end iOS input on hardware. Those remain the next physical validation step.

Source integration: [shared PWRAP patch 0191](https://github.com/squeeeezy/HelioDarwin/blob/x676c/xnu/patches/0191-x676c-shared-pwrap-swinf.patch) and [input patch 0192](https://github.com/squeeeezy/HelioDarwin/blob/x676c/xnu/patches/0192-x676c-real-touch-and-keys.patch).
