#include <stddef.h>
#include <stdint.h>

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
