from . import _common

_KW = (
    "auto break case char const continue default do double else enum extern "
    "float for goto if inline int long register restrict return short signed "
    "sizeof static struct switch typedef union unsigned void volatile while "
    "bool true false NULL uint8_t uint16_t uint32_t uint64_t int8_t int16_t "
    "int32_t int64_t size_t ssize_t uintptr_t intptr_t"
).split()


def highlight(src):
  return _common.c_style(src, _KW, "pp")
