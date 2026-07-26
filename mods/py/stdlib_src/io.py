# Metal's own io (see os/__init__.py's note on why -- no MICROPY_VFS/uos
# here). Bytes-only: read()/write() always work in bytes, "r"/"w" are not
# distinct from "rb"/"wb" (MICROPY_CPYTHON_COMPAT is off, so there's no
# str.encode()/bytes.decode() to build a text layer on top of).
#
# BytesIO is re-exported here from the *native* uio module (py/objstringio.c,
# MICROPY_PY_IO_BYTESIO), not written in Python: its type carries the real
# MicroPython stream protocol (mp_stream_p_t/mp_get_stream), which is a
# C-level type slot no plain Python class can set -- zlib.py/gzip.py's
# deflate.DeflateIO(stream, ...) calls mp_get_stream_raise() on whatever
# it's handed, so a hand-written FileIO-style class would fail there.
# `uio` (rather than `io`) is the forced-built-in-import alias every
# extensible built-in gets for free (py/objmodule.c) -- this file itself
# occupies the bare "io" name via sys.path, so plain `import io` still
# gets this module, only this one line reaches past it to the native type.
# StringIO rides along the same way -- unittest/__init__.py's own
# io.StringIO() (for capturing traceback text) needs it, and it costs
# nothing extra (unconditionally registered whenever MICROPY_PY_IO is on,
# no separate flag unlike BytesIO/IOBase/BufferedWriter).
from uio import BytesIO, StringIO

import pymergetic.metal.fs as _fs

SEEK_SET = 0
SEEK_CUR = 1
SEEK_END = 2


class FileIO:
    def __init__(self, path, mode="r", encoding=None):
        # encoding is accepted-and-ignored, not honoured: bytes-only (see
        # this module's own docstring) -- callers written against a text
        # layer (e.g. pathlib.py's read_text()/write_text()) still get
        # bytes back, not str, but at least don't crash on the kwarg.
        self._h = _fs.open(path, mode)
        if self._h < 0:
            raise OSError(2, path)
        self._closed = False

    def read(self, n=-1):
        if n < 0:
            pos = _fs.lseek(self._h, 0, SEEK_CUR)
            end = _fs.lseek(self._h, 0, SEEK_END)
            _fs.lseek(self._h, pos, SEEK_SET)
            n = end - pos
        return _fs.read(self._h, n)

    def readline(self):
        out = b""
        while True:
            b = self.read(1)
            if not b:
                break
            out += b
            if b == b"\n":
                break
        return out

    def write(self, data):
        return _fs.write(self._h, data)

    def seek(self, off, whence=SEEK_SET):
        return _fs.lseek(self._h, off, whence)

    def tell(self):
        return _fs.lseek(self._h, 0, SEEK_CUR)

    def close(self):
        if not self._closed:
            _fs.close(self._h)
            self._closed = True

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()
        return False

    def __iter__(self):
        return self

    def __next__(self):
        line = self.readline()
        if not line:
            raise StopIteration
        return line


def open(path, mode="r", encoding=None):
    return FileIO(path, mode, encoding=encoding)
