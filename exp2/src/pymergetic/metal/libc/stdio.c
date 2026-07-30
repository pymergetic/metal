/*
 * metal/libc stdio — firmware-host only (-nostdinc consumers).
 * Guests must not link this; use Metal mem/log/console APIs instead.
 */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

int printf(const char *fmt, ...)
{
  (void)fmt;
  return 0;
}

static int append_char(char *dst, size_t cap, size_t *pos, char c)
{
  if (*pos + 1 >= cap) {
    return -1;
  }
  dst[*pos] = c;
  (*pos)++;
  dst[*pos] = '\0';
  return 0;
}

static int append_u32(char *dst, size_t cap, size_t *pos, uint32_t v)
{
  char   tmp[12];
  size_t n = 0;
  size_t i;

  if (v == 0) {
    return append_char(dst, cap, pos, '0');
  }
  while (v > 0) {
    tmp[n++] = (char)('0' + (v % 10u));
    v /= 10u;
  }
  for (i = n; i > 0; i--) {
    if (append_char(dst, cap, pos, tmp[i - 1]) != 0) {
      return -1;
    }
  }
  return 0;
}

static int append_hex2(char *dst, size_t cap, size_t *pos, unsigned v)
{
  static const char hex[] = "0123456789abcdef";
  if (append_char(dst, cap, pos, hex[(v >> 4) & 0x0fu]) != 0) {
    return -1;
  }
  return append_char(dst, cap, pos, hex[v & 0x0fu]);
}

int snprintf(char *dst, size_t dst_cap, const char *fmt, ...)
{
  va_list ap;
  size_t  pos = 0;

  if (dst == NULL || dst_cap == 0) {
    return -1;
  }
  dst[0] = '\0';
  if (fmt == NULL) {
    return 0;
  }

  va_start(ap, fmt);
  for (; *fmt != '\0'; fmt++) {
    if (*fmt != '%') {
      if (append_char(dst, dst_cap, &pos, *fmt) != 0) {
        va_end(ap);
        return (int)pos;
      }
      continue;
    }
    fmt++;
    if (*fmt == '%') {
      if (append_char(dst, dst_cap, &pos, '%') != 0) {
        va_end(ap);
        return (int)pos;
      }
      continue;
    }
    if (*fmt == '0' && fmt[1] == '2' && fmt[2] == 'x') {
      fmt += 2;
      if (append_hex2(dst, dst_cap, &pos, (unsigned)va_arg(ap, unsigned)) != 0) {
        va_end(ap);
        return (int)pos;
      }
      continue;
    }
    if (*fmt == 's') {
      const char *s = va_arg(ap, const char *);
      size_t      i;

      if (s == NULL) {
        s = "(null)";
      }
      for (i = 0; s[i] != '\0'; i++) {
        if (append_char(dst, dst_cap, &pos, s[i]) != 0) {
          va_end(ap);
          return (int)pos;
        }
      }
      continue;
    }
    if (*fmt == 'u') {
      if (append_u32(dst, dst_cap, &pos, va_arg(ap, uint32_t)) != 0) {
        va_end(ap);
        return (int)pos;
      }
      continue;
    }
    if (*fmt == 'd') {
      int32_t v = va_arg(ap, int32_t);
      if (v < 0) {
        if (append_char(dst, dst_cap, &pos, '-') != 0) {
          va_end(ap);
          return (int)pos;
        }
        v = -v;
      }
      if (append_u32(dst, dst_cap, &pos, (uint32_t)v) != 0) {
        va_end(ap);
        return (int)pos;
      }
      continue;
    }
  }
  va_end(ap);
  return (int)pos;
}
