# Metal's own uu (uuencode/uudecode) -- not copied from micropython-lib
# (it doesn't ship one) or CPython's uu.py (which is written against
# binascii.b2a_uu()/a2b_uu(), and this build's extmod/modbinascii.c has
# neither -- upstream MicroPython's own binascii never implemented them,
# a genuine gap, not a Metal config choice; see docs/MICROPYTHON.md).
# This is the codec itself in plain Python, encode()/decode() working on
# file-like objects (Metal's own io.FileIO/BytesIO, or anything with
# read()/write()) the same shape as CPython's uu module.

Error = OSError


def _b2a_uu(chunk):
    n = len(chunk)
    out = bytearray(1 + ((n + 2) // 3) * 4)
    out[0] = (n & 0x3F) + 0x20
    padded = chunk + b"\x00\x00"
    pos = 1
    i = 0
    while i < n:
        b0 = padded[i]
        b1 = padded[i + 1]
        b2 = padded[i + 2]
        c1 = b0 >> 2
        c2 = ((b0 & 0x03) << 4) | (b1 >> 4)
        c3 = ((b1 & 0x0F) << 2) | (b2 >> 6)
        c4 = b2 & 0x3F
        for c in (c1, c2, c3, c4):
            out[pos] = 0x60 if c == 0 else (c + 0x20)
            pos += 1
        i += 3
    return bytes(out[:pos]) + b"\n"


def _a2b_uu(line):
    if len(line) == 0:
        return b""
    n = line[0] - 0x20
    if n < 0:
        n = 0
    data = line[1:]
    out = bytearray()
    i = 0
    while i < len(data) and len(out) < n:
        vals = []
        for k in range(4):
            if i + k >= len(data):
                vals.append(0)
                continue
            ch = data[i + k]
            vals.append(0 if ch == 0x60 else (ch - 0x20) & 0x3F)
        c1, c2, c3, c4 = vals
        out.append(((c1 << 2) | (c2 >> 4)) & 0xFF)
        out.append(((c2 << 4) | (c3 >> 2)) & 0xFF)
        out.append(((c3 << 6) | c4) & 0xFF)
        i += 4
    return bytes(out[:n])


def encode(in_file, out_file, name=None, mode=0o666, *, backtick=False):
    if name is None:
        name = "-"
    # A plain str is fine to hand to out_file.write() here: MicroPython
    # strings carry the same read-only buffer protocol as bytes (see
    # py/objstr.c's mp_obj_str_get_buffer, shared by both types), so no
    # str.encode() (unavailable -- MICROPY_CPYTHON_COMPAT is off) is
    # needed just to turn this header line into something write()-able.
    out_file.write("begin %o %s\n" % (mode, name))
    while True:
        chunk = in_file.read(45)
        if not chunk:
            break
        out_file.write(_b2a_uu(chunk))
    out_file.write(b"`\nend\n" if backtick else b" \nend\n")


def decode(in_file, out_file, quiet=False):
    """Decode uuencoded in_file into out_file (both already-open file-like
    objects). Metal patch: upstream's uu.decode() can open out_file itself
    by guessing a name from the 'begin MODE name' header line -- that name
    comes back as bytes (readline() is bytes-only here, no str.decode()
    available to turn it into a path str), so this facade always requires
    an explicit out_file instead of guessing one.
    """
    line = in_file.readline()
    while line and not line.startswith(b"begin "):
        line = in_file.readline()
    if not line:
        raise Error("Missing 'begin' line in input file")

    while True:
        line = in_file.readline()
        if not line or line.rstrip(b"\r\n") in (b"", b"`", b"end"):
            break
        out_file.write(_a2b_uu(line.rstrip(b"\r\n")))
