# Import every seat registered in pymergetic.metal.reg (runtime SoT).
# Run via: make -C tests/matrix browser
from pymergetic.metal import reg


def _imp(s):
    # Progressive import: builtin leaf, else parent nest attr (boot.tree etc.).
    # Also avoids SyntaxError on keyword seat name `async`.
    parts = ("pymergetic", "metal") + tuple(s.split("."))
    cur = None
    for i in range(len(parts)):
        dotted = ".".join(parts[: i + 1])
        leaf = parts[i]
        try:
            cur = __import__(dotted, None, None, (leaf,))
        except ImportError:
            cur = getattr(cur, leaf)
    return cur


n = reg.seat_count()
assert n > 0
for i in range(n):
    path, _kind, _fw, browser, _has_test = reg.seat_at(i)
    if not browser:
        continue
    _ = _imp(path)
print("MATRIX_BROWSER_OK", n)
