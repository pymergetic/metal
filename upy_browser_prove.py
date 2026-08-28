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
if st != 200 or '"asgi":true' not in body or '"microdot":true' not in body or '"zenoh":true' not in body:
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
    # metal.build ledger: round-trip through the /changes read pane on the
    # browser seat too (the fs card is arena-backed here as everywhere).
    import pymergetic.metal.inspect as inspect

    st = inspect.handle("GET", "/changes/pymergetic.metal.build")
    if st != 200 or 'decision' not in (inspect.body() or ""):
        raise SystemExit("ledger seed %s" % (st,))
    print("upy ledger round-trip")
    # metal.build accessor spine: b.at(fqn, name) resolves against the live
    # registry + embedded source table on the browser seat too.
    import pymergetic.metal.build as build

    h = build.at("pymergetic.metal.build", "pm_metal_build_at")
    if h == 0:
        raise SystemExit("at resolve")
    info = build.at_info(h)
    if info is None or info[2] != "fn" or info[3] != "c" or len(info[8]) == 0:
        raise SystemExit("at info %r" % (info,))
    ast = build.at_ast(h)
    if ast is None or ast[0] != 1 or ast[1] != "c":
        raise SystemExit("at ast %r" % (ast,))
    print("upy accessor spine")
    if m.process.up() != 0:
        raise SystemExit("process up")
    print("upy process")
    if m.net.ssh.up() != 0:
        raise SystemExit("ssh up")
    print("upy ssh session")
    # net.zenoh: the card must mount cooperatively here too. Peer config, an
    # immediate up() (even with no listener, because z_open drives the whole
    # handshake across poll()), bounded poll() steps that don't hang the guest,
    # and a 16-byte local ZID. js.fetch cannot nest inside the cdn exec, but
    # zenoh needs no CDN fetch — it is resident C on every seat.
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

    # net.swarm.*: the three fleet cards must mount here too. Callback faces
    # (offer, declare) are host-C only; the off-on defers, the membership node
    # identity, and the discovery arm/teardown must hold with no peer present.
    import pymergetic.metal.net.swarm.membership as sm
    import pymergetic.metal.net.swarm.task as st
    import pymergetic.metal.net.swarm.discovery as sd

    if sm is None or st is None or sd is None:
        raise SystemExit("swarm card import")
    if sm.alive() != 0 or sm.stop() != 0 or sm.start("fleet") != 0:
        raise SystemExit("swarm membership not-open")
    nid = sm.node_id()
    if not isinstance(nid, bytes) or len(nid) != 32:
        raise SystemExit("swarm membership node_id %r" % (nid,))
    if st.offering() != 0 or st.declaring() != 0 or st.done() != 0:
        raise SystemExit("swarm task not-open")
    if st.dispatch("render", b"\x01\x02\x03") != 0:
        raise SystemExit("swarm task dispatch defer")
    sr = sd.scout()
    if not isinstance(sr, tuple) or len(sr) != 2 or sr[0] != 0:
        raise SystemExit("swarm discovery scout no-peer %r" % (sr,))
    if sd.answer_on() != 1 or sd.answer_on() != 0:
        raise SystemExit("swarm discovery answer")
    sd.answer_off()
    if sd.pump() != 0:
        raise SystemExit("swarm discovery pump")
    print("upy swarm")
