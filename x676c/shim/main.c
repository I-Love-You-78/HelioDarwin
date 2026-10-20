/*
 * Milestone 1c for the Infinix X676C (MT6789). The panel is a DSI command
 * mode panel (ili7835 "cmd"), so framebuffer writes alone never reach the
 * glass. 1b showed LK leaves the whole path configured (MUTEX0 modules
 * 0x147b7, DSI in CMD mode with DCS 0x2c) but idle with MUTEX0_EN = 0.
 * Visible layers: OVL0_2L L0 @ 0xfe0c8000 (pitch 1088 px) under the opaque
 * OVL0 L1 @ 0xfeabe000 (pitch 1080 px, the logo).
 *
 * This boot fills RED, GREEN, BLUE and pushes one frame after each the way
 * LK does for the logo (MUTEX0_EN, then DSI_START 0 -> 1), logging DSI state
 * to the ramoops console zone (see plog.c), then resets.
 * Read it in Android: /sys/fs/pstore/console-ramoops-0
 * Any CPU exception logs ESR/ELR/FAR and resets.
 */
#include <stdint.h>
#include "plog.h"

#define FB_BASE		0xfe0c8000UL	/* mblock-14-framebuffer reg */
#define FB_SIZE		0x01f2f000UL

#define TOPRGU_BASE	0x10007000UL	/* 10007000.watchdog */
#define WDT_RESTART	(TOPRGU_BASE + 0x08)
#define WDT_RESTART_KEY	0x1971U

#define DSI0_BASE	0x14013000UL	/* dsi@14013000 */
#define DISP_MUTEX_BASE	0x14001000UL	/* disp_mutex@14001000 */
#define OVL0_BASE	0x14005000UL	/* disp_ovl0@14005000 */
#define OVL0_2L_BASE	0x14006000UL	/* disp_ovl0_2l@14006000 */
#define OVL_ADDR_REGS	0xf40		/* MTK OVL L0..L3 ADDR block */

#define DSI_START	(DSI0_BASE + 0x00)
#define DSI_STA		(DSI0_BASE + 0x04)
#define DSI_INTSTA	(DSI0_BASE + 0x0c)	/* write 0 to clear */
#define DSI_CMDQ	(DSI0_BASE + 0xd00)
#define MUTEX0_EN	(DISP_MUTEX_BASE + 0x20)

#define PSCI_SYSTEM_RESET	0x84000009UL
#define CNTFRQ_FALLBACK		13000000UL	/* MT6789: BogoMIPS 26 -> 13 MHz */

#define RED	0xffff0000U
#define GREEN	0xff00ff00U
#define BLUE	0xff0000ffU

#define rsr(r) ({ uint64_t _v; __asm__ volatile("mrs %0, " r : "=r"(_v)); _v; })
#define isb() __asm__ volatile("isb" ::: "memory")

extern char _head[];

static inline uint32_t rd32(uintptr_t a) { return *(volatile uint32_t *)a; }
static inline void wr32(uintptr_t a, uint32_t v) { *(volatile uint32_t *)a = v; }

static void wdt_kick(void)
{
	wr32(WDT_RESTART, WDT_RESTART_KEY);
}

static uint64_t now(void)
{
	isb();
	return rsr("cntpct_el0");
}

/* Busy-wait, kicking the watchdog so LK's timer does not bite meanwhile. */
static void wait_ms(unsigned ms)
{
	uint64_t freq = rsr("cntfrq_el0");
	uint64_t end;

	if (!freq)
		freq = CNTFRQ_FALLBACK;
	end = now() + freq / 1000 * ms;
	while (now() < end)
		wdt_kick();
}

/* MMU off = Device memory: aligned 64-bit stores only. */
static void fill(uint32_t color)
{
	volatile uint64_t *p = (volatile uint64_t *)FB_BASE;
	uint64_t v = (uint64_t)color << 32 | color;

	for (uint64_t i = 0; i < FB_SIZE / 8; i++) {
		p[i] = v;
		if (!(i & 0xfffff))
			wdt_kick();
	}
}

static uint64_t smc(uint64_t fn)
{
	register uint64_t x0 __asm__("x0") = fn;
	__asm__ volatile("smc #0"
			 : "+r"(x0)
			 :
			 : "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9",
			   "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17",
			   "memory");
	return x0;
}

/* PSCI reset; if TF-A refuses, stop kicking and let the watchdog bite. */
static void __attribute__((noreturn)) reboot(void)
{
	plog_str("reset\n");
	__asm__ volatile("dsb sy" ::: "memory");
	smc(PSCI_SYSTEM_RESET);
	for (;;)
		__asm__ volatile("wfe");
}

static int current_el(void)
{
	return rsr("CurrentEL") >> 2 & 3;
}

static void log_cpu(uint64_t dtb)
{
	plog_hex("CurrentEL", current_el());
	plog_hex("load", (uintptr_t)_head);
	plog_hex("dtb", dtb);
	plog_hex("cntfrq", rsr("cntfrq_el0"));
	plog_hex("mpidr", rsr("mpidr_el1"));
	plog_hex("midr", rsr("midr_el1"));
	plog_hex("sctlr_el1", rsr("sctlr_el1"));
	plog_hex("id_aa64mmfr0", rsr("id_aa64mmfr0_el1"));
}

/* Registers as LK left them; display clocks are still on after the logo. */
static void log_display(void)
{
	plog_regs("dsi0", DSI0_BASE, 0x80);
	plog_regs("dsi0 cmdq", DSI0_BASE + 0x200, 0x20);
	plog_regs("disp_mutex", DISP_MUTEX_BASE, 0x100);
	plog_regs("ovl0", OVL0_BASE, 0x40);
	plog_regs("ovl0 addr", OVL0_BASE + OVL_ADDR_REGS, 0x10);
	plog_regs("ovl0_2l", OVL0_2L_BASE, 0x40);
	plog_regs("ovl0_2l addr", OVL0_2L_BASE + OVL_ADDR_REGS, 0x10);
}

/*
 * One command-mode frame: let the mutex start the OVL->DSI engines, kick
 * DSI, give the ~16 ms frame time to finish, then idle the mutex again as
 * LK left it.
 */
static void dsi_push_frame(void)
{
	wr32(DSI_INTSTA, 0);
	wr32(MUTEX0_EN, 1);
	wr32(DSI_START, 0);
	wr32(DSI_START, 1);
	wait_ms(50);
	plog_hex("dsi intsta", rd32(DSI_INTSTA));
	plog_hex("dsi sta", rd32(DSI_STA));
	plog_hex("mutex0 en", rd32(MUTEX0_EN));
	wr32(MUTEX0_EN, 0);
}

void shim_main(uint64_t dtb)
{
	static const uint32_t colors[] = { RED, GREEN, BLUE };

	plog_init();
	plog_str("infinix-shim 1c: alive\n");
	log_cpu(dtb);
	plog_regs("dsi0 cmdq", DSI_CMDQ, 0x10);
	for (unsigned i = 0; i < 3; i++) {
		plog_hex("fill", colors[i]);
		fill(colors[i]);
		dsi_push_frame();
		wait_ms(3000);
	}
	reboot();
}

void shim_exception(void)
{
	plog_str("EXCEPTION\n");
	if (current_el() == 2) {
		plog_hex("esr_el2", rsr("esr_el2"));
		plog_hex("elr_el2", rsr("elr_el2"));
		plog_hex("far_el2", rsr("far_el2"));
	} else {
		plog_hex("esr_el1", rsr("esr_el1"));
		plog_hex("elr_el1", rsr("elr_el1"));
		plog_hex("far_el1", rsr("far_el1"));
	}
	wait_ms(1000);
	reboot();
}
