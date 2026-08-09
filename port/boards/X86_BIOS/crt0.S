/* Multiboot1 ELF32 entry — protected mode only (no long mode). */
	.section .multiboot, "a"
	.align 4
mb_header:
	.long 0x1BADB002
	.long 0x00000003
	.long -(0x1BADB002 + 0x00000003)

	.section .text.boot, "ax"
	.code32
	.globl _start
	.type _start, @function
_start:
	cli
	movl $boot_stack_top, %esp
	andl $-16, %esp

	/* Enable SSE lightly (clang freestanding uses movups etc.). */
	movl %cr0, %eax
	andl $~0x4, %eax
	orl $0x2, %eax
	movl %eax, %cr0
	movl %cr4, %eax
	orl $(0x200 | 0x400), %eax
	movl %eax, %cr4

	/* Multiboot: %eax = magic, %ebx = info ptr (cdecl stack args). */
	pushl %ebx
	pushl %eax
	call pm_metal_bios_main
1:
	hlt
	jmp 1b

	.section .bss
	.align 16
boot_stack:
	.space 65536
boot_stack_top:

	.section .note.GNU-stack,"",@progbits
