/* 64-bit entry + Multiboot2 32-bit bootstrap for Metal BIOS. */
	.section .bootinfo, "a"
	.align 8
	.globl metal_bootinfo
metal_bootinfo:
	.long 0x4C54454D          /* 'METL' */
	.long 0
	.quad pm_metal_bios_entry64
	/* Trampoline fills these after Multiboot1 handoff (phys 0x100010). */
	.globl metal_boot_magic, metal_boot_info
metal_boot_magic:
	.long 0
metal_boot_info:
	.long 0

	.section .multiboot2, "a"
	.align 8
mb2_header_start:
	.long 0xE85250D6
	.long 0
	.long mb2_header_end - mb2_header_start
	.long -(0xE85250D6 + 0 + (mb2_header_end - mb2_header_start))
	.align 8
	.word 1
	.word 0
	.long 16
	.long 6
	.long 8
	.align 8
	.word 5
	.word 1
	.long 20
	.long 1024
	.long 768
	.long 32
	.align 8
	.word 0
	.word 0
	.long 8
mb2_header_end:

	.section .text.boot, "ax"
	.code32
	.globl _start
	.type _start, @function
_start:
	cli
	mov %eax, mb_magic
	mov %ebx, mb_info
	mov %eax, metal_boot_magic
	mov %ebx, metal_boot_info

	mov $boot_pml4, %edi
	xor %eax, %eax
	mov $(4096*6/4), %ecx
	rep stosl
	mov $boot_pml4, %eax
	mov $boot_pdpt, %ecx
	or $0x03, %ecx
	mov %ecx, (%eax)

	xor %ebx, %ebx
2:
	mov $boot_pd, %eax
	mov %ebx, %ecx
	shl $12, %ecx
	add %ecx, %eax
	or $0x03, %eax
	mov %eax, boot_pdpt(,%ebx,8)

	mov $boot_pd, %edi
	add %ecx, %edi
	xor %ecx, %ecx
3:
	mov %ebx, %eax
	shl $30, %eax
	mov %ecx, %edx
	shl $21, %edx
	add %edx, %eax
	or $0x83, %eax
	mov %eax, (%edi,%ecx,8)
	inc %ecx
	cmp $512, %ecx
	jb 3b

	inc %ebx
	cmp $4, %ebx
	jb 2b

	mov $boot_pml4, %eax
	mov %eax, %cr3
	mov %cr4, %eax
	or $0x20, %eax
	mov %eax, %cr4
	mov $0xC0000080, %ecx
	rdmsr
	or $0x100, %eax
	wrmsr
	mov %cr0, %eax
	or $0x80000001, %eax
	mov %eax, %cr0
	lgdt gdt64_desc
	ljmp $0x08, $pm_metal_bios_entry64

	.code64
	.globl pm_metal_bios_entry64
	.type pm_metal_bios_entry64, @function
pm_metal_bios_entry64:
	mov $0x10, %ax
	mov %ax, %ds
	mov %ax, %es
	mov %ax, %ss
	mov %ax, %fs
	mov %ax, %gs
	lea boot_stack_top(%rip), %rsp
	and $-16, %rsp

	/* Enable SSE (clang freestanding uses movups etc.). */
	mov %cr0, %rax
	and $~0x4, %rax          /* clear EM */
	or $0x2, %rax            /* set MP */
	mov %rax, %cr0
	mov %cr4, %rax
	or $(0x200 | 0x400), %rax /* OSFXSR | OSXMMEXCPT */
	mov %rax, %cr4

	/* Prefer trampoline handoff in .bootinfo; else Multiboot regs / locals. */
	movl metal_boot_magic(%rip), %eax
	test %eax, %eax
	jz 1f
	movl %eax, %edi
	movl metal_boot_info(%rip), %esi
	jmp 2f
1:
	cmpl $0, mb_magic(%rip)
	je 2f
	movl mb_magic(%rip), %edi
	movl mb_info(%rip), %esi
2:
	call pm_metal_bios_main
3:
	hlt
	jmp 3b

	.section .data
	.align 4096
boot_pml4:
	.space 4096
boot_pdpt:
	.space 4096
boot_pd:
	.space 4096 * 4
	.align 16
gdt64:
	.quad 0
	.quad 0x00AF9A000000FFFF
	.quad 0x00AF92000000FFFF
gdt64_end:
gdt64_desc:
	.word gdt64_end - gdt64 - 1
	.long gdt64
mb_magic:
	.long 0
mb_info:
	.long 0

	.section .bss
	.align 16
boot_stack:
	.space 65536
boot_stack_top:

	.section .note.GNU-stack,"",@progbits
