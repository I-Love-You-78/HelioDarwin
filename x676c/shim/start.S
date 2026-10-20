/*
 * Infinix X676C (MT6789) boot shim entry: arm64 Linux Image header so the
 * MediaTek LK treats us like a kernel. LK enters with MMU/D-cache off,
 * x0 = DTB pointer. We may land in EL1 (under GenieZone) or EL2.
 */
	.section .text.head, "ax"
	.globl _head
_head:
	b	_start			/* code0 */
	.long	0			/* code1 */
	.quad	0			/* text_offset */
	.quad	_end			/* image_size (linked at 0) */
	.quad	0xa			/* flags: LE, 4K pages, place anywhere */
	.quad	0			/* res2 */
	.quad	0			/* res3 */
	.quad	0			/* res4 */
	.ascii	"ARM\x64"		/* magic */
	.long	0			/* res5 (PE offset) */

	.text
	.globl _start
_start:
	msr	daifset, #0xf
	msr	spsel, #1
	adr	x1, _stack_top
	mov	sp, x1
	adr	x2, vectors
	mrs	x1, CurrentEL
	cmp	x1, #(2 << 2)
	b.ne	1f
	msr	vbar_el2, x2
	b	2f
1:	msr	vbar_el1, x2
2:	isb
	bl	shim_main
3:	wfe
	b	3b

/* PSCI CPU_ON target (display pump core). MMU off, EL1, own stack. */
	.globl	secondary_entry
secondary_entry:
	msr	daifset, #0xf
	msr	spsel, #1
	adr	x1, _pump_stack_top
	mov	sp, x1
	adr	x2, vectors
	msr	vbar_el1, x2
	isb
	bl	pump_main
5:	wfe
	b	5b

/* Any exception: fresh stack, magenta screen, reboot (shim_exception). */
	.balign	0x800
vectors:
	.rept	16
	.balign	0x80
	b	exc_entry
	.endr

exc_entry:
	adr	x1, _stack_top
	mov	sp, x1
	bl	shim_exception
4:	wfe
	b	4b
