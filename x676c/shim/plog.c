/*
 * ramoops console zone writer. Layout from the X676C kernel parameters:
 * mem_address 0x48090000, mem_size 0xe0000, record 0x1000, console 0x40000,
 * ftrace 0x1000, pmsg 0x10000 -> dmesg zones take 0x8f000, console follows.
 * Zone header is struct persistent_ram_buffer { sig, start, size } and the
 * kernel accepts it when sig == PERSISTENT_RAM_SIG (ecc=0).
 */
#include "plog.h"

#define PRZ_BASE	0x4811f000UL
#define PRZ_SIZE	0x40000UL
#define PRZ_SIG		0x43474244U	/* "DBGC" */
#define PRZ_DATA	(PRZ_BASE + 12)
#define PRZ_CAP		(PRZ_SIZE - 12)

static volatile uint32_t *const hdr = (volatile uint32_t *)PRZ_BASE;
static uint32_t pos;

static void put(char c)
{
	if (pos >= PRZ_CAP)
		return;
	*(volatile char *)(PRZ_DATA + pos) = c;
	pos++;
	hdr[1] = pos;	/* start: next write offset */
	hdr[2] = pos;	/* size: bytes valid */
}

void plog_init(void)
{
	pos = 0;
	hdr[0] = PRZ_SIG;
	hdr[1] = 0;
	hdr[2] = 0;
}

void plog_str(const char *s)
{
	while (*s)
		put(*s++);
}

static void put_hex(uint64_t v, int digits)
{
	for (int i = digits - 1; i >= 0; i--) {
		unsigned d = (v >> (i * 4)) & 0xf;
		put(d < 10 ? '0' + d : 'a' + d - 10);
	}
}

void plog_hex(const char *label, uint64_t v)
{
	plog_str(label);
	plog_str("=0x");
	put_hex(v, 16);
	put('\n');
}

void plog_regs(const char *label, uintptr_t base, unsigned n)
{
	plog_str("-- ");
	plog_str(label);
	put('\n');
	for (unsigned i = 0; i < n; i += 4) {
		put_hex(base + i * 4, 8);
		put(':');
		for (unsigned j = i; j < i + 4 && j < n; j++) {
			put(' ');
			put_hex(*(volatile uint32_t *)(base + j * 4), 8);
		}
		put('\n');
	}
}
