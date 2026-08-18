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
import pymergetic.wasmmod_examples.hello as hello

if hello is None:
    raise SystemExit("pack import")
print("upy pack import")
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
