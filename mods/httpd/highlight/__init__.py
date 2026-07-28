# Language highlighters for iface file view — dispatch by path; unknown = escape only.

from httpd.util import esc

from . import c as _c
from . import cpp as _cpp
from . import md as _md
from . import py as _py
from . import rust as _rust
from . import sh as _sh

# Re-export per-language entry points (tests / direct use).
highlight_c = _c.highlight
highlight_cpp = _cpp.highlight
highlight_md = _md.highlight
highlight_py = _py.highlight
highlight_rust = _rust.highlight
highlight_sh = _sh.highlight

_C_EXT = (".c", ".h", ".S", ".s")
_CPP_EXT = (".cpp", ".cc", ".cxx", ".hpp", ".hh", ".hxx", ".C")
_PY_EXT = (".py", ".pyi")
_RS_EXT = (".rs",)
_SH_EXT = (".sh", ".bash")
_MD_EXT = (".md", ".markdown")


def _basename(path):
  if path is None:
    return ""
  p = str(path)
  return p.rsplit("/", 1)[-1]


def highlight_file(path, text):
  """Pick highlighter from path; unknown types are escaped only (no C fallback)."""
  base = _basename(path)
  if base == "LICENSE" or base.endswith(".md") or base.endswith(".markdown"):
    return highlight_md(text)
  lower = base.lower()
  for ext in _PY_EXT:
    if lower.endswith(ext):
      return highlight_py(text)
  for ext in _C_EXT:
    if lower.endswith(ext) or base.endswith(ext):
      return highlight_c(text)
  for ext in _CPP_EXT:
    if lower.endswith(ext) or base.endswith(ext):
      return highlight_cpp(text)
  for ext in _RS_EXT:
    if lower.endswith(ext):
      return highlight_rust(text)
  for ext in _SH_EXT:
    if lower.endswith(ext):
      return highlight_sh(text)
  return esc(text if text is not None else "")
