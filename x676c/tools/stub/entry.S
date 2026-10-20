/* xnu-style entry: MMU off, EL1, x0 = boot_args PA. */
	.section .text.head, "ax"
	.globl	_start
_start:
	msr	daifset, #0xf
	msr	spsel, #1
	adr	x1, _stack_top
	mov	sp, x1
	adr	x1, _start		/* where the loader actually put us */
	bl	stub_main
1:	wfe
	b	1b

	.bss
	.balign	16
	.space	0x2000
_stack_top:
