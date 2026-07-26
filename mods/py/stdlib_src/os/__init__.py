# Metal's own `os`, not micropython-lib's (that one is `from uos import *` --
# this build has no MICROPY_VFS/uos, Metal FS is its own async-shaped thing,
# not a mounted VFS). Backed directly by pymergetic.metal.fs.* (real sync C
# bindings, one function per op -- see docs/MICROPYTHON.md and
# fs/fs_py_bind.c). Bytes-only: MICROPY_CPYTHON_COMPAT is off in this build
# (see pickle.py's own note), so there is no str.encode()/bytes.decode() to
# build a text-mode layer on top of -- every path here is effectively "rb"/
# "wb" shaped.

import errno

import pymergetic.metal.fs as _fs

sep = "/"


def stat(path):
    st = _fs.stat(path)
    if st is None:
        raise OSError(errno.ENOENT, path)
    type_, size = st
    mode = 0o040000 if type_ == 2 else 0o100000
    return (mode, 0, 0, 0, 0, 0, size, 0, 0, 0)


def mkdir(path):
    if _fs.mkdir(path) != 0:
        raise OSError(errno.EEXIST, path)


def remove(path):
    if _fs.unlink(path) != 0:
        raise OSError(errno.ENOENT, path)


unlink = remove


def rename(old, new):
    if _fs.rename(old, new) != 0:
        raise OSError(errno.ENOENT, old)


def listdir(path="."):
    return _fs.listdir(path)


def ilistdir(path="."):
    # Metal's FS is a flat namespace (no real VFS/mount tree -- see this
    # file's own note), so there is no cheaper way to get (name, mode)
    # pairs than stat()-ing each name listdir() already gave us.
    out = []
    base = path if path.endswith("/") else path + "/"
    for name in listdir(path):
        st = stat(base + name)
        out.append((name, st[0], 0))
    return out


rmdir = remove


def getcwd():
    # No per-task working directory here -- every Metal FS path this
    # module ever hands out is already absolute from the single root.
    return "/"


try:
    from . import path
except ImportError:
    pass
