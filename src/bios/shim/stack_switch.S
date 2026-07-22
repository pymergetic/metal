/* x86_64 SetJump / LongJump / SwitchStack */
	.text
	.globl	SetJump
	.type	SetJump, @function
SetJump:
	movq	%rbx, 0(%rdi)
	movq	%rsp, 8(%rdi)
	movq	%rbp, 16(%rdi)
	movq	%r12, 24(%rdi)
	movq	%r13, 32(%rdi)
	movq	%r14, 40(%rdi)
	movq	%r15, 48(%rdi)
	movq	(%rsp), %rax
	movq	%rax, 56(%rdi)
	xorl	%eax, %eax
	ret

	.globl	LongJump
	.type	LongJump, @function
LongJump:
	movq	%rsi, %rax
	movq	0(%rdi), %rbx
	movq	8(%rdi), %rsp
	movq	16(%rdi), %rbp
	movq	24(%rdi), %r12
	movq	32(%rdi), %r13
	movq	40(%rdi), %r14
	movq	48(%rdi), %r15
	movq	56(%rdi), %rdx
	movq	%rdx, (%rsp)
	ret

	.globl	SwitchStack
	.type	SwitchStack, @function
SwitchStack:
	/* rdi=Entry, rsi=Context1, rdx=Context2, rcx=NewStack */
	movq	%rcx, %rsp
	andq	$-16, %rsp
	/* SysV: rdi=Context1, rsi=Context2 — Entry already in... need save Entry */
	movq	%rdi, %r8
	movq	%rsi, %rdi
	movq	%rdx, %rsi
	call	*%r8
1:
	jmp	1b

	.section .note.GNU-stack,"",@progbits
