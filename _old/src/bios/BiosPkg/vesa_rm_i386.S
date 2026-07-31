/* i386 protected ↔ real-mode bounce for BIOS software interrupts.
 * Stub installed once at 0x8000; regs at 0x7000.
 *
 *   0x7000  ax,bx,cx,dx,es,di  (uint16)
 *   0x700c  intno              (uint8)
 *   0x700e  flags out          (uint16)
 *   0x7c00  real-mode stack
 *   0x8000  stub (persistent)
 */
	.section .text
	.code32
	.globl pm_bios_rm_int
	.type pm_bios_rm_int, @function

/* cdecl: rm_int(intno, *ax,*bx,*cx,*dx,*es,*di,*flags) */
pm_bios_rm_int:
	pushl %ebp
	movl %esp, %ebp
	pushl %ebx
	pushl %esi
	pushl %edi
	pushfl
	cli

	/* Install stub once — copying every INT made keyboard feel glacial. */
	cmpl $0, vesa_stub_ready
	jne .Lstub_ok
	movl $vesa_rm_blob, %esi
	movl $0x8000, %edi
	movl $(vesa_rm_blob_end - vesa_rm_blob), %ecx
	cld
	rep movsb
	movl $1, vesa_stub_ready
	movb $0xff, vesa_last_int
.Lstub_ok:

	/* Patch int imm8 only when intno changes */
	movb 8(%ebp), %al
	cmpb vesa_last_int, %al
	je .Lint_patched
	movb %al, vesa_last_int
	movb %al, (0x8000 + (rm_int_op - vesa_rm_blob) + 1)
.Lint_patched:

	/* Pack in-regs → 0x7000 */
	movl 12(%ebp), %eax
	movw (%eax), %cx
	movw %cx, 0x7000
	movl 16(%ebp), %eax
	movw (%eax), %cx
	movw %cx, 0x7002
	movl 20(%ebp), %eax
	movw (%eax), %cx
	movw %cx, 0x7004
	movl 24(%ebp), %eax
	movw (%eax), %cx
	movw %cx, 0x7006
	movl 28(%ebp), %eax
	movw (%eax), %cx
	movw %cx, 0x7008
	movl 32(%ebp), %eax
	movw (%eax), %cx
	movw %cx, 0x700a

	movl %esp, vesa_saved_esp

	/* Keep our flat GDT + IVT permanently after first use */
	cmpl $0, vesa_gdt_ready
	jne .Lgdt_ok
	lgdt vesa_tmp_gdtr
	lidt vesa_ivt_idtr
	movw $0x10, %ax
	movw %ax, %ds
	movw %ax, %es
	movw %ax, %fs
	movw %ax, %gs
	movw %ax, %ss
	movl $1, vesa_gdt_ready
.Lgdt_ok:

	ljmp $0x18, $0

	.align 4
vesa_pm_ret:
	.code32
	movw $0x10, %ax
	movw %ax, %ds
	movw %ax, %es
	movw %ax, %fs
	movw %ax, %gs
	movw %ax, %ss
	movl vesa_saved_esp, %esp

	movl 12(%ebp), %eax
	movw 0x7000, %cx
	movw %cx, (%eax)
	movl 16(%ebp), %eax
	movw 0x7002, %cx
	movw %cx, (%eax)
	movl 20(%ebp), %eax
	movw 0x7004, %cx
	movw %cx, (%eax)
	movl 24(%ebp), %eax
	movw 0x7006, %cx
	movw %cx, (%eax)
	movl 28(%ebp), %eax
	movw 0x7008, %cx
	movw %cx, (%eax)
	movl 32(%ebp), %eax
	movw 0x700a, %cx
	movw %cx, (%eax)
	movl 36(%ebp), %eax
	testl %eax, %eax
	jz 1f
	movw 0x700e, %cx
	movw %cx, (%eax)
1:
	xorl %eax, %eax
	popfl
	popl %edi
	popl %esi
	popl %ebx
	popl %ebp
	ret

	.align 16
vesa_rm_blob:
	.code16
	movw $0x20, %ax
	movw %ax, %ds
	movw %ax, %es
	movw %ax, %fs
	movw %ax, %gs
	movw %ax, %ss
	movl $0x7c00, %esp

	movl %cr0, %eax
	andl $0xfffffffe, %eax
	movl %eax, %cr0

	.byte 0xea
	.word (stub_real - vesa_rm_blob)
	.word 0x0800

stub_real:
	xorw %ax, %ax
	movw %ax, %ds
	movw %ax, %ss
	movw %ax, %fs
	movw %ax, %gs
	movw $0x7c00, %sp

	movw 0x7000, %ax
	movw 0x7002, %bx
	movw 0x7004, %cx
	movw 0x7006, %dx
	movw 0x7008, %si
	movw %si, %es
	movw 0x700a, %di

rm_int_op:
	int $0x10

	pushf
	popw %si
	movw %si, 0x700e

	movw %ax, 0x7000
	movw %bx, 0x7002
	movw %cx, 0x7004
	movw %dx, 0x7006
	movw %es, %si
	movw %si, 0x7008
	movw %di, 0x700a

	cli
	movl %cr0, %eax
	orl $1, %eax
	movl %eax, %cr0

	.byte 0x66, 0xea
	.long vesa_pm_ret
	.word 0x08

vesa_rm_blob_end:

	.code32
	.section .data
	.align 8
vesa_stub_ready:
	.long 0
vesa_gdt_ready:
	.long 0
vesa_last_int:
	.byte 0xff
	.byte 0, 0, 0
vesa_saved_esp:
	.long 0

vesa_ivt_idtr:
	.word 0x3ff
	.long 0

	.align 8
vesa_tmp_gdt:
	.quad 0x0000000000000000
	.quad 0x00cf9a000000ffff
	.quad 0x00cf92000000ffff
	.quad 0x00009a008000ffff
	.quad 0x000092000000ffff
vesa_tmp_gdtr:
	.word vesa_tmp_gdtr - vesa_tmp_gdt - 1
	.long vesa_tmp_gdt

	.section .note.GNU-stack,"",@progbits
