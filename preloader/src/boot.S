.extern _stack_end

_start:
	ldr sp, =_stack_end
	blx init_ram
	blx start_kernel
_trap_loop:
	b _trap_loop
