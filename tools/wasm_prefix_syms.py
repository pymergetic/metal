#!/usr/bin/env python3
"""wasm_prefix_syms.py — rename every defined global in a wasm object.

The wasm twin of tools/tcc_prefix_syms.sh: same job (every defined global
gets an instance prefix so N TCC instances link into one binary), for the
seat whose objects are relocatable wasm (emcc) — GNU nm/objcopy cannot
read that format and llvm-objcopy has no --redefine-sym for wasm.

The symbol table of a relocatable wasm object lives in the "linking"
custom section (version 2):

    version: uleb
    subsections, each:  tag: uleb, size: uleb, payload
    tag 8 = SYMTAB:
        nsyms: uleb
        per symbol (WebAssembly/tool-conventions Linking.md):
            kind:   u8   (0=func 1=data 2=global 3=section 4=event 5=table)
            flags:  uleb (bit 0x10 = undefined/import, 0x40 = explicit
                          name, binding 0x3 — 3 is common)
            section symbols (3): sectionIndex: uleb, never named
            data symbols (1):   name ALWAYS, then
                    defined + not common: segment, offset, size (3x uleb)
                    common:              size (uleb) + alignment (u8)
                    undefined:           name only
            func/global/event/table: index: uleb, then
                    name ONLY if defined or EXPLICIT_NAME — an undefined
                    symbol without 0x40 takes its name from the import
                    section and stores none here

Only *defined* symbols are renamed — imports keep their libc names, the
same rule as the objcopy twin's nm --defined-only. Function and data
bodies reference symbols by index, never by name, so nothing else moves;
reloc.* sections target symbol indexes/imports likewise.

Names only grow, so the SYMTAB subsection size, the linking section size
and every following section's file offset shift — sizes elsewhere are
untouched, so the file is re-emitted with just those two headers fixed.

usage: wasm_prefix_syms.py <prefix> <raw-object> <final-object> [nm] [objcopy]
  prefix       e.g. pm_tccx_ (with trailing underscore)
  raw-object   the freshly compiled wasm object
  final-object destination for the renamed object
The trailing nm/objcopy arguments are the sh twin's tool seam
(tools/tcc_instances.mk invokes both twins with the same argv): GNU
binutils cannot read wasm objects at all, so they are accepted and
ignored here.
exit: 0 ok, 1 usage, 2 parse/write failure
"""

import os
import sys

SYM_UNDEFINED = 0x10
SYM_EXPLICIT_NAME = 0x40
SYM_BINDING_COMMON = 3
SYM_KIND_SECTION = 3
SYM_KIND_DATA = 1
SYM_TAG_SYMTAB = 8


def read_uleb(buf, off):
    result = 0
    shift = 0
    while True:
        b = buf[off]
        off += 1
        result |= (b & 0x7F) << shift
        if not (b & 0x80):
            return result, off
        shift += 7


def write_uleb(value):
    out = bytearray()
    while True:
        b = value & 0x7F
        value >>= 7
        if value:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)


def find_linking(buf):
    """Locate the 'linking' custom section.

    Returns (sec_id_pos, name_start, payload_start, payload_end): sec_id_pos
    is the file offset of the 0x00 custom-section id byte, name_start/
    payload_start delimit the section-name bytes, and the payload spans
    the version and subsections. The name-length + name bytes sit between
    the size uleb and the payload (emcc pads the size uleb to 5 bytes, so
    never assume the header is only one byte wide).
    """
    if buf[:4] != b"\x00asm":
        return None
    off = 8
    while off < len(buf):
        sec_id_pos = off
        sec_id = buf[off]
        off += 1
        size, off = read_uleb(buf, off)
        body = off
        if sec_id == 0:
            nlen, p = read_uleb(buf, body)
            if buf[p:p + nlen] == b"linking":
                return sec_id_pos, p, p + nlen, body + size
        off = body + size
    return None


def parse_symbols(buf, off, end):
    """Parse one SYMTAB payload span into entry records.

    Returns a list of (entry_bytes, defined, len_start, name_start,
    name_end) where entry_bytes is the exact on-disk span of the entry,
    and len_start/name_start/name_end delimit the encoded-name length
    uleb and name bytes INSIDE that span, or None for unnamed symbols.
    """
    nsyms, off = read_uleb(buf, off)
    entries = []
    for _ in range(nsyms):
        start = off
        kind = buf[off]
        off += 1
        flags, off = read_uleb(buf, off)
        defined = not (flags & SYM_UNDEFINED)
        common = (flags & 0x3) == SYM_BINDING_COMMON
        explicit = bool(flags & SYM_EXPLICIT_NAME)
        len_start = None
        name_start = None
        name_end = None
        if kind == SYM_KIND_SECTION:
            # section symbols carry an index, never a name
            _, off = read_uleb(buf, off)
            if off > end:
                raise ValueError("symbol entry overruns SYMTAB subsection")
            entries.append((bytes(buf[start:off]), False, None, None, None))
            continue
        elif kind == SYM_KIND_DATA:
            # data symbols always carry a name; defined ones then carry
            # segment/offset/size (common: size + u8 alignment)
            len_start = off
            nlen, off = read_uleb(buf, off)
            name_start = off
            name_end = off + nlen
            off = name_end
            if defined and not common:
                _, off = read_uleb(buf, off)
                _, off = read_uleb(buf, off)
                _, off = read_uleb(buf, off)
            elif common:
                _, off = read_uleb(buf, off)
                off += 1
        else:
            # func/global/event/table: index, then a name only when
            # defined or EXPLICIT_NAME (imports store no name here)
            _, off = read_uleb(buf, off)
            if defined or explicit:
                len_start = off
                nlen, off = read_uleb(buf, off)
                name_start = off
                name_end = off + nlen
                off = name_end
        if off > end:
            raise ValueError("symbol entry overruns SYMTAB subsection")
        if name_start is None:
            entries.append((bytes(buf[start:off]), defined, None, None,
                            None))
        else:
            entries.append((bytes(buf[start:off]), defined,
                            len_start - start, name_start - start,
                            name_end - start))
    if off != end:
        raise ValueError("SYMTAB walk ended at %d, subsection ends at %d"
                         % (off, end))
    return entries


def rebuild_linking(buf, payload_start, payload_end, prefix):
    """Rebuild the linking payload with defined names prefixed."""
    out = bytearray()
    off = payload_start
    ver, off = read_uleb(buf, off)
    out += write_uleb(ver)
    renamed = 0
    while off < payload_end:
        tag, off = read_uleb(buf, off)
        subsz, off = read_uleb(buf, off)
        sub_end = off + subsz
        if tag == SYM_TAG_SYMTAB:
            entries = parse_symbols(buf, off, sub_end)
            sub = bytearray()
            sub += write_uleb(len(entries))
            for entry, defined, lstart, nstart, nend in entries:
                if not defined or nstart is None:
                    sub += entry
                    continue
                new_name = prefix + entry[nstart:nend]
                sub += entry[:lstart]          # up to the OLD name-length uleb
                sub += write_uleb(len(new_name))
                sub += new_name
                sub += entry[nend:]             # trailing fields after the name
                renamed += 1
            out += write_uleb(tag)
            out += write_uleb(len(sub))
            out += sub
        else:
            out += write_uleb(tag)
            out += write_uleb(subsz)
            out += buf[off:sub_end]
        off = sub_end
    if renamed == 0:
        raise ValueError("no defined symbols found to rename")
    return bytes(out), renamed


def main():
    args = sys.argv[1:]
    if len(args) < 3:
        sys.stderr.write("usage: wasm_prefix_syms.py <prefix> <raw> <final>"
                         " [nm] [objcopy]\n")
        return 1
    prefix = args[0].encode()
    raw_path, final_path = args[1], args[2]

    try:
        with open(raw_path, "rb") as f:
            buf = f.read()
        loc = find_linking(buf)
        if loc is None:
            raise ValueError("no 'linking' custom section")
        sec_id_pos, name_start, payload_start, payload_end = loc
        payload, renamed = rebuild_linking(buf, payload_start, payload_end,
                                           prefix)

        # Re-emit: the custom section header (id 0x00 + size + name) is
        # rebuilt around the new payload; everything after shifts by the
        # delta. Sizes elsewhere are untouched. The raw object's size
        # uleb may be padded (emcc writes 5-byte sizes) — rebuild it
        # instead of trying to splice.
        name = buf[name_start:payload_start]
        body = bytearray()
        body += write_uleb(len(name))
        body += name
        body += payload
        out = bytearray()
        out += buf[:sec_id_pos]
        out += b"\x00"
        out += write_uleb(len(body))
        out += body
        out += buf[payload_end:]
    except (ValueError, IndexError) as e:
        sys.stderr.write("wasm_prefix_syms: %s: %s\n" % (raw_path, e))
        return 2

    tmp = final_path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(out)
    os.replace(tmp, final_path)
    # same cleanup as the sh twin: the raw object is an intermediate
    if raw_path != final_path:
        try:
            os.unlink(raw_path)
        except OSError:
            pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
