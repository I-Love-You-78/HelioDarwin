/*
 * Act as iBoot for xnu (port of topaz shim/loader.c to MT6789).
 *
 * tools/mkpayload.py already resolved the kernel's chained fixups and
 * mkadt.py built the ADT, so this only has to: find the payload in LK's
 * initrd, copy the segments to phys_base + (vmaddr - virt_base), place the
 * ADT and boot_args behind the kernel, and jump to the entry point with the
 * MMU and caches off, x0 = boot_args (physical).
 *
 * MT6789 LK passes a boot image v4 initrd as vendor ramdisk (lz4) followed
 * by ours, so the payload sits somewhere inside /chosen linux,initrd-*.
 */
#include <stdint.h>
#include "display.h"
#include "fdt.h"
#include "loader.h"
#include "plog.h"

#define BOOT_LINE_LENGTH	608
#define PAGE_16K		0x4000UL
#define PAYLOAD_VERSION		1

/*
 * DRAM xnu may own: rank 1, between the 4K dramc training pages that close
 * each rank (rk0 0x13ffff000, rk1 0x23ffff000); every other fixed MediaTek
 * carve-out is below 4 GiB. Must match mkadt.py.
 */
#define KERNEL_RAM_LO		0x140000000UL
#define KERNEL_RAM_HI		0x23ffff000UL

struct payload_seg {
	uint64_t vmaddr, vmsize, file_off, file_size;
};

struct payload {
	char magic[8];
	uint32_t version, nsegs;
	uint64_t virt_base, entry_va, adt_off, adt_size, kernel_end;
	uint64_t phys_base, mem_size;
	char cmdline[BOOT_LINE_LENGTH];
	struct payload_seg segs[];
};

/* pexpert/pexpert/arm64/boot.h, Revision/Version 2 */
struct boot_video {
	uint64_t v_baseAddr, v_display, v_rowBytes, v_width, v_height, v_depth;
};

struct boot_args {
	uint16_t Revision, Version;
	uint64_t virtBase, physBase, memSize, topOfKernelData;
	struct boot_video Video;
	uint32_t machineType;
	uint64_t deviceTreeP;
	uint32_t deviceTreeLength;
	char CommandLine[BOOT_LINE_LENGTH];
	uint64_t bootFlags, memSizeActual;
};

_Static_assert(__builtin_offsetof(struct payload, segs) == 0x2a8, "payload header layout");
_Static_assert(__builtin_offsetof(struct boot_args, Video) == 40, "boot_args layout");
_Static_assert(__builtin_offsetof(struct boot_args, CommandLine) == 108, "boot_args layout");
_Static_assert(sizeof(struct boot_args) == 736, "boot_args layout");

/* MMU off: everything is Device memory, so copy in aligned 8-byte words. */
static void copy64(uintptr_t dst, uintptr_t src, uint64_t n)
{
	volatile uint64_t *d = (volatile uint64_t *)dst;
	volatile const uint64_t *s = (volatile const uint64_t *)src;
	for (uint64_t i = 0; i < (n + 7) / 8; i++)
		d[i] = s[i];
}

static void zero64(uintptr_t dst, uint64_t n)
{
	volatile uint64_t *d = (volatile uint64_t *)dst;
	for (uint64_t i = 0; i < n / 8; i++)
		d[i] = 0;
}

static int magic_ok(uintptr_t a)
{
	static const char want[] = "SWEETXNU";
	volatile const char *m = (volatile const char *)a;

	for (int i = 0; i < 8; i++)
		if (m[i] != want[i])
			return 0;
	return 1;
}

/* Little-endian loads from a possibly unaligned address (Device memory). */
static uint64_t ld_le(uintptr_t a, int bytes)
{
	volatile const uint8_t *b = (volatile const uint8_t *)a;
	uint64_t v = 0;

	for (int i = bytes - 1; i >= 0; i--)
		v = v << 8 | b[i];
	return v;
}

/*
 * The vendor ramdisk ends unaligned, so ours may start at any byte. Scan
 * byte by byte and accept a hit only if its header checks out in place;
 * then move it down to the 8-byte boundary below (those bytes belong to the
 * unused vendor ramdisk). Nothing is moved for a false magic match.
 */
static uintptr_t find_payload(uint64_t rd_start, uint64_t *rd_end)
{
	for (uintptr_t a = rd_start; a + sizeof(struct payload) <= *rd_end; a++) {
		if (!magic_ok(a))
			continue;
		uint64_t n = *rd_end - a;
		uint64_t adt_off = ld_le(a + __builtin_offsetof(struct payload, adt_off), 8);
		uint64_t adt_size = ld_le(a + __builtin_offsetof(struct payload, adt_size), 8);

		if (ld_le(a + __builtin_offsetof(struct payload, version), 4) != PAYLOAD_VERSION ||
		    (adt_off & 7) || adt_off + adt_size > n)
			continue;

		uintptr_t dst = a & ~7UL;	/* never above the source: forward copy is safe */
		if (a != dst) {
			volatile uint8_t *d = (volatile uint8_t *)dst;
			volatile const uint8_t *src = (volatile const uint8_t *)a;
			for (uint64_t i = 0; i < n; i++)
				d[i] = src[i];
		}
		*rd_end = dst + n;
		return dst;
	}
	return 0;
}

typedef void (*xnu_entry_t)(uint64_t boot_args_pa);

const char *loader_boot(uintptr_t dtb)
{
	uint64_t rd_start, rd_end;

	if (!fdt_chosen_u64(dtb, "linux,initrd-start", &rd_start) ||
	    !fdt_chosen_u64(dtb, "linux,initrd-end", &rd_end))
		return "no initrd in /chosen";
	plog_hex("initrd lo", rd_start);
	plog_hex("initrd hi", rd_end);

	uintptr_t base = find_payload(rd_start, &rd_end);
	if (!base)
		return "no SWEETXNU payload";
	const struct payload *p = (const struct payload *)base;
	plog_hex("payload", base);

	uint64_t pa_of_va_base = p->phys_base - p->virt_base;	/* pa = va + this */
	plog_hex("phys base", p->phys_base);
	plog_hex("mem size", p->mem_size);
	plog_hex("virt base", p->virt_base);
	plog_hex("entry va", p->entry_va);

	if (p->phys_base < KERNEL_RAM_LO || p->phys_base + p->mem_size > KERNEL_RAM_HI)
		return "kernel RAM outside the carve-out-free range";
	if (rd_end > p->phys_base && rd_start < p->phys_base + p->mem_size)
		return "initrd overlaps kernel memory";

	for (uint32_t i = 0; i < p->nsegs; i++) {
		const struct payload_seg *s = &p->segs[i];
		uintptr_t dst = s->vmaddr + pa_of_va_base;
		uint64_t copied = (s->file_size + 7) & ~7UL;

		copy64(dst, base + s->file_off, s->file_size);
		if (s->vmsize > copied)
			zero64(dst + copied, s->vmsize - copied);
	}
	plog_hex("segments", p->nsegs);

	/* ADT, then boot_args, behind the kernel; everything below topOfKernelData is ours. */
	uintptr_t adt_pa = p->kernel_end + pa_of_va_base;
	copy64(adt_pa, base + p->adt_off, p->adt_size);
	uintptr_t ba_pa = (adt_pa + p->adt_size + PAGE_16K - 1) & ~(PAGE_16K - 1);
	uintptr_t top = ba_pa + PAGE_16K;

	zero64(ba_pa, PAGE_16K);
	volatile struct boot_args *ba = (volatile struct boot_args *)ba_pa;
	ba->Revision = 2;
	ba->Version = 2;
	ba->virtBase = p->virt_base;
	ba->physBase = p->phys_base;
	ba->memSize = p->mem_size;
	ba->topOfKernelData = top;
	ba->Video.v_baseAddr = FB_BASE;
	ba->Video.v_display = 1;
	ba->Video.v_rowBytes = FB_STRIDE;
	ba->Video.v_width = FB_W;
	ba->Video.v_height = FB_H;
	ba->Video.v_depth = 32;
	ba->deviceTreeP = adt_pa - pa_of_va_base;	/* xnu reads it with the MMU on: a VA */
	ba->deviceTreeLength = (uint32_t)p->adt_size;
	for (int i = 0; i < BOOT_LINE_LENGTH; i++)
		ba->CommandLine[i] = p->cmdline[i];
	ba->memSizeActual = p->mem_size;
	plog_hex("boot_args", ba_pa);
	plog_hex("top", top);

	uintptr_t entry_pa = p->entry_va + pa_of_va_base;
	plog_hex("jump", entry_pa);

	/* Everything was written with the MMU and caches off; drop any stale I-cache lines too. */
	__asm__ volatile("dsb sy; ic iallu; dsb sy; isb" ::: "memory");
	((xnu_entry_t)entry_pa)(ba_pa);
	return "xnu returned";
}
