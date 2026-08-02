//! Export: Catalog -> Rust `.rs` face (FFI + inline twins).

use alloc::string::String;
use alloc::vec;
use alloc::vec::Vec;

use crate::_banner::generated_banner;
use crate::_catalog::{Catalog, Fn};
use crate::_template::{self, Value};

fn rs_ident(name: &str) -> String {
    match name {
        "type" | "fn" | "mod" | "impl" | "self" | "super" | "crate" | "async" | "await"
        | "dyn" | "match" | "move" | "ref" | "where" | "use" | "pub" | "struct" | "enum"
        | "trait" | "const" | "static" | "mut" | "let" | "if" | "else" | "while" | "loop"
        | "for" | "in" | "break" | "continue" | "return" | "as" | "extern" | "box" => {
            alloc::format!("{}_", name)
        }
        _ => String::from(name),
    }
}

/// C function-pointer typedef RHS (`RET (*)(ARGS)`) -> Rust `Option<unsafe
/// extern "C" fn(ARGS) -> RET>`. The typedef importer only ever produces
/// this C shape from a Rust `Option<unsafe extern "C" fn ...>` source (see
/// `_import_rs::import`'s `pub type` handling), so round-tripping back to
/// `Option<...>` here is lossless.
fn rs_fn_ptr_ty(c_ty: &str) -> Option<String> {
    let t = collapse_ws(c_ty);
    let star_at = t.find("(*)")?;
    let ret_part = t[..star_at].trim();
    let rest = t[star_at + 3..].trim_start();
    let rest = rest.strip_prefix('(')?;
    let close = rest.rfind(')')?;
    let args_part = &rest[..close];
    let ret = rs_ty(ret_part);
    let args_s = if args_part.trim().is_empty() || args_part.trim() == "void" {
        String::new()
    } else {
        args_part
            .split(',')
            .map(|a| {
                let a = a.trim();
                let (ty_part, name) = match a.rsplit_once(' ') {
                    Some((t, n))
                        if !n.is_empty()
                            && n.chars().all(|c| c == '_' || c.is_ascii_alphanumeric()) =>
                    {
                        (t, n)
                    }
                    _ => (a, "a"),
                };
                alloc::format!("{}: {}", rs_ident(name), rs_ty(ty_part))
            })
            .collect::<Vec<_>>()
            .join(", ")
    };
    if ret == "()" {
        Some(alloc::format!("Option<unsafe extern \"C\" fn({})>", args_s))
    } else {
        Some(alloc::format!(
            "Option<unsafe extern \"C\" fn({}) -> {}>",
            args_s, ret
        ))
    }
}

fn rs_typedef_ty(c_ty: &str) -> String {
    let t = collapse_ws(c_ty);
    if t.contains("(*)") {
        if let Some(fp) = rs_fn_ptr_ty(&t) {
            return fp;
        }
    }
    if let Some(br) = t.rfind('[') {
        if t.ends_with(']') {
            let head = t[..br].trim();
            let n = t[br + 1..t.len() - 1].trim();
            let elem = rs_ty(head);
            /* Known macro lengths used by fourcc/eightcc headers. */
            let nlit = match n {
                "PM_METAL_UTIL_FOURCC_LEN" | "4" => "4",
                "PM_METAL_UTIL_EIGHTCC_LEN" | "8" => "8",
                other => other,
            };
            return alloc::format!("[{}; {}]", elem, nlit);
        }
    }
    rs_ty(&t)
}

fn rs_ty(c_ty: &str) -> String {
    let t = collapse_ws(c_ty);
    /* C array params decay to pointers: char out[N] -> *mut c_char */
    if let Some(br) = t.rfind('[') {
        if t.ends_with(']') {
            let head = t[..br].trim();
            let const_arr = head.split_whitespace().any(|x| x == "const");
            let base: String = head
                .split_whitespace()
                .filter(|x| *x != "const")
                .collect::<Vec<_>>()
                .join(" ");
            let elem = rs_ty(&base);
            return if const_arr {
                alloc::format!("*const {}", elem)
            } else {
                alloc::format!("*mut {}", elem)
            };
        }
    }
    let stars = t.chars().filter(|c| *c == '*').count();
    if stars == 0 {
        let is_const = t.split_whitespace().any(|x| x == "const");
        let base: String = t
            .split_whitespace()
            .filter(|x| *x != "const")
            .collect::<Vec<_>>()
            .join(" ");
        /* Array typedefs as params decay to pointers (const|mut). */
        if base.ends_with("_wire_t") {
            return if is_const {
                String::from("*const u8")
            } else {
                String::from("*mut u8")
            };
        }
        return match base.as_str() {
            "void" => String::from("()"),
            "int" => String::from("i32"),
            "int8_t" => String::from("i8"),
            "uint8_t" => String::from("u8"),
            "int16_t" => String::from("i16"),
            "uint16_t" => String::from("u16"),
            "int32_t" => String::from("i32"),
            "uint32_t" => String::from("u32"),
            "int64_t" => String::from("i64"),
            "uint64_t" => String::from("u64"),
            "size_t" | "uintptr_t" => String::from("usize"),
            "char" => String::from("core::ffi::c_char"),
            _ => base,
        };
    }
    let head = t.split('*').next().unwrap_or("void");
    let const_ptr = head.split_whitespace().any(|x| x == "const");
    let base_tokens: Vec<&str> = head
        .split_whitespace()
        .filter(|x| *x != "const")
        .collect();
    let base = *base_tokens.last().unwrap_or(&"void");
    let mut out = if base == "void" {
        String::from("core::ffi::c_void")
    } else {
        rs_ty(base)
    };
    for i in 0..stars {
        if i == 0 && const_ptr {
            out = alloc::format!("*const {}", out);
        } else {
            out = alloc::format!("*mut {}", out);
        }
    }
    out
}

fn collapse_ws(s: &str) -> String {
    let mut out = String::new();
    let mut sp = false;
    for c in s.chars() {
        if c.is_whitespace() {
            sp = true;
        } else {
            if sp && !out.is_empty() {
                out.push(' ');
            }
            sp = false;
            out.push(c);
        }
    }
    out
}

/// Reproduce a `static inline` C border function as a real Rust twin --
/// only for the handful of concrete, hand-verified patterns below. A
/// `static inline` function has no externally-linkable definition (it is
/// inlined per translation unit in C, same as `#[inline]` in Rust), so a
/// foreign-language consumer's generated face cannot resolve it via
/// `extern "C"` the way a real border function can; the only honest way
/// to give another language a working twin is to actually reproduce its
/// body. Anything not covered here returns `None` -- the caller omits it
/// entirely rather than emit a same-named function whose body would have
/// to be a lie (a fake `unimplemented!()`/no-op stands in for working
/// code, which is exactly what's banned; see `metal-finished-quality`).
/// The module's own real (non-inline) border functions, and same-language
/// callers who `#include`/`use` the human source directly, are unaffected.
fn emit_rs_inline_twin(fn_: &Fn) -> Option<Vec<String>> {
    let name = &fn_.name;
    let mut lines = vec![String::from("#[inline]")];
    if name.ends_with("host_is_le") {
        lines.push(alloc::format!("pub fn {}() -> i32 {{", name));
        lines.push(String::from("    1"));
        lines.push(String::from("}"));
        lines.push(String::new());
        return Some(lines);
    }
    if let Some(idx) = name.find("load_u") {
        if let Some(bits_s) = name[idx + "load_u".len()..].strip_suffix("_le") {
        if let Ok(bits) = bits_s.parse::<u32>() {
            if fn_.args.len() == 1 {
                let nbytes = (bits / 8) as usize;
                let src = rs_ident(&fn_.args[0].name);
                let ret = rs_ty(&fn_.ret);
                lines.push(alloc::format!(
                    "pub unsafe fn {}({}: *const u8) -> {} {{",
                    name, src, ret
                ));
                if bits == 64 {
                    lines.push(alloc::format!("    let mut v: {} = 0;", ret));
                    lines.push(String::from("    let mut i = 0usize;"));
                    lines.push(String::from("    while i < 8 {"));
                    lines.push(alloc::format!(
                        "        v |= (*{}.add(i) as {}) << (8 * i);",
                        src, ret
                    ));
                    lines.push(String::from("        i += 1;"));
                    lines.push(String::from("    }"));
                    lines.push(String::from("    v"));
                } else {
                    let parts: Vec<String> = (0..nbytes)
                        .map(|i| {
                            alloc::format!("(*{}.add({}) as {}) << {}", src, i, ret, 8 * i)
                        })
                        .collect();
                    lines.push(alloc::format!("    {}", parts.join(" | ")));
                }
                lines.push(String::from("}"));
                lines.push(String::new());
                return Some(lines);
            }
        }
        }
    }
    if let Some(idx) = name.find("store_u") {
        if let Some(bits_s) = name[idx + "store_u".len()..].strip_suffix("_le") {
            if let Ok(bits) = bits_s.parse::<u32>() {
                if fn_.args.len() == 2 {
                    let nbytes = (bits / 8) as usize;
                    let dst = rs_ident(&fn_.args[0].name);
                    let val = rs_ident(&fn_.args[1].name);
                    let vty = rs_ty(&fn_.args[1].ty);
                    lines.push(alloc::format!(
                        "pub unsafe fn {}({}: *mut u8, {}: {}) {{",
                        name, dst, val, vty
                    ));
                    if bits == 64 {
                        lines.push(String::from("    let mut i = 0usize;"));
                        lines.push(String::from("    while i < 8 {"));
                        lines.push(alloc::format!(
                            "        *{}.add(i) = (({} >> (8 * i)) & 0xff) as u8;",
                            dst, val
                        ));
                        lines.push(String::from("        i += 1;"));
                        lines.push(String::from("    }"));
                    } else {
                        for i in 0..nbytes {
                            lines.push(alloc::format!(
                                "    *{}.add({}) = (({} >> {}) & 0xff) as u8;",
                                dst,
                                i,
                                val,
                                8 * i
                            ));
                        }
                    }
                    lines.push(String::from("}"));
                    lines.push(String::new());
                    return Some(lines);
                }
            }
        }
    }
    None
}

/// Append `lines`, each followed by its own newline -- the uniform
/// convention every template render in this file also follows (see
/// [`export`]'s doc comment), so hand-built segments (like
/// [`emit_rs_inline_twin`]'s synthesized bodies) splice into a
/// template-rendered one without a seam.
fn push_lines(out: &mut String, lines: &[String]) {
    for l in lines {
        out.push_str(l);
        out.push('\n');
    }
}

/// Struct/union field type: array typedef names stay as their typedef
/// (not param-decayed to a pointer -- that decay only applies to
/// function parameters, see [`rs_ty`]); everything else goes through the
/// normal C-to-Rust type mapping.
fn field_rs_ty(ty: &str) -> String {
    let t = collapse_ws(ty);
    let base: String = t
        .split_whitespace()
        .filter(|x| *x != "const")
        .collect::<Vec<_>>()
        .join(" ");
    if base.ends_with("_wire_t") {
        base
    } else {
        rs_ty(ty)
    }
}

/// Enums/structs/typedefs: identical ABI shape regardless of whether the
/// provider is `unloadable` -- only how the functions below them resolve
/// differs (see [`export`] vs [`export_proxy`]). Structural assembly
/// (the loops/braces below) is templated; per-item type-string
/// computation (`rs_ty`/`field_rs_ty`/`rs_typedef_ty`) stays ordinary
/// Rust -- a template has nothing to offer an algorithm, only a shape.
const TYPES_TEMPLATE: &str = include_str!("templates/export_rs_types.tpl");

fn emit_types(cat: &Catalog) -> String {
    let enums = Value::list_map(&cat.enums, |en| {
        let variants = Value::list_map(&en.variants, |v| {
            Value::map(alloc::vec![
                ("name", Value::str(v.name.clone())),
                ("value", Value::str(alloc::format!("{}", v.value))),
            ])
        });
        Value::map(alloc::vec![("name", Value::str(en.name.clone())), ("variants", variants)])
    });
    let structs = Value::list_map(&cat.structs, |st| {
        let kind = if st.is_union { "union" } else { "struct" };
        let fields = Value::list_map(&st.fields, |f| {
            Value::map(alloc::vec![
                ("name", Value::str(rs_ident(&f.name))),
                ("ty", Value::str(field_rs_ty(&f.ty))),
            ])
        });
        Value::map(alloc::vec![
            ("kind", Value::str(kind)),
            ("name", Value::str(st.name.clone())),
            ("fields", fields),
        ])
    });
    let typedefs = Value::list_map(&cat.typedefs, |td| {
        Value::map(alloc::vec![
            ("name", Value::str(td.name.clone())),
            ("rs", Value::str(rs_typedef_ty(&td.ty))),
        ])
    });
    let ctx = Value::map(alloc::vec![("enums", enums), ("structs", structs), ("typedefs", typedefs)]);
    _template::render_str(TYPES_TEMPLATE, ctx).expect("rs types template")
}

fn rs_args(fn_: &Fn) -> String {
    fn_.args
        .iter()
        .map(|a| alloc::format!("{}: {}", rs_ident(&a.name), rs_ty(&a.ty)))
        .collect::<Vec<_>>()
        .join(", ")
}

/// `extern "C" { ... }` block shared by [`export`]'s `if` branch --
/// `fns` is `extern_fns` pre-rendered as `pub fn NAME(ARGS);` /
/// `pub fn NAME(ARGS) -> RET;` per [`rs_ty`]'s `()`-means-no-return-type
/// convention.
const EXTERN_BLOCK_TEMPLATE: &str = include_str!("templates/export_rs_extern.tpl");

fn fn_decl(fn_: &Fn, args: &str, ret: &str) -> String {
    if ret == "()" {
        alloc::format!("pub fn {}({});", fn_.name, args)
    } else {
        alloc::format!("pub fn {}({}) -> {};", fn_.name, args, ret)
    }
}

/// Fast-path face (provider `unloadable == false`, the permanent-module
/// case): plain `extern "C"` declarations resolved at link time -- no
/// cache slot, no runtime connect step, no refcount.
///
/// Every segment below (hand-built [`emit_rs_inline_twin`] bodies and
/// template-rendered blocks alike) is built to always end in its own
/// trailing newline, then the *whole* concatenation has exactly one
/// trailing newline stripped at the very end -- the same "N lines, N-1
/// separators" shape `Vec<String>::join("\n")` gives for free, without
/// needing every template to special-case "is this the last line".
pub fn export(
    module_name: &str,
    base: &str,
    cat: &Catalog,
    human: &str,
    source_sha: &str,
) -> String {
    let mut out = String::new();
    for line in generated_banner("rs", human, &alloc::format!("{}.rs", base), source_sha) {
        out.push_str(&line);
        out.push('\n');
    }
    out.push('\n');
    out.push_str("#![allow(dead_code, non_camel_case_types)]\n");
    out.push('\n');
    out.push_str(&emit_types(cat));

    let inline_fns: Vec<&Fn> = cat.fns.iter().filter(|f| f.inline).collect();
    let extern_fns: Vec<&Fn> = cat.fns.iter().filter(|f| !f.inline).collect();
    for fn_ in inline_fns {
        if let Some(twin) = emit_rs_inline_twin(fn_) {
            push_lines(&mut out, &twin);
        }
    }
    if !extern_fns.is_empty() || cat.fns.is_empty() {
        let fns = Value::list_map(&extern_fns, |f| {
            let args = rs_args(f);
            let ret = rs_ty(&f.ret);
            Value::map(alloc::vec![("decl", Value::str(fn_decl(f, &args, &ret)))])
        });
        let ctx = Value::map(alloc::vec![
            ("fns", fns),
            ("module_empty", Value::Bool(cat.fns.is_empty())),
            ("module_name", Value::str(module_name)),
        ]);
        out.push_str(&_template::render_str(EXTERN_BLOCK_TEMPLATE, ctx).expect("rs extern block"));
    }
    if out.ends_with('\n') {
        out.pop();
    }
    out
}

/// Registry-proxy face (provider `unloadable == true`): each non-inline
/// export gets a cached [`pymergetic_metal_reg::ImportRow`][row], filled
/// in by the kernel's connect pass, and a safe-signature wrapper that
/// resolves through it and calls straight through the cached pointer --
/// no refcount, no lock: `unload` quiesces every async runner before
/// withdrawing anything (see `pymergetic_metal_reg::kernel::unload`), so
/// there is no concurrent caller to race against. `pymergetic_metal_reg`
/// is the registry spine, so depending on it directly here is the one
/// Cargo-dependency exception (see docs/definitions/module.md "Consume
/// foreign modules").
///
/// Calling a wrapper before the provider has loaded/connected is a
/// documented misuse (same contract as calling through a null function
/// pointer) and is reported by an `assert!`, not a synthesized fake
/// return value -- there is no type-generic "half-open" result to invent
/// for an arbitrary return type.
///
/// [row]: pymergetic_metal_reg::ImportRow
const IMPORT_ROWS_TEMPLATE: &str = include_str!("templates/export_rs_import_rows.tpl");

const PROXY_FNS_TEMPLATE: &str = include_str!("templates/export_rs_proxy_fns.tpl");

pub fn export_proxy(
    module_name: &str,
    base: &str,
    cat: &Catalog,
    human: &str,
    source_sha: &str,
) -> String {
    let mut out = String::new();
    for line in generated_banner("rs", human, &alloc::format!("{}.rs", base), source_sha) {
        out.push_str(&line);
        out.push('\n');
    }
    out.push('\n');
    out.push_str("#![allow(dead_code, non_camel_case_types)]\n");
    out.push('\n');
    out.push_str(&emit_types(cat));

    let inline_fns: Vec<&Fn> = cat.fns.iter().filter(|f| f.inline).collect();
    let extern_fns: Vec<&Fn> = cat.fns.iter().filter(|f| !f.inline).collect();
    for fn_ in inline_fns {
        if let Some(twin) = emit_rs_inline_twin(fn_) {
            push_lines(&mut out, &twin);
        }
    }

    if extern_fns.is_empty() {
        out.push_str(&alloc::format!("// module {}: empty catalog", module_name));
        return out;
    }

    let fns = Value::list_map(&extern_fns, |f| {
        let args = rs_args(f);
        let ret = rs_ty(&f.ret);
        let arg_tys = f.args.iter().map(|a| rs_ty(&a.ty)).collect::<Vec<_>>().join(", ");
        let call_args = f
            .args
            .iter()
            .map(|a| rs_ident(&a.name))
            .collect::<Vec<_>>()
            .join(", ");
        let sig_ret = if ret == "()" {
            String::new()
        } else {
            alloc::format!(" -> {}", ret)
        };
        let fn_ty_ret = sig_ret.clone();
        Value::map(alloc::vec![
            ("name", Value::str(f.name.clone())),
            ("args", Value::str(args)),
            ("sig_ret", Value::str(sig_ret)),
            ("arg_tys", Value::str(arg_tys)),
            ("fn_ty_ret", Value::str(fn_ty_ret)),
            ("call_args", Value::str(call_args)),
        ])
    });
    let ctx = Value::map(alloc::vec![("fns", fns), ("module_name", Value::str(module_name))]);
    out.push_str(&_template::render_str(IMPORT_ROWS_TEMPLATE, ctx.clone()).expect("rs import rows"));
    out.push_str(&_template::render_str(PROXY_FNS_TEMPLATE, ctx).expect("rs proxy fns"));
    if out.ends_with('\n') {
        out.pop();
    }
    out
}

