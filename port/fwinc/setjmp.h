#ifndef PM_METAL_FW_SETJMP_H
#define PM_METAL_FW_SETJMP_H

typedef unsigned long jmp_buf[10];

int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);

#endif
