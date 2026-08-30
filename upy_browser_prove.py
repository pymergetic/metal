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
    # metal.edit C editor on the browser seat: same parse/locate/edit/write
    # gates as unix — the card is resident C on every seat, so the editor
    # works identically under js.fetch-side conditions.
    import pymergetic.metal.edit as edit

    h = edit.parse_c(
        "#include <stdint.h>\n"
        "#define EDIT_PROBE_BUF 64\n"
        "int32_t edit_probe_add(int32_t a) {\n"
        "    return a + EDIT_PROBE_BUF;\n"
        "}\n"
    )
    if h is None:
        raise SystemExit("edit parse")
    n = edit.locate(h, "define", "EDIT_PROBE_BUF")
    if n is None or n[1] != "EDIT_PROBE_BUF":
        raise SystemExit("edit locate %r" % (n,))
    out = edit.set_define(h, "EDIT_PROBE_BUF", "128")
    if out is None or "#define EDIT_PROBE_BUF 128" not in out:
        raise SystemExit("edit set_define")
    wb = edit.write_back("upy.edit.probe", "/src/upy_edit_probe.c", out)
    if wb is None or wb[0] == 0 or "ledger note" not in wb[1]:
        raise SystemExit("edit write no-note %r" % (wb,))
    print("upy editor")
    # metal.workspace: the card tree materializes into the fs card on the
    # browser seat too (the embedded src table ships in the wasm image);
    # no mirror here — emscripten has no host FS, and mirror_set refuses.
    import pymergetic.metal.workspace as workspace

    n = workspace.materialize()
    if not isinstance(n, tuple) or n[0] != 0 or n[1] == 0:
        raise SystemExit("workspace materialize %r" % (n,))
    if workspace.mirror_set("/tmp/x") == 0:
        raise SystemExit("workspace mirror must refuse on emcc")
    st = m.fs.stat("/src/pymergetic/metal/build/__impl__.c")
    if not isinstance(st, tuple) or st[0] != 0 or st[1] == 0:
        raise SystemExit("workspace fs stat %r" % (st,))
    body = m.fs.read("/src/pymergetic/metal/build/__impl__.c", 32)
    if not isinstance(body, bytes) or len(body) != 32:
        raise SystemExit("workspace fs read %r" % (body,))
    if body[:25] != b"/* pymergetic.metal.build":
        raise SystemExit("workspace bytes %r" % (body[:25],))
    print("upy workspace")
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

    # metal.build wasm-seat link (Phase 13): the browser cell's TCC targets
    # wasm32, so compile_source produces a wasm module whose named exports the
    # loader publishes into the registry — the software-defined link. The
    # prove: compile -> link -> lookup resolves, destroy unloads.
    import pymergetic.metal.build as build

    obj = build.compile_source(
        "upy.build.probe",
        "int probe_two(void) { return 2; }\n"
        "int probe_add_one(int x) { return x + 1; }\n",
    )
    print("compiled:", type(obj), len(obj) if isinstance(obj, bytes) else obj)
    if not isinstance(obj, bytes) or len(obj) < 8 or obj[:4] != b"\x00asm":
        raise SystemExit("build compile %r" % (type(obj),))
    lk = build.link("upy.build.probe", obj)
    print("linked:", lk)
    if not isinstance(lk, tuple) or lk[0] != 0:
        raise SystemExit("build link %r" % (lk,))
    if build.artifact_lookup("probe_two") is None:
        raise SystemExit("build lookup probe_two")
    if build.artifact_lookup("probe_add_one") is None:
        raise SystemExit("build lookup probe_add_one")
    if build.artifact_lookup("no_such_export") is not None:
        raise SystemExit("build lookup negative")
    # call into the linked wasm module: the same face, routed through the
    # registry trampoline on this seat (the wasm32 C ints ride the i32
    # spine). The result widens to i64 on the way back.
    if build.artifact_call("probe_two") != 2:
        raise SystemExit("build call probe_two")
    if build.artifact_call("probe_add_one", 41) != 42:
        raise SystemExit("build call probe_add_one")
    try:
        build.artifact_call("no_such_export")
        raise SystemExit("build call unknown should refuse")
    except Exception:
        pass
    build.artifact_destroy()
    if build.artifact_lookup("probe_two") is not None:
        raise SystemExit("build lookup after destroy")
    print("upy wasm build link")

    # cross-compile knob (browser seat): the seat's own backend IS wasm32,
    # so an explicit target=1 (WASM32) is the same face as the default —
    # object bytes come out \0asm either way, and the link below proves the
    # full loop. But ELF bytes have no backend on this seat: build.link
    # refuses them politely (an error tuple), never an abort — a missing
    # fill is a stub that fails the prove, not a dark port.
    import pymergetic.metal.jit.c as jc

    _xobj = jc.object_compile(
        "int xcross_magic(void) { return 42; }\n"
        "int xcross_add(int a, int b) { return a + b; }\n",
        target=1,
    )
    if not isinstance(_xobj, bytes) or len(_xobj) < 8 or _xobj[:4] != b"\x00asm":
        raise SystemExit("cross object %r" % (type(_xobj),))
    _xlk = build.link("upy.browser.xcross", _xobj)
    if not isinstance(_xlk, tuple) or _xlk[0] != 0:
        raise SystemExit("cross link %r" % (_xlk,))
    if build.artifact_call("xcross_magic") != 42:
        raise SystemExit("cross call magic")
    if build.artifact_call("xcross_add", 19, 23) != 42:
        raise SystemExit("cross call add")
    build.artifact_destroy()
    _elf_lk = build.link("upy.browser.elfrefuse", b"\x7fELF" + b"\x00" * 64)
    if not isinstance(_elf_lk, tuple) or _elf_lk[0] == 0:
        raise SystemExit("elf refuse %r" % (_elf_lk,))
    print("upy browser cross knob + elf refuse")

    # metal.jit.py object loop (browser seat): same card, same faces as the
    # unix seat — µPy compiles Python to mpy bytes and loads them back, all
    # inside the browser cell. No host tool, no fetch: the compiler is in
    # this wasm binary.
    import pymergetic.metal.jit.py as jpy

    _selfhost_src = (
        "VAL = 11\n"
        "def ping(x):\n"
        "    return x + VAL\n"
    )
    _mpy = jpy.object_compile(_selfhost_src, "upy_jitpy_selfhost")
    if not isinstance(_mpy, bytes) or len(_mpy) < 8 or _mpy[:1] != b"M":
        raise SystemExit("jit py compile %r" % (type(_mpy),))
    if jpy.object_load(_mpy, "upy_jitpy_selfhost") != 0:
        raise SystemExit("jit py load")
    # born at runtime (object_load published it) — __import__ is the same
    # call the import statement lowers to; the analyzer stops at the face.
    upy_jitpy_selfhost = __import__("upy_jitpy_selfhost")

    if upy_jitpy_selfhost.ping(31) != 42:
        raise SystemExit("jit py run")
    print("upy jit py object loop")

    # metal.jit.cpp REPL rebuild loop (browser seat): the same chain the unix
    # prove drives, on wasm32 — the C++ card transpiles its own source from
    # the kernel fs through lex -> parse -> lower, the lowered C re-lowers
    # byte-identical (the fixed point holds on this seat too), and TCC
    # (wasm32 backend) makes the object. The link face on this seat loads
    # wasm modules, so the full object path stops at compile here; the
    # build-link loop above already proves the wasm link on this seat.
    import pymergetic.metal.jit.cpp as jcpp

    _cpp_src = m.fs.read("/src/pymergetic/metal/jit/cpp/__impl__.c", 300000)
    if not isinstance(_cpp_src, bytes) or len(_cpp_src) < 100000:
        raise SystemExit("cpp own source %r" % (type(_cpp_src),))
    _cpp_src = _cpp_src.decode()
    _c1 = jcpp.lower(jcpp.parse(jcpp.lex(_cpp_src)))
    if not isinstance(_c1, str) or len(_c1) < 100000:
        raise SystemExit("cpp lower %r" % (type(_c1),))
    _c2 = jcpp.lower(jcpp.parse(jcpp.lex(_c1)))
    if _c1 != _c2:
        raise SystemExit("cpp fixed point")
    print("upy jit cpp rebuild loop")

    # metal.process memory budget (browser seat): the REPL caps its own
    # compile scratch and the compile bridge honors it — a small compile
    # still works inside the budget, a compile whose object cannot fit the
    # cap is refused (None), never an abort. Same faces as the unix seat.
    import pymergetic.metal.process as proc

    if proc.budget(0) != 0:
        raise SystemExit("process budget default %r" % (proc.budget(0),))
    if proc.budget_set(0, 64 * 1024) != 0:
        raise SystemExit("process budget set")
    if proc.budget(0) != 64 * 1024:
        raise SystemExit("process budget readback %r" % (proc.budget(0),))
    if proc.budget_used(0) <= 0:
        raise SystemExit("process budget used %r" % (proc.budget_used(0),))
    _mpy2 = jpy.object_compile(_selfhost_src, "upy_jitpy_budgeted")
    if not isinstance(_mpy2, bytes) or len(_mpy2) < 8:
        raise SystemExit("jit py budgeted compile")
    # an ~80KB string constant: the mpy object embeds it whole, so it cannot
    # fit a 64KB compile cap — refused, never an abort
    _big = 'v = "' + "_x" * 40000 + '"'
    try:
        _r = jpy.object_compile(_big, "upy_jitpy_toobig")
        if _r is not None:
            raise SystemExit("jit py past-cap compile should refuse")
    except Exception as _e:
        print("jit py past-cap refused: %s" % (_e,))
    print("upy process budget loop")
