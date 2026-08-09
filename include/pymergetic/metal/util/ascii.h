/* FIGlet "small" + rainbow banner (boot UX). */
#ifndef PYMERGETIC_METAL_UTIL_ASCII_H_
#define PYMERGETIC_METAL_UTIL_ASCII_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

size_t pm_metal_util_ascii_bound(size_t text_len);
int pm_metal_util_ascii_render(const char *text, char ink, char *out, size_t out_cap);
/* Plain FIGlet lines (caller may wrap with ANSI). */
void pm_metal_util_ascii_log(const char *text);
/* Each FIGlet line wrapped in \033[36m … \033[0m (classic Metal banner). */
void pm_metal_util_ascii_log_cyan(const char *text);
void pm_metal_util_ascii_log_rainbow(const char *text);

#ifdef __cplusplus
}
#endif

#endif
