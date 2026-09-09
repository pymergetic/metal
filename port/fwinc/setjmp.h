#ifndef PM_METAL_FW_SETJMP_H
#define PM_METAL_FW_SETJMP_H

/* 10 * 8 bytes on EVERY seat: unsigned long is LLP64-4 on the UEFI/Win64
 * triplet, and the register save below writes 10 quadwords (rsp/rip ride
 * slots 8/9). A 4-byte long made this 40 bytes on UEFI while the x86_64
 * setjmp still wrote 80 — error_jmp_buf is embedded in TCCState, so the
 * overflow smashed 40 bytes of neighboring state fields (the crash free
 * was error_jmp_buf's shadow: a stack address in the high half). */
typedef unsigned long long jmp_buf[10];

int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);

#endif
