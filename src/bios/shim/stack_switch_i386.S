/* i386 SetJump / LongJump / SwitchStack (cdecl). */
	.text
	.code32
	.globl	SetJump
	.type	SetJump, @function
SetJump:
	/* 4(%esp) = JumpBuffer* */
	movl	4(%esp), %eax
	movl	%ebx, 0(%eax)
	movl	%esp, 4(%eax)
	movl	%ebp, 8(%eax)
	movl	%esi, 12(%eax)
	movl	%edi, 16(%eax)
	movl	(%esp), %edx
	movl	%edx, 20(%eax)
	xorl	%eax, %eax
	ret

	.globl	LongJump
	.type	LongJump, @function
LongJump:
	/* 4(%esp)=JumpBuffer*, 8(%esp)=Value */
	movl	4(%esp), %ecx
	movl	8(%esp), %eax
	testl	%eax, %eax
	jnz	1f
	movl	$1, %eax
1:
	movl	0(%ecx), %ebx
	movl	4(%ecx), %esp
	movl	8(%ecx), %ebp
	movl	12(%ecx), %esi
	movl	16(%ecx), %edi
	movl	20(%ecx), %edx
	movl	%edx, (%esp)
	ret

	.globl	SwitchStack
	.type	SwitchStack, @function
SwitchStack:
	/*
	 * cdecl: 4=Entry, 8=Context1, 12=Context2, 16=NewStack
	 * Call Entry(Context1, Context2) on NewStack.
	 */
	movl	16(%esp), %eax
	andl	$-16, %eax
	movl	4(%esp), %ecx
	movl	8(%esp), %edx
	movl	12(%esp), %ebx
	movl	%eax, %esp
	pushl	%ebx
	pushl	%edx
	call	*%ecx
1:
	jmp	1b

	.section .note.GNU-stack,"",@progbits
