#include "PmBiosUefi.h"
#include "Library/PrintLib.h"
#include "Library/IoLib.h"

static VOID
Com1Put(CHAR8 c)
{
  UINTN spins;
  for (spins = 0; spins < 100000; spins++) {
    if ((IoRead8(0x3FD) & 0x20) != 0)
      break;
  }
  IoWrite8(0x3F8, (UINT8)c);
}

static VOID
OutStr(CHAR8 **dst, UINTN *left, CONST CHAR8 *s)
{
  if (s == NULL)
    s = "(null)";
  while (*s && *left > 1) {
    if (*dst != NULL) {
      **dst = *s;
      (*dst)++;
    } else {
      Com1Put(*s);
    }
    s++;
    (*left)--;
  }
}

static VOID
OutU64(CHAR8 **dst, UINTN *left, UINT64 v, UINTN base, INT32 width)
{
  CHAR8 tmp[32];
  UINTN i = 0;
  CONST CHAR8 *digits = "0123456789abcdef";
  if (v == 0)
    tmp[i++] = '0';
  while (v > 0 && i < sizeof(tmp)) {
    tmp[i++] = digits[v % base];
    v /= base;
  }
  while ((INT32)i < width)
    tmp[i++] = '0';
  while (i > 0) {
    CHAR8 ch[2];
    ch[0] = tmp[--i];
    ch[1] = 0;
    OutStr(dst, left, ch);
  }
}

static INT32
ParseWidth (
  CONST CHAR8  **fmt
  )
{
  INT32  w;

  w = 0;
  while (**fmt >= '0' && **fmt <= '9') {
    w = (w * 10) + (**fmt - '0');
    (*fmt)++;
  }

  return w;
}

UINTN
AsciiVSPrint(CHAR8 *StartOfBuffer, UINTN BufferSize, CONST CHAR8 *FormatString,
	     VA_LIST Marker)
{
  CHAR8 *dst = StartOfBuffer;
  UINTN left = BufferSize;
  CONST CHAR8 *fmt = FormatString;
  INT32 width;

  if (fmt == NULL)
    return 0;
  if (dst != NULL && left == 0)
    return 0;
  if (dst != NULL)
    left = BufferSize;

  while (*fmt) {
    if (*fmt != '%') {
      CHAR8 ch[2] = { *fmt, 0 };
      OutStr(&dst, &left, ch);
      fmt++;
      continue;
    }
    fmt++;
    width = ParseWidth (&fmt);
    if (*fmt == 'a' || *fmt == 's') {
      OutStr(&dst, &left, VA_ARG(Marker, CONST CHAR8 *));
      fmt++;
    } else if (*fmt == 'c') {
      CHAR8 ch[2] = { (CHAR8)VA_ARG(Marker, INT32), 0 };
      OutStr(&dst, &left, ch);
      fmt++;
    } else if (*fmt == 'd' || *fmt == 'i') {
      INT32 v = VA_ARG(Marker, INT32);
      if (v < 0) {
	OutStr(&dst, &left, "-");
	OutU64(&dst, &left, (UINT64)(-(INT64)v), 10, width);
      } else {
	OutU64(&dst, &left, (UINT64)v, 10, width);
      }
      fmt++;
    } else if (*fmt == 'u') {
      OutU64(&dst, &left, (UINT64)VA_ARG(Marker, UINT32), 10, width);
      fmt++;
    } else if (*fmt == 'x' || *fmt == 'X') {
      OutU64(&dst, &left, (UINT64)VA_ARG(Marker, UINT32), 16, width);
      fmt++;
    } else if (*fmt == 'l' && (fmt[1] == 'x' || fmt[1] == 'X')) {
      OutU64(&dst, &left, VA_ARG(Marker, UINT64), 16, width);
      fmt += 2;
    } else if (*fmt == 'l' && fmt[1] == 'u') {
      OutU64(&dst, &left, VA_ARG(Marker, UINT64), 10, width);
      fmt += 2;
    } else if (*fmt == 'L' && fmt[1] == 'u') {
      OutU64(&dst, &left, VA_ARG(Marker, UINT64), 10, width);
      fmt += 2;
    } else if (*fmt == 'r') {
      OutU64(&dst, &left, (UINT64)VA_ARG(Marker, UINTN), 16, width);
      fmt++;
    } else if (*fmt == '%') {
      OutStr(&dst, &left, "%");
      fmt++;
    } else {
      fmt++;
    }
  }
  if (StartOfBuffer != NULL && BufferSize > 0) {
    UINTN used = BufferSize - left;
    if (used >= BufferSize)
      used = BufferSize - 1;
    StartOfBuffer[used] = '\0';
    return used;
  }
  return 0;
}

UINTN
AsciiSPrint(CHAR8 *StartOfBuffer, UINTN BufferSize, CONST CHAR8 *FormatString,
	    ...)
{
  VA_LIST ap;
  UINTN n;
  VA_START(ap, FormatString);
  n = AsciiVSPrint(StartOfBuffer, BufferSize, FormatString, ap);
  VA_END(ap);
  return n;
}

UINTN
Print(CONST VOID *Format, ...)
{
  VA_LIST ap;
  CONST CHAR8 *line;
  (VOID)Format;
  VA_START(ap, Format);
  line = VA_ARG(ap, CONST CHAR8 *);
  VA_END(ap);
  if (line != NULL) {
    while (*line)
      Com1Put(*line++);
    Com1Put('\r');
    Com1Put('\n');
  }
  return 0;
}
