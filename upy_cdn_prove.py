# Unix µPy: same pymergetic.wasmmod.net.cdn face as browser/firmware.
# Fill is POSIX io.fetch (host loopback). argv[1] = http base.
import sys

import pymergetic.metal as m

if not m.ready():
    raise SystemExit("metal not ready")
print("upy metal ready")

import pymergetic.wasmmod.net.cdn as cdn

cdn.session_id("sess-1")
cdn.configure(sys.argv[1], "tok-cdn")
b = cdn.fetch_pack("hello")
if b[:4] != b"\x00asm":
    raise SystemExit("cdn pack")

# What a seat holds is the knob, not a row per export reserved up front: a
# pack's faces are rows taken as they register. With the knob down at what is
# already live there is no room for them, and the loader rolls the whole load
# back rather than publish half a module.
import pymergetic.util.limits as limits

_rx = limits.find("wasmmod.registry.exports")
if _rx < 0 or limits.counted(_rx) != 1:
    raise SystemExit("no registry exports knob")
if limits.set("wasmmod.registry.exports", limits.used(_rx)) != 0:
    raise SystemExit("shrink the registry exports knob")
_refused = False
try:
    import pymergetic.wasmmod_examples.hello as hello
except (ImportError, OSError):
    _refused = True
if not _refused:
    raise SystemExit("a pack over the registry exports knob should refuse")
if limits.reset("wasmmod.registry.exports") != 0:
    raise SystemExit("reset the registry exports knob")
print("upy registry export knob")

import pymergetic.wasmmod_examples.hello as hello

if hello is None:
    raise SystemExit("pack import")
print("upy pack import")

# The image a pack arrives as is a knob now, not a reserved row: the loader
# owns one allocation of exactly the pack's length, taken when the pack lands
# and given back when the module unloads. Under a pack's size the load refuses;
# put the knob back and the same pack lands.
if limits.set("wasmmod.loader.image", 1024) != 0:
    raise SystemExit("shrink the loader image knob")
refused = False
try:
    import pymergetic.wasmmod_examples.test_a as test_a
except (ImportError, OSError):
    refused = True
if not refused:
    raise SystemExit("a pack over the image knob should refuse")
if limits.reset("wasmmod.loader.image") != 0:
    raise SystemExit("reset the loader image knob")
import pymergetic.wasmmod_examples.test_a as test_a

if test_a.a_ping() != 11:
    raise SystemExit("pack once the image knob is back")
print("upy loader image knob")
cdn.reset()
print("upy cdn")
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
if m.process.up() != 0:
    raise SystemExit("process up")
print("upy process")
if m.net.ssh.up() != 0:
    raise SystemExit("ssh up")
print("upy ssh session")
