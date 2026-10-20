#pragma once
#include <stdint.h>

/*
 * Log into the ramoops console zone. DRAM survives the warm reset, so the
 * next Android boot shows it as /sys/fs/pstore/console-ramoops-0.
 */
void plog_init(void);
void plog_str(const char *s);
void plog_hex(const char *label, uint64_t v);
/* "base+off: w0 w1 w2 w3" lines for n 32-bit registers starting at base */
void plog_regs(const char *label, uintptr_t base, unsigned n);
