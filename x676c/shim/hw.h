#pragma once
#include <stdint.h>

/* MT6789 (Infinix X676C) addresses, from the live Android FDT. */
#define TOPRGU_BASE	0x10007000UL	/* 10007000.watchdog */
#define WDT_RESTART	(TOPRGU_BASE + 0x08)
#define WDT_RESTART_KEY	0x1971U

#define PSCI_CPU_ON		0xc4000003UL
#define PSCI_SYSTEM_RESET	0x84000009UL
#define CNTFRQ_FALLBACK		13000000UL	/* MT6789: BogoMIPS 26 -> 13 MHz */

#define rsr(r) ({ uint64_t _v; __asm__ volatile("mrs %0, " r : "=r"(_v)); _v; })
#define isb() __asm__ volatile("isb" ::: "memory")

static inline uint32_t rd32(uintptr_t a) { return *(volatile uint32_t *)a; }
static inline void wr32(uintptr_t a, uint32_t v) { *(volatile uint32_t *)a = v; }

uint64_t smc(uint64_t fn, uint64_t a1, uint64_t a2, uint64_t a3);
int current_el(void);
void wdt_kick(void);
/* Busy-wait, kicking the watchdog so LK's timer does not bite meanwhile. */
void wait_ms(unsigned ms);
/* PSCI reset; if TF-A refuses, stop kicking and let the watchdog bite. */
void __attribute__((noreturn)) reboot(void);
