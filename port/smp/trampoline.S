/* Real-mode AP trampoline. Copied to phys 0x8000. Data page at 0x7000.
 *
 * 0x7000 cr3
 * 0x7008 next_id (BSP leaves this at 1)
 * 0x7010 entry
 * 0x7020 ncpu
 * 0x70F0 GDTR
 * 0x7100 GDT
 * 0x7200 stack[256]
 */
	.section .text
	.globl pm_metal_smp_tramp
	.globl pm_metal_smp_tramp_end
	.code16
	.align 16
pm_metal_smp_tramp:
	cli
	lgdtl 0x70F0
	movl %cr0, %eax
	orb $1, %al
	movl %eax, %cr0
	ljmpl $0x18, $0x8100

	.org 0x100
	.code32
	movw $0x10, %ax
	movw %ax, %ds
	movw %ax, %es
	movw %ax, %ss
	movl 0x7000, %eax
	movl %eax, %cr3
	movl %cr4, %eax
	orl $0x20, %eax
	movl %eax, %cr4
	movl $0xC0000080, %ecx
	rdmsr
	orl $0x100, %eax
	wrmsr
	movl %cr0, %eax
	orl $0x80000000, %eax
	movl %eax, %cr0
	ljmpl $0x08, $0x8200

	.org 0x200
	.code64
	movw $0x10, %ax
	movw %ax, %ds
	movw %ax, %es
	movw %ax, %ss
	movw %ax, %fs
	movw %ax, %gs
	movq $0x7000, %rdx
	movq (%rdx), %rax
	movq %rax, %cr3
	movq $0x7008, %rdx
	movl $1, %eax
	lock xaddl %eax, (%rdx)
	movl %eax, %ecx
	movq $0x7020, %rdx
	cmpl (%rdx), %eax
	jae 1f
	shlq $3, %rcx
	movq $0x7200, %rdx
	movq (%rdx,%rcx), %rsp
	andq $-16, %rsp
	movq %rax, %rdi
	movq %rax, %rcx
	movq $0x7010, %rdx
	movq (%rdx), %rax
	callq *%rax
1:
	hlt
	jmp 1b
pm_metal_smp_tramp_end:
#if defined(__ELF__)
	.section .note.GNU-stack,"",@progbits
#endif
