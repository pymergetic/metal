# Metal's own os.path (see os/__init__.py's note) -- no real cwd concept
# (pymergetic.metal.fs cleans every path relative to the ESP root, see
# fs.c's MetalFsCleanPath), so abspath()/expanduser() are trivial/no-ops
# rather than upstream's os.getcwd()/$HOME-based versions.

import pymergetic.metal.fs as _fs

sep = "/"


def normcase(s):
    return s


def normpath(s):
    return s


def abspath(s):
    return s


def join(*args):
    parts = []
    for a in args:
        if a:
            parts.append(a)
    return "/".join(parts)


def split(path):
    if path == "":
        return ("", "")
    r = path.rsplit("/", 1)
    if len(r) == 1:
        return ("", path)
    head = r[0]
    if not head:
        head = "/"
    return (head, r[1])


def dirname(path):
    return split(path)[0]


def basename(path):
    return split(path)[1]


def exists(path):
    return _fs.stat(path) is not None


lexists = exists


def isdir(path):
    st = _fs.stat(path)
    return st is not None and st[0] == 2


def isfile(path):
    st = _fs.stat(path)
    return st is not None and st[0] == 1
