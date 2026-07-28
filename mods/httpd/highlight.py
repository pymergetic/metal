# Tiny C/header highlighter -> HTML (text nodes escaped).

from util import esc

_C_KW = (
    "auto break case char const continue default do double else enum extern "
    "float for goto if inline int long register restrict return short signed "
    "sizeof static struct switch typedef union unsigned void volatile while "
    "bool true false NULL uint8_t uint16_t uint32_t uint64_t int8_t int16_t "
    "int32_t int64_t size_t ssize_t uintptr_t intptr_t"
).split()


def _c_is_digit(c):
  o = ord(c)
  return 48 <= o <= 57


def _c_is_alpha(c):
  o = ord(c)
  return (65 <= o <= 90) or (97 <= o <= 122)


def _c_is_alnum(c):
  return _c_is_digit(c) or _c_is_alpha(c)


def highlight_c(src):
  if src is None:
    return ""
  if not isinstance(src, str):
    try:
      src = src.decode()
    except Exception:
      src = str(src)
  kw = {}
  for w in _C_KW:
    kw[w] = 1
  out = []
  i = 0
  n = len(src)

  def emit(cls, text):
    if not text:
      return
    if cls:
      out.append('<span class="%s">' % cls)
      out.append(esc(text))
      out.append("</span>")
    else:
      out.append(esc(text))

  while i < n:
    c = src[i]
    if c == "/" and i + 1 < n and src[i + 1] == "/":
      j = i
      while j < n and src[j] != "\n":
        j += 1
      emit("cm", src[i:j])
      i = j
      continue
    if c == "/" and i + 1 < n and src[i + 1] == "*":
      j = i + 2
      while j + 1 < n and not (src[j] == "*" and src[j + 1] == "/"):
        j += 1
      j = j + 2 if j + 1 < n else n
      emit("cm", src[i:j])
      i = j
      continue
    if c == "#":
      j = i
      while j < n and src[j] != "\n":
        j += 1
      emit("pp", src[i:j])
      i = j
      continue
    if c == '"' or c == "'":
      q = c
      j = i + 1
      while j < n:
        if src[j] == "\\":
          j += 2
          continue
        if src[j] == q:
          j += 1
          break
        j += 1
      emit("str", src[i:j])
      i = j
      continue
    if _c_is_digit(c):
      j = i
      while j < n and (_c_is_alnum(src[j]) or src[j] in "xX.uUlL"):
        j += 1
      emit("num", src[i:j])
      i = j
      continue
    if _c_is_alpha(c) or c == "_":
      j = i
      while j < n and (_c_is_alnum(src[j]) or src[j] == "_"):
        j += 1
      word = src[i:j]
      emit("kw" if word in kw else None, word)
      i = j
      continue
    emit(None, c)
    i += 1
  return "".join(out)
