import pymergetic.metal as m

if not m.ready():
    raise SystemExit("metal not ready")
print("upy metal ready")

# Cards resolve from the registry at any depth, with no name list to maintain.
import pymergetic.metal.net.ip as ip

if ip.socket is None:
    raise SystemExit("native card import")
if m.net.ip is not ip:
    raise SystemExit("namespace walk")
print("upy native card import")

import pymergetic.metal.inspect as inspect

st = inspect.handle("GET", "/inspect/self")
body = inspect.body()
if st != 200 or '"name":"pymergetic.metal"' not in body:
    raise SystemExit("inspect self %s %s" % (st, body))
st = inspect.handle("GET", "/inspect/reg")
body = inspect.body()
if st != 200 or "pymergetic.metal" not in body:
    raise SystemExit("inspect reg %s %s" % (st, body))
print("upy inspect")
st = inspect.handle("GET", "/capabilities")
body = inspect.body()
if st != 200 or '"asgi":true' not in body or '"microdot":true' not in body:
    raise SystemExit("inspect caps %s %s" % (st, body))
print("upy inspect caps")
# The source pane is real code on this seat too: every card with a C/Rust
# muscle ships its manifest + first file through the python bridge on every
# µPy seat. (An over-HTTP /src fetch is not gated here — js.fetch is Asyncify
# and cannot nest inside an already-asyncified exec; the /src asgi route is the
# same C handler the unix HTTP seat, the host C prove and firmware exercise.)
if inspect.src_manifest("pymergetic.metal.inspect") is None:
    raise SystemExit("src manifest")
body = inspect.src_read("pymergetic.metal.inspect", "__impl__.c")
if body is None or "pm_metal_inspect_init" not in body:
    raise SystemExit("src read")
print("upy src")
import pymergetic.metal.net.dns as dns

if dns.resolve is None:
    raise SystemExit("dns")
print("upy dns")
print("upy socket")


def _cdn(base):
    import pymergetic.wasmmod.net.cdn as cdn

    cdn.session_id("sess-1")
    cdn.configure(base, "tok-cdn")
    b = cdn.fetch_pack("hello")
    if b[:4] != b"\x00asm":
        raise SystemExit("cdn pack")
    import pymergetic.wasmmod_examples.hello as hello

    if hello is None:
        raise SystemExit("pack import")
    print("upy pack import")
    cdn.reset()
    print("upy cdn js.fetch")
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
