# Some strings for ctype-style character classification
whitespace = " \t\n\r\v\f"
ascii_lowercase = "abcdefghijklmnopqrstuvwxyz"
ascii_uppercase = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
ascii_letters = ascii_lowercase + ascii_uppercase
digits = "0123456789"
hexdigits = digits + "abcdef" + "ABCDEF"
octdigits = "01234567"
# r-string: "\]" is not a real escape, just a literal backslash + "]" --
# CPython's own Lib/string.py uses the same r-prefix for this exact reason.
punctuation = r"""!"#$%&'()*+,-./:;<=>?@[\]^_`{|}~"""
printable = digits + ascii_letters + punctuation + whitespace


def translate(s, map):
    # Upstream builds this with io.StringIO; Metal's minimal build has no
    # io module yet (needs-glue), so accumulate into a list and join instead
    # — same result, one fewer stdlib dependency.
    parts = []
    for c in s:
        v = ord(c)
        if v in map:
            v = map[v]
            if isinstance(v, int):
                parts.append(chr(v))
            elif v is not None:
                parts.append(v)
        else:
            parts.append(c)
    return "".join(parts)
