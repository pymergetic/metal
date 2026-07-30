"""Minimal C header -> Catalog extract (upy-safe: re only). Not a full parser."""
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


def _strip_c_noise(text: str) -> str:
    """Drop comments and preprocessor lines (keep code tokens)."""
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
        if text[i] == "#" and (i == 0 or text[i - 1] == "\n"):
            while i < n and text[i] != "\n":
                i += 1
            continue
        out.append(text[i])
        i += 1
    return "".join(out)


def _split_c_args(arglist: str) -> list[Arg]:
    arglist = arglist.strip()
    if not arglist or arglist == "void":
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
                args.append(_parse_c_arg(part, len(args)))
            cur = []
            continue
        cur.append(ch)
    part = "".join(cur).strip()
    if part:
        args.append(_parse_c_arg(part, len(args)))
    return args


def _parse_c_arg(part: str, idx: int) -> Arg:
    part = " ".join(part.split())
    # function pointer arg: ret (*name)(args)
    m = re.match(r"^(.+?)\s*\(\s*\*\s*(\w+)\s*\)\s*\((.*)\)$", part)
    if m:
        return Arg(name=m.group(2), ty=f"{m.group(1).strip()} (*)({m.group(3).strip()})")
    tokens = part.replace("*", " * ").split()
    if not tokens:
        return Arg(name=f"a{idx}", ty="void")
    name = tokens[-1]
    stars = 0
    while name.startswith("*"):
        stars += 1
        name = name[1:]
    if not name or name in ("const", "struct", "volatile", "enum"):
        name = f"a{idx}"
        ty = part
    else:
        ty = " ".join(tokens[:-1])
        if stars:
            ty = (ty + " " + ("*" * stars)).strip()
    if name in ("class", "new", "template", "delete"):
        name = name + "_"
    return Arg(name=name, ty=" ".join(ty.split()))


def _fn_ptr_field(decl: str) -> tuple[str, str] | None:
    """``void (*write)(const char *s, size_t n)`` -> (write, void (*)(...))."""
    m = re.match(
        r"^(.+?)\s*\(\s*\*\s*(\w+)\s*\)\s*\((.*)\)\s*$",
        " ".join(decl.split()),
    )
    if not m:
        return None
    ret, name, args = m.group(1).strip(), m.group(2), m.group(3).strip()
    return name, f"{ret} (*)({args})"


def _plain_field(decl: str) -> tuple[str, str] | None:
    decl = " ".join(decl.split()).rstrip(";")
    if not decl or "(" in decl:
        return None
    # arrays: ty name[N]
    m = re.match(r"^(.+?)\s+(\w+)\[(\d+)\]$", decl)
    if m:
        return m.group(2), f"{m.group(1).strip()}[{m.group(3)}]"
    tokens = decl.replace("*", " * ").split()
    if len(tokens) < 2:
        return None
    name = tokens[-1]
    stars = 0
    while name.startswith("*"):
        stars += 1
        name = name[1:]
    if not re.match(r"^[A-Za-z_][A-Za-z0-9_]*$", name):
        return None
    ty = " ".join(tokens[:-1])
    if stars:
        ty = (ty + " " + ("*" * stars)).strip()
    return name, " ".join(ty.split())


def catalog_from_c(path: Path) -> Catalog:
    """Extract typedef struct/enum, structs, and non-static function decls from a .h/.c."""
    raw = _strip_c_noise(path.read_text(encoding="utf-8"))
    # Collapse whitespace for block matching but keep ; boundaries
    cat = Catalog()
    fp_typedef_i = 0

    # typedef struct [tag] { body } name;
    for m in re.finditer(
        r"typedef\s+struct\s+(?:\w+\s+)?\{([^{}]*)\}\s*(\w+)\s*;",
        raw,
        flags=re.S,
    ):
        body, sname = m.group(1), m.group(2)
        fields: list[Field] = []
        for part in body.split(";"):
            part = part.strip()
            if not part:
                continue
            fp = _fn_ptr_field(part)
            if fp:
                fname, fty = fp
                tname = f"{sname}_{fname}_fn"
                cat.typedefs.append(Typedef(name=tname, ty=fty))
                fields.append(Field(name=fname, ty=tname))
                fp_typedef_i += 1
                continue
            plain = _plain_field(part)
            if plain:
                fields.append(Field(name=plain[0], ty=plain[1]))
        if fields:
            cat.structs.append(Struct(name=sname, fields=fields))

    # typedef enum { NAME = 0, ... } name;
    for m in re.finditer(
        r"typedef\s+enum\s+(?:\w+\s+)?\{([^{}]*)\}\s*(\w+)\s*;",
        raw,
        flags=re.S,
    ):
        variants: list[EnumVariant] = []
        for vm in re.finditer(
            r"([A-Za-z_][A-Za-z0-9_]*)\s*(?:=\s*(0x[0-9A-Fa-f]+|\d+))?\s*,?",
            m.group(1),
        ):
            raw_v = vm.group(2)
            if raw_v is None:
                continue  # require explicit values for round-trip
            variants.append(EnumVariant(name=vm.group(1), value=int(raw_v, 0)))
        if variants:
            cat.enums.append(EnumDef(name=m.group(2), variants=variants))
        else:
            cat.typedefs.append(Typedef(name=m.group(2), ty="uint32_t"))

    # Top-level function declarations (line-based; skip fn-ptr fields / static inline).
    seen_fns: set[str] = set()
    for line in raw.splitlines():
        line = line.strip()
        if not line.endswith(";"):
            continue
        if "(*" in line or "->" in line or line.startswith("typedef"):
            continue
        head = line.split("(", 1)[0]
        toks = head.replace("*", " * ").split()
        if any(t in toks for t in ("static", "inline", "typedef", "else", "return")):
            continue
        m = re.match(r"^(.+?)\b(\w+)\s*\((.*)\)\s*;$", line)
        if not m:
            continue
        ret = " ".join(m.group(1).split())
        fname = m.group(2)
        if not ret or fname in seen_fns:
            continue
        if fname in ("if", "for", "while", "switch", "return", "sizeof"):
            continue
        # Reject call-like leftovers (``foo()->bar(...)`` already skipped via ->).
        if "(" in ret or ")" in ret:
            continue
        seen_fns.add(fname)
        args = _split_c_args(m.group(3))
        cat.fns.append(Fn(name=fname, ret=ret, args=args))

    return cat
