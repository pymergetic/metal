from . import _common

_KW = (
    "as async await break const continue crate dyn else enum extern false fn "
    "for if impl in let loop match mod move mut pub ref return self Self "
    "static struct super trait true type unsafe use where while "
    "abstract become box do final macro override priv typeof unsized "
    "virtual yield try"
).split()


def highlight(src):
  return _common.c_style(src, _KW, "pp")
