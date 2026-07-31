from . import _common

_KW = (
    "if then else elif fi case esac for while until do done in function "
    "select time coproc "
    "source return exit break continue declare local export readonly "
    "set unset shift trap eval exec"
).split()


def highlight(src):
  # No // /* — only # line comments (hash_mode cm).
  return _common.c_style(src, _KW, "cm")
