"""A line typed into the corner panel, run on the seat.

The panel's write half: `POST /console/exec?cmd=<line>` hands the line to
metal_repl, which runs it in one session whose output goes where every other
print on this seat goes — console 0 — so the panel sees its own echo, value
and traceback through the same cursor poll it already uses for the boot tree.

Proved through the packs renderer's local face, not a socket, so the browser
cell can run it too. The seats that carry that renderer are the seats that
serve pages; the read half (upy_console_prove.py) is on every seat.

What this pins:
  * the line survives the query string: percent-escapes, '+', and a long one
  * it echoes, prints its value, keeps its names between lines, and lands a
    failing line's traceback in the same ring
  * `m` is bound in the session, so the panel is a metal REPL and not a
    bare eval
  * the panel's own stylesheet and script are on the pages this seat renders
"""

import pymergetic.metal as m
import pymergetic.metal.console as console

if not m.ready():
    raise SystemExit("metal not ready")

try:
    import metal_repl
except ImportError:
    from pymergetic.metal.inspect import metal_repl
try:
    import metal_packs
except ImportError:
    from pymergetic.metal.inspect import metal_packs

import pymergetic.metal.inspect as inspect

_fails = []
_report = []


def check(name, ok, detail: object = ""):
    """Records; prints at the end — a check that printed as it went would be
    writing into the very ring the next check reads."""
    if not ok:
        _fails.append("%s%s" % (name, (" — " + str(detail)[:200]) if detail else ""))
    _report.append("  %-52s %s" % (name, "ok" if ok else "FAIL"))


def _body_bytes():
    """Return the body as a str, handling binary content safely.

    MicroPython's FFI decodes const char * returns as UTF-8 str, which
    fails for binary content (e.g. .mjs / .wasm windowed fetches).
    When the raw body isn't valid UTF-8, we read byte-by-byte through
    the card's body_at(i) face and decode with replacement chars."""
    try:
        return inspect.body()
    except UnicodeError:
        n = inspect.body_len()
        if n <= 0:
            return ""
        # MicroPython cannot catch UnicodeError from FFI return conversion,
        # and uctypes.addressof const char * returns a big-endian word in
        # some builds. The safe path: body_at(i) via the card itself.
        out = bytearray()
        for i in range(n):
            ch = inspect.body_at(i)
            if ch < 0:
                break
            out.append(ch)
        try:
            return bytes(out).decode("utf-8", "replace")
        except Exception:
            return str(bytes(out))


def get(path):
    st = inspect.handle("GET", path)
    return st, _body_bytes()


check("the line is percent-decoded",
      metal_repl.unquote("print%28%22a%3Fb%26c%20d%22%29") == 'print("a?b&c d")',
      metal_repl.unquote("print%28%22a%3Fb%26c%20d%22%29"))
check("and '+' is a space", metal_repl.unquote("1+%2B+41") == "1 + 41",
      metal_repl.unquote("1+%2B+41"))

metal_repl.reset()
at = console.seq()
body, _ct = metal_packs.render("/console/exec?cmd=1%20%2B%2041")
check("exec answers rc=0", b'"rc":0' in body, body)
st, tail = get("/console/0?since=%d" % at)
check("the line echoes into the ring", '">>> 1 + 41"' in tail, tail[:240])
check("an expression prints its value", '"42"' in tail, tail[:240])

# A seat whose own REPL is sitting at a prompt has already written it: the
# console's unfinished line is that prompt, and the echo continues it the way a
# locally typed line would rather than printing ">>> >>> 8 * 5".
console.write("\x1b[36m>>>\x1b[0m ")
at = console.seq()
body, _ct = metal_packs.render("/console/exec?cmd=8%20%2A%205")
st, tail = get("/console/0?since=%d" % at)
check("an echo continues a pending prompt", "8 * 5" in tail, tail[:240])
check("and does not print a second one", ">>> 8 * 5" not in tail, tail[:240])

at = console.seq()
body, _ct = metal_packs.render("/console/exec?cmd=x%20%3D%207")
st, tail = get("/console/0?since=%d" % at)
check("a statement prints no value", '"7"' not in tail, tail[:240])

at = console.seq()
body, _ct = metal_packs.render("/console/exec?cmd=x%20%2A%206")
st, tail = get("/console/0?since=%d" % at)
check("names persist between lines", '"42"' in tail, tail[:240])

at = console.seq()
body, _ct = metal_packs.render("/console/exec?cmd=no_such_name_here")
check("a failing line reports rc=-1", b'"rc":-1' in body, body)
st, tail = get("/console/0?since=%d" % at)
check("its traceback lands in the same ring", "NameError" in tail, tail[:300])

at = console.seq()
body, _ct = metal_packs.render("/console/exec?cmd=")
check("an empty line is a no-op", b'"rc":0' in body and console.seq() == at, body)

long_src = "sum([" + ",".join([str(i) for i in range(60)]) + "])"
at = console.seq()
body, _ct = metal_packs.render("/console/exec?cmd=" + long_src.replace("[", "%5B")
                               .replace("]", "%5D").replace(",", "%2C"))
st, tail = get("/console/0?since=%d" % at)
check("a long line survives the query path", '"1770"' in tail,
      "%d chars" % len(long_src))

at = console.seq()
body, _ct = metal_packs.render("/console/exec?cmd=m.console.id%28%29")
st, tail = get("/console/0?since=%d" % at)
check("`m` is bound in the session", '"0"' in tail, tail[:240])

page, _ct = metal_packs.render("/packs/pymergetic.metal.console")
page = page.decode() if isinstance(page, bytes) else page
check("a rendered page pulls in the panel script",
      "/static/seat/console.js" in page, page[:200])
check("and its stylesheet", "/static/css/console.css" in page, page[:200])

# The panel's second tab is a seat of its own: the browser build of this tree,
# fetched from this seat and booted in the page. What the seat owes it is the
# image — the emcc loader and its module, under a board of their own — through
# the same windowed face the factory pane downloads with. The fill differs (a
# seat with no build lists none and the tab says so); the face does not.
st, body = get("/images")
check("the image face answers on this seat", '"images":' in body, body[:160])
has_browser = '"board":"BROWSER"' in body
if has_browser:
    check("the browser image is listed as its own board",
          '"micropython.mjs"' in body and '"micropython.wasm"' in body, body[:400])
    # Window by window, the way the panel fetches it: the loader is 260 kB
    # against a body that holds a fraction of that, and `loadMicroPython` —
    # the entry point the tab calls — lives near its end. A face that only
    # answered the first window would pass a check on offset 0 and hand the
    # panel a truncated module.
    off = 0
    found = False
    while off < 300000:
        st, win = get("/images/BROWSER/micropython.mjs?off=%d" % off)
        if not win:
            break
        if "loadMicroPython" in win:
            found = True
            break
        off = off + len(win)
    check("and its loader is µPy's, window by window", found, "at %d" % off)
else:
    check("or the list says this seat has no image tree",
          '"note"' in body or '"images":[]' in body, body[:160])

for line in _report:
    print(line)
print("")
if _fails:
    for f in _fails:
        print("FAIL " + f)
    raise SystemExit("console panel prove: %d failed" % len(_fails))
print("upy console panel prove")
