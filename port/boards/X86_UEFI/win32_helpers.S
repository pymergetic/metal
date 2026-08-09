/* i686-windows COFF helpers (MSVC names). Asm symbols are literal (no extra _).
 * __aulldiv / __aullrem adapted from ReactOS CRT (Michael Ringgaard / Alex Ionescu). */

	.text

	.globl	__chkstk
__chkstk:
	ret

	.globl	___chkstk_ms
___chkstk_ms:
	ret

	/* clang may emit _alloca for large stack frames. */
	.globl	_alloca
_alloca:
	pushl	%ecx
	movl	%esp, %ecx
	addl	$8, %ecx		/* skip saved ecx + return */
	subl	%eax, %ecx
	andl	$-16, %ecx
	movl	4(%esp), %eax		/* return address */
	movl	%ecx, %esp
	pushl	%eax
	leal	4(%esp), %eax		/* pointer to alloca region */
	ret

/* unsigned 64/64 → quotient in EDX:EAX; pops 16 bytes of args. */
	.globl	__aulldiv
__aulldiv:
	pushl	%ebx
	pushl	%esi
	movl	24(%esp), %eax		/* DVSRHI */
	orl	%eax, %eax
	jnz	1f
	movl	20(%esp), %ecx		/* DVSRLO */
	movl	16(%esp), %eax		/* DVNDHI */
	xorl	%edx, %edx
	divl	%ecx
	movl	%eax, %ebx
	movl	12(%esp), %eax		/* DVNDLO */
	divl	%ecx
	movl	%ebx, %edx
	jmp	2f
1:
	movl	%eax, %ecx
	movl	20(%esp), %ebx
	movl	16(%esp), %edx
	movl	12(%esp), %eax
3:
	shrl	$1, %ecx
	rcrl	$1, %ebx
	shrl	$1, %edx
	rcrl	$1, %eax
	orl	%ecx, %ecx
	jnz	3b
	divl	%ebx
	movl	%eax, %esi
	mull	24(%esp)
	movl	%eax, %ecx
	movl	20(%esp), %eax
	mull	%esi
	addl	%ecx, %edx
	jc	4f
	cmpl	16(%esp), %edx
	ja	4f
	jb	5f
	cmpl	12(%esp), %eax
	jbe	5f
4:
	decl	%esi
5:
	xorl	%edx, %edx
	movl	%esi, %eax
2:
	popl	%esi
	popl	%ebx
	ret	$16

/* unsigned 64/64 → remainder in EDX:EAX; pops 16 bytes of args. */
	.globl	__aullrem
__aullrem:
	pushl	%ebx
	movl	20(%esp), %eax		/* DVSRHI */
	orl	%eax, %eax
	jnz	1f
	movl	16(%esp), %ecx		/* DVSRLO */
	movl	12(%esp), %eax		/* DVNDHI */
	xorl	%edx, %edx
	divl	%ecx
	movl	8(%esp), %eax		/* DVNDLO */
	divl	%ecx
	movl	%edx, %eax
	xorl	%edx, %edx
	jmp	2f
1:
	movl	%eax, %ecx
	movl	16(%esp), %ebx
	movl	12(%esp), %edx
	movl	8(%esp), %eax
3:
	shrl	$1, %ecx
	rcrl	$1, %ebx
	shrl	$1, %edx
	rcrl	$1, %eax
	orl	%ecx, %ecx
	jnz	3b
	divl	%ebx
	movl	%eax, %ecx
	mull	20(%esp)
	xchgl	%ecx, %eax
	mull	16(%esp)
	addl	%ecx, %edx
	jc	4f
	cmpl	12(%esp), %edx
	ja	4f
	jb	5f
	cmpl	8(%esp), %eax
	jbe	5f
4:
	subl	16(%esp), %eax
	sbbl	20(%esp), %edx
5:
	subl	8(%esp), %eax
	sbbl	12(%esp), %edx
	negl	%edx
	negl	%eax
	sbbl	$0, %edx
2:
	popl	%ebx
	ret	$16
