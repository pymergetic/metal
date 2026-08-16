/* 64-bit tail of Multiboot trampoline — assemble with -m64, objcopy to elf32.
 *
 * On entry (from trampoline32 ljmp): EDI=mb_magic, ESI=mb_info, EBX=entry.
 */
	.code64
	.globl long_mode
long_mode:
	mov $0x10, %ax
	mov %ax, %ds
	mov %ax, %es
	mov %ax, %ss
	mov %ax, %fs
	mov %ax, %gs
	mov $0x90000, %rsp
	and $-16, %rsp

	mov %ebx, %eax
	call *%rax
1:
	hlt
	jmp 1b

	.section .note.GNU-stack,"",@progbits
