/*
 * ili7835 DSI command-mode panel: framebuffer writes reach the glass only
 * when a frame is pushed. LK leaves the path configured (MUTEX0 modules
 * 0x147b7, DSI CMD mode, CMDQ0 = DCS 0x2c write_memory_start) but idle.
 */
#include "display.h"
#include "hw.h"

#define DSI0_BASE	0x14013000UL	/* dsi@14013000 */
#define DSI_START	(DSI0_BASE + 0x00)
#define DSI_INTSTA	(DSI0_BASE + 0x0c)	/* write 0 to clear */
#define DISP_MUTEX_BASE	0x14001000UL	/* disp_mutex@14001000 */
#define MUTEX0_EN	(DISP_MUTEX_BASE + 0x20)

#define PUMP_MPIDR	0x700UL		/* cpu@101, second A76 */
#define PUMP_PERIOD_MS	33

extern char secondary_entry[];

static volatile unsigned pump_lifetime_s;

void fb_fill(uint32_t color)
{
	volatile uint64_t *p = (volatile uint64_t *)FB_REGION;
	uint64_t v = (uint64_t)color << 32 | color;

	for (uint64_t i = 0; i < FB_REGION_SIZE / 8; i++) {
		p[i] = v;
		if (!(i & 0xfffff))
			wdt_kick();
	}
}

void dsi_push_frame(void)
{
	wr32(DSI_INTSTA, 0);
	wr32(MUTEX0_EN, 1);
	wr32(DSI_START, 0);
	wr32(DSI_START, 1);
}

uint32_t dsi_intsta(void)
{
	return rd32(DSI_INTSTA);
}

void pump_main(void)
{
	unsigned frames = pump_lifetime_s * 1000 / PUMP_PERIOD_MS;

	for (unsigned i = 0; i < frames; i++) {
		dsi_push_frame();
		wait_ms(PUMP_PERIOD_MS);
	}
	reboot();
}

int64_t pump_start(unsigned lifetime_s)
{
	pump_lifetime_s = lifetime_s;
	__asm__ volatile("dsb sy" ::: "memory");
	return (int64_t)smc(PSCI_CPU_ON, PUMP_MPIDR, (uintptr_t)secondary_entry, 0);
}
