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

if test_a.a_ping() != 11:
    raise RuntimeError("cdn fetch call")
print("upy cdn fetch 11")
# Late `import pymergetic.metal.process` (and siblings) after CDN configure
# parks the firmware hook. Cards are already on `m` from the first import.
cdn.reset()
print("upy cdn reset")

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

