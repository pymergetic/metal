#ifndef _METAL_DB_SETJMP_H
#define _METAL_DB_SETJMP_H
/*
 * Real setjmp/longjmp for Dropbear on Metal.
 * __builtin_setjmp/longjmp across Dropbear frames left the guest in a
 * tight spin (qemu ~100% CPU, HTTP dead) after SSH peer close.
 */
#if defined(__x86_64__)
typedef unsigned long jmp_buf[8];
#elif defined(__i386__)
typedef unsigned long jmp_buf[6];
#else
#error "dropbear setjmp: unsupported arch"
#endif
typedef jmp_buf sigjmp_buf;

int  setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val) __attribute__((noreturn));
#define sigsetjmp(env, sav) setjmp(env)
#define siglongjmp(env, val) longjmp((env), (val))
#endif
