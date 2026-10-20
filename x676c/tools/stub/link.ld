ENTRY(_start)
SECTIONS
{
	. = 0;
	/* One flat blob: the Mach-O wrapper has a single __TEXT segment. */
	.text : {
		*(.text.head) *(.text*) *(.rodata*) *(.data*)
		*(.bss*) *(COMMON)
		QUAD(0)
	}
	/DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
