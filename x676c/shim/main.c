/*
 * x676c shim: boot an xnu payload on the Infinix X676C (MT6789).
 *
 *   1. log CPU state to the ramoops console zone (plog.c); read it in
 *      Android from /sys/fs/pstore/console-ramoops-0
 *   2. start the display pump on the second A76 (display.c): it pushes
 *      command-mode frames and kicks the MTK watchdog for xnu, then resets
 *      the board after PUMP_LIFETIME_S so a hung boot falls back to Android
 *   3. hand over to xnu (loader.c); on error paint RED and reset
 *
 * Any CPU exception logs ESR/ELR/FAR and resets.
 */
#include <stdint.h>
#include "display.h"
#include "hw.h"
#include "loader.h"
#include "plog.h"

#define PUMP_LIFETIME_S		120
#define ERROR_HOLD_MS		5000

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

void shim_main(uint64_t dtb)
{
	plog_init();
	plog_str("x676c shim: xnu loader\n");
	log_cpu(dtb);
	plog_hex("pump psci", (uint64_t)pump_start(PUMP_LIFETIME_S));

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
