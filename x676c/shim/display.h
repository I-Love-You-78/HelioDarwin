#pragma once
#include <stdint.h>

/*
 * LK's splash buffers (reserved-memory mblock-14-framebuffer). Two OVL layers
 * scan out of it: OVL0_2L L0 @ 0xfe0c8000 (pitch 1088 px) under the opaque
 * OVL0 L1 @ 0xfeabe000 (pitch 1080 px), so only the latter is visible.
 */
#define FB_REGION	0xfe0c8000UL
#define FB_REGION_SIZE	0x01f2f000UL
#define FB_BASE		0xfeabe000UL	/* visible top layer, handed to xnu */
#define FB_W		1080
#define FB_H		2400
#define FB_STRIDE	(FB_W * 4)	/* a8r8g8b8 */

#define RED	0xffff0000U
#define GREEN	0xff00ff00U
#define BLUE	0xff0000ffU

/* Fill the whole splash region, both layers. MMU off: Device stores. */
void fb_fill(uint32_t color);
/* One command-mode frame: MUTEX0_EN self-clears when the frame is done. */
void dsi_push_frame(void);
uint32_t dsi_intsta(void);
/*
 * Power on the last A76 (MPIDR 0x700, kept out of the ADT) to push a frame
 * every 33 ms and kick the MTK watchdog, which xnu knows nothing about.
 * After lifetime_s it resets the board so a hung boot falls back to Android.
 * Returns the PSCI status (0 = started).
 */
int64_t pump_start(unsigned lifetime_s);
