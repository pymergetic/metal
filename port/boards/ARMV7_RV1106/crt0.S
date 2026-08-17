/* Luckfox Pico Max / RV1106 — boot_fit/bootz lands here (kernel_addr_r).
 * Offset 0x24 must be Linux ARM zImage magic or bootz refuses to jump. */
	.syntax unified
	.arm
	.fpu neon-vfpv4
	.section .text.boot, "ax"
	.global _start
	.type _start, %function
_start:
	b real_start
	.space 0x24 - (. - _start)
	.word 0x016f2818
	.word 0x00008000
	.word 0x00108000
real_start:
	cpsid if

	/* SCTLR: MMU off, D-cache off, I-cache off, low vectors. */
	mrc p15, 0, r0, c1, c0, 0
	bic r0, r0, #0x00000001
	bic r0, r0, #0x00000004
	bic r0, r0, #0x00001000
	bic r0, r0, #0x00002000
	mcr p15, 0, r0, c1, c0, 0
	isb

	mov r0, #0
	mcr p15, 0, r0, c7, c5, 0	/* ICIALLU */
	mcr p15, 0, r0, c8, c7, 0	/* TLBIALL */
	dsb sy
	isb

	/* CPACR: full access to cp10/cp11, then enable VFP/NEON (hard-float). */
	mrc p15, 0, r0, c1, c0, 2
	orr r0, r0, #(0xf << 20)
	mcr p15, 0, r0, c1, c0, 2
	isb
	mov r0, #0x40000000
	vmsr fpexc, r0

	/* CNTKCTL: PL1 CNTPCT. U-Boot/Linux already run the generic timer. */
	mrc p15, 0, r0, c14, c1, 0
	orr r0, r0, #1
	mcr p15, 0, r0, c14, c1, 0

	/* I-cache on. */
	mrc p15, 0, r0, c1, c0, 0
	orr r0, r0, #0x00001000
	mcr p15, 0, r0, c1, c0, 0
	isb

	/* One ~0.4s on/off: bootz jumped. I-cache is on; 8e6 was a few ms. */
	ldr r0, =0xff550000
	ldr r1, =0x00400040
	str r1, [r0, #0x0c]
	str r1, [r0, #0x04]
	ldr r2, =250000000
6:
	subs r2, r2, #1
	bne 6b
	ldr r1, =0x00400000
	str r1, [r0, #0x04]
	ldr r2, =250000000
7:
	subs r2, r2, #1
	bne 7b

	/* U-Boot copies the zImage file only. Zero .bss before any C. */
	ldr r0, =__bss_start
	ldr r1, =__bss_end
	mov r2, #0
2:
	cmp r0, r1
	bhs 3f
	str r2, [r0], #4
	b 2b
3:
	ldr sp, =boot_stack_top
	bic sp, sp, #7
	mov r0, #0
	mov r1, #0
	bl pm_metal_bios_main
1:
	wfi
	b 1b
	.size _start, .-_start

	.section .bss
	.align 3
boot_stack:
	.space 262144
	.global boot_stack_top
boot_stack_top:
