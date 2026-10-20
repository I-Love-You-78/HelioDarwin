/*
 * Stand-in for xnu to test the shim's loader without a kernel build.
 * mkstub.py wraps this into a Mach-O shaped like the kernelcache (__TEXT,
 * LC_UNIXTHREAD), mkpayload.py packs it, and the shim jumps here exactly as
 * it would into xnu: MMU off, EL1, x0 = boot_args (physical).
 *
 * It appends what it received to the ramoops console zone behind the
 * shim's own log, then resets: /sys/fs/pstore/console-ramoops-0 in Android.
 */
#include <stdint.h>

#define PRZ_BASE	0x4811f000UL	/* same zone as shim/plog.c */
#define PRZ_CAP		(0x40000UL - 12)
#define WDT_RESTART	0x10007008UL
#define PSCI_SYSTEM_RESET 0x84000009UL

struct boot_args {
	uint16_t Revision, Version;
	uint64_t virtBase, physBase, memSize, topOfKernelData;
	uint64_t v_baseAddr, v_display, v_rowBytes, v_width, v_height, v_depth;
	uint32_t machineType;
	uint64_t deviceTreeP;
	uint32_t deviceTreeLength;
	char CommandLine[608];
	uint64_t bootFlags, memSizeActual;
};

static volatile uint32_t *const hdr = (volatile uint32_t *)PRZ_BASE;

static void put(char c)
{
	uint32_t pos = hdr[2];

	if (pos >= PRZ_CAP)
		return;
	*(volatile char *)(PRZ_BASE + 12 + pos) = c;
	hdr[1] = pos + 1;
	hdr[2] = pos + 1;
}

static void str(const char *s)
{
	while (*s)
		put(*s++);
}

static void hex(const char *label, uint64_t v)
{
	str(label);
	str("=0x");
	for (int i = 15; i >= 0; i--) {
		unsigned d = (v >> (i * 4)) & 0xf;
		put(d < 10 ? '0' + d : 'a' + d - 10);
	}
	put('\n');
}

static void __attribute__((noreturn)) reset(void)
{
	uint64_t end;
	__asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(end));
	end += 13000000UL * 2;
	for (uint64_t t = 0; t < end;) {
		*(volatile uint32_t *)WDT_RESTART = 0x1971;
		__asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(t));
	}
	register uint64_t x0 __asm__("x0") = PSCI_SYSTEM_RESET;
	__asm__ volatile("dsb sy; smc #0" : "+r"(x0) :: "memory");
	for (;;)
		__asm__ volatile("wfe");
}

void stub_main(uint64_t ba_pa, uint64_t pc)
{
	const volatile struct boot_args *ba = (const volatile struct boot_args *)ba_pa;
	uint64_t pa_of_va = ba->physBase - ba->virtBase;
	const volatile uint32_t *adt = (const volatile uint32_t *)(ba->deviceTreeP + pa_of_va);

	str("STUB: entered as xnu\n");
	hex("pc", pc);
	hex("boot_args", ba_pa);
	hex("Revision", ba->Revision);
	hex("Version", ba->Version);
	hex("virtBase", ba->virtBase);
	hex("physBase", ba->physBase);
	hex("memSize", ba->memSize);
	hex("topOfKernelData", ba->topOfKernelData);
	hex("video", ba->v_baseAddr);
	hex("rowBytes", ba->v_rowBytes);
	hex("deviceTreeP", ba->deviceTreeP);
	hex("deviceTreeLength", ba->deviceTreeLength);
	/* ADT root: nProperties, nChildren, then the first property name ("name") */
	hex("adt nprops", adt[0]);
	hex("adt nchildren", adt[1]);
	str("cmdline: ");
	for (int i = 0; i < 608 && ba->CommandLine[i]; i++)
		put(ba->CommandLine[i]);
	str("\nSTUB: reset\n");
	reset();
}
