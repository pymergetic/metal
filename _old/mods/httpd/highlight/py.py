from . import _common

_KW = (
    "False None True and as assert async await break class continue def del "
    "elif else except finally for from global if import in is lambda "
    "nonlocal not or pass raise return try while with yield"
).split()


def highlight(src):
  return _common.c_style(src, _KW, "cm")
