/*
 * x676c shim: boot an xnu payload on the Infinix X676C (MT6789).
 *
 *   1. log CPU state to the ramoops console zone (plog.c); read it in
 *      Android from /sys/fs/pstore/console-ramoops-0
 *   2. start the display pump on the second A76 (display.c): it pushes
 *      command-mode frames and kicks the MTK watchdog for xnu, then resets
 *      the board after PUMP_LIFETIME_S so a hung boot falls back to Android
 *   3. draw the boot splash (splash.c) on the visible layer, the way the
 *      xnu console expects to find it: black with the apple in the middle
 *   4. hand over to xnu (loader.c); on error paint RED and reset
 *
 * Any CPU exception logs ESR/ELR/FAR and resets.
 */
#include <stdint.h>
#include "display.h"
#include "hw.h"
#include "loader.h"
#include "plog.h"
#include "splash.h"

#define PUMP_LIFETIME_S		1800
#define ERROR_HOLD_MS		5000

#define UFSHCI_BASE		0x11270000UL	/* ufshci@11270000, mediatek,mt8183-ufshci */

extern char _head[];

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

/*
 * The UFS host as LK left it (LK read boot_a through it): standard UFSHCI
 * registers and the MediaTek vendor block, for the xnu UFS driver design.
 */
static void log_ufs(void)
{
	plog_regs("ufshci std", UFSHCI_BASE, 0x40);
	plog_regs("ufshci mtk 0x140", UFSHCI_BASE + 0x140, 0x8);
	plog_regs("ufshci mtk 0x2100", UFSHCI_BASE + 0x2100, 0x4);
	plog_regs("ufshci mtk 0x2200", UFSHCI_BASE + 0x2200, 0x34);
}

void shim_main(uint64_t dtb)
{
	plog_init();
	plog_str("x676c shim: xnu loader\n");
	log_cpu(dtb);
	log_ufs();
	plog_hex("pump psci", (uint64_t)pump_start(PUMP_LIFETIME_S));
	splash_draw((volatile uint32_t *)FB_BASE, FB_STRIDE / 4, FB_W, FB_H);
	plog_str("splash drawn\n");

	const char *why = loader_boot(dtb);

	plog_str("loader: ");
	plog_str(why);
	plog_str("\n");
	fb_fill(RED);
	wait_ms(ERROR_HOLD_MS);
	plog_str("reset\n");
	reboot();
}

void shim_exception(void)
{
	plog_str("EXCEPTION\n");
	plog_hex("mpidr", rsr("mpidr_el1"));
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
	plog_str("reset\n");
	reboot();
}
