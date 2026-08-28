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
