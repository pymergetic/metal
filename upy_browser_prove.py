import pymergetic.metal as m

if not m.ready():
    raise SystemExit("metal not ready")
print("upy metal ready")


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
