"""The seat's console REPL, for a caller that is not sitting at the console.

The panel in the corner of the seat's pages is a second viewport on console 0:
it reads the console card's ring by cursor (`GET /console/0?since=`) and sends
lines here (`POST /console/exec?cmd=`). Nothing is captured or mirrored — on
every µPy seat `mp_hal_stdout_tx_strn` already writes through
`pm_metal_console_write`, so what a line prints lands in the same ring the
serial console, the framebuffer and the panel all read. Type a line in the
browser and it appears on the seat's terminal, and the other way round.

One namespace for the whole session, the way a REPL has one: `m` is bound on
first use so `m.serve()` works without an import line.

Frozen under a top-level name for the same reason metal_packs is: a card module
has no __path__ for a Python submodule to hang off.
"""

_g = None


def namespace():
    """The session's globals. Created once, kept for the seat's lifetime."""
    global _g
    if _g is None:
        # Filled key by key, not from a literal: these globals hold whatever
        # the session binds — a module here, any object a typed line leaves
        # behind later.
        _g = {}
        _g["__name__"] = "__console__"
        try:
            import pymergetic.metal as m

            _g["m"] = m
        except ImportError:  # a seat without the module still gets a REPL
            pass
    return _g


def reset():
    """Drop the session's names. The next line starts from a clean namespace."""
    global _g
    _g = None


def _hexval(c):
    if "0" <= c <= "9":
        return ord(c) - 48
    if "a" <= c <= "f":
        return ord(c) - 87
    if "A" <= c <= "F":
        return ord(c) - 55
    return -1


def unquote(s):
    """Percent-decode one query argument, `+` for space included.

    The seat's http card never reads a request body, so the line rides in the
    query string; this is the other half of the browser's encodeURIComponent.
    Bytes are decoded as UTF-8 — a percent-escape is a byte, not a character,
    so decoding per-character would mangle anything non-ASCII.
    """
    out = bytearray()
    i = 0
    n = len(s)
    while i < n:
        c = s[i]
        if c == "+":
            out.append(32)
            i += 1
        elif c == "%" and i + 2 < n:
            hi = _hexval(s[i + 1])
            lo = _hexval(s[i + 2])
            if hi < 0 or lo < 0:
                out.append(ord(c))
                i += 1
            else:
                out.append(hi * 16 + lo)
                i += 3
        else:
            out.append(ord(c))
            i += 1
    try:
        return bytes(out).decode()
    except Exception:  # not UTF-8: show the bytes rather than refusing the line
        return "".join(chr(b) for b in out)


def _tb(e):
    """The traceback, wherever this seat keeps its printer.

    µPy's is sys.print_exception, which CPython does not have and a seat built
    without it does not either; the fallbacks are traceback, then the type and
    message on their own line.
    """
    import sys

    printer = getattr(sys, "print_exception", None)
    if printer is not None:
        printer(e)
        return
    try:
        import traceback

        traceback.print_exc()
    except Exception:
        print("%s: %s" % (type(e).__name__, e))


def _prompt():
    """The prompt to echo the line behind, or nothing.

    A seat whose own REPL is sitting at a prompt has already written it: the
    console's unfinished line is that prompt, and the echo continues it the way
    a locally typed line would. Printing another one there reads as a stutter
    (">>> >>> m.services()"). A seat with nothing pending — a browser tab, a
    board with no serial REPL — has no prompt to continue, so the echo brings
    its own.
    """
    try:
        import pymergetic.metal.console as console

        if console.pending():
            return ""
    except (ImportError, ValueError):  # no console card, or no face for it
        pass
    return ">>> "


def prompt():
    """Sit at a prompt, for a seat that has no REPL loop to write one.

    A seat with a REPL of its own writes this the moment it is ready for the
    next line, which is why the panel shows a `>>> ` waiting before anything is
    typed. The panel's wasm tab has no such loop — it calls run() per line — so
    without this the caret sits on a bare line and the prompt only turns up
    behind a line once it has been sent.

    Guarded like the echo is: a seat already sitting at one keeps the prompt it
    has rather than writing a second.
    """
    p = _prompt()
    if p:
        print(p, end="")
    return 0


def run(line):
    """Run one REPL line. Everything it prints goes to console 0, as does the
    echo of the line itself, so every viewport sees the same session.

    An expression prints its value and a statement does not — the difference a
    REPL makes, without MICROPY_PY_BUILTINS_COMPILE: `eval` refuses a statement
    with a SyntaxError before running any of it, so falling through to `exec`
    cannot run it twice.
    """
    line = line.strip()
    if not line:
        return 0
    g = namespace()
    print(_prompt() + line)
    try:
        try:
            value = eval(line, g, g)
        except SyntaxError:
            exec(line, g, g)
        else:
            if value is not None:
                g["_"] = value
                print(repr(value))
    except KeyboardInterrupt:
        print("KeyboardInterrupt")
        return -1
    except Exception as e:
        _tb(e)
        return -1
    return 0
