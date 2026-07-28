HIGHEST_PROTOCOL = 0


# Metal delta: str.encode()/bytes.decode() are gated behind
# MICROPY_CPYTHON_COMPAT (off in this minimal build, see
# docs/MICROPYTHON.md's stdlib categorization) -- upstream's dumps()/loads()
# round-trip through bytes only to immediately decode back to str anyway
# (this "pickle" is really repr()+eval(), never the real binary protocol),
# so dumps()/loads() just work on str directly instead.
def dump(obj, f, proto=0):
    f.write(repr(obj))


def dumps(obj, proto=0):
    return repr(obj)


def load(f):
    s = f.read()
    return loads(s)


def loads(s):
    d = {}
    if "(" in s:
        qualname = s.split("(", 1)[0]
        if "." in qualname:
            pkg = qualname.rsplit(".", 1)[0]
            mod = __import__(pkg)
            d[pkg] = mod
    return eval(s, d)
