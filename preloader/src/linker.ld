MEMORY {

	ITCM (rx) : ORIGIN = 0x1f400000, LENGTH = 16M
	RAM (rwx) : ORIGIN = 0xa0000000, LENGTH = 16M

}

SECTIONS {

	.text : {
		src/boot.o(.text)
		. = ALIGN(4);
		*(.text*)
		. = ALIGN(4);
		*(.rodata*)
		. = ALIGN(4);
	} > ITCM

	.stack : {
		. = 0x10000;
		_stack_end = .;
	} > RAM

	.data : {
		_s_imageaddr_data = .;
		*(.data*)
		. = ALIGN(4);
	} > RAM AT > ITCM

	_s_loadaddr_data = LOADADDR(.data);
	_s_imagesize_data = SIZEOF(.data);

	.bss : {
		_s_imageaddr_bss = .;
		*(.bss*)
		. = ALIGN(4);
	} > RAM

	_s_imagesize_bss = SIZEOF(.bss);

}
