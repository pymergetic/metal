"""The console ring read by cursor, on the faces every µPy seat has.

The corner panel on the seat's pages is a second viewport on console 0: it
polls `GET /console/<id>?since=` and draws what came in. That read half is
resident C on every seat — the card and the inspect route — so it is proved
on every seat, here, through the in-process face rather than a socket: the
browser cell and the firmware seats have no client to poll themselves with,
and the face answers the same bytes the browser gets.

The write half (a line typed into the panel) is Python on top of the packs
renderer, which only the seats with that renderer have; it is proved in
upy_repl_prove.py.

What this pins:
  * print() on this seat lands in console 0 — without that the panel is a
    boot tree and nothing after it
  * the cursor is a cursor: a poll at the seat's current sequence returns
    nothing, and a line written after it arrives at exactly that cursor
  * a reader that fell behind the ring is told it dropped, not handed a gap
  * the half-written line is visible before its newline
"""

import pymergetic.metal as m
import pymergetic.metal.console as console
import pymergetic.metal.inspect as inspect

if not m.ready():
    raise SystemExit("metal not ready")

_fails = []
_report = []


def check(name, ok, detail: object = ""):
    """Records; prints at the end.

    Printing here would be part of what is under test: this seat mirrors
    print() into the very ring these checks read, so a line reporting one
    check would move the cursor the next one reads, and a line printed while
    a partial line is pending would land inside it.
    """
    if not ok:
        _fails.append("%s%s" % (name, (" — " + str(detail)[:200]) if detail else ""))
    _report.append("  %-52s %s" % (name, "ok" if ok else "FAIL"))


def report(title):
    for line in _report:
        print(line)
    print("")
    if _fails:
        for f in _fails:
            print("FAIL " + f)
        raise SystemExit("%s: %d failed" % (title, len(_fails)))
    print(title)


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


at = console.seq()
print("console-print-prove")
check("print lands in the console ring", console.seq() == at + 1,
      "seq %d -> %d" % (at, console.seq()))

console.write("console-panel-prove\n")
st, body = get("/console/0?since=0")
check("GET /console/0 answers", st == 200, "%s %s" % (st, body[:120]))
check("the ring is in the body", '"lines"' in body and "console-panel-prove" in body,
      body[:200])
check("a first poll is not a drop", '"dropped":0' in body, body[:160])

seq = int(body.split('"seq":')[1].split(",")[0].split("}")[0])
check("seq is a cursor", seq > 0, seq)

st, body = get("/console/0?since=%d" % seq)
check("an up-to-date poll returns nothing new", '"lines":[]' in body, body[:160])
check("and does not move the cursor", ('"seq":%d' % seq) in body, body[:160])

# The colour is the line's meaning here: the boot tree is dim stems and a
# green `ok`, and a reader that loses the escape byte gets "[32mok" as text.
console.write("\x1b[32mgreen-ok\x1b[0m\n")
st, body = get("/console/0?since=%d" % seq)
check("the escape byte survives the JSON", "\\u001b[32mgreen-ok" in body, body[:200])

# A colour line is far longer than the screen it is drawn on: one true-colour
# run costs up to 19 bytes, so the banner's rainbow rows are ~340 bytes for
# ~50 glyphs. The ring stores bytes and truncates rather than wraps, so a ring
# only as wide as a terminal keeps the first few letters of the boot art and
# drops the rest.
wide = ""
want = ""
for i in range(15):
    esc = "[38;2;%d;%d;0m" % (i * 17, 255 - i * 17)
    wide = wide + "\x1b" + esc + "##"
    want = want + "\\u001b" + esc + "##"
wide = wide + "\x1b[0m"
want = want + "\\u001b[0m"
console.write(wide + "\n")
st, body = get("/console/0?since=%d" % seq)
check("a colour line wider than the screen is kept whole", want in body, len(wide))

console.write("after-the-cursor\n")
st, body = get("/console/0?since=%d" % seq)
check("a later line arrives at the cursor", '"after-the-cursor"' in body, body[:200])

seq2 = int(body.split('"seq":')[1].split(",")[0].split("}")[0])
console.write("half-a-line-no-newline")
st, body = get("/console/0?since=%d" % seq2)
check("the unterminated line shows as pending",
      '"pending":"half-a-line-no-newline"' in body, body[:220])
console.write("\n")
st, body = get("/console/0?since=%d" % seq2)
check("and becomes a line once terminated",
      '"half-a-line-no-newline"' in body and '"pending":""' in body, body[:220])

# Scroll a ring right past an old cursor and ask for it again. On the last
# console, not console 0: console 0 is the one this seat is printing on, and
# scrolling its whole history away to prove a point is not a favour to the
# next reader — or to the checks below, which read it.
scratch = console.count() - 1
# A cursor of 0 is not a reader that fell behind, it is a reader that has
# never read; the route says dropped=0 for it on purpose. So take the cursor
# after a line, where falling behind means something.
console.write_id(scratch, "scratch-start\n")
at = console.seq_id(scratch)
for _ in range(80):
    console.write_id(scratch, "scroll\n")
st, body = get("/console/%d?since=%d" % (scratch, at))
check("a reader past the ring is told it dropped", '"dropped":0' not in body, body[:160])
check("and is handed the ring it does have", '"scroll"' in body, body[:160])

st, body = get("/console/%d?since=0" % console.count())
check("an out-of-range console is not served", st != 200, "%s %s" % (st, body[:80]))

# A cursor from before a restart: bigger than anything this ring has. The
# answer's seq being *lower* than the cursor asked for is how a reader knows it
# is looking at a different life of the seat — the panel starts its view over
# instead of stacking the new seat's lines onto the old seat's transcript.
asked = console.seq() + 1000
st, body = get("/console/0?since=%d" % asked)
check("a cursor past the end is handed the end",
      ('"seq":%d' % console.seq()) in body and '"lines":[]' in body, body[:160])
check("and the answer is behind the cursor, which says so",
      int(body.split('"seq":')[1].split(",")[0].split("}")[0]) < asked, body[:160])

# A reader that pulls is a view of the console too. The card cannot push to it,
# so it counts for as long as it keeps asking — and it has to name itself, or
# two tabs would look like one reader (and one reader like a crowd). Counted on
# the scratch console: console 0 is what this seat prints on, and its own
# viewports differ per seat (posix stdout, a framebuffer, none in a cell).
def viewports(body):
    return int(body.split('"viewports":')[1].split(",")[0].split("}")[0])


st, body = get("/console/%d?since=0" % scratch)
base = viewports(body)
st, body = get("/console/%d?since=0&who=4242" % scratch)
check("a reader that names itself is a viewport", viewports(body) == base + 1,
      "%d -> %d" % (base, viewports(body)))
st, body = get("/console/%d?since=0&who=4242" % scratch)
check("and asking again is still one reader", viewports(body) == base + 1,
      viewports(body))
st, body = get("/console/%d?since=0&who=4243" % scratch)
check("a second reader is a second viewport", viewports(body) == base + 2,
      viewports(body))
st, body = get("/console/0?since=0")
check("console 0 counts what it pushes to and what reads it",
      viewports(body) >= 1, body[:120])

# What the panel's second tab needs of a seat: the image list. The panel boots
# the browser build of this tree in the page, and this is where it looks for it
# — so every seat has to answer, including the ones that have no such image and
# say so.
st, body = get("/images")
check("the image face answers on this seat", '"images":' in body, body[:160])

report("upy console ring prove")
