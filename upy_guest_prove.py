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
print("guest prove ok")
