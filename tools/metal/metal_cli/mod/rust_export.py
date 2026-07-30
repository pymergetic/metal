"""Minimal Rust -> Catalog extract (upy-safe: re only). Not a full parser."""
from __future__ import annotations

import re
from pathlib import Path

from metal_cli.mod.catalog import (
    Arg,
    Catalog,
    EnumDef,
    EnumVariant,
    Field,
    Fn,
    Struct,
    Typedef,
)

_RS_TO_C = {
    "u8": "uint8_t",
    "i8": "int8_t",
    "u16": "uint16_t",
    "i16": "int16_t",
    "u32": "uint32_t",
    "i32": "int32_t",
    "u64": "uint64_t",
    "i64": "int64_t",
    "usize": "size_t",
    "isize": "intptr_t",
    "bool": "bool",
    "f32": "float",
    "f64": "double",
    "c_void": "void",
    "core::ffi::c_void": "void",
    "!": "_Noreturn void",  # Rust never-type (halt/panic)
}


def _strip_rs_noise(text: str) -> str:
    """Drop line comments and simple block comments (good enough for ABI scan)."""
    out: list[str] = []
    i = 0
    n = len(text)
    while i < n:
        if text.startswith("//", i):
            while i < n and text[i] != "\n":
                i += 1
            continue
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
            continue
        out.append(text[i])
        i += 1
    return "".join(out)


def _rs_type_to_c(ty: str) -> str:
    t = " ".join(ty.split())
    # arrays: [u32; 4]
    m = re.match(r"^\[(.+);\s*(\d+)\]$", t)
    if m:
        return f"{_rs_type_to_c(m.group(1))}[{m.group(2)}]"
    # pointers
    if t.startswith("*const "):
        return f"const {_rs_type_to_c(t[7:])} *"
    if t.startswith("*mut "):
        return f"{_rs_type_to_c(t[5:])} *"
    if t in _RS_TO_C:
        return _RS_TO_C[t]
    # path tail: core::ffi::c_void already handled; Foo::Bar -> Bar
    if "::" in t:
        t = t.rsplit("::", 1)[-1]
        if t in _RS_TO_C:
            return _RS_TO_C[t]
    return t


def _split_args(arglist: str) -> list[Arg]:
    arglist = arglist.strip()
    if not arglist or arglist == "":
        return []
    args: list[Arg] = []
    depth = 0
    cur: list[str] = []
    for ch in arglist:
        if ch in "<([":
            depth += 1
        elif ch in ">)]":
            depth = max(0, depth - 1)
        if ch == "," and depth == 0:
            part = "".join(cur).strip()
            if part:
                args.append(_parse_arg(part))
            cur = []
            continue
        cur.append(ch)
    part = "".join(cur).strip()
    if part:
        args.append(_parse_arg(part))
    return args


def _parse_arg(part: str) -> Arg:
    part = part.strip()
    if ":" not in part:
        return Arg(name="a0", ty=_rs_type_to_c(part))
    name, ty = part.split(":", 1)
    name = name.strip().lstrip("_")
    if not name:
        name = "a0"
    # rust keywords as C args: class -> class_
    if name in ("class", "new", "template", "delete"):
        name = name + "_"
    return Arg(name=name, ty=_rs_type_to_c(ty.strip()))


def catalog_from_rust(path: Path) -> Catalog:
    """Extract #[no_mangle] extern \"C\" fns + #[repr(C)] pub structs + #[repr(u32)] enums."""
    raw = _strip_rs_noise(path.read_text(encoding="utf-8"))
    cat = Catalog()

    # #[repr(u32)] pub enum Name { VAR = 0, ... }
    for m in re.finditer(
        r"#\[repr\(u32\)\]\s*(?:#\[[^\]]*\]\s*)*pub\s+enum\s+(\w+)\s*\{([^}]*)\}",
        raw,
    ):
        name = m.group(1)
        variants: list[EnumVariant] = []
        for vm in re.finditer(
            r"([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(0x[0-9A-Fa-f]+|\d+)\s*,?",
            m.group(2),
        ):
            variants.append(
                EnumVariant(name=vm.group(1), value=int(vm.group(2), 0))
            )
        if variants:
            cat.enums.append(EnumDef(name=name, variants=variants))

    # structs
    for m in re.finditer(
        r"#\[repr\(C\)\]\s*(?:#\[[^\]]*\]\s*)*pub\s+struct\s+(\w+)\s*\{([^}]*)\}",
        raw,
    ):
        name = m.group(1)
        body = m.group(2)
        fields: list[Field] = []
        for fm in re.finditer(r"pub\s+(\w+)\s*:\s*([^,\n}]+)", body):
            fields.append(Field(name=fm.group(1), ty=_rs_type_to_c(fm.group(2).strip())))
        # Skip opaque / private-field layouts (empty typedef struct is wrong size).
        if fields:
            cat.structs.append(Struct(name=name, fields=fields))
    # type aliases used as callbacks: pub type Foo = Option<unsafe extern "C" fn(...) -> T>;
    for m in re.finditer(
        r"pub\s+type\s+(\w+)\s*=\s*Option<\s*(?:unsafe\s+)?extern\s+\"C\"\s+fn\s*\(([^)]*)\)\s*(?:->\s*([^>;]+))?\s*>\s*;",
        raw,
    ):
        tname = m.group(1)
        args_c = []
        for a in _split_args(m.group(2)):
            args_c.append(f"{a.ty} {a.name}")
        ret = _rs_type_to_c(m.group(3).strip()) if m.group(3) else "void"
        arg_s = ", ".join(args_c) if args_c else "void"
        cat.typedefs.append(Typedef(name=tname, ty=f"{ret} (*)({arg_s})"))

    # functions
    for m in re.finditer(
        r"#\[no_mangle\]\s*pub\s+(?:unsafe\s+)?extern\s+\"C\"\s+fn\s+(\w+)\s*\(([^)]*)\)\s*(?:->\s*([^{]+))?",
        raw,
    ):
        fname = m.group(1)
        args = _split_args(m.group(2))
        ret_raw = (m.group(3) or "").strip()
        ret = "void" if not ret_raw else _rs_type_to_c(ret_raw)
        cat.fns.append(Fn(name=fname, ret=ret, args=args))

    return cat
