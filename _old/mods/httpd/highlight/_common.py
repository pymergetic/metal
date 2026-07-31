# Shared helpers for httpd.highlight.* language modules.

from httpd.util import esc


def as_str(src):
  if src is None:
    return ""
  if not isinstance(src, str):
    try:
      src = src.decode()
    except Exception:
      src = str(src)
  return src


def is_digit(c):
  o = ord(c)
  return 48 <= o <= 57


def is_alpha(c):
  o = ord(c)
  return (65 <= o <= 90) or (97 <= o <= 122)


def is_alnum(c):
  return is_digit(c) or is_alpha(c)


def emit_span(out, cls, text):
  if not text:
    return
  if cls:
    out.append('<span class="%s">' % cls)
    out.append(esc(text))
    out.append("</span>")
  else:
    out.append(esc(text))


def kw_set(words):
  d = {}
  for w in words:
    d[w] = 1
  return d


def c_style(src, keywords, hash_mode):
  """C-like lexer: // /* */, strings, idents.
  hash_mode: 'pp' (# preprocessor), 'cm' (# line comment), None (plain #).
  """
  src = as_str(src)
  kw = kw_set(keywords)
  out = []
  i = 0
  n = len(src)

  while i < n:
    c = src[i]
    if c == "/" and i + 1 < n and src[i + 1] == "/":
      j = i
      while j < n and src[j] != "\n":
        j += 1
      emit_span(out, "cm", src[i:j])
      i = j
      continue
    if c == "/" and i + 1 < n and src[i + 1] == "*":
      j = i + 2
      while j + 1 < n and not (src[j] == "*" and src[j + 1] == "/"):
        j += 1
      j = j + 2 if j + 1 < n else n
      emit_span(out, "cm", src[i:j])
      i = j
      continue
    if c == "#":
      if hash_mode is None:
        emit_span(out, None, c)
        i += 1
        continue
      j = i
      while j < n and src[j] != "\n":
        j += 1
      emit_span(out, "cm" if hash_mode == "cm" else "pp", src[i:j])
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
      emit_span(out, "str", src[i:j])
      i = j
      continue
    if is_digit(c):
      j = i
      while j < n and (is_alnum(src[j]) or src[j] in "xX.uUlLfF"):
        j += 1
      emit_span(out, "num", src[i:j])
      i = j
      continue
    if is_alpha(c) or c == "_":
      j = i
      while j < n and (is_alnum(src[j]) or src[j] == "_"):
        j += 1
      word = src[i:j]
      emit_span(out, "kw" if word in kw else None, word)
      i = j
      continue
    emit_span(out, None, c)
    i += 1
  return "".join(out)
