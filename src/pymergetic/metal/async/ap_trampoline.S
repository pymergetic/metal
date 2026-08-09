/*
 * Relocatable AP bootstrap (copied to physical 0x8000).
 * Param page @ 0x7000:
 *   +0  u32 pml4
 *   +4  u16 gdt_limit
 *   +6  u32 gdt_base
 *   +16 u64 stack
 *   +24 u64 entry
 *   +32 u32 cpu
 *   +36 gdt (null, 32-code, data, 64-code)
 */
	.section .rodata
	.globl pm_metal_ap_tramp
	.globl pm_metal_ap_tramp_size
	.align 16
pm_metal_ap_tramp:
	.code16
	cli
	xor %ax, %ax
	mov %ax, %ds
	mov %ax, %es
	mov %ax, %ss
	mov $0x7000, %sp
	lgdtl 0x7004
	mov %cr0, %eax
	or $1, %eax
	mov %eax, %cr0
	ljmpl $0x08, $0x8000 + (1f - pm_metal_ap_tramp)
	.code32
1:
	mov $0x10, %ax
	mov %ax, %ds
	mov %ax, %es
	mov %ax, %ss
	mov %ax, %fs
	mov %ax, %gs
	mov %cr4, %eax
	or $0x20, %eax
	mov %eax, %cr4
	mov 0x7000, %eax
	mov %eax, %cr3
	mov $0xC0000080, %ecx
	rdmsr
	or $0x100, %eax
	wrmsr
	mov %cr0, %eax
	or $0x80000000, %eax
	mov %eax, %cr0
	ljmpl $0x18, $0x8000 + (2f - pm_metal_ap_tramp)
	.code64
2:
	mov $0x10, %ax
	mov %ax, %ds
	mov %ax, %es
	mov %ax, %ss
	mov %ax, %fs
	mov %ax, %gs
	movabs $0x7010, %rsp
	mov (%rsp), %rsp
	and $-16, %rsp
	movabs $0x7020, %rdi
	mov (%rdi), %edi
	movabs $0x7018, %rax
	mov (%rax), %rax
	call *%rax
3:
	hlt
	jmp 3b

pm_metal_ap_tramp_end:
	.align 8
pm_metal_ap_tramp_size:
	.quad pm_metal_ap_tramp_end - pm_metal_ap_tramp

#ifdef __ELF__
	/* PE/COFF (UEFI --target=*-windows) rejects @progbits; note is ELF-only. */
	.section .note.GNU-stack,"",@progbits
#endif
