#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

/* The fwinc contract this file implements. Relative paths, not <...>:
 * the three board rules disagree on -I order (BIOS compiles with no -I at
 * all, UEFI adds fwinc, RV1106 adds the full INC set) — the explicit
 * include keeps one definition whichever seat builds lib.o. */
#include "fwinc/stdlib.h"
#include "fwinc/stdio.h"
#include "fwinc/string.h"
#include "fwinc/unistd.h"
#include "fwinc/fcntl.h"
#include "fwinc/errno.h"
#include "fwinc/time.h"

void fflush(void *stream) {
    /* No stdio buffering on firmware; the zenoh-pico core's stray fflush(stdout)
     * has nothing to flush. */
    (void)stream;
}

void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--) {
        *d++ = *s++;
    }
    return dst;
}

void *memset(void *dst, int c, size_t n) {
    unsigned char *d = dst;
    while (n--) {
        *d++ = (unsigned char)c;
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *x = a;
    const unsigned char *y = b;
    while (n--) {
        if (*x != *y) {
            return (int)*x - (int)*y;
        }
        x++;
        y++;
    }
    return 0;
}

void *memchr(const void *s, int c, size_t n) {
    const unsigned char *p = s;
    unsigned char ch = (unsigned char)c;
    while (n--) {
        if (*p == ch) {
            return (void *)p;
        }
        p++;
    }
    return NULL;
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
    if (n == 0) {
        return 0;
    }
    while (n > 1u && *a && *a == *b) {
        a++;
        b++;
        n--;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

size_t strlen(const char *s) {
    size_t n = 0;
    if (s == NULL) {
        return 0;
    }
    while (s[n] != 0) {
        n++;
    }
    return n;
}

char *strchr(const char *s, int c) {
    char ch = (char)c;
    if (s == NULL) {
        return NULL;
    }
    for (;;) {
        if (*s == ch) {
            return (char *)s;
        }
        if (*s == 0) {
            return NULL;
        }
        s++;
    }
}

char *strrchr(const char *s, int c) {
    char ch = (char)c;
    const char *last = NULL;
    if (s == NULL) {
        return NULL;
    }
    while (*s) {
        if (*s == ch) {
            last = s;
        }
        s++;
    }
    if (ch == 0) {
        return (char *)s;
    }
    return (char *)last;
}

char *strcpy(char *dst, const char *src) {
    char *d = dst;
    while ((*d++ = *src++) != 0) {
    }
    return dst;
}

char *strncpy(char *dst, const char *src, size_t n) {
    char *d = dst;
    while (n > 0 && *src) {
        *d++ = *src++;
        n--;
    }
    while (n > 0) {
        *d++ = 0;
        n--;
    }
    return dst;
}

size_t strnlen(const char *s, size_t maxlen) {
    size_t n = 0;
    if (s == NULL) {
        return 0;
    }
    while (n < maxlen && s[n] != 0) {
        n++;
    }
    return n;
}

char *strncat(char *dst, const char *src, size_t n) {
    char *d = dst;
    while (*d) {
        d++;
    }
    while (n > 0 && *src) {
        *d++ = *src++;
        n--;
    }
    *d = 0;
    return dst;
}

char *strcat(char *dst, const char *src) {
    char *d = dst;
    while (*d) {
        d++;
    }
    while ((*d++ = *src++) != 0) {
    }
    return dst;
}

char *strstr(const char *hay, const char *needle) {
    size_t n;
    if (hay == NULL || needle == NULL) {
        return NULL;
    }
    n = strlen(needle);
    if (n == 0) {
        return (char *)hay;
    }
    while (*hay) {
        if (strncmp(hay, needle, n) == 0) {
            return (char *)hay;
        }
        hay++;
    }
    return NULL;
}

char *strtok_r(char *str, const char *delim, char **saveptr) {
    char *s;
    const char *d;
    if (str != NULL) {
        *saveptr = str;
    }
    s = *saveptr;
    if (s == NULL || delim == NULL) {
        return NULL;
    }
    while (*s) {
        int is_del = 0;
        for (d = delim; *d; d++) {
            if (*s == *d) {
                is_del = 1;
                break;
            }
        }
        if (!is_del) {
            break;
        }
        s++;
    }
    if (*s == '\0') {
        *saveptr = s;
        return NULL;
    }
    str = s;
    while (*s) {
        int is_del = 0;
        for (d = delim; *d; d++) {
            if (*s == *d) {
                is_del = 1;
                break;
            }
        }
        if (is_del) {
            *s = '\0';
            *saveptr = s + 1;
            return str;
        }
        s++;
    }
    *saveptr = s;
    return str;
}

long strtol(const char *nptr, char **endptr, int base) {
    const char *s = nptr;
    long sign = 1;
    long v = 0;
    if (base != 0 && (base < 2 || base > 36)) {
        if (endptr) {
            *endptr = (char *)nptr;
        }
        return 0;
    }
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') {
        s++;
    }
    if (*s == '+' || *s == '-') {
        if (*s == '-') {
            sign = -1;
        }
        s++;
    }
    if (base == 0) {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
            base = 16;
            s += 2;
        } else if (s[0] == '0') {
            base = 8;
        } else {
            base = 10;
        }
    } else if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }
    for (;;) {
        int d;
        char c = *s;
        if (c >= '0' && c <= '9') {
            d = c - '0';
        } else if (c >= 'a' && c <= 'z') {
            d = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'Z') {
            d = c - 'A' + 10;
        } else {
            break;
        }
        if (d >= base) {
            break;
        }
        v = v * (long)base + (long)d;
        s++;
    }
    if (endptr) {
        *endptr = (char *)s;
    }
    return sign * v;
}

unsigned long strtoul(const char *nptr, char **endptr, int base) {
    return (unsigned long)strtol(nptr, endptr, base);
}

double strtod(const char *nptr, char **endptr) {
    /* Minimal freestanding strtod for decimal literals (the zenoh-pico time-range
     * parser feeds it "1.5", "0.001", "-3e2", ...). No hex/inf/nan handling. */
    const char *p = nptr;
    int neg = 0;
    double value = 0.0, frac = 0.0, scale = 0.1;
    int exp10 = 0, eneg = 0;
    while (*p == ' ' || *p == '\t' || *p == '\n') {
        p++;
    }
    if (*p == '+' || *p == '-') {
        neg = (*p == '-');
        p++;
    }
    while (*p >= '0' && *p <= '9') {
        value = value * 10.0 + (double)(*p - '0');
        p++;
    }
    if (*p == '.') {
        p++;
        while (*p >= '0' && *p <= '9') {
            frac += (double)(*p - '0') * scale;
            scale *= 0.1;
            p++;
        }
    }
    value += frac;
    if (*p == 'e' || *p == 'E') {
        int en = 0;
        p++;
        if (*p == '+' || *p == '-') {
            eneg = (*p == '-');
            p++;
        }
        while (*p >= '0' && *p <= '9') {
            en = en * 10 + (*p - '0');
            p++;
        }
        exp10 = en;
    }
    {
        int k;
        double mag = 1.0;
        for (k = 0; k < exp10; k++) {
            mag *= 10.0;
        }
        if (eneg) {
            value /= mag;
        } else {
            value *= mag;
        }
    }
    if (endptr != NULL) {
        *endptr = (char *)p;
    }
    return neg ? -value : value;
}

int atoi(const char *s) {
    return (int)strtol(s, NULL, 10);
}

int abs(int x) {
    return x < 0 ? -x : x;
}

long labs(long x) {
    return x < 0 ? -x : x;
}

void qsort(void *base, size_t nmemb, size_t size, int (*cmp)(const void *, const void *)) {
    unsigned char *b = base;
    size_t i, j, k;
    if (base == NULL || cmp == NULL || size == 0) {
        return;
    }
    for (i = 0; i < nmemb; i++) {
        for (j = i + 1; j < nmemb; j++) {
            unsigned char *a = b + i * size;
            unsigned char *c = b + j * size;
            if (cmp(a, c) > 0) {
                for (k = 0; k < size; k++) {
                    unsigned char t = a[k];
                    a[k] = c[k];
                    c[k] = t;
                }
            }
        }
    }
}

void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
    int (*cmp)(const void *, const void *)) {
    const unsigned char *b = base;
    size_t lo = 0;
    size_t hi = nmemb;
    if (key == NULL || base == NULL || cmp == NULL || size == 0) {
        return NULL;
    }
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int c = cmp(key, b + mid * size);
        if (c == 0) {
            return (void *)(b + mid * size);
        }
        if (c < 0) {
            hi = mid;
        } else {
            lo = mid + 1;
        }
    }
    return NULL;
}

void abort(void) {
    for (;;) {
    }
}

int *pm_metal_errno_loc(void) {
    static int e;
    return &e;
}

#ifdef PM_METAL_UEFI
void __chkstk(void) {
}

void *pm_metal_efi_rsp;

/* Win64: rcx=fn, rdx=stack_hi. 16-align before call, 32-byte shadow.
 * Save EFI rsp in a global — rbx is callee-saved but WAMR/rust have
 * clobbered it across this call. */
__attribute__((naked)) int pm_metal_upy_on_stack(int (*fn)(void), void *stack_hi) {
    __asm volatile (
        "movq %%rsp, pm_metal_efi_rsp(%%rip)\n\t"
        "movq %%rdx, %%rsp\n\t"
        "andq $-16, %%rsp\n\t"
        "subq $32, %%rsp\n\t"
        "call *%%rcx\n\t"
        "movq pm_metal_efi_rsp(%%rip), %%rsp\n\t"
        "ret\n\t"
        :
        :
        : "memory");
}
#endif

void *memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    if (d < s) {
        while (n--) {
            *d++ = *s++;
        }
    } else {
        d += n;
        s += n;
        while (n--) {
            *--d = *--s;
        }
    }
    return dst;
}

#include <stdarg.h>

static const char *scan_uint(const char *s, unsigned *out) {
    unsigned v = 0;
    int any = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10u + (unsigned)(*s - '0');
        s++;
        any = 1;
    }
    if (!any) {
        return NULL;
    }
    *out = v;
    return s;
}

int sscanf(const char *str, const char *fmt, ...) {
    va_list ap;
    int n = 0;
    if (str == NULL || fmt == NULL) {
        return 0;
    }
    va_start(ap, fmt);
    while (*fmt && *str) {
        if (*fmt != '%') {
            if (*fmt == *str) {
                fmt++;
                str++;
                continue;
            }
            break;
        }
        fmt++;
        if (fmt[0] == '*' && fmt[1] == '[' && fmt[2] == '^' && fmt[3] == ':') {
            /* %*[^:] */
            while (*str && *str != ':') {
                str++;
            }
            fmt += 4;
            while (*fmt && *fmt != ']') {
                fmt++;
            }
            if (*fmt == ']') {
                fmt++;
            }
            continue;
        }
        if (*fmt == 'u') {
            unsigned *p = va_arg(ap, unsigned *);
            const char *next = scan_uint(str, p);
            if (next == NULL) {
                break;
            }
            str = next;
            fmt++;
            n++;
        } else {
            break;
        }
    }
    va_end(ap);
    return n;
}

#ifdef PM_METAL_UEFI
#include <setjmp.h>

/* Win64 callee-save + rip/rsp. C is -mno-sse so XMM6-15 stay out. */
__attribute__((naked)) int setjmp(jmp_buf env) {
    __asm volatile (
        "movq %%rbx, 0(%%rcx)\n\t"
        "movq %%rbp, 8(%%rcx)\n\t"
        "movq %%rdi, 16(%%rcx)\n\t"
        "movq %%rsi, 24(%%rcx)\n\t"
        "movq %%r12, 32(%%rcx)\n\t"
        "movq %%r13, 40(%%rcx)\n\t"
        "movq %%r14, 48(%%rcx)\n\t"
        "movq %%r15, 56(%%rcx)\n\t"
        "leaq 8(%%rsp), %%rax\n\t"
        "movq %%rax, 64(%%rcx)\n\t"
        "movq (%%rsp), %%rax\n\t"
        "movq %%rax, 72(%%rcx)\n\t"
        "xorl %%eax, %%eax\n\t"
        "ret\n\t"
        :
        :
        : "memory");
}

__attribute__((naked)) void longjmp(jmp_buf env, int val) {
    __asm volatile (
        "movl %%edx, %%eax\n\t"
        "testl %%eax, %%eax\n\t"
        "jne 1f\n\t"
        "movl $1, %%eax\n\t"
        "1:\n\t"
        "movq 0(%%rcx), %%rbx\n\t"
        "movq 8(%%rcx), %%rbp\n\t"
        "movq 16(%%rcx), %%rdi\n\t"
        "movq 24(%%rcx), %%rsi\n\t"
        "movq 32(%%rcx), %%r12\n\t"
        "movq 40(%%rcx), %%r13\n\t"
        "movq 48(%%rcx), %%r14\n\t"
        "movq 56(%%rcx), %%r15\n\t"
        "movq 64(%%rcx), %%rsp\n\t"
        "jmpq *72(%%rcx)\n\t"
        :
        :
        : "memory");
}
#endif

#if defined(__arm__) && !defined(__aarch64__)
#include <setjmp.h>

__attribute__((naked)) int setjmp(jmp_buf env) {
    __asm__ volatile(
        "stmia r0, {r4-r11}\n\t"
        "str sp, [r0, #32]\n\t"
        "str lr, [r0, #36]\n\t"
        "mov r0, #0\n\t"
        "bx lr\n\t");
}

__attribute__((naked)) void longjmp(jmp_buf env, int val) {
    __asm__ volatile(
        "movs r2, r1\n\t"
        "bne 1f\n\t"
        "mov r2, #1\n\t"
        "1:\n\t"
        "ldr sp, [r0, #32]\n\t"
        "ldr lr, [r0, #36]\n\t"
        "ldmia r0, {r4-r11}\n\t"
        "mov r0, r2\n\t"
        "bx lr\n\t");
}
#endif

#if defined(__x86_64__) && !defined(PM_METAL_UEFI)
#include "fwinc/setjmp.h"

/* SysV x86_64: rbx, rbp, r12-r15 callee-save; [8]=rsp, [9]=rip in the
 * fwinc jmp_buf (10 * unsigned long long — 80 bytes on every seat;
 * see fwinc/setjmp.h for why it is not unsigned long). -mno-sse keeps
 * XMM out of the picture (SysV's fp-state slots stay unused — TCC jumps
 * within one compile, never across a float context). */
__attribute__((naked)) int setjmp(jmp_buf env) {
    __asm__ volatile(
        "movq %%rbx, 0(%%rdi)\n\t"
        "movq %%rbp, 8(%%rdi)\n\t"
        "movq %%r12, 16(%%rdi)\n\t"
        "movq %%r13, 24(%%rdi)\n\t"
        "movq %%r14, 32(%%rdi)\n\t"
        "movq %%r15, 40(%%rdi)\n\t"
        "movq %%rsp, 48(%%rdi)\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "movq %%rax, 56(%%rdi)\n\t"
        "xorl %%eax, %%eax\n\t"
        "ret\n"
        "1:\n\t"
        :
        :
        : "memory");
}

__attribute__((naked)) void longjmp(jmp_buf env, int val) {
    __asm__ volatile(
        "movq 0(%%rdi), %%rbx\n\t"
        "movq 8(%%rdi), %%rbp\n\t"
        "movq 16(%%rdi), %%r12\n\t"
        "movq 24(%%rdi), %%r13\n\t"
        "movq 32(%%rdi), %%r14\n\t"
        "movq 40(%%rdi), %%r15\n\t"
        "movq 48(%%rdi), %%rsp\n\t"
        "testl %%esi, %%esi\n\t"
        "jne 1f\n\t"
        "movl $1, %%esi\n\t"
        "1:\n\t"
        "movl %%esi, %%eax\n\t"
        "jmpq *56(%%rdi)\n\t"
        :
        :
        : "memory");
}
#endif

/*==================== embedded TCC support (fwinc surface) =================
 * The vendored TCC links into every firmware seat with the in-kernel compile
 * face (fw_tcc.mk → jit.c object path). Everything below is the fwinc
 * contract; the µPy core (shared/libc/printf.c) and lib.c's own earlier
 * sections already provide printf/vprintf/putchar/puts/snprintf/vsnprintf,
 * memcpy family, strtol/strtoul/atoi/qsort/bsearch and malloc family
 * (firmware_upy.c). What TCC still needs beyond that lives here:
 *
 *   - a printf family with real %llu: TCC stringifies EVERY integer token
 *     through sprintf(p, "%llu", ...) (tccpp.c get_tok_str), and µPy's
 *     internal printf drops the 'll' on LP64 (mpprint.c gates ll behind
 *     MP_INT_MAX > LONG_MAX, impossible when both are 64-bit — it would
 *     render "42" as "l42"). So sprintf/fprintf/vfprintf/fputs have their
 *     own formatter here, not a forward to the µPy core.
 *   - the arena-backed temp FILE layer (fopen/fdopen/fclose/fwrite/fread/
 *     fputc/fgetc/ftell/fseek/rewind, open/close/read/write/lseek/unlink/
 *     mkstemp): TCC writes its ET_REL object through FILE* and jit.c reads
 *     it back. Boot-arena scratch, dies with the arena — not a general fs.
 *   - POSIX stubs the -run machinery references: exit, environ, getcwd,
 *     realpath, getenv, localtime (+struct tm in fwinc/time.h), strerror,
 *     strtof/strtold/strtoll/strtoull, mprotect, and the SysV setjmp/longjmp
 *     pair (BIOS/ELF seats; UEFI and arm define their own above).
 */

/*---- console sink ----*/

void uart_write(const char *s, size_t n);

static void fw_console_write(const char *s, size_t n) {
    /* console card when up (registered face), else the raw UART: the same
     * shape metal_platform.c's WAMR vprintf uses, so diagnostics land on one
     * channel no matter which seat emits them. */
    int32_t (*cw)(const char *, uint32_t);
    (void)cw; /* resolved weakly below to avoid a hard card dependency */
    extern int32_t pm_metal_console_write(const char *, uint32_t);
    if (pm_metal_console_write(s, (uint32_t)n) == 0) {
        return;
    }
    uart_write(s, n);
}

/*---- printf family with real long long (µPy's drops %llu on LP64) ----*/

static void fw_fmt_u64(char *out, size_t cap, unsigned long long v,
                       int base, int upper, int width, char pad, int left,
                       int neg, int plus, int space, size_t *written) {
    char tmp[68];
    const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int n = 0;
    int i;
    size_t w = 0;

    if (v == 0) {
        tmp[n++] = '0';
    }
    while (v != 0) {
        tmp[n++] = dig[v % (unsigned)base];
        v /= (unsigned)base;
    }
    if (neg) {
        tmp[n++] = '-';
    } else if (plus) {
        tmp[n++] = '+';
    } else if (space) {
        /* the ' ' flag: sign slot for a non-negative signed conversion.
         * '+' wins when both; a '-' makes it redundant. Counts toward width
         * like any sign char. No effect on the unsigned calls below. */
        tmp[n++] = ' ';
    }
    if (!left && width > n) {
        for (i = 0; i < width - n; i++) {
            if (w + 1 < cap) {
                out[w] = pad;
            }
            w++;
        }
    }
    for (i = n - 1; i >= 0; i--) {
        if (w + 1 < cap) {
            out[w] = tmp[i];
        }
        w++;
    }
    if (left && width > n) {
        for (i = 0; i < width - n; i++) {
            if (w + 1 < cap) {
                out[w] = ' ';
            }
            w++;
        }
    }
    *written = w;
}

int fw_vsnprintf(char *str, size_t size, const char *fmt, va_list ap) {
    /* The formatter for TCC's diagnostics and token stringification: flags
     * -0+ and space, width, the l/ll/h length mods, %d/%i/%u/%x/%X/%o/%c/%s/%p
     * and %%. That is the whole format grammar the vendored tree's calls
     * sites use (grep -oh '%[a-z]*' externals/tcc). No float: firmware seats
     * build with MICROPY float off for this layer, and TCC never formats
     * floats into strings on the object path. */
    size_t w = 0;
    const char *p = fmt;

    while (*p != '\0') {
        if (*p != '%') {
            if (w + 1 < size) {
                str[w] = *p;
            }
            w++;
            p++;
            continue;
        }
        p++;
        {
            char pad = ' ';
            int left = 0, plus = 0, space = 0;
            int width = 0, lmod = 0;
            unsigned long long v = 0;
            int neg = 0;
            char cbuf[2];
            const char *sarg;
            size_t sw;

            while (*p == '-' || *p == '0' || *p == '+' || *p == ' ') {
                if (*p == '-') {
                    left = 1;
                } else if (*p == '0') {
                    pad = '0';
                } else if (*p == '+') {
                    plus = 1;
                } else {
                    space = 1;
                }
                p++;
            }
            while (*p >= '0' && *p <= '9') {
                width = width * 10 + (*p - '0');
                p++;
            }
            while (*p == 'l') {
                lmod++;
                p++;
            }
            if (*p == 'h') {
                lmod = -1;
                p++;
                if (*p == 'h') {
                    p++;
                }
            }
            switch (*p) {
            case 'd':
            case 'i': {
                long long sv;
                if (lmod >= 2) {
                    sv = va_arg(ap, long long);
                } else if (lmod == 1) {
                    sv = va_arg(ap, long);
                } else {
                    sv = va_arg(ap, int);
                }
                if (sv < 0) {
                    neg = 1;
                    v = (unsigned long long)(-sv);
                } else {
                    v = (unsigned long long)sv;
                }
                fw_fmt_u64(str + w, size - (w < size ? w : size), v, 10, 0,
                    width, pad, left, neg, plus, space, &sw);
                w += sw;
                break;
            }
            case 'u':
            case 'x':
            case 'X':
            case 'o': {
                int base = *p == 'u' ? 10 : (*p == 'o' ? 8 : 16);
                if (lmod >= 2) {
                    v = va_arg(ap, unsigned long long);
                } else if (lmod == 1) {
                    v = va_arg(ap, unsigned long);
                } else {
                    v = va_arg(ap, unsigned int);
                }
                fw_fmt_u64(str + w, size - (w < size ? w : size), v,
                    base, *p == 'X', width, pad, left, 0, plus, space, &sw);
                w += sw;
                break;
            }
            case 'c':
                cbuf[0] = (char)va_arg(ap, int);
                cbuf[1] = '\0';
                sarg = cbuf;
                goto str_out;
            case 's':
                sarg = va_arg(ap, const char *);
                if (sarg == NULL) {
                    sarg = "(null)";
                }
            str_out:
                sw = 0;
                while (sarg[sw] != '\0') {
                    sw++;
                }
                if (!left && width > 0 && (int)sw < width) {
                    int i;
                    for (i = 0; i < width - (int)sw; i++) {
                        if (w + 1 < size) {
                            str[w] = pad;
                        }
                        w++;
                    }
                }
                {
                    size_t i;
                    for (i = 0; i < sw; i++) {
                        if (w + 1 < size) {
                            str[w] = sarg[i];
                        }
                        w++;
                    }
                }
                break;
            case 'p':
                v = (unsigned long long)(uintptr_t)va_arg(ap, void *);
                if (w + 1 < size) {
                    str[w] = '0';
                }
                w++;
                if (w + 1 < size) {
                    str[w] = 'x';
                }
                w++;
                fw_fmt_u64(str + w, size - (w < size ? w : size), v, 16, 0,
                    0, ' ', 0, 0, 0, 0, &sw);
                w += sw;
                break;
            case '%':
                if (w + 1 < size) {
                    str[w] = '%';
                }
                w++;
                break;
            default:
                /* unknown spec: emit it raw, never loop */
                if (w + 1 < size) {
                    str[w] = '%';
                }
                w++;
                if (*p != '\0') {
                    if (w + 1 < size) {
                        str[w] = *p;
                    }
                    w++;
                }
                break;
            }
            if (*p != '\0') {
                p++;
            }
        }
    }
    if (size > 0) {
        str[w < size - 1 ? w : size - 1] = '\0';
        /* a truncated write still counts its full length (POSIX) */
    }
    return (int)w;
}

int sprintf(char *str, const char *fmt, ...) {
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = fw_vsnprintf(str, (size_t)-1 / 2, fmt, ap);
    va_end(ap);
    return n;
}

int vfprintf(void *stream, const char *fmt, va_list ap) {
    char buf[512];
    int n = fw_vsnprintf(buf, sizeof(buf), fmt, ap);
    /* every FILE the temp layer hands out, and both fwinc stdin/stdout/
     * stderr macros, are ignored here — the sink is the console (diagnostics
     * must be visible in the serial log, not dropped into a temp stream) */
    (void)stream;
    fw_console_write(buf, (size_t)(n > 0 ? n : 0));
    return n;
}

int fprintf(void *stream, const char *fmt, ...) {
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vfprintf(stream, fmt, ap);
    va_end(ap);
    return n;
}

int fputs(const char *s, void *stream) {
    size_t n = 0;
    (void)stream;
    while (s[n] != '\0') {
        n++;
    }
    fw_console_write(s, n);
    return 0;
}

/*---- arena-backed temp FILE layer ----
 * One growable boot-arena buffer per stream; fds are indices into a small
 * table. The only files that exist are the temp names jit.c mints and
 * TCC's output name — nothing enumerates the layer. Memory comes from
 * malloc (the firmware_upy slab), matching every other compile-scratch
 * allocation; it is freed on fclose/unlink, so the layer is reusable, not
 * arena-doomed. */

#define FW_MAX_OPEN 12

typedef struct fw_temp_file {
    char name[64];
    unsigned char *data;
    size_t len;
    size_t cap;
    int in_use;
} fw_temp_file_t;

static fw_temp_file_t fw_files[FW_MAX_OPEN];

static fw_temp_file_t *fw_file_find(const char *name) {
    int i;
    for (i = 0; i < FW_MAX_OPEN; i++) {
        if (fw_files[i].in_use && strcmp(fw_files[i].name, name) == 0) {
            return &fw_files[i];
        }
    }
    return NULL;
}

static fw_temp_file_t *fw_file_slot(void) {
    int i;
    for (i = 0; i < FW_MAX_OPEN; i++) {
        if (!fw_files[i].in_use) {
            fw_files[i].in_use = 1;
            fw_files[i].data = NULL;
            fw_files[i].len = 0;
            fw_files[i].cap = 0;
            fw_files[i].name[0] = '\0';
            return &fw_files[i];
        }
    }
    return NULL;
}

static int fw_file_grow(fw_temp_file_t *f, size_t need) {
    if (need <= f->cap) {
        return 0;
    }
    {
        size_t ncap = f->cap == 0 ? 512 : f->cap;
        unsigned char *nd;
        while (ncap < need) {
            ncap *= 2;
        }
        nd = realloc(f->data, ncap);
        if (nd == NULL) {
            return -1;
        }
        f->data = nd;
        f->cap = ncap;
    }
    return 0;
}

int open(const char *path, int flags, ...) {
    fw_temp_file_t *f;
    int i;

    /* create/truncate semantics: the write side of tcc_write_elf_file */
    if (flags & O_CREAT) {
        f = fw_file_find(path);
        if (f != NULL) {
            f->len = 0; /* O_TRUNC */
        } else {
            f = fw_file_slot();
        }
        if (f == NULL) {
            return -1;
        }
        strncpy(f->name, path, sizeof(f->name) - 1);
        f->name[sizeof(f->name) - 1] = '\0';
    } else {
        f = fw_file_find(path);
        if (f == NULL) {
            return -1;
        }
    }
    for (i = 0; i < FW_MAX_OPEN; i++) {
        /* fd = table index of the file; pos lives in the FILE wrapper */
        if (&fw_files[i] == f) {
            return i;
        }
    }
    return -1;
}

int close(int fd) {
    (void)fd; /* the buffer stays until unlink/fclose — read-back follows write */
    return 0;
}

int unlink(const char *path) {
    fw_temp_file_t *f = fw_file_find(path);
    if (f == NULL) {
        return -1;
    }
    free(f->data);
    f->data = NULL;
    f->in_use = 0;
    return 0;
}

int mkstemp(char *template_) {
    /* jit.c's object path: mint a unique name into the template and "open"
     * it. The counter + board marker keeps names collision-free across
     * compiles in one boot. */
    static unsigned seq;
    size_t n = 0;
    while (template_[n] != '\0') {
        n++;
    }
    while (n > 0 && template_[n - 1] == 'X') {
        n--;
    }
    for (;;) {
        unsigned try = seq++;
        char name[64];
        size_t i = n;
        int k;
        for (k = 0; k < 6 && i + 1 < sizeof(name); k++) {
            name[i++] = (char)('A' + (try % 26));
            try = (try / 26u) + 7u;
        }
        name[i] = '\0';
        memcpy(name, template_, n);
        if (fw_file_find(name) == NULL) {
            fw_temp_file_t *f = fw_file_slot();
            if (f == NULL) {
                return -1;
            }
            strncpy(f->name, name, sizeof(f->name) - 1);
            f->name[sizeof(f->name) - 1] = '\0';
            memcpy(template_, name, i + 1);
            return (int)(f - fw_files);
        }
        if (seq > 1000000u) {
            return -1;
        }
    }
}

FILE *fdopen(int fd, const char *mode) {
    FILE *f;
    (void)mode;
    if (fd < 0 || fd >= FW_MAX_OPEN || !fw_files[fd].in_use) {
        return NULL;
    }
    f = malloc(sizeof(*f));
    if (f == NULL) {
        return NULL;
    }
    f->fd = fd;
    f->pos = 0;
    return f;
}

FILE *fopen(const char *path, const char *mode) {
    int fd = open(path, mode != NULL && mode[0] == 'w' ? O_CREAT : O_RDONLY, 0);
    if (fd < 0) {
        return NULL;
    }
    return fdopen(fd, mode);
}

int fclose(FILE *f) {
    if (f == NULL || f->fd < 0) {
        return -1;
    }
    /* the temp buffer stays for the read-back; only the wrapper dies */
    free(f);
    return 0;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *f) {
    fw_temp_file_t *t;
    size_t n = size * nmemb;

    if (f == NULL || f->fd < 0 || f->fd >= FW_MAX_OPEN) {
        return 0;
    }
    t = &fw_files[f->fd];
    if (fw_file_grow(t, f->pos + n) != 0) {
        return 0;
    }
    memcpy(t->data + f->pos, ptr, n);
    f->pos += (long)n;
    if ((size_t)f->pos > t->len) {
        t->len = (size_t)f->pos;
    }
    return nmemb;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *f) {
    fw_temp_file_t *t;
    size_t avail;
    size_t n = size * nmemb;

    if (f == NULL || f->fd < 0 || f->fd >= FW_MAX_OPEN) {
        return 0;
    }
    t = &fw_files[f->fd];
    if ((size_t)f->pos >= t->len) {
        return 0;
    }
    avail = t->len - (size_t)f->pos;
    if (n > avail) {
        n = avail;
    }
    memcpy(ptr, t->data + f->pos, n);
    f->pos += (long)n;
    return size == 0 ? 0 : n / size;
}

int fputc(int c, FILE *f) {
    unsigned char b = (unsigned char)c;
    return fwrite(&b, 1, 1, f) == 1 ? (int)b : -1;
}

int fgetc(FILE *f) {
    unsigned char b;
    return fread(&b, 1, 1, f) == 1 ? (int)b : -1;
}

long ftell(FILE *f) {
    if (f == NULL) {
        return -1;
    }
    return f->pos;
}

int fseek(FILE *f, long offset, int whence) {
    long npos;
    if (f == NULL || f->fd < 0 || f->fd >= FW_MAX_OPEN) {
        return -1;
    }
    switch (whence) {
    case SEEK_SET:
        npos = offset;
        break;
    case SEEK_CUR:
        npos = f->pos + offset;
        break;
    case SEEK_END:
        npos = (long)fw_files[f->fd].len + offset;
        break;
    default:
        return -1;
    }
    if (npos < 0) {
        return -1;
    }
    f->pos = npos;
    return 0;
}

void rewind(FILE *f) {
    (void)fseek(f, 0, SEEK_SET);
}

FILE *freopen(const char *path, const char *mode, FILE *stream) {
    /* the -run machinery's stdin rebind; no firmware seat drives -run —
     * answer the temp layer's file when it exists, else refuse (POSIX) */
    if (stream != NULL) {
        free(stream);
    }
    return fopen(path, mode);
}

ssize_t read(int fd, void *buf, size_t count) {
    /* fd reads on the temp layer are only reached through a FILE wrapper;
     * nothing in the compiled TCC set reads a bare fd after open(). */
    (void)fd;
    (void)buf;
    (void)count;
    return -1;
}

ssize_t write(int fd, const void *buf, size_t count) {
    (void)fd;
    (void)buf;
    (void)count;
    return (ssize_t)count;
}

long lseek(int fd, long offset, int whence) {
    (void)fd;
    (void)whence;
    return offset;
}

/*---- POSIX stubs the -run machinery references ----*/

void exit(int status) {
    /* TCC's hard-error paths are all noabort in library builds; this fires
     * only from the never-driven -run half. Visible halt, no silent return. */
    fw_console_write("tcc: exit(", 10);
    {
        char b[16];
        size_t sw;
        fw_fmt_u64(b, sizeof(b), (unsigned long long)(unsigned)status, 10, 0,
            0, ' ', 0, 0, 0, 0, &sw);
        fw_console_write(b, sw);
    }
    fw_console_write(")\n", 2);
    for (;;) {
    }
}

char **environ = NULL;

char *getenv(const char *name) {
    /* no environment on firmware: TCC's LD_SO probe falls to the default */
    (void)name;
    return NULL;
}

char *getcwd(char *buf, size_t size) {
    /* firmware has no cwd; the debug-info/coverage paths prefix relative
     * names with it — the empty string keeps names as-written */
    if (buf == NULL || size == 0) {
        return NULL;
    }
    buf[0] = '\0';
    return buf;
}

char *realpath(const char *path, char *resolved_path) {
    /* normalized_PATHCMP (#pragma once dedup) resolves through here; no
     * filesystem means identity is the only correct answer */
    size_t n = 0;
    if (path == NULL) {
        return NULL;
    }
    while (path[n] != '\0') {
        n++;
    }
    if (resolved_path != NULL) {
        memcpy(resolved_path, path, n + 1);
        return resolved_path;
    }
    {
        char *p = malloc(n + 1);
        if (p == NULL) {
            return NULL;
        }
        memcpy(p, path, n + 1);
        return p;
    }
}

char *strerror(int errnum) {
    (void)errnum;
    /* every temp-layer failure is "not found" */
    return (char *)"not found";
}

static struct tm fw_tm_epoch;

struct tm *localtime(const time_t *timep) {
    /* __DATE__/__TIME__ builtins: no RTC read on this path — the one static
     * epoch serves every call (documented fill difference, see fwinc/time.h) */
    (void)timep;
    return &fw_tm_epoch;
}

long long strtoll(const char *nptr, char **endptr, int base) {
    return (long long)strtol(nptr, endptr, base);
}

unsigned long long strtoull(const char *nptr, char **endptr, int base) {
    return (unsigned long long)strtoul(nptr, endptr, base);
}

float strtof(const char *nptr, char **endptr) {
    return (float)strtod(nptr, endptr);
}

long double strtold(const char *nptr, char **endptr) {
    return (long double)strtod(nptr, endptr);
}

int mprotect(void *addr, unsigned long len, int prot) {
    /* tcc_relocate's page-protection pass: arena memory — no MMU dance on
     * the firmware seats; W^X is a host luxury, the object path never
     * executes from these buffers */
    (void)addr;
    (void)len;
    (void)prot;
    return 0;
}

