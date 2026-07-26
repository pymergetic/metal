# Metal's own io (see os/__init__.py's note on why -- no MICROPY_VFS/uos
# here). Bytes-only: read()/write() always work in bytes, "r"/"w" are not
# distinct from "rb"/"wb" (MICROPY_CPYTHON_COMPAT is off, so there's no
# str.encode()/bytes.decode() to build a text layer on top of).
#
# BytesIO is re-exported here from the *native* uio module (py/objstringio.c,
# MICROPY_PY_IO_BYTESIO). `uio` (rather than `io`) is the forced-built-in-
# import alias every extensible built-in gets for free (py/objmodule.c) --
# this file itself occupies the bare "io" name via sys.path, so plain
# `import io` still gets this module, only this one line reaches past it to
# the native types. StringIO rides along the same way -- unittest/__init__.py's
# own io.StringIO() (for capturing traceback text) needs it, and it costs
# nothing extra (unconditionally registered whenever MICROPY_PY_IO is on, no
# separate flag unlike BytesIO/IOBase/BufferedWriter).
#
# FileIO subclasses the native IOBase (py/modio.c, MICROPY_PY_IO_IOBASE) --
# subclassing wires readinto()/write()/ioctl() straight into the real
# MicroPython stream protocol slot (mp_stream_p_t), the same C-level type
# slot BytesIO's own native type carries, which zlib.py/gzip.py's
# deflate.DeflateIO(stream, ...) requires (it calls mp_get_stream_raise() on
# whatever it's handed) -- so DeflateIO can wrap a real on-disk file the
# same way it already wraps a BytesIO.
from uio import BytesIO, IOBase, StringIO

import pymergetic.metal.fs as _fs

SEEK_SET = 0
SEEK_CUR = 1
SEEK_END = 2

# Stream ioctl request codes IOBase's native protocol dispatches to
# ioctl(self, request, arg) -- see py/stream.h; only flush/close are
# meaningful for a plain fd-backed file (seek/poll/etc go through this
# object's own seek()/tell() methods directly instead, called by name, not
# through the C stream protocol -- DeflateIO/mp_stream_* never need them).
_IOCTL_FLUSH = 1
_IOCTL_CLOSE = 4
_EINVAL = 22


class FileIO(IOBase):
    def __init__(self, path, mode="r", encoding=None):
        # encoding is accepted-and-ignored, not honoured: bytes-only (see
        # this module's own docstring) -- callers written against a text
        # layer (e.g. pathlib.py's read_text()/write_text()) still get
        # bytes back, not str, but at least don't crash on the kwarg.
        self._h = _fs.open(path, mode)
        if self._h < 0:
            raise OSError(2, path)
        self._closed = False

    def readinto(self, buf):
        # Plain per-byte copy, not buf[:n] = data -- bytearray slice
        # *assignment* is gated behind MICROPY_PY_ARRAY_SLICE_ASSIGN
        # (py/objarray.c), off at this build's MICROPY_CONFIG_ROM_LEVEL_MINIMUM
        # (see this module's other Metal patch notes on the same rom-level
        # tradeoff); single-index item assignment has no such gate.
        data = _fs.read(self._h, len(buf))
        n = len(data)
        for i in range(n):
            buf[i] = data[i]
        return n

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

    def ioctl(self, request, arg):
        if request == _IOCTL_FLUSH:
            return 0
        if request == _IOCTL_CLOSE:
            self.close()
            return 0
        return -_EINVAL

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
