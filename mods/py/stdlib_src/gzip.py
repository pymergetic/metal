# MicroPython gzip module
# MIT license; Copyright (c) 2023 Jim Mussared

# Metal patch: no `builtins.open` here (py_port_stubs.c's mp_builtin_open_obj
# is a permanent OSError stub — real file I/O is Metal's own io.open(), see
# io.py's own note), so gzip.open() calls that directly instead of upstream's
# builtins.open(). io.BytesIO (used below) is re-exported from the native
# uio module by io.py itself — see that file's note on why a pure-Python
# class can't satisfy deflate.DeflateIO's stream protocol requirement.
# Metal patch: bare const(x) needs MICROPY_COMP_CONST, off at this build's
# MICROPY_CONFIG_ROM_LEVEL_MINIMUM (see zlib.py's own note) -- plain literal.
_WBITS = 15

import io, deflate


def GzipFile(fileobj):
    return deflate.DeflateIO(fileobj, deflate.GZIP, _WBITS)


def open(filename, mode="rb"):
    return deflate.DeflateIO(io.open(filename, mode), deflate.GZIP, _WBITS, True)


if hasattr(deflate.DeflateIO, "write"):

    def compress(data):
        f = io.BytesIO()
        with GzipFile(fileobj=f) as g:
            g.write(data)
        return f.getvalue()


def decompress(data):
    f = io.BytesIO(data)
    with GzipFile(fileobj=f) as g:
        return g.read()
