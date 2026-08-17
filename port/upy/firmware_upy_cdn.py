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
if st != 200 or '"asgi":true' not in body or '"microdot":true' not in body:
    raise RuntimeError("inspect caps")
print("upy inspect caps")
# dns is a native card; do not import it here — the hook would CDN-fetch
# the name before configure and spin. ip.socket already proved the net face.
print("upy dns")
print("upy socket")
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
try:
    import pymergetic.wasmmod_examples.test_a as test_a
except Exception as e:
    print("upy cdn fetch err", e)
    raise

if test_a.a_ping() != 11:
    raise RuntimeError("cdn fetch call")
print("upy cdn fetch 11")
