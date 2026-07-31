/* 64-bit tail of Multiboot trampoline — assemble with -m64, objcopy to elf32.
 *
 * On entry (from trampoline32 ljmp): EDI=mb_magic, ESI=mb_info, EBX=entry.
 * Registers survive the far jump; avoid RIP-relative data loads (elf32 link
 * mishandles the converted R_X86_64_PC32 addends).
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

	/* EDI/ESI already hold Multiboot magic/info; EBX holds Metal entry. */
	mov %ebx, %eax
	call *%rax
1:
	hlt
	jmp 1b

	.section .note.GNU-stack,"",@progbits
