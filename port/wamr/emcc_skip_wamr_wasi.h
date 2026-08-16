#ifndef PM_METAL_EMCC_SKIP_WAMR_WASI_H
#define PM_METAL_EMCC_SKIP_WAMR_WASI_H
/* emcc libc already typedefs __wasi_*; WAMR's platform_wasi_types.h
 * clashes (__wasi_size_t uint32 vs unsigned long). Skip that header
 * and use the emcc types for platform_api_extension.h declarations. */
#ifndef _PLATFORM_WASI_TYPES_H
#define _PLATFORM_WASI_TYPES_H
#endif
#if defined(__EMSCRIPTEN__)
#include <wasi/api.h>
#endif
#include <string.h>
/* -U__linux__ hides POSIX strtok_r. Do not call strtok (2 args; also
 * undeclared on firmware fwinc). Same saveptr walk as port/lib.c. */
static inline int pm_emcc_is_delim(char c, const char *delim) {
    for (; *delim != '\0'; delim++) {
        if (c == *delim) {
            return 1;
        }
    }
    return 0;
}
__attribute__((unused))
static inline char *pm_emcc_strtok_r(char *str, const char *delim, char **saveptr) {
    char *s;
    if (str != NULL) {
        *saveptr = str;
    }
    s = *saveptr;
    if (s == NULL || delim == NULL) {
        return NULL;
    }
    while (*s != '\0' && pm_emcc_is_delim(*s, delim)) {
        s++;
    }
    if (*s == '\0') {
        *saveptr = s;
        return NULL;
    }
    str = s;
    while (*s != '\0') {
        if (pm_emcc_is_delim(*s, delim)) {
            *s = '\0';
            *saveptr = s + 1;
            return str;
        }
        s++;
    }
    *saveptr = s;
    return str;
}
#define strtok_r pm_emcc_strtok_r
#endif
