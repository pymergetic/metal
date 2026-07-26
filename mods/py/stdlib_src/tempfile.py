import errno
import io
import os
import random
import shutil

_ascii_letters = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"


def _get_candidate_name(size=8):
    return "".join(random.choice(_ascii_letters) for _ in range(size))


class _TemporaryFileWrapper:
    """Not part of micropython-lib's tempfile (only mkdtemp/TemporaryDirectory
    there) -- CPython's tempfile.TemporaryFile()/NamedTemporaryFile() are
    common enough that real callers expect them. Deletes the backing file
    on close(); Metal's FS has no unlink-on-last-close (no open-file-table
    semantics to hook), so "delete=False" isn't offered here -- close()
    always removes it."""

    def __init__(self, path):
        self.name = path
        self._f = io.open(path, "r+b")

    def __getattr__(self, item):
        return getattr(self._f, item)

    def close(self):
        self._f.close()
        try:
            os.unlink(self.name)
        except OSError:
            pass

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()
        return False


def NamedTemporaryFile(suffix=None, prefix=None, dir=None):
    suffix, prefix, dir = _sanitize_inputs(suffix, prefix, dir)
    _try(os.mkdir, dir)
    name = dir + "/" + prefix + _get_candidate_name() + suffix
    io.open(name, "wb").close()
    return _TemporaryFileWrapper(name)


TemporaryFile = NamedTemporaryFile


def _sanitize_inputs(suffix, prefix, dir):
    if dir is None:
        dir = "/tmp"
    if suffix is None:
        suffix = ""
    if prefix is None:
        prefix = ""
    return suffix, prefix, dir


def _try(action, *args, **kwargs):
    try:
        action(*args, **kwargs)
        return True
    except OSError as e:
        if e.errno != errno.EEXIST:
            raise e
    return False


def mkdtemp(suffix=None, prefix=None, dir=None):
    suffix, prefix, dir = _sanitize_inputs(suffix, prefix, dir)

    _try(os.mkdir, dir)

    while True:
        name = _get_candidate_name()
        file = dir + "/" + prefix + name + suffix
        if _try(os.mkdir, file):
            return file


class TemporaryDirectory:
    def __init__(self, suffix=None, prefix=None, dir=None):
        self.name = mkdtemp(suffix, prefix, dir)

    def __repr__(self):
        return "<{} {!r}>".format(self.__class__.__name__, self.name)

    def __enter__(self):
        return self.name

    def __exit__(self, exc, value, tb):
        self.cleanup()

    def cleanup(self):
        _try(shutil.rmtree, self.name)
