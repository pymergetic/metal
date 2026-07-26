# Reimplement, because CPython3.3 impl is rather bloated
import os

import io


def copyfile(src, dst):
    """Not part of micropython-lib's shutil -- CPython's shutil has it and
    real callers expect it; a thin io.open()+copyfileobj() wrapper is all
    it ever needs to be."""
    with io.open(src, "rb") as fsrc, io.open(dst, "wb") as fdst:
        copyfileobj(fsrc, fdst)


def rmtree(d):
    if not d:
        raise ValueError

    for name, type, *_ in os.ilistdir(d):
        path = d + "/" + name
        if type & 0x4000:  # dir
            rmtree(path)
        else:  # file
            os.unlink(path)
    os.rmdir(d)


def copyfileobj(src, dest, length=512):
    if hasattr(src, "readinto"):
        # buf[:sz] here is a slice *read* (always available, unlike slice
        # *assignment* -- see io.py's FileIO.readinto() note on
        # MICROPY_PY_ARRAY_SLICE_ASSIGN); no memoryview() needed either,
        # which is gated off the same way at this build's ROM level.
        buf = bytearray(length)
        while True:
            sz = src.readinto(buf)
            if not sz:
                break
            if sz == length:
                dest.write(buf)
            else:
                dest.write(buf[:sz])
    else:
        while True:
            buf = src.read(length)
            if not buf:
                break
            dest.write(buf)


# disk_usage() dropped: upstream micropython-lib's version calls
# os.statvfs(), which this build's os/__init__.py never implements (no
# statvfs-shaped facade over pymergetic.metal.fs) -- dead code that would
# AttributeError on the first real call, not a supported surface.
