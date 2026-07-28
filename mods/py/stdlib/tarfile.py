# Metal's own tarfile -- not micropython-lib's upstream package (that one
# needs uctypes, which this build doesn't carry). Backed directly by
# pymergetic.metal.tar.* (real sync C bindings over util/tar.c's own
# read/write ustar implementation, already used by the wasm-guest side --
# see docs/MICROPYTHON.md). Whole-archive-in-memory only: no streaming to
# a file descriptor, no GNU/PAX long-name support (ustar's 100-byte name
# field, same limit util/tar.h documents) -- deliberately small, not a
# drop-in for CPython's tarfile.

import pymergetic.metal.tar as _tar


class TarInfo:
    def __init__(self, name, size, isdir):
        self.name = name
        self.size = size
        self.isdir_ = isdir

    def isdir(self):
        return self.isdir_

    def isfile(self):
        return not self.isdir_


class TarFile:
    def __init__(self, data):
        self._h = _tar.open_read(data)

    def getnames(self):
        names = []
        for info in self.getmembers():
            names.append(info.name)
        return names

    def getmembers(self):
        out = []
        while True:
            r = _tar.next(self._h)
            if r != 1:
                break
            out.append(TarInfo(_tar.name(self._h), _tar.size(self._h), _tar.is_dir(self._h) == 1))
        return out

    def extractfile(self, info):
        # getmembers() already consumed the archive's own read cursor, so
        # extraction re-opens a fresh cursor and walks up to the wanted
        # entry again -- simplest correct thing for the small archives
        # this build actually deals with (mods packages, not multi-MB
        # tars), matching TarFile.__init__'s own "whole thing in RAM" cost.
        return _tarfile_extract_by_name(self, info.name)

    def close(self):
        _tar.close_read(self._h)

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()
        return False


def _tarfile_extract_by_name(tf, name):
    tf.close()
    tf._h = _tar.open_read(tf._data)
    while True:
        r = _tar.next(tf._h)
        if r != 1:
            raise KeyError(name)
        if _tar.name(tf._h) == name:
            size = _tar.size(tf._h)
            return _tar.read(tf._h, size)


def open_bytes(data):
    tf = TarFile(data)
    tf._data = data
    return tf


class TarWriter:
    def __init__(self, cap=65536):
        self._h = _tar.open_write(cap)

    def add_bytes(self, name, data):
        _tar.put(self._h, name, 0, data)

    def add_dir(self, name):
        _tar.put(self._h, name, 1, None)

    def finish(self):
        return _tar.finish(self._h)

    def close(self):
        _tar.close_write(self._h)

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        if exc_type is None:
            self.finish()
        else:
            self.close()
        return False
