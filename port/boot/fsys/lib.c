/* Minimal freestanding libc bodies for UEFI µPy (GC owns heap). */
#include <stddef.h>

void *malloc(size_t n) {
    (void)n;
    return NULL;
}
void *realloc(void *p, size_t n) {
    (void)p;
    (void)n;
    return NULL;
}
void free(void *p) {
    (void)p;
}
void abort(void) {
    for (;;) {
        __asm__ volatile("hlt");
    }
}
void exit(int status) {
    (void)status;
    abort();
}
int abs(int x) {
    return x < 0 ? -x : x;
}
long labs(long x) {
    return x < 0 ? -x : x;
}
int atoi(const char *s) {
    (void)s;
    return 0;
}
long strtol(const char *nptr, char **endptr, int base) {
    (void)nptr;
    (void)endptr;
    (void)base;
    return 0;
}
unsigned long strtoul(const char *nptr, char **endptr, int base) {
    (void)nptr;
    (void)endptr;
    (void)base;
    return 0;
}
