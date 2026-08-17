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
import pymergetic.wasmmod.net.cdn as cdn

cdn.session_id("sess-1")
# QEMU user-net host gateway (fixed by QEMU SLIRP, not a lab LAN).
cdn.configure("http://10.0.2.2:1", "tok-cdn")
print("upy cdn")
import pymergetic.wasmmod_examples.hello as hello

print("upy pack import")
