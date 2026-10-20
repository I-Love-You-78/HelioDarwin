ENTRY(_head)
SECTIONS
{
	. = 0;
	.text : { *(.text.head) *(.text*) }
	.rodata : { *(.rodata*) }
	/*
	 * bss and the stack live inside .data so they are real zero bytes in
	 * the Image: LK copies the file and nothing zeroes memory for us.
	 */
	.data : {
		*(.data*)
		*(.bss*)
		*(COMMON)
		. = ALIGN(16);
		. += 0x4000;
		_stack_top = .;
		QUAD(0)
	}
	. = ALIGN(4096);
	_end = .;
	/DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
