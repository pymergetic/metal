from . import _common


def highlight(src):
  src = _common.as_str(src)
  out = []
  i = 0
  n = len(src)

  while i < n:
    if src.startswith("<!--", i):
      j = src.find("-->", i + 4)
      j = n if j < 0 else j + 3
      _common.emit_span(out, "cm", src[i:j])
      i = j
      continue
    if src.startswith("```", i):
      k = src.find("\n```", i + 3)
      if k < 0:
        _common.emit_span(out, "str", src[i:])
        break
      end = k + 4
      while end < n and src[end] != "\n":
        end += 1
      if end < n:
        end += 1
      _common.emit_span(out, "str", src[i:end])
      i = end
      continue
    if (i == 0 or src[i - 1] == "\n") and src[i] == "#":
      k = i
      while k < n and src[k] != "\n":
        k += 1
      _common.emit_span(out, "kw", src[i:k])
      i = k
      continue
    if src[i] == "`":
      j = i + 1
      while j < n and src[j] != "`" and src[j] != "\n":
        j += 1
      if j < n and src[j] == "`":
        _common.emit_span(out, "str", src[i : j + 1])
        i = j + 1
        continue
    if src[i] == "[":
      rb = src.find("]", i + 1)
      if rb > i and rb + 1 < n and src[rb + 1] == "(":
        re = src.find(")", rb + 2)
        if re > rb:
          _common.emit_span(out, "pp", src[i : re + 1])
          i = re + 1
          continue
    _common.emit_span(out, None, src[i])
    i += 1
  return "".join(out)
