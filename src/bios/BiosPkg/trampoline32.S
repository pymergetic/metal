/* 32-bit Multiboot1 trampoline for QEMU -kernel (ELF32 only).
 *
 * Linked at 1MiB; embeds Metal ELF64 and copies it to 4MiB. Stack at 0x90000.
 * long_mode is in trampoline64.S (assembled -m64, objcopy→elf32).
 */
	.section .multiboot, "a"
	.align 4
	.long 0x1BADB002
	.long 0x00000003
	.long -(0x1BADB002 + 0x00000003)

	.equ TRAMP_STACK, 0x90000

	.section .text
	.code32
	.globl _start
	.extern long_mode
_start:
	cli
	mov $TRAMP_STACK, %esp
	mov %eax, mb_magic
	mov %ebx, mb_info

	/* Identity map 0..4GiB with 2MiB pages (covers PCI MMIO near 4GiB). */
	mov $boot_pml4, %edi
	xor %eax, %eax
	mov $(4096*6/4), %ecx
	rep stosl

	mov $boot_pdpt, %eax
	or $0x03, %eax
	mov %eax, boot_pml4

	xor %ebx, %ebx
pdpt_loop:
	mov $boot_pd, %eax
	mov %ebx, %ecx
	shl $12, %ecx
	add %ecx, %eax
	or $0x03, %eax
	mov %eax, boot_pdpt(,%ebx,8)

	mov $boot_pd, %edi
	add %ecx, %edi
	xor %ecx, %ecx
pd_loop:
	mov %ebx, %eax
	shl $30, %eax
	mov %ecx, %edx
	shl $21, %edx
	add %edx, %eax
	or $0x83, %eax
	mov %eax, (%edi,%ecx,8)
	inc %ecx
	cmp $512, %ecx
	jb pd_loop

	inc %ebx
	cmp $4, %ebx
	jb pdpt_loop

	call trampoline_load_elf64
	test %eax, %eax
	jz halt
	mov %eax, entry32

	/* Enter IA-32e: PAE → CR3 → EFER.LME → PG → far jump */
	mov %cr4, %eax
	or $0x20, %eax
	mov %eax, %cr4

	mov $boot_pml4, %eax
	mov %eax, %cr3

	mov $0xC0000080, %ecx
	rdmsr
	or $0x100, %eax
	wrmsr

	mov %cr0, %eax
	or $0x80000000, %eax
	mov %eax, %cr0

	lgdt gdt64_desc
	mov mb_magic, %edi
	mov mb_info, %esi
	mov entry32, %ebx
	ljmp $0x08, $long_mode

halt:
	hlt
	jmp halt

	.data
	.align 4
	.globl mb_magic, mb_info, entry32
mb_magic: .long 0
mb_info: .long 0
entry32: .long 0
	.align 16
gdt64:
	.quad 0
	.quad 0x00AF9A000000FFFF
	.quad 0x00CF92000000FFFF
gdt64_end:
gdt64_desc:
	.word gdt64_end - gdt64 - 1
	.long gdt64
	.align 4096
boot_pml4: .space 4096
boot_pdpt: .space 4096
boot_pd: .space 4096 * 4

	.section .note.GNU-stack,"",@progbits
