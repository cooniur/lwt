.text
.align 16
.globl __lwt_trampoline

__lwt_trampoline:
	
	jmp __lwt_start
	
	// Should never get here, otherwise access violation at memory address 0x00
	movl $0, %eax
	movl (%eax), %ebx
