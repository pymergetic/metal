"""Parse catalog.toml and emit C / Rust / Python projections."""
from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

try:
    import tomllib
except ModuleNotFoundError:  # pragma: no cover
    import tomli as tomllib  # type: ignore


@dataclass
class Field:
    name: str
    ty: str


@dataclass
class Struct:
    name: str
    fields: list[Field] = field(default_factory=list)


@dataclass
class Typedef:
    name: str
    ty: str


@dataclass
class EnumVariant:
    name: str
    value: int


@dataclass
class EnumDef:
    """C ``typedef enum`` / Rust ``#[repr(u32)] enum`` with explicit values."""

    name: str
    variants: list[EnumVariant] = field(default_factory=list)


@dataclass
class Arg:
    name: str
    ty: str


@dataclass
class Fn:
    name: str
    ret: str
    args: list[Arg] = field(default_factory=list)
    # True for C static inline border (ported Rust face, not extern FFI).
    inline: bool = False


@dataclass
class Catalog:
    structs: list[Struct] = field(default_factory=list)
    typedefs: list[Typedef] = field(default_factory=list)
    enums: list[EnumDef] = field(default_factory=list)
    fns: list[Fn] = field(default_factory=list)

    def is_empty(self) -> bool:
        return not self.structs and not self.typedefs and not self.enums and not self.fns

    def has_border(self) -> bool:
        """True if this catalog is a public C/lang border (needs exported fns)."""
        return bool(self.fns)


def _as_list(val: Any) -> list[Any]:
    if val is None:
        return []
    if isinstance(val, list):
        return val
    raise TypeError(f"expected list, got {type(val).__name__}")


def load_catalog(path: Path) -> Catalog:
    data = tomllib.loads(path.read_text(encoding="utf-8"))
    cat = Catalog()
    for raw in _as_list(data.get("struct")):
        fields = [
            Field(name=str(f["name"]), ty=str(f["ty"]))
            for f in _as_list(raw.get("fields"))
        ]
        cat.structs.append(Struct(name=str(raw["name"]), fields=fields))
    for raw in _as_list(data.get("typedef")):
        cat.typedefs.append(Typedef(name=str(raw["name"]), ty=str(raw["ty"])))
    for raw in _as_list(data.get("enum")):
        variants = [
            EnumVariant(name=str(v["name"]), value=int(v["value"]))
            for v in _as_list(raw.get("variants"))
        ]
        cat.enums.append(EnumDef(name=str(raw["name"]), variants=variants))
    for raw in _as_list(data.get("fn")):
        args = [
            Arg(name=str(a["name"]), ty=str(a["ty"]))
            for a in _as_list(raw.get("args"))
        ]
        cat.fns.append(
            Fn(
                name=str(raw["name"]),
                ret=str(raw.get("ret", "void")),
                args=args,
                inline=bool(raw.get("inline", False)),
            )
        )
    return cat


def _c_field_decl(ty: str, name: str) -> str:
    """Emit 'ty name' or 'base name[N]' when ty is like uint32_t[4]."""
    t = " ".join(ty.split())
    if "[" in t and t.endswith("]"):
        base, _, rest = t.partition("[")
        return f"{base.strip()} {name}[{rest.rstrip(']')}]"
    return f"{t} {name}"


def _c_typedef(td: Typedef) -> str:
    """Emit typedef; inject name into function-pointer ty containing (*)."""
    t = " ".join(td.ty.split())
    if "(*)" in t:
        return f"typedef {t.replace('(*)', f'(*{td.name})', 1)};"
    return f"typedef {t} {td.name};"


def _c_args(fn: Fn) -> str:
    if not fn.args:
        return "void"
    return ", ".join(f"{a.ty} {a.name}" for a in fn.args)


_PRIM = {
    "void": "()",
    "int": "i32",
    "int8_t": "i8",
    "uint8_t": "u8",
    "int16_t": "i16",
    "uint16_t": "u16",
    "int32_t": "i32",
    "uint32_t": "u32",
    "int64_t": "i64",
    "uint64_t": "u64",
    "size_t": "usize",
    "uintptr_t": "usize",
    "char": "core::ffi::c_char",
}


def _rs_base(name: str) -> str:
    name = name.strip()
    if name == "void":
        return "core::ffi::c_void"
    return _PRIM.get(name, name)


def _rs_ty(c_ty: str) -> str:
    """Best-effort C type -> Rust FFI type for consumer bindings."""
    t = " ".join(c_ty.split())
    if "(*)" in t:
        # Function-pointer typedef name used as a type - leave as-is (caller aliases).
        return t
    stars = t.count("*")
    if stars == 0:
        return _PRIM.get(t, t)
    head = t.split("*")[0]
    const_ptr = "const" in head.split()
    base_tokens = [x for x in head.replace("const", " ").split() if x]
    base = base_tokens[-1] if base_tokens else "void"
    inner = _rs_base(base)
    # Multi-level pointers: *mut/*const of remaining
    out = inner
    for i in range(stars):
        # outermost const applies to first *; deeper levels default mut
        if i == 0 and const_ptr:
            out = f"*const {out}"
        else:
            out = f"*mut {out}"
    return out


# Ownership banner (ASCII only). Must stay readable as the word GENERATED.
_GENERATED_MARK = "GENERATED"


def generated_banner(style: str, human: str, this_file: str) -> list[str]:
    """Return banner lines for C (/* */), Rust (//!), or Python (#)."""
    hints = [
        _GENERATED_MARK,
        "DO NOT HAND-EDIT THIS FILE.",
        f"This file is:  {this_file}",
        f"Edit instead:  {human}",
        "Regenerate:    metal mod sync",
        "Owned by:      metal mod sync (banner = write gate)",
    ]
    if style == "c":
        return ["/*"] + [" * " + h for h in hints] + [" */"]
    if style == "rs":
        return [f"//! {h}" for h in hints]
    # py / pyi
    return [f"# {h}" for h in hints]


_RS_KEYWORDS = frozenset(
    {
        "as",
        "async",
        "await",
        "break",
        "const",
        "continue",
        "crate",
        "dyn",
        "else",
        "enum",
        "extern",
        "false",
        "fn",
        "for",
        "if",
        "impl",
        "in",
        "let",
        "loop",
        "match",
        "mod",
        "move",
        "mut",
        "pub",
        "ref",
        "return",
        "self",
        "Self",
        "static",
        "struct",
        "super",
        "trait",
        "true",
        "type",
        "unsafe",
        "use",
        "where",
        "while",
    }
)


def _rs_ident(name: str) -> str:
    """Rust field/arg ident; escape keywords (e.g. C ``type`` -> ``r#type``)."""
    if name in _RS_KEYWORDS:
        return f"r#{name}"
    return name


def _toml_str(s: str) -> str:
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def emit_catalog_toml(cat: Catalog, human: str, this_file: str) -> str:
    """Serialize catalog IR with GENERATED banner (``{base}.toml`` face)."""
    lines = [
        *generated_banner("py", human, this_file),
        "",
    ]
    for st in cat.structs:
        lines.append("[[struct]]")
        lines.append(f"name = {_toml_str(st.name)}")
        if st.fields:
            lines.append("fields = [")
            for f in st.fields:
                lines.append(
                    f"  {{ name = {_toml_str(f.name)}, ty = {_toml_str(f.ty)} }},"
                )
            lines.append("]")
        lines.append("")
    for td in cat.typedefs:
        lines.append("[[typedef]]")
        lines.append(f"name = {_toml_str(td.name)}")
        lines.append(f"ty = {_toml_str(td.ty)}")
        lines.append("")
    for en in cat.enums:
        lines.append("[[enum]]")
        lines.append(f"name = {_toml_str(en.name)}")
        lines.append("variants = [")
        for v in en.variants:
            lines.append(
                f"  {{ name = {_toml_str(v.name)}, value = {int(v.value)} }},"
            )
        lines.append("]")
        lines.append("")
    for fn in cat.fns:
        lines.append("[[fn]]")
        lines.append(f"name = {_toml_str(fn.name)}")
        lines.append(f"ret = {_toml_str(fn.ret)}")
        if fn.args:
            lines.append("args = [")
            for a in fn.args:
                lines.append(
                    f"  {{ name = {_toml_str(a.name)}, ty = {_toml_str(a.ty)} }},"
                )
            lines.append("]")
        lines.append("")
    if cat.is_empty():
        lines.append("# empty catalog")
        lines.append("")
    return "\n".join(lines)


_C_BUILTIN_TY = frozenset(
    {
        "void",
        "bool",
        "char",
        "float",
        "double",
        "int8_t",
        "uint8_t",
        "int16_t",
        "uint16_t",
        "int32_t",
        "uint32_t",
        "int64_t",
        "uint64_t",
        "size_t",
        "intptr_t",
        "uintptr_t",
        "ptrdiff_t",
    }
)


def _opaque_struct_names(cat: Catalog) -> list[str]:
    """Names used as ``Foo *`` / ``const Foo *`` without a defined struct body."""
    known = (
        {st.name for st in cat.structs}
        | {td.name for td in cat.typedefs}
        | {en.name for en in cat.enums}
    )
    found: set[str] = set()
    tys: list[str] = []
    for fn in cat.fns:
        tys.append(fn.ret)
        tys.extend(a.ty for a in fn.args)
    for ty in tys:
        m = re.match(r"^(?:const\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*\*$", " ".join(ty.split()))
        if not m:
            continue
        name = m.group(1)
        if name in _C_BUILTIN_TY or name in known:
            continue
        found.add(name)
    return sorted(found)


# Sibling C includes for faces that name types owned by another stem in the
# same package. Keyed by (package, stem). Keep small — prefer catalog-local
# types when adding new APIs.
_SIBLING_C_INCLUDES: dict[tuple[str, str], list[str]] = {
    ("pymergetic.metal.async", "await"): [
        "#include <pymergetic/metal/async/handle.h>",
    ],
    ("pymergetic.metal.async", "phase"): [
        "#include <pymergetic/metal/async/handle.h>",
    ],
    ("pymergetic.metal.async", "task"): [
        "#include <pymergetic/metal/async/coro.h>",
    ],
    ("pymergetic.metal.async", "process"): [
        "#include <pymergetic/metal/async/handle.h>",
    ],
}

# Extra typedefs when rust `pub type Alias = u32` is not exported as a catalog
# typedef (only Option<fn> aliases are today).
_SIBLING_C_TYPEDEFS: dict[tuple[str, str], list[str]] = {
    ("pymergetic.metal.async", "process"): [
        "typedef uint32_t pm_metal_async_pid_t;",
    ],
}


def emit_c_header(module_name: str, base: str, cat: Catalog, human: str) -> str:
    guard = "PM_METAL_" + module_name.upper().replace(".", "_").replace("-", "_") + "_H_"
    # Package for sibling lookup: strip trailing .<stem> when base is not entry.
    pkg = module_name
    if base != "__init__" and pkg.endswith("." + base):
        pkg = pkg[: -(len(base) + 1)]
    sib_key = (pkg, base)
    lines = [
        *generated_banner("c", human, f"{base}.h"),
        "",
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
        "#include <stddef.h>",
        "#include <stdint.h>",
    ]
    for inc in _SIBLING_C_INCLUDES.get(sib_key, []):
        lines.append(inc)
    lines += [
        "",
        "#ifdef __cplusplus",
        'extern "C" {',
        "#endif",
        "",
    ]
    for td in _SIBLING_C_TYPEDEFS.get(sib_key, []):
        lines.append(td)
        lines.append("")
    # Forward decls so typedefs (fn-pointer aliases) may name structs.
    fwd = sorted({st.name for st in cat.structs} | set(_opaque_struct_names(cat)))
    for name in fwd:
        lines.append(f"typedef struct {name} {name};")
        lines.append("")
    for en in cat.enums:
        lines.append("typedef enum {")
        for i, v in enumerate(en.variants):
            comma = "," if i + 1 < len(en.variants) else ""
            lines.append(f"  {v.name} = {int(v.value)}{comma}")
        lines.append(f"}} {en.name};")
        lines.append("")
    for td in cat.typedefs:
        lines.append(_c_typedef(td))
        lines.append("")
    for st in cat.structs:
        lines.append(f"struct {st.name} {{")
        for f in st.fields:
            lines.append(f"  {_c_field_decl(f.ty, f.name)};")
        lines.append("};")
        lines.append("")
    for fn in cat.fns:
        lines.append(f"{fn.ret} {fn.name}({_c_args(fn)});")
    if cat.is_empty():
        lines.append(f"/* module {module_name}: empty catalog.toml */")
    lines += [
        "",
        "#ifdef __cplusplus",
        "}",
        "#endif",
        "",
        f"#endif /* {guard} */",
        "",
    ]
    return "\n".join(lines)


def _rs_inline_arg_ty(ty: str) -> str:
    """Map C inline-arg types (incl. ``uint8_t[N]``) to Rust pointer form."""
    t = " ".join(ty.split())
    m = re.match(r"^(const\s+)?uint8_t\[(\d+)\]$", t)
    if m:
        return "*const u8" if m.group(1) else "*mut u8"
    return _rs_ty(t)


def _emit_rs_inline_twin(fn: Fn) -> list[str]:
    """Ported Rust body for a C ``static inline`` border fn (not extern FFI)."""
    name = fn.name
    lines: list[str] = ["#[inline]"]

    if name.endswith("host_is_le"):
        lines += [f"pub fn {name}() -> i32 {{", "    1", "}", ""]
        return lines

    m_load = re.search(r"load_u(\d+)_le$", name)
    if m_load and len(fn.args) == 1:
        bits = int(m_load.group(1))
        nbytes = bits // 8
        src = _rs_ident(fn.args[0].name)
        ret = _rs_ty(fn.ret)
        lines.append(f"pub unsafe fn {name}({src}: *const u8) -> {ret} {{")
        if bits == 64:
            lines += [
                f"    let mut v: {ret} = 0;",
                "    let mut i = 0usize;",
                "    while i < 8 {",
                f"        v |= (*{src}.add(i) as {ret}) << (8 * i);",
                "        i += 1;",
                "    }",
                "    v",
            ]
        else:
            parts = [
                f"(*{src}.add({i}) as {ret}) << {8 * i}" for i in range(nbytes)
            ]
            lines.append("    " + " | ".join(parts))
        lines += ["}", ""]
        return lines

    m_store = re.search(r"store_u(\d+)_le$", name)
    if m_store and len(fn.args) == 2:
        bits = int(m_store.group(1))
        nbytes = bits // 8
        dst = _rs_ident(fn.args[0].name)
        val = _rs_ident(fn.args[1].name)
        vty = _rs_ty(fn.args[1].ty)
        lines.append(
            f"pub unsafe fn {name}({dst}: *mut u8, {val}: {vty}) {{"
        )
        if bits == 64:
            lines += [
                "    let mut i = 0usize;",
                "    while i < 8 {",
                f"        *{dst}.add(i) = (({val} >> (8 * i)) & 0xff) as u8;",
                "        i += 1;",
                "    }",
            ]
        else:
            for i in range(nbytes):
                lines.append(
                    f"    *{dst}.add({i}) = (({val} >> {8 * i}) & 0xff) as u8;"
                )
        lines += ["}", ""]
        return lines

    # Fallback: signature-only stub (should not hit for endian).
    args = ", ".join(
        f"{_rs_ident(a.name)}: {_rs_inline_arg_ty(a.ty)}" for a in fn.args
    )
    ret = _rs_ty(fn.ret)
    if ret == "()":
        lines += [f"pub unsafe fn {name}({args}) {{", "}", ""]
    else:
        lines += [
            f"pub unsafe fn {name}({args}) -> {ret} {{",
            "    core::unimplemented!()",
            "}",
            "",
        ]
    return lines


def emit_rs_bindings(module_name: str, base: str, cat: Catalog, human: str) -> str:
    # No #![no_std]: consumer faces are #[path] mods inside the parent crate.
    lines = [
        *generated_banner("rs", human, f"{base}.rs"),
        "",
        "#![allow(dead_code, non_camel_case_types)]",
        "",
    ]
    typedef_names = {td.name for td in cat.typedefs}
    enum_names = {en.name for en in cat.enums}
    for en in cat.enums:
        lines.append("#[repr(u32)]")
        lines.append("#[derive(Clone, Copy)]")
        lines.append("#[allow(non_camel_case_types)]")
        lines.append(f"pub enum {en.name} {{")
        for v in en.variants:
            lines.append(f"    {v.name} = {int(v.value)},")
        lines.append("}")
        lines.append("")

    for st in cat.structs:
        lines.append("#[repr(C)]")
        lines.append("#[derive(Clone, Copy)]")
        lines.append(f"pub struct {st.name} {{")
        for f in st.fields:
            lines.append(f"    pub {_rs_ident(f.name)}: {_field_rs(f.ty)},")
        lines.append("}")
        lines.append("")

    for td in cat.typedefs:
        lines.append(f"pub type {td.name} = {_rs_typedef(td)};")
    if cat.typedefs:
        lines.append("")

    def arg_ty(ty: str) -> str:
        t = " ".join(ty.split())
        if t in typedef_names or t in enum_names:
            return t
        return _rs_ty(t)

    inline_fns = [fn for fn in cat.fns if fn.inline]
    extern_fns = [fn for fn in cat.fns if not fn.inline]

    for fn in inline_fns:
        lines.extend(_emit_rs_inline_twin(fn))

    if extern_fns or not cat.fns:
        lines.append('extern "C" {')
        for fn in extern_fns:
            args = ", ".join(f"{_rs_ident(a.name)}: {arg_ty(a.ty)}" for a in fn.args)
            ret = arg_ty(fn.ret) if fn.ret.strip() in typedef_names else _rs_ty(fn.ret)
            if ret == "()":
                lines.append(f"    pub fn {fn.name}({args});")
            else:
                lines.append(f"    pub fn {fn.name}({args}) -> {ret};")
        if not cat.fns:
            lines.append(f"    // module {module_name}: empty catalog")
        lines += ["}", ""]
    return "\n".join(lines)


def _field_rs(c_ty: str) -> str:
    t = " ".join(c_ty.split())
    # uint32_t[4]
    if "[" in t and t.endswith("]"):
        base, _, rest = t.partition("[")
        n = rest.rstrip("]")
        inner = _rs_ty(base.strip())
        return f"[{inner}; {n}]"
    return _rs_ty(t)


def _rs_typedef(td: Typedef) -> str:
    """Map a C typedef to a Rust type alias RHS."""
    t = " ".join(td.ty.split())
    if "(*)" not in t:
        return _rs_ty(t)
    # ret (*)(args) -> Option<unsafe extern "C" fn(args) -> ret>
    m = re.match(r"^(.+?)\s*\(\*\)\s*\((.*)\)$", t)
    if not m:
        return "*mut core::ffi::c_void"
    ret_c, args_c = m.group(1).strip(), m.group(2).strip()
    ret = _rs_ty(ret_c)
    if not args_c or args_c == "void":
        args_rs = ""
    else:
        parts = []
        for i, raw in enumerate(args_c.split(",")):
            raw = raw.strip()
            tokens = raw.split()
            if not tokens:
                continue
            name = tokens[-1]
            stars = 0
            while name.startswith("*"):
                stars += 1
                name = name[1:]
            if not name or name in ("const", "struct", "volatile"):
                name = f"a{i}"
                ty = raw
            else:
                ty = " ".join(tokens[:-1])
                if stars:
                    ty = (ty + " " + ("*" * stars)).strip()
            parts.append(f"{_rs_ident(name)}: {_rs_ty(ty)}")
        args_rs = ", ".join(parts)
    if ret == "()":
        return f'Option<unsafe extern "C" fn({args_rs})>'
    return f'Option<unsafe extern "C" fn({args_rs}) -> {ret}>'


def emit_pyi(module_name: str, base: str, cat: Catalog, human: str) -> str:
    lines = [
        *generated_banner("py", human, f"{base}.pyi"),
        "",
        f'"""Stubs for {module_name}."""',
        "",
    ]
    for fn in cat.fns:
        args = ", ".join(a.name for a in fn.args)
        lines.append(f"def {fn.name}({args}) -> int: ...")
    if not cat.fns:
        lines.append("# package marker (no exported symbols)")
    lines.append("")
    return "\n".join(lines)

