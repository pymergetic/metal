/* Native i386 Multiboot entry — stay in protected mode (no long mode).
 * Multiboot1 header: QEMU -kernel. Multiboot2: GRUB / PXE.
 */
	.section .multiboot, "a"
	.align 4
	.long 0x1BADB002
	.long 0x00000003		/* page-align + mem info (ELF load) */
	.long -(0x1BADB002 + 0x00000003)

	.section .multiboot2, "a"
	.align 8
mb2_header_start:
	.long 0xE85250D6
	.long 0
	.long mb2_header_end - mb2_header_start
	.long -(0xE85250D6 + 0 + (mb2_header_end - mb2_header_start))
	.align 8
	/* Tag: information request — mmap (6), framebuffer (8) */
	.word 1
	.word 0
	.long 16
	.long 6
	.long 8
	.align 8
	/* Tag: framebuffer — 1024x768x32 (optional; GRUB may provide) */
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
	movl %eax, mb_magic
	movl %ebx, mb_info

	/* Flat protected-mode segments already set by Multiboot loader. */
	leal boot_stack_top, %esp
	andl $-16, %esp

	/* Enable SSE if available (clang freestanding may emit movups). */
	movl %cr0, %eax
	andl $~0x4, %eax		/* clear EM */
	orl $0x2, %eax			/* set MP */
	movl %eax, %cr0
	movl %cr4, %eax
	orl $(0x200 | 0x400), %eax	/* OSFXSR | OSXMMEXCPT */
	movl %eax, %cr4

	/* cdecl: pm_metal_bios_main(magic, info) */
	pushl mb_info
	pushl mb_magic
	call pm_metal_bios_main
	addl $8, %esp
1:
	hlt
	jmp 1b

	.section .data
	.align 4
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
