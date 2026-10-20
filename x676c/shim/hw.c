#include "hw.h"

uint64_t smc(uint64_t fn, uint64_t a1, uint64_t a2, uint64_t a3)
{
	register uint64_t x0 __asm__("x0") = fn;
	register uint64_t x1 __asm__("x1") = a1;
	register uint64_t x2 __asm__("x2") = a2;
	register uint64_t x3 __asm__("x3") = a3;
	__asm__ volatile("smc #0"
			 : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)
			 :
			 : "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11",
			   "x12", "x13", "x14", "x15", "x16", "x17", "memory");
	return x0;
}

int current_el(void)
{
	return rsr("CurrentEL") >> 2 & 3;
}

void wdt_kick(void)
{
	wr32(WDT_RESTART, WDT_RESTART_KEY);
}

static uint64_t now(void)
{
	isb();
	return rsr("cntpct_el0");
}

void wait_ms(unsigned ms)
{
	uint64_t freq = rsr("cntfrq_el0");
	uint64_t end;

	if (!freq)
		freq = CNTFRQ_FALLBACK;
	end = now() + freq / 1000 * ms;
	while (now() < end)
		wdt_kick();
}

void reboot(void)
{
	__asm__ volatile("dsb sy" ::: "memory");
	smc(PSCI_SYSTEM_RESET, 0, 0, 0);
	for (;;)
		__asm__ volatile("wfe");
}
