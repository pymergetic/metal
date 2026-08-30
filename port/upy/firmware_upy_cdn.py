import pymergetic.metal as m

if not m.ready():
    raise RuntimeError("ready")
print("upy metal ready")

# Cards resolve from the registry at any depth, with no name list to maintain.
import pymergetic.metal.net.ip as ip

if ip.socket is None:
    raise RuntimeError("native card import")
if m.net.ip is not ip:
    raise RuntimeError("namespace walk")
print("upy native card import")

import pymergetic.metal.inspect as inspect

st = inspect.handle("GET", "/inspect/self")
body = inspect.body()
if st != 200 or '"name":"pymergetic.metal"' not in body:
    raise RuntimeError("inspect self")
st = inspect.handle("GET", "/inspect/reg")
body = inspect.body()
if st != 200 or "pymergetic.metal" not in body:
    raise RuntimeError("inspect reg")
print("upy inspect")
st = inspect.handle("GET", "/capabilities")
body = inspect.body()
if st != 200 or '"asgi":true' not in body or '"microdot":true' not in body or '"zenoh":true' not in body:
    raise RuntimeError("inspect caps")
print("upy inspect caps")

# metal.build change ledger: the seed materializes on the fs card and the
# /changes read pane serves it - on firmware seats too (arena fs, no POSIX).
st = inspect.handle("GET", "/changes/pymergetic.metal.build")
body = inspect.body()
if st != 200 or 'decision' not in body:
    raise RuntimeError("changes ledger")
print("upy changes ledger")

# metal.edit C editor (Phase 12): parse/locate/set_define on the firmware seat
# - the editor is resident C, so the span splice works on arena memory with no
# POSIX. The write-back gate (no note -> refusal) is the card contract.
import pymergetic.metal.edit as edit

SRC = (
    "#include <stdint.h>\n"
    "#define EDIT_PROBE_BUF 64\n"
    "int32_t edit_probe_add(int32_t a) {\n"
    "    return a + EDIT_PROBE_BUF;\n"
    "}\n"
)
h = edit.parse_c(SRC)
if h is None:
    raise RuntimeError("edit parse")
n = edit.locate(h, "define", "EDIT_PROBE_BUF")
if n is None or n[1] != "EDIT_PROBE_BUF":
    raise RuntimeError("edit locate")
out = edit.set_define(h, "EDIT_PROBE_BUF", "128")
if out is None or "128" not in out:
    raise RuntimeError("edit set_define")
wb = edit.write_back("fw.edit.probe", "/src/fw_edit_probe.c", out)
if wb is None or wb[0] == 0:
    raise RuntimeError("edit write gate")
print("upy editor")
if m.net.dns.resolve is None:
    raise RuntimeError("dns")
print("upy dns")
print("upy socket")

# net.swarm.*: the three fleet cards must mount on firmware too. Callback faces
# (offer, declare) are host-C only; the off-on defers, the membership node
# identity, and the discovery arm/teardown must hold with no peer present.
import pymergetic.metal.net.swarm.membership as sm
import pymergetic.metal.net.swarm.task as st
import pymergetic.metal.net.swarm.discovery as sd

if sm is None or st is None or sd is None:
    raise RuntimeError("swarm card import")
if sm.alive() != 0 or sm.stop() != 0 or sm.start("fleet") != 0:
    raise RuntimeError("swarm membership not-open")
nid = sm.node_id()
if not isinstance(nid, bytes) or len(nid) != 32:
    raise RuntimeError("swarm membership node_id")
if st.offering() != 0 or st.declaring() != 0 or st.done() != 0:
    raise RuntimeError("swarm task not-open")
if st.dispatch("render", b"\x01\x02\x03") != 0:
    raise RuntimeError("swarm task dispatch defer")
sr = sd.scout()
if not isinstance(sr, tuple) or len(sr) != 2 or sr[0] != 0:
    raise RuntimeError("swarm discovery scout no-peer")
if sd.answer_on() != 1 or sd.answer_on() != 0:
    raise RuntimeError("swarm discovery answer")
sd.answer_off()
if sd.pump() != 0:
    raise RuntimeError("swarm discovery pump")
print("upy swarm")

import pymergetic.wasmmod.net.cdn as cdn

cdn.session_id("sess-1")
# QEMU user-net host gateway (fixed by QEMU SLIRP, not a lab LAN). The prove
# serves extmod/wasmmod/examples/packs there; port 18124 is agreed with
# port/live_cdn.sh (18123 is often left bound by a prior prove).
cdn.configure("http://10.0.2.2:18124", "tok-cdn")
print("upy cdn")
import pymergetic.wasmmod_examples.hello as hello

print("upy pack import")

# Off the box for real: the address came from the wire's DHCP server, and this
# pack comes over TCP from a server that is not us. a_ping is 11 in test_a.
# The guest TCP stack has no retransmit timer yet, so a segment QEMU SLIRP
# delivers late (rare, UEFI seat) fails the fetch outright; one retry covers
# it. The retry is the CDN resilience prove, not a mask: a broken CDN (wrong
# base, no server, 404s) still fails — only the transport hiccup is retried.
test_a = None
for attempt in range(2):
    try:
        import pymergetic.wasmmod_examples.test_a as test_a
        break
    except ImportError as e:
        if "no pack" not in str(e) or attempt == 1:
            print("upy cdn fetch err", e)
            raise
        print("upy cdn fetch retry")

# The loop cannot fall through with None (the second attempt raises on
# failure), so this gate never fires at runtime — it narrows the imported
# module for the checker, same convention as the card gates above.
if test_a is None:
    raise RuntimeError("cdn fetch module")

if test_a.a_ping() != 11:
    raise RuntimeError("cdn fetch call")
print("upy cdn fetch 11")
# Late `import pymergetic.metal.process` (and siblings) after CDN configure
# parks the firmware hook. Cards are already on `m` from the first import.
cdn.reset()
print("upy cdn reset")

# metal.jit.py object loop: firmware compiles with no PERSISTENT_CODE (the
# MINIMUM ROM budget leaves no room for mpy save), so object_compile must
# refuse politely, not crash and not silently succeed. The face is wired on
# every seat; the fill says no on this one, and the prove pins that.
import pymergetic.metal.jit.py as jpy

_fw_mpy = jpy.object_compile("VAL = 1\n", "fw_jitpy_refuse")
if _fw_mpy is not None:
    raise RuntimeError("jit py firmware should refuse")
print("upy jit py refuses (no mpy save on firmware)")
if jpy.object_load(b"M\x06\x00\x00", "fw_jitpy_refuse") == 0:
    raise RuntimeError("jit py firmware load should refuse")
print("upy jit py load refuses")

# metal.process budget faces (firmware seat): the faces are wired here like
# on every seat. A budget set succeeds (the sub-arena is small and the boot
# arena has room), and a compile under it is still refused by the jit.py
# fill — the cap and the refusal compose, nothing aborts.
import pymergetic.metal.process as proc

if proc.budget(0) != 0:
    raise RuntimeError("process budget default %r" % (proc.budget(0),))
if proc.budget_set(0, 64 * 1024) != 0:
    raise RuntimeError("process budget set")
if proc.budget(0) != 64 * 1024:
    raise RuntimeError("process budget readback %r" % (proc.budget(0),))
_fw_mpy2 = jpy.object_compile("VAL = 1\n", "fw_jitpy_budgeted")
if _fw_mpy2 is not None:
    raise RuntimeError("jit py firmware budgeted should refuse")
print("upy process budget refuses (cap set, compile still no)")

# pymergetic.types (firmware seat): the universal 16-byte value crosses the
# Python face as 16-byte bytes. Same card, same faces as unix/browser — no
# float/longlong on this build, so the prove sticks to nil/i32/str probes
# and the descriptor registry round-trip.
import pymergetic.types as t

_tn = t.nil()
if t.kind(_tn) != 0:
    raise RuntimeError("types nil kind %r" % (t.kind(_tn),))
_ti = t.i32(42)
if t.kind(_ti) != 1 or t.is_nil(_ti):
    raise RuntimeError("types i32 kind/is_nil")
if t.kind(t.str("hello")) != 8:
    raise RuntimeError("types str kind")
if t.name_hash("x") <= 0:
    raise RuntimeError("types name hash")
if t.registry_find("pymergetic.types.i32") is None:
    raise RuntimeError("types registry find i32")
if t.registry_find("pymergetic.types.list") is None:
    raise RuntimeError("types registry find list")
if t.registry_count() < 12:
    raise RuntimeError("types registry count %r" % (t.registry_count(),))
print("upy types value loop")

if m.display.up() != 0:
    raise RuntimeError("display up")
print("upy display up")
if m.console.fb_attach() != 0:
    raise RuntimeError("console fb")
print("upy display present")
if m.input.up() != 0:
    raise RuntimeError("input up")
print("upy input feed")
if m.console.up() != 0:
    raise RuntimeError("console ids")
print("upy console ids")
if m.fs.up() != 0:
    raise RuntimeError("fs up")
print("upy fs embed")
if m.process.up() != 0:
    raise RuntimeError("process up")
print("upy process")
if m.net.ssh.up() != 0:
    raise RuntimeError("ssh up")
print("upy ssh session")

# net.zenoh: cooperative mount on bare-metal firmware too. Peer config, an
# immediate up() (z_open drives the whole handshake across poll(), never
# blocking the boot), bounded poll() steps, and a 16-byte local ZID. Resident
# C on the firmware build — no CDN pack.
if m.net.zenoh is None:
    raise RuntimeError("zenoh card")
import pymergetic.metal.net.zenoh as zenoh

if zenoh.peer is None:
    raise RuntimeError("zenoh peer")
if zenoh.peer(0x7F000001, 7447, 0) != 0:
    raise RuntimeError("zenoh peer cfg")
zenoh.up()
for _ in range(4):
    zenoh.poll()
zid = zenoh.zid()
if not isinstance(zid, bytes) or len(zid) != 16:
    raise RuntimeError("zenoh zid %r" % (zid,))
print("upy zenoh")

# Render the module help (the same banner the REPL shows), which proves the
# packages()/packages_catalog()/search/filter API surface is wired and listed.
m.help()

