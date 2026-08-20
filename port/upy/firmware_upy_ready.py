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
