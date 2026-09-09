/* ARMV7_QEMU — QEMU virt machine, cortex-a7. -kernel loads this ELF at
 * 0x40010000 (RAM base 0x40000000 + 64KiB, matching the virt machine's
 * kernel load addr). No zImage magic needed: QEMU's arm loader accepts a
 * plain ELF with a single entry. */
	.syntax unified
	.arm
	.fpu neon-vfpv4
	.section .text.boot, "ax"
	.global _start
	.type _start, %function
_start:
	cpsid if

	/* SCTLR: MMU off, D-cache off, I-cache off. */
	mrc p15, 0, r0, c1, c0, 0
	bic r0, r0, #0x00000001
	bic r0, r0, #0x00000004
	bic r0, r0, #0x00001000
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

	/* CNTKCTL: PL1 CNTPCT — the generic timer QEMU's virt board runs. */
	mrc p15, 0, r0, c14, c1, 0
	orr r0, r0, #1
	mcr p15, 0, r0, c14, c1, 0

	/* I-cache on. */
	mrc p15, 0, r0, c1, c0, 0
	orr r0, r0, #0x00001000
	mcr p15, 0, r0, c1, c0, 0
	isb

	/* Zero .bss before any C. QEMU loads the ELF; nothing else did it. */
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
	/* ELF .init_array — same job as BIOS crt0. fw_lock's PM_MOD_BOOT_RS!
	 * ctors (asgi!) stage boot records through pm_mod_boot_add before
	 * pm_metal_boot() walks the linker sections. */
	bl pm_metal_elf_ctors_run
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
