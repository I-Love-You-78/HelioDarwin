# Display Pipeline: Native MediaTek DSI Hardware Scanout

## 1. Overview in Plain English

In consumer smartphones, displaying a smooth 60 or 120 FPS picture requires a dedicated display controller communicating with the screen panel over high-speed differential lanes called **MIPI DSI (Display Serial Interface)**.

There are two primary modes for mobile screens:
1. **Video Mode**: The display processor continuously streams pixels to the screen on every refresh cycle.
2. **Command Mode (CMD)**: The screen panel has its own built-in frame memory (GRAM). The display processor only transmits pixels when the image actually changes, using a synchronization signal called **TE (Tearing Effect)** or hardware **MUTEX**.

The Infinix Note 12 2023 features an **ili7835 AMOLED panel (1080×2400)** operating in **Command Mode**. 

Earlier experimental ports relied on software CPU pump loops (having the CPU repeatedly copy memory in a background thread), which caused high CPU usage, stuttering, and display freezes. HelioDarwin completely eliminates CPU display pumping by implementing direct hardware scanout in the kernel driver `AppleCLCD` / `SweetFramebuffer`.

---

## 2. Hardware Subsystems Involved

| Component | Physical Address | Role |
| :--- | :--- | :--- |
| **`DISP_MUTEX0`** | `0x14001000` | Coordinates frame synchronization and atomic hardware trigger. |
| **`DSI0`** | `0x14013000` | MediaTek MIPI DSI controller engine. |
| **`ili7835 Panel`** | Connected via MIPI DSI | 1080×2400 AMOLED display with integrated command-mode GRAM. |

---

## 3. Hardware Scanout Sequence (`mtkDsiPush`)

In `iokit/Kernel/arm/SweetFramebuffer.cpp`, every frame update triggers `mtkDsiPush()`:

```text
[ CoreAnimation / SpringBoard ]
         │
         ▼
[ IOSurface / SweetSurface Framebuffer ]
         │ (Double-buffered contiguous physical RAM)
         ▼
[ AppleCLCD::vsyncFired() (60 Hz Timer Tick) ]
         │
         ├── Poll DSI0_INTSTA (0x1401300c) -> Wait until previous transfer completes
         ├── Write DISP_MUTEX0_EN (0x14001020) = 1 -> Arm hardware mutex
         ├── Write DSI0_START (0x14013000) = 1 -> Trigger DMA push over MIPI DSI
         └── Hardware pushes frame directly to ili7835 GRAM at 60 Hz
```

### Driver Implementation
```cpp
static void mtkDsiPush(void)
{
    if (g_dsi0 == NULL || g_mutex0 == NULL) return;

    /* Check if DSI engine is busy */
    uint32_t sta = g_dsi0[0x000c / 4]; /* DSI_INTSTA */
    if (sta & 0x1) {
        g_dsi0[0x000c / 4] = 0x1;       /* Clear completion status */
    }

    /* Trigger hardware display mutex to transmit DMA buffer */
    g_mutex0[0x0020 / 4] = 1;          /* DISP_MUTEX0_EN */
    g_dsi0[0x0000 / 4] = 0;            /* Reset DSI START */
    g_dsi0[0x0000 / 4] = 1;            /* Assert DSI START */
}
```

---

## 4. Resolution and Scaling

* **Physical Panel Resolution:** 1080×2400 (20:9 aspect ratio, 393 ppi).
* **iOS Viewport Emulation:** iOS 16 userland expects the native iPhone X canvas (`1125×2436`).
* `AppleCLCD` centers the 1080×2400 panel window onto the iOS surface canvas with 16-byte alignment, ensuring that SpringBoard, UIKit elements, and status bar icons render with pixel-perfect clarity.
