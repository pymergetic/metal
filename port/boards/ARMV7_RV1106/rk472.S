/* 472 trampoline — PIC. Copy metal.bin to 0x8000 via 64MiB scratch, then jump. */
	.syntax unified
	.arm
	.section .text, "ax"
	.global _start
_start:
	ldr r0, payload_rel
	add r0, pc, r0
	b 1f
payload_rel:
	.word payload - payload_rel
1:
	mov r5, r0			/* r5 = &payload */
	ldr r1, payload_size
	add r1, r1, r0
	ldr r2, scratch
2:
	cmp r0, r1
	bhs 3f
	ldr r3, [r0], #4
	str r3, [r2], #4
	b 2b
3:
	mov r0, r5
	ldr r1, payload_size
	ldr r2, scratch
	ldr r4, dest
4:
	cmp r1, #0
	beq 5f
	ldr r3, [r2], #4
	str r3, [r4], #4
	sub r1, r1, #4
	b 4b
5:
	ldr r0, dest
	bx r0

	.align 2
payload_size:
	.word payload_end - payload
scratch:
	.word 0x04000000
dest:
	.word 0x00008000

	.align 4
payload:
	.incbin "metal.bin"
payload_end:
