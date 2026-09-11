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

# Factory floor panes on the firmware seat: the events ring answers (empty -
# no in-kernel rebuild fill here, so nothing ever emits) and a rebuild POST
# answers the honest refusal WITHOUT dirtying the ring. Same faces as unix;
# the fill differs. The TCC object probes below prove the cross lanes that
# this seat CAN do; the full rebuild chain is the unix fill's.
st = inspect.handle("GET", "/build/events?since=0")
body = inspect.body()
if st != 200 or '"latest":' not in body or '"events":[' not in body:
    raise RuntimeError("build events pane")
print("upy build events pane")
st = inspect.handle("POST", "/build/pymergetic.metal.util.ascii")
body = inspect.body()
if st != 200 or '"rebuild":"refused"' not in body:
    raise RuntimeError("build refuse")
st = inspect.handle("GET", "/build/events?since=0")
body = inspect.body()
if st != 200 or '"latest":0' not in body:
    raise RuntimeError("build events refused ring")
print("upy build refuse keeps ring empty")

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
# The hiccup surfaces two ways: ImportError "no pack" when the fetch gives
# up before a byte lands, and OSError EINVAL from the wasmmod registry when
# a truncated segment stream still 200s — the trampoline then instantiates
# half a pack and the call refuses. Both are the same wire race; retry both.
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
    except OSError as e:
        if attempt == 1:
            print("upy cdn fetch err", e)
            raise
        print("upy cdn fetch retry (oserr)")

# The loop cannot fall through with None (the second attempt raises on
# failure), so this gate never fires at runtime — it narrows the imported
# module for the checker, same convention as the card gates above.
if test_a is None:
    raise RuntimeError("cdn fetch module")

if test_a.a_ping() != 11:
    raise RuntimeError("cdn fetch call")
print("upy cdn fetch 11")

# A registered face is a row taken when it registers, not one of 64 export
# rows reserved per module whether or not a card has that many (which is what
# this board's bss held before). These three knobs are what the board will
# hold; `used` is what it holds, and on a board most of it is the cards' own
# faces, registered from crt0 before there was an allocator at all.
#
# `mixed` is the pack the refusals below are proven with: it is this prove's
# only use of it, so nothing is loaded until the knobs are back, and its two
# exports are what is left of the loader's trampoline pool (8 slots, of which
# hello took 2 and test_a 4) — a pack with four exports cannot be the third
# one loaded on any seat, knob or no knob. A refused load rolls all the way
# back, so the same pack lands once the knob is reset.
import pymergetic.util.limits as _limits

for _rname, _rwant in (
    ("wasmmod.registry.exports", 1536),
    ("wasmmod.registry.tests", 384),
    ("wasmmod.registry.benches", 64),
):
    _rat = _limits.find(_rname)
    if _rat < 0:
        raise RuntimeError("no %s knob" % _rname)
    if _limits.soft(_rat) != _rwant or _limits.default(_rat) != _rwant:
        raise RuntimeError("%s default %r" % (_rname, _limits.soft(_rat)))
    if _limits.counted(_rat) != 1:
        raise RuntimeError("%s does not count its rows" % _rname)
    if _limits.set(_rname, _rwant * 2) != 0 or _limits.soft(_rat) != _rwant * 2:
        raise RuntimeError("raise %s" % _rname)
    if _limits.reset(_rname) != 0 or _limits.soft(_rat) != _rwant:
        raise RuntimeError("reset %s" % _rname)
_rx = _limits.find("wasmmod.registry.exports")
_rused = _limits.used(_rx)
if _rused == 0:
    raise RuntimeError("the board's own faces are rows too")
if _limits.set("wasmmod.registry.exports", _rused) != 0:
    raise RuntimeError("shrink the registry exports knob")
_refused = False
try:
    import pymergetic.wasmmod_examples.mixed as _mixed
except (ImportError, OSError):
    _refused = True
if not _refused:
    raise RuntimeError("a pack over the registry exports knob should refuse")
if _limits.reset("wasmmod.registry.exports") != 0:
    raise RuntimeError("reset the registry exports knob")
print("upy registry row knobs", _rused)

# The type registry's rows and facegen's staging rows. Descriptors register
# from this board's crt0, so the run they land in is the one in .bss (64 of
# them, widened from a heap only on a seat that has one); staging is a host
# tool's path, so a board holds none of it — where it used to carry 96 rows
# of 64 fields each, better than a megabyte of this image's bss.
for _tname, _twant in (("types.stage", 96), ("types.registry", 512)):
    _tat = _limits.find(_tname)
    if _tat < 0:
        raise RuntimeError("no %s knob" % _tname)
    if _limits.soft(_tat) != _twant or _limits.default(_tat) != _twant:
        raise RuntimeError("%s default %r" % (_tname, _limits.soft(_tat)))
    if _limits.counted(_tat) != 1:
        raise RuntimeError("%s does not count what it holds" % _tname)
    if _limits.set(_tname, _twant * 2) != 0 or _limits.soft(_tat) != _twant * 2:
        raise RuntimeError("raise %s" % _tname)
    if _limits.reset(_tname) != 0 or _limits.soft(_tat) != _twant:
        raise RuntimeError("reset %s" % _tname)
if _limits.used(_limits.find("types.stage")) != 0:
    raise RuntimeError("a board stages nothing")
_tregistry = _limits.used(_limits.find("types.registry"))
if _tregistry == 0:
    raise RuntimeError("types.registry used is the live count")
print("upy types row knobs", _tregistry)

# A NIC's ring is taken when the NIC attaches. This board's frames came in
# over one of these three cards — virtio-net on the QEMU boards, the sim fill
# elsewhere — and whichever it was, its ring was cut when it bound, not
# reserved in this image for eight devices that never showed up.
for _dname, _dwant in (
    ("drivers.net.virtio.device", 8),
    ("drivers.net.virtio.queue", 8),
    ("drivers.net.virtio.frame", 2048),
    ("drivers.net.bge.device", 8),
    ("drivers.net.bge.queue", 8),
    ("drivers.net.bge.frame", 2048),
    ("drivers.net.sim.device", 4),
    ("drivers.net.sim.queue", 8),
    ("drivers.net.sim.frame", 2048),
    ("drivers.net.tap.device", 2),
    ("drivers.net.tap.frame", 2048),
):
    _dat = _limits.find(_dname)
    if _dat < 0:
        raise RuntimeError("no %s knob" % _dname)
    if _limits.soft(_dat) != _dwant or _limits.default(_dat) != _dwant:
        raise RuntimeError("%s default %r" % (_dname, _limits.soft(_dat)))
    if _limits.set(_dname, _dwant * 2) != 0 or _limits.soft(_dat) != _dwant * 2:
        raise RuntimeError("raise %s" % _dname)
    if _limits.reset(_dname) != 0 or _limits.soft(_dat) != _dwant:
        raise RuntimeError("reset %s" % _dname)
_nics = 0
for _dname in ("drivers.net.virtio.device", "drivers.net.bge.device", "drivers.net.sim.device"):
    _dat = _limits.find(_dname)
    if _limits.counted(_dat) != 1:
        raise RuntimeError("%s does not count its NICs" % _dname)
    _nics += _limits.used(_dat)
if _nics == 0:
    raise RuntimeError("this board got its packs over a NIC")
print("upy driver nic knobs", _nics)

# Above the driver cards sit the class tables: how many NICs, scanouts,
# disks, input devices and clocks this board carries at all, whichever card
# they came from. Each widens a row at a time instead of standing at its
# ceiling in the image.
for _dname, _dwant in (
    ("drivers.net.device", 32),
    ("drivers.gfx.device", 32),
    ("drivers.blk.device", 8),
    ("drivers.input.device", 8),
    ("drivers.rtc.device", 4),
    ("drivers.gfx.lfb.device", 4),
    ("drivers.gfx.lfb.shadow", 1536),
    ("drivers.blk.virtio.device", 4),
    ("drivers.input.virtio.device", 4),
    ("drivers.rtc.sim.device", 4),
):
    _dat = _limits.find(_dname)
    if _dat < 0:
        raise RuntimeError("no %s knob" % _dname)
    if _limits.soft(_dat) != _dwant or _limits.default(_dat) != _dwant:
        raise RuntimeError("%s default %r" % (_dname, _limits.soft(_dat)))
    if _limits.set(_dname, _dwant * 2) != 0 or _limits.soft(_dat) != _dwant * 2:
        raise RuntimeError("raise %s" % _dname)
    if _limits.reset(_dname) != 0 or _limits.soft(_dat) != _dwant:
        raise RuntimeError("reset %s" % _dname)
_cnet = _limits.find("drivers.net.device")
if _limits.counted(_cnet) != 1 or _limits.used(_cnet) < 1:
    raise RuntimeError("the class table counts every bound NIC")
print("upy device class knobs", _limits.used(_cnet))

# The image a pack arrives as is a knob now, not a row reserving 64KB for a
# pack that may never come: the loader owns one allocation of exactly the
# pack's length, given back when the module unloads. Under a pack's size the
# load refuses; put the knob back and the same pack lands. The wire race the
# fetch above retries is retried here the same way.
if _limits.set("wasmmod.loader.image", 1024) != 0:
    raise RuntimeError("shrink the loader image knob")
_refused = False
try:
    import pymergetic.wasmmod_examples.mixed as _mixed
except ImportError:
    _refused = True
except OSError:
    _refused = True
if not _refused:
    raise RuntimeError("a pack over the image knob should refuse")
if _limits.reset("wasmmod.loader.image") != 0:
    raise RuntimeError("reset the loader image knob")
_mixed = None
for _attempt in range(2):
    try:
        import pymergetic.wasmmod_examples.mixed as _mixed

        break
    except (ImportError, OSError) as e:
        if _attempt == 1:
            print("upy cdn fetch err", e)
            raise
        print("upy cdn fetch retry (image knob)")
if _mixed is None or _mixed.mixed_answer() != 42:
    raise RuntimeError("pack once the image knob is back")
print("upy loader image knob")
# Late `import pymergetic.metal.process` (and siblings) after CDN configure
# parks the firmware hook. Cards are already on `m` from the first import.
cdn.reset()
print("upy cdn reset")

# metal.jit.py object loop: firmware compiles with no PERSISTENT_CODE (the
# MINIMUM ROM budget leaves no room for mpy save), so object_compile must
# refuse politely, not crash and not silently succeed. The face is wired on
# every seat; the fill says no on this one, and the prove pins that.
import pymergetic.metal.jit.py as jpy

_fw_mpy = jpy.object_compile("VAL = 1\n", "fw_jitpy_refuse")
if _fw_mpy is not None:
    raise RuntimeError("jit py firmware should refuse")
print("upy jit py refuses (no mpy save on firmware)")
if jpy.object_load(b"M\x06\x00\x00", "fw_jitpy_refuse") == 0:
    raise RuntimeError("jit py firmware load should refuse")
print("upy jit py load refuses")

# metal.jit.c object path (the in-kernel compile): the vendored TCC linked
# into every firmware seat compiles a C string to a native ET_REL object
# through the arena temp-FILE layer (port/lib.c) and hands the bytes back.
# The prove pins the whole chain: compile, ELF magic, ET_REL type, and the
# ELF class + e_machine of the seat's own arch (x64 seats: ELF64/EM_X86_64
# = 62; arm seats: ELF32/EM_ARM = 40), the same contract the cross-instance
# tests assert on the hosted seats. The seat says which pair to expect via
# pymergetic.metal.boot.seat() — the same name the tree prints as `arch`.
# Index reads only: the ASCII-only firmware lexer chokes on slice syntax
# (verified by bisect), so startswith() stands in for the magic compare.
import pymergetic.metal.jit.c as jc
import pymergetic.metal.boot as mboot

_fw_class = 2
_fw_machine_hi = 62
_fw_machine_lo = 0
if mboot.seat() == "armv7qemu" or mboot.seat() == "armv7rv1106":
    _fw_class = 1
    _fw_machine_hi = 40

_fw_src = "int fw_tcc_probe(void) { return 0x2a; }\n"
_fw_obj = jc.object_compile(_fw_src)
if _fw_obj is None or len(_fw_obj) < 52:
    raise RuntimeError("jit c firmware object_compile")
if not _fw_obj.startswith(bytes([127, 69, 76, 70])):
    raise RuntimeError("jit c firmware object not ELF")
if _fw_obj[4] != _fw_class:
    raise RuntimeError("jit c firmware object class %d" % (_fw_obj[4],))
if _fw_obj[16] != 1 or _fw_obj[17] != 0:
    raise RuntimeError("jit c firmware object not ET_REL")
if _fw_obj[18] != _fw_machine_hi or _fw_obj[19] != _fw_machine_lo:
    raise RuntimeError("jit c firmware object e_machine %d" % (_fw_obj[18],))
print("upy jit c tcc object")

# Cross lanes (the every-seat-builds-every-arch matrix): each firmware seat
# also links prefixed TCC instances for every OTHER arch and emits that
# arch's object in-kernel — x64 seats emit wasm32 + arm, arm seats emit
# x64 + wasm32. Same asserts as the native lane, per target: wasm32 = a
# serialized module (\0asm magic), arm = ELF32/EM_ARM, x64 = ELF64/EM_X86_64.
# Index reads only (the firmware lexer has no slice syntax).
_cross_src = "int fw_cross_probe(void) { return 0x2a; }\n"
if _fw_class == 2:
    _wasm_obj = jc.object_compile(_cross_src, target=1)
    if _wasm_obj is None or len(_wasm_obj) < 8:
        raise RuntimeError("jit c firmware wasm cross")
    if not _wasm_obj.startswith(bytes([0, 97, 115, 109])):
        raise RuntimeError("jit c firmware wasm cross magic")
    _arm_obj = jc.object_compile(_cross_src, target=2)
    if _arm_obj is None or len(_arm_obj) < 52:
        raise RuntimeError("jit c firmware arm cross")
    if not _arm_obj.startswith(bytes([127, 69, 76, 70])):
        raise RuntimeError("jit c firmware arm cross not ELF")
    if _arm_obj[4] != 1 or _arm_obj[18] != 40 or _arm_obj[19] != 0:
        raise RuntimeError("jit c firmware arm cross class/machine")
    print("upy jit c tcc cross wasm+arm")
else:
    _x64_obj = jc.object_compile(_cross_src, target=3)
    if _x64_obj is None or len(_x64_obj) < 52:
        raise RuntimeError("jit c firmware x64 cross")
    if not _x64_obj.startswith(bytes([127, 69, 76, 70])):
        raise RuntimeError("jit c firmware x64 cross not ELF")
    if _x64_obj[4] != 2 or _x64_obj[18] != 62 or _x64_obj[19] != 0:
        raise RuntimeError("jit c firmware x64 cross class/machine")
    _wasm_obj = jc.object_compile(_cross_src, target=1)
    if _wasm_obj is None or len(_wasm_obj) < 8:
        raise RuntimeError("jit c firmware wasm cross")
    if not _wasm_obj.startswith(bytes([0, 97, 115, 109])):
        raise RuntimeError("jit c firmware wasm cross magic")
    print("upy jit c tcc cross x64+wasm")

# metal.process budget faces (firmware seat): the faces are wired here like
# on every seat. A budget set succeeds (the sub-arena is small and the boot
# arena has room), and a compile under it is still refused by the jit.py
# fill — the cap and the refusal compose, nothing aborts.
import pymergetic.metal.process as proc

if proc.budget(0) != 0:
    raise RuntimeError("process budget default %r" % (proc.budget(0),))
if proc.budget_set(0, 64 * 1024) != 0:
    raise RuntimeError("process budget set")
if proc.budget(0) != 64 * 1024:
    raise RuntimeError("process budget readback %r" % (proc.budget(0),))
_fw_mpy2 = jpy.object_compile("VAL = 1\n", "fw_jitpy_budgeted")
if _fw_mpy2 is not None:
    raise RuntimeError("jit py firmware budgeted should refuse")
print("upy process budget refuses (cap set, compile still no)")

# pymergetic.types (firmware seat): the universal 16-byte value crosses the
# Python face as 16-byte bytes. Same card, same faces as unix/browser — no
# float/longlong on this build, so the prove sticks to nil/i32/str probes
# and the descriptor registry round-trip.
import pymergetic.types as t

_tn = t.nil()
if t.kind(_tn) != 0:
    raise RuntimeError("types nil kind %r" % (t.kind(_tn),))
_ti = t.i32(42)
if t.kind(_ti) != 1 or t.is_nil(_ti):
    raise RuntimeError("types i32 kind/is_nil")
if t.kind(t.str("hello")) != 8:
    raise RuntimeError("types str kind")
if t.name_hash("x") <= 0:
    raise RuntimeError("types name hash")
if t.registry_find("pymergetic.types.i32") is None:
    raise RuntimeError("types registry find i32")
if t.registry_find("pymergetic.types.list") is None:
    raise RuntimeError("types registry find list")
if t.registry_count() < 12:
    raise RuntimeError("types registry count %r" % (t.registry_count(),))
print("upy types value loop")

# pymergetic.util.limits (firmware seat): the board's capacities are the same
# knobs the hosted seats carry, readable and movable from the guest. A board is
# exactly where this matters — its memory is what the seat found in the memmap,
# so the number of connections it takes is a decision, not a compile-time one.
import pymergetic.util.limits as limits

if not limits.ready():
    raise RuntimeError("limits not ready")
if limits.count() < 20:
    raise RuntimeError("limits count %r" % (limits.count(),))
# The list asked by number, which is how a seat lists its knobs before it
# knows any of their names: in order, and nothing past the last one.
_lnames = [limits.name(_k) for _k in range(limits.count())]
if sorted(_lnames) != _lnames or limits.name(limits.count()) is not None:
    raise RuntimeError("limits by index %r" % (_lnames,))
_li = limits.find("net.http.asgi.connection")
if _li < 0:
    raise RuntimeError("no asgi connection knob")
_lwas = limits.soft(_li)
if limits.set("net.http.asgi.connection", _lwas + 4) != 0 or limits.soft(_li) != _lwas + 4:
    raise RuntimeError("raise the asgi connection knob")
if limits.reset("net.http.asgi.connection") != 0 or limits.soft(_li) != _lwas:
    raise RuntimeError("reset the asgi connection knob")
_lci = limits.find("console.scrollback")
_lcwas = limits.soft(_lci)
if limits.set("console.scrollback", 128) != 0 or limits.soft(_lci) != 128:
    raise RuntimeError("deepen the console")
if limits.reset("console.scrollback") != 0 or limits.soft(_lci) != _lcwas:
    raise RuntimeError("reset the console scrollback")
print("upy limits knob loop")

# The rooms the bridge builds for a job are knobs on a board too, and a board
# is where it matters most: the compile workspace used to be reserved in bss
# whether or not this seat ever compiled anything. Shrink it and the in-kernel
# C compile refuses; put it back and it compiles again.
for _rn in ("upy.compile.arena", "upy.cpp.arena", "upy.link.arena", "upy.dump"):
    _rj = limits.find(_rn)
    if _rj < 0:
        raise RuntimeError("no room knob %r" % (_rn,))
    if limits.soft(_rj) != limits.default(_rj):
        raise RuntimeError("room knob %r is not at its default" % (_rn,))
_lri = limits.find("upy.compile.arena")
# The room a compile gets is the smaller of this knob and the process budget
# the section above left set (64KB, which no C compile fits), and this test is
# about the knob: lift the budget over the room for it and put it back after.
# There is no clearing it — budget_set refuses a cap of 0.
if proc.budget_set(0, 4 * 1024 * 1024) != 0:
    raise RuntimeError("lift the process budget")
if limits.set("upy.compile.arena", 4096) != 0:
    raise RuntimeError("shrink the compile room")
if jc.object_compile("int fw_room_probe(void) { return 1; }\n") is not None:
    raise RuntimeError("a compile in a 4KB room should refuse")
if limits.reset("upy.compile.arena") != 0 or limits.soft(_lri) != limits.default(_lri):
    raise RuntimeError("reset the compile room")
_lrobj = jc.object_compile("int fw_room_probe(void) { return 1; }\n")
if _lrobj is None or len(_lrobj) < 52:
    raise RuntimeError("compile once the room is back")
if proc.budget_set(0, 64 * 1024) != 0:
    raise RuntimeError("put the process budget back")
print("upy room knob loop")

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
if zenoh.peer("127.0.0.1", 7447, 0) != 0:
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

