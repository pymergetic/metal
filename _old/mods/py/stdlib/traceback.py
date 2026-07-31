import sys


def format_tb(tb, limit):
    return ["traceback.format_tb() not implemented\n"]


def format_exception_only(type, value):
    return [repr(value) + "\n"]


def format_exception(etype, value, tb, limit=None, chain=True):
    return format_exception_only(etype, value)


def print_exception(t, e, tb, limit=None, file=None, chain=True):
    # sys.print_exception (py/modsys.c) already defaults its own missing
    # 2nd arg to the platform's plat_print stream -- MICROPY_PY_SYS_STDFILES
    # is off in this minimal build (docs/MICROPYTHON.md's stdlib
    # categorization), so unlike upstream this must not touch sys.stdout
    # itself, just let the 1-arg form fall back the same way.
    # sys.print_exception (py/modsys.c) is a MicroPython-only extension to
    # sys, not in CPython's typeshed stub the linter uses -- type: ignore
    # here, not a stub rewrite, since replacing typings/sys.pyi would mean
    # re-declaring the rest of the real sys module by hand.
    if file is None:
        sys.print_exception(e)  # type: ignore[attr-defined]
    else:
        sys.print_exception(e, file)  # type: ignore[attr-defined]


def print_exc(limit=None, file=None, chain=True):
    print_exception(*sys.exc_info(), limit=limit, file=file, chain=chain)


def format_exc(limit=None, chain=True):
    return "".join(format_exception(*sys.exc_info(), limit=limit, chain=chain))
