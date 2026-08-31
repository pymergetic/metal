# Prove Python PM_MOD_BOOT* run (dep order) and late PM_METAL_DRV_* attach.
from pymergetic.wasmmod.guest import PM_MOD_BOOT, PM_MOD_BOOTDEP, PM_MOD_BOOT_CHILD

order = []


def a_init():
    order.append("a")
    return 0


def b_init():
    if order != ["a"]:
        raise RuntimeError("BOOTDEP order")
    order.append("b")
    return 0


def c_init():
    order.append("c")
    return 0


def nop():
    pass


PM_MOD_BOOT("pymergetic.test.a", a_init, nop)
PM_MOD_BOOT("pymergetic.test.b", b_init, nop)
PM_MOD_BOOT("pymergetic.test.c", c_init, nop)
PM_MOD_BOOTDEP("pymergetic.test.b", "pymergetic.test.a")
PM_MOD_BOOT_CHILD("pymergetic.test.a", "pymergetic.test.c")

import pymergetic.metal as m

if not m.ready():
    raise SystemExit("metal not ready")
if order != ["a", "b", "c"] and order != ["a", "c", "b"]:
    raise SystemExit("boot order %s" % (order,))
if "a" not in order or order.index("a") > order.index("b"):
    raise SystemExit("b before a: %s" % (order,))
if order.index("a") > order.index("c"):
    raise SystemExit("c before a: %s" % (order,))

g = (x for x in (1, 2))
m.register_upy(g)
m.poll()
m.poll()

life = []


def i2():
    life.append("init")
    return 0


def d2():
    life.append("deinit")


def r2():
    return -1


try:
    PM_MOD_BOOT("pymergetic.test.failready", i2, d2, r2)
    raise SystemExit("ready-fail should raise")
except RuntimeError:
    pass
if life != ["init", "deinit"]:
    raise SystemExit("deinit %s" % (life,))

hits = []


def attach(bus, loc0, loc1, loc2, loc3):
    hits.append(bus)
    return 0


m.PM_METAL_DRV_PLATFORM("pymergetic.test.plat", attach)
m.PM_METAL_DRV_ISA("pymergetic.test.isa", 0x300, attach)
if len(hits) < 2:
    raise SystemExit("late drv attach %s" % (hits,))

# Cards resolve from the registry at any depth, with no name list to maintain.
import pymergetic.metal.net.ip as ip

if ip.socket is None:
    raise SystemExit("native card import")
if m.net.ip is not ip:
    raise SystemExit("namespace walk")
if "ip" not in m.net.__dict__:
    raise SystemExit("namespace listing")
print("upy native card import")

import pymergetic.metal.inspect as inspect

try:
    from pymergetic.metal.inspect.face import handle
except ImportError:
    def handle(method, path, role="metal", theme="metal"):
        _ = role, theme
        st = inspect.handle(method, path)
        body = inspect.body()
        return st, body or ""

st, body = handle("GET", "/inspect/self")
if st != 200 or '"name":"pymergetic.metal"' not in body:
    raise SystemExit("inspect self %s %s" % (st, body))
st, body = handle("GET", "/inspect/reg")
if st != 200 or "pymergetic.metal" not in body:
    raise SystemExit("inspect reg %s %s" % (st, body))
st, body = handle("GET", "/health")
if st != 200 or '"ok":true' not in body:
    raise SystemExit("inspect health %s %s" % (st, body))
st, body = handle("GET", "/capabilities")
if st != 200 or '"asgi":true' not in body or '"microdot":true' not in body or '"zenoh":true' not in body:
    raise SystemExit("inspect caps %s %s" % (st, body))
print("upy inspect caps")
import pymergetic.metal.net.dns as dns

if dns.resolve is None:
    raise SystemExit("dns")
print("upy dns")
print("upy socket")
import pymergetic.wasmmod as w

mods = w.modules()
if not any("pymergetic.metal" in m for m in mods):
    raise SystemExit("wasmmod.modules empty")
print("upy inspect")
if m.display.up() != 0:
    raise SystemExit("display up")
if m.console.fb_attach() != 0:
    raise SystemExit("console fb")
print("upy display present")
if m.input.up() != 0:
    raise SystemExit("input up")
print("upy input feed")
if m.console.up() != 0:
    raise SystemExit("console ids")
print("upy console ids")
if m.fs.up() != 0:
    raise SystemExit("fs up")
print("upy fs embed")

# metal.build ledger: the fs-backed change ledger round-trips on this seat —
# seeded lines query, a note_add appends, note_has gates on kind.
import pymergetic.metal.build as build

lines = build.notes_query("pymergetic.metal.build", -1)
if lines is None or '"kind":"decision"' not in lines:
    raise SystemExit("ledger seed %s" % (lines,))
if build.note_add(
    "upy.prove.ledger",
    0,
    "upy prove: ledger append from the unix seat",
    ("pymergetic.metal.fs",),
) != 0:
    raise SystemExit("ledger add")
lines = build.notes_query("upy.prove.ledger", -1)
if lines is None or '"kind":"change"' not in lines:
    raise SystemExit("ledger query %s" % (lines,))
if build.note_has("upy.prove.ledger", 0) != 1:
    raise SystemExit("ledger has")
if build.note_has("never.noted", 0) != 0:
    raise SystemExit("ledger gate")
print("upy ledger round-trip")

# metal.build accessor spine: b.at(fqn, name) joins the live registry, the
# embedded source table (lang/doc/file/line), the ledger notes, and the build
# record. at() on a real face, at_info() carrying the joined answer, at_ast()
# dispatching C to the Phase-12 editor leaf.
h = build.at("pymergetic.metal.build", "pm_metal_build_at")
if h == 0:
    raise SystemExit("at resolve")
info = build.at_info(h)
if info is None:
    raise SystemExit("at info")
if info[0] != "pymergetic.metal.build" or info[1] != "pm_metal_build_at":
    raise SystemExit("at identity %r" % (info,))
if info[2] != "fn" or info[3] != "c":
    raise SystemExit("at kind/lang %r" % (info,))
if "const char *" not in info[4]:
    raise SystemExit("at sig %r" % (info,))
if len(info[8]) == 0 or info[9] != "__impl__.c" or info[10] == 0:
    raise SystemExit("at doc %r" % (info,))
if info[11] is not None and len(info[11]) == 0:
    raise SystemExit("at notes %r" % (info,))
ast = build.at_ast(h)
if ast is None or ast[0] != 1 or ast[1] != "c":
    raise SystemExit("at ast %r" % (ast,))
if build.at("no.such.card", None) != 0:
    raise SystemExit("at negative")
print("upy accessor spine")

# metal.edit C editor: parse/locate/set_define/set_fn_body through the
# nativecall bridges, then the write-back gates (no note -> refusal, note +
# typecheck -> fs write) — the Phase 12 editor prove on the unix seat.
import pymergetic.metal.edit as edit

SRC = (
    "#include <stdint.h>\n"
    "#define EDIT_PROBE_BUF 64\n"
    "\n"
    "int32_t edit_probe_add(int32_t a) {\n"
    "    return a + EDIT_PROBE_BUF;\n"
    "}\n"
)
h = edit.parse_c(SRC)
if h is None:
    raise SystemExit("edit parse")
n = edit.locate(h, "define", "EDIT_PROBE_BUF")
if n is None or n[1] != "EDIT_PROBE_BUF" or n[2] != 2:
    raise SystemExit("edit locate %r" % (n,))
fn = edit.locate(h, "fn", "edit_probe_add")
if fn is None or fn[2] != 4:
    raise SystemExit("edit locate fn %r" % (fn,))
out = edit.set_define(h, "EDIT_PROBE_BUF", "128")
if out is None or "#define EDIT_PROBE_BUF 128" not in out:
    raise SystemExit("edit set_define")
if "return a + EDIT_PROBE_BUF;" not in out:
    raise SystemExit("edit set_define collateral")
body = edit.set_fn_body(h, "edit_probe_add", " return a * 2; ")
if body is None or "return a * 2;" not in body:
    raise SystemExit("edit set_fn_body")
if "return a + EDIT_PROBE_BUF;" in body:
    raise SystemExit("edit set_fn_body old body remains")
tc = edit.typecheck_c(out)
if tc is None or tc[0] != 0:
    raise SystemExit("edit typecheck %r" % (tc,))
bad = edit.typecheck_c("int f( {\n")
if bad is None or bad[0] == 0 or len(bad[1]) == 0:
    raise SystemExit("edit typecheck broken %r" % (bad,))
# the write gates: no note -> refusal, nothing written
wb = edit.write_back("upy.edit.probe", "/src/upy_edit_probe.c", out)
if wb is None or wb[0] == 0 or "ledger note" not in wb[1]:
    raise SystemExit("edit write no-note %r" % (wb,))
# note -> write
if build.note_add("upy.edit.probe", 0, "phase 12 upy editor prove") != 0:
    raise SystemExit("edit note_add")
wb = edit.write_back("upy.edit.probe", "/src/upy_edit_probe.c", out)
if wb is None or wb[0] != 0:
    raise SystemExit("edit write %r" % (wb,))
print("upy editor")

# metal.workspace: the full card tree materializes out of the embedded src
# table into the fs card, byte-identical on read-back, idempotent on a second
# walk — the Phase 14 workspace prove on the unix seat. The mirror is a
# host-side projection (checked by the host C test); here fs is the truth.
import pymergetic.metal.workspace as workspace

n = workspace.materialize()
if not isinstance(n, tuple) or n[0] != 0 or n[1] == 0:
    raise SystemExit("workspace materialize %r" % (n,))
count = n[1]
st = m.fs.stat("/src/pymergetic/metal/build/__impl__.c")
if not isinstance(st, tuple) or st[0] != 0 or st[1] == 0:
    raise SystemExit("workspace fs stat %r" % (st,))
body = m.fs.read("/src/pymergetic/metal/build/__impl__.c", 32)
if not isinstance(body, bytes) or len(body) != 32:
    raise SystemExit("workspace fs read %r" % (body,))
if body[:25] != b"/* pymergetic.metal.build":
    raise SystemExit("workspace bytes %r" % (body[:25],))
n2 = workspace.materialize()
if not isinstance(n2, tuple) or n2[0] != 0 or n2[1] != count:
    raise SystemExit("workspace idempotent %r" % (n2,))
if workspace.file_count() != count:
    raise SystemExit("workspace count %r" % (workspace.file_count(),))
print("upy workspace")
if m.process.up() != 0:
    raise SystemExit("process up")
print("upy process")
if m.net.ssh.up() != 0:
    raise SystemExit("ssh up")
print("upy ssh session")

# net.zenoh: the card must be importable, peer-configurable, and its open step
# must be cooperative — up() returns immediately even with no listener up, a few
# poll() steps must not hang the µPy guest, and a 16-byte ZID must resolve.
if m.net.zenoh is None:
    raise SystemExit("zenoh card")
import pymergetic.metal.net.zenoh as zenoh
if zenoh.peer is None:
    raise SystemExit("zenoh peer")
if zenoh.peer(0x7F000001, 7447, 0) != 0:
    raise SystemExit("zenoh peer cfg")
zenoh.up()
for _ in range(4):
    zenoh.poll()
zid = zenoh.zid()
if not isinstance(zid, bytes) or len(zid) != 16:
    raise SystemExit("zenoh zid %r" % (zid,))
print("upy zenoh")

# net.swarm.*: the three fleet cards must be importable and their stateless,
# no-session faces provable on this seat. Callback-taking faces (offer, declare)
# are host-C only by design; the off-on defers, the membership node identity,
# and the discovery arm/teardown must all hold with no peer present.
import pymergetic.metal.net.swarm.membership as sm
import pymergetic.metal.net.swarm.task as st
import pymergetic.metal.net.swarm.discovery as sd

if sm is None or st is None or sd is None:
    raise SystemExit("swarm card import")
if sm.alive() != 0:
    raise SystemExit("swarm membership alive not-open")
if sm.stop() != 0:
    raise SystemExit("swarm membership stop noop")
if sm.start("fleet") != 0:
    raise SystemExit("swarm membership start defer")
nid = sm.node_id()
if not isinstance(nid, bytes) or len(nid) != 32:
    raise SystemExit("swarm membership node_id %r" % (nid,))
if st.offering() != 0 or st.declaring() != 0:
    raise SystemExit("swarm task arms not-open")
if st.done() != 0:
    raise SystemExit("swarm task done noop")
if st.dispatch("render", b"\x01\x02\x03") != 0:
    raise SystemExit("swarm task dispatch defer")
sr = sd.scout()
if not isinstance(sr, tuple) or len(sr) != 2 or sr[0] != 0:
    raise SystemExit("swarm discovery scout no-peer %r" % (sr,))
if sd.answer_on() != 1:
    raise SystemExit("swarm discovery answer on")
if sd.answer_on() != 0:
    raise SystemExit("swarm discovery answer already armed")
sd.answer_off()
if sd.pump() != 0:
    raise SystemExit("swarm discovery pump")
print("upy swarm")

# metal.jit.py object loop: µPy compiles Python to mpy bytes in-process —
# its own bytecode compiler (py/compile.c + persistentcode.c), no host
# tool anywhere — then loads those bytes back into a live module. The
# Python twin of the C (TCC) and Rust (micro-rustc) object proves.
import pymergetic.metal.jit.py as jpy

_selfhost_src = (
    "VAL = 11\n"
    "def ping(x):\n"
    "    return x + VAL\n"
)
_mpy = jpy.object_compile(_selfhost_src, "upy_jitpy_selfhost")
if not isinstance(_mpy, bytes) or len(_mpy) < 8 or _mpy[:1] != b"M":
    raise SystemExit("jit py compile %r" % (type(_mpy),))
if jpy.object_load(_mpy, "upy_jitpy_selfhost") != 0:
    raise SystemExit("jit py load")
# the module was born at runtime (object_load published it into the loader
# dict) — __import__ is the same call the import statement lowers to, but
# the analyzer can chase it no further than the card's own face.
upy_jitpy_selfhost = __import__("upy_jitpy_selfhost")

if upy_jitpy_selfhost.ping(31) != 42:
    raise SystemExit("jit py run")
print("upy jit py object loop")

# metal.build py-card rebuild: a real impl="py" card (pymergetic.wasmmod.net)
# through the build face's unit_compile on this POSIX µPy seat — its .py
# muscle from the embed table -> mpy bytecode in-process. The rebuild route
# is the same /build/<fqn> the browser console serves.
st, body = handle("POST", "/build/pymergetic.wasmmod.net")
if st != 200 or '"rebuild":"ok"' not in body:
    raise SystemExit("build py rebuild %s %s" % (st, body))
if '"fqn":"pymergetic.wasmmod.net"' not in body:
    raise SystemExit("build py fqn %s" % (body,))
print("upy build py unit_compile")

# metal.jit.cpp REPL rebuild loop: the C++ card transpiles ITS OWN source
# from the kernel fs (workspace) through lex -> parse -> lower, the lowered
# C re-lowers byte-identical (the fixed point), TCC makes a real ELF object,
# and the build card's relocator links it in-process — the linked card's own
# faces resolve. C++ -> C -> TCC -> link, all from Python, no host cc or
# cc1plus anywhere. The Python-level twin of selfhost_cycle.sh stage 6b.
import pymergetic.metal.jit.cpp as jcpp

_cpp_src = m.fs.read("/src/pymergetic/metal/jit/cpp/__impl__.c", 300000)
if not isinstance(_cpp_src, bytes) or len(_cpp_src) < 100000:
    raise SystemExit("cpp own source %r" % (type(_cpp_src),))
_cpp_src = _cpp_src.decode()
_c1 = jcpp.lower(jcpp.parse(jcpp.lex(_cpp_src)))
if not isinstance(_c1, str) or len(_c1) < 100000:
    raise SystemExit("cpp lower %r" % (type(_c1),))
_c2 = jcpp.lower(jcpp.parse(jcpp.lex(_c1)))
if _c1 != _c2:
    raise SystemExit("cpp fixed point")
import pymergetic.metal.jit.c as jc

_cpp_obj = jc.object_compile(_c1)
if not isinstance(_cpp_obj, bytes) or len(_cpp_obj) < 4096:
    raise SystemExit("cpp object %r" % (type(_cpp_obj),))
if _cpp_obj[:4] != b"\x7fELF":
    raise SystemExit("cpp object not ELF")
_lk = build.link("upy.cppx.selfhost", _cpp_obj)
if not isinstance(_lk, tuple) or _lk[0] != 0:
    raise SystemExit("cpp link %r" % (_lk,))
if build.artifact_lookup("pm_metal_jit_cpp_lower") is None:
    raise SystemExit("cpp linked lower face")
if build.artifact_lookup("pm_metal_jit_cpp_lex") is None:
    raise SystemExit("cpp linked lex face")
build.artifact_destroy()
print("upy jit cpp rebuild loop")

# artifact call: the REPL links a small scalar C object and CALLS into the
# linked image — the same face the C feeds use, now from Python. Two arities
# (void and 2-arg) plus the refuse path (unknown symbol). The artifact slot
# is live here; destroy releases it.
_call_src = (
    "int magic(void) { return 7; }\n"
    "int scale(int a, int b) { return a * 100 + b; }\n"
)
_call_obj = jc.object_compile(_call_src)
if not isinstance(_call_obj, bytes) or _call_obj[:4] != b"\x7fELF":
    raise SystemExit("call object %r" % (type(_call_obj),))
_lk2 = build.link("upy.artifact.call", _call_obj)
if not isinstance(_lk2, tuple) or _lk2[0] != 0:
    raise SystemExit("call link %r" % (_lk2,))
if build.artifact_call("magic") != 7:
    raise SystemExit("artifact call magic")
if build.artifact_call("scale", 3, 21) != 321:
    raise SystemExit("artifact call scale")
try:
    build.artifact_call("no_such_fn")
    raise SystemExit("artifact call unknown should refuse")
except Exception:
    pass
build.artifact_destroy()
print("upy artifact call loop")

# cross-compile: the unix REPL asks jit.c for a wasm32 object (the second,
# pm_tccw_-prefixed TCC instance), the build card links it through the wasm
# loader (magic-byte routing, same face), and artifact_call reaches the
# module's exports through the WAMR trampoline — all from Python, on the
# ELF seat. TARGET 0 is the seat's own backend (ELF), refused politely
# where no backend is linked (browser has no ELF relocator).
_cross_src = (
    "int cross_magic(void) { return 42; }\n"
    "int cross_add(int a, int b) { return a + b; }\n"
)
_cross_obj = jc.object_compile(_cross_src, target=1)
if not isinstance(_cross_obj, bytes) or len(_cross_obj) < 8:
    raise SystemExit("cross object %r" % (type(_cross_obj),))
if _cross_obj[:4] != b"\x00asm":
    raise SystemExit("cross object not wasm")
_lk3 = build.link("upy.cross.wasm", _cross_obj)
if not isinstance(_lk3, tuple) or _lk3[0] != 0:
    raise SystemExit("cross link %r" % (_lk3,))
if build.artifact_call("cross_magic") != 42:
    raise SystemExit("cross call magic")
if build.artifact_call("cross_add", 19, 23) != 42:
    raise SystemExit("cross call add")
build.artifact_destroy()
print("upy cross compile wasm loop")

# metal.process memory budget: the REPL caps its own compile scratch and the
# compile bridges honor it — a small compile still works inside the budget,
# a compile whose object cannot fit the cap is refused (None), not an abort.
# Then the cap is lifted and the same compile works again. Same faces on
# every seat; firmware refuses politely (it has no persistent-code save
# anyway).
import pymergetic.metal.process as proc

if proc.budget(0) != 0:
    raise SystemExit("process budget default %r" % (proc.budget(0),))
if proc.budget_set(0, 64 * 1024) != 0:
    raise SystemExit("process budget set")
if proc.budget(0) != 64 * 1024:
    raise SystemExit("process budget readback %r" % (proc.budget(0),))
if proc.budget_used(0) <= 0:
    raise SystemExit("process budget used %r" % (proc.budget_used(0),))
_mpy2 = jpy.object_compile(_selfhost_src, "upy_jitpy_budgeted")
if not isinstance(_mpy2, bytes) or len(_mpy2) < 8:
    raise SystemExit("jit py budgeted compile")
# an ~80KB string constant: the mpy object embeds it whole, so it cannot fit
# a 64KB compile cap — refused with None (or an exception), never an abort.
# The cap stays set: it is the REPL's own budget, and this is the prove's
# last compile — the budget is per-process accounting, not a leak.
_big = 'v = "' + "_x" * 40000 + '"'
try:
    _r = jpy.object_compile(_big, "upy_jitpy_toobig")
    if _r is not None:
        raise SystemExit("jit py past-cap compile should refuse")
except Exception as _e:
    print("jit py past-cap refused: %s" % (_e,))
print("upy process budget loop")

# pymergetic.types — the universal 16-byte value crosses the Python face as
# 16-byte bytes. Constructors, probes (kind/is_nil), field access by
# name_hash, mutation via rebound value bytes, and the descriptor registry
# round-trip. Same faces on every seat.
import pymergetic.types as t

_tn = t.nil()
if t.kind(_tn) != 0:
    raise SystemExit("types nil kind %r" % (t.kind(_tn),))
_ti = t.i32(42)
if t.kind(_ti) != 1:
    raise SystemExit("types i32 kind")
if t.is_nil(_ti):
    raise SystemExit("types i32 is_nil")
_tf = t.f64(2.5)
if t.kind(_tf) != 6:
    raise SystemExit("types f64 kind")
_ts = t.str("hello")
if t.kind(_ts) != 8:
    raise SystemExit("types str kind")
_hx = t.name_hash("x")
if _hx <= 0 or t.name_hash("x") != _hx:
    raise SystemExit("types name hash")
if t.registry_find("pymergetic.types.i32") is None:
    raise SystemExit("types registry find i32")
if t.registry_find("pymergetic.types.Entity") is None:
    # Entity/Person/Point are prove-card types (cargo __tests__.c registers
    # them on the host seat); a resident seat still has the list descriptor.
    if t.registry_find("pymergetic.types.list") is None:
        raise SystemExit("types registry find list")
if t.registry_find("pymergetic.types.Nope") is not None:
    raise SystemExit("types registry find Nope")
if t.registry_count() < 12:
    raise SystemExit("types registry count %r" % (t.registry_count(),))
print("upy types value loop")
print("guest prove ok")
