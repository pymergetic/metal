//! Export: Catalog -> Rust `.rs` face (FFI + inline twins).

use alloc::string::String;
use alloc::vec;
use alloc::vec::Vec;

use crate::_banner::generated_banner;
use crate::_catalog::{Catalog, Fn};

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

fn emit_rs_inline_twin(fn_: &Fn) -> Vec<String> {
    let name = &fn_.name;
    let mut lines = vec![String::from("#[inline]")];
    if name.ends_with("host_is_le") {
        lines.push(alloc::format!("pub fn {}() -> i32 {{", name));
        lines.push(String::from("    1"));
        lines.push(String::from("}"));
        lines.push(String::new());
        return lines;
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
                return lines;
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
                    return lines;
                }
            }
        }
    }
    let args = fn_
        .args
        .iter()
        .map(|a| alloc::format!("{}: {}", rs_ident(&a.name), rs_ty(&a.ty)))
        .collect::<Vec<_>>()
        .join(", ");
    let ret = rs_ty(&fn_.ret);
    if ret == "()" {
        lines.push(alloc::format!("pub unsafe fn {}({}) {{", name, args));
        lines.push(String::from("}"));
    } else {
        lines.push(alloc::format!(
            "pub unsafe fn {}({}) -> {} {{",
            name, args, ret
        ));
        lines.push(String::from("    core::unimplemented!()"));
        lines.push(String::from("}"));
    }
    lines.push(String::new());
    lines
}

/// Enums/structs/typedefs: identical ABI shape regardless of whether the
/// provider is `unloadable` -- only how the functions below them resolve
/// differs (see [`export`] vs [`export_proxy`]).
fn emit_types(cat: &Catalog) -> Vec<String> {
    let mut lines = Vec::new();
    for en in &cat.enums {
        lines.push(String::from("#[repr(u32)]"));
        lines.push(String::from("#[derive(Clone, Copy)]"));
        lines.push(String::from("#[allow(non_camel_case_types)]"));
        lines.push(alloc::format!("pub enum {} {{", en.name));
        for v in &en.variants {
            lines.push(alloc::format!("    {} = {},", v.name, v.value));
        }
        lines.push(String::from("}"));
        lines.push(String::new());
    }
    for st in &cat.structs {
        lines.push(String::from("#[repr(C)]"));
        lines.push(String::from("#[derive(Clone, Copy)]"));
        let kind = if st.is_union { "union" } else { "struct" };
        lines.push(alloc::format!("pub {} {} {{", kind, st.name));
        for f in &st.fields {
            let fty = {
                let t = collapse_ws(&f.ty);
                let base: String = t
                    .split_whitespace()
                    .filter(|x| *x != "const")
                    .collect::<Vec<_>>()
                    .join(" ");
                /* Struct/union fields keep array typedef names (not param decay). */
                if base.ends_with("_wire_t") {
                    base
                } else {
                    rs_ty(&f.ty)
                }
            };
            lines.push(alloc::format!(
                "    pub {}: {},",
                rs_ident(&f.name),
                fty
            ));
        }
        lines.push(String::from("}"));
        lines.push(String::new());
    }
    for td in &cat.typedefs {
        let rs = rs_typedef_ty(&td.ty);
        lines.push(alloc::format!("pub type {} = {};", td.name, rs));
    }
    if !cat.typedefs.is_empty() {
        lines.push(String::new());
    }
    lines
}

fn rs_args(fn_: &Fn) -> String {
    fn_.args
        .iter()
        .map(|a| alloc::format!("{}: {}", rs_ident(&a.name), rs_ty(&a.ty)))
        .collect::<Vec<_>>()
        .join(", ")
}

/// Fast-path face (provider `unloadable == false`, the permanent-module
/// case): plain `extern "C"` declarations resolved at link time -- no
/// cache slot, no runtime connect step, no refcount.
pub fn export(
    module_name: &str,
    base: &str,
    cat: &Catalog,
    human: &str,
    source_sha: &str,
) -> String {
    let mut lines =
        generated_banner("rs", human, &alloc::format!("{}.rs", base), source_sha);
    lines.push(String::new());
    lines.push(String::from("#![allow(dead_code, non_camel_case_types)]"));
    lines.push(String::new());
    lines.extend(emit_types(cat));

    let inline_fns: Vec<&Fn> = cat.fns.iter().filter(|f| f.inline).collect();
    let extern_fns: Vec<&Fn> = cat.fns.iter().filter(|f| !f.inline).collect();
    for fn_ in inline_fns {
        lines.extend(emit_rs_inline_twin(fn_));
    }
    if !extern_fns.is_empty() || cat.fns.is_empty() {
        lines.push(String::from("extern \"C\" {"));
        for fn_ in &extern_fns {
            let args = rs_args(fn_);
            let ret = rs_ty(&fn_.ret);
            if ret == "()" {
                lines.push(alloc::format!("    pub fn {}({});", fn_.name, args));
            } else {
                lines.push(alloc::format!(
                    "    pub fn {}({}) -> {};",
                    fn_.name, args, ret
                ));
            }
        }
        if cat.fns.is_empty() {
            lines.push(alloc::format!(
                "    // module {}: empty catalog",
                module_name
            ));
        }
        lines.push(String::from("}"));
        lines.push(String::new());
    }
    lines.join("\n")
}

/// Registry-proxy face (provider `unloadable == true`): each non-inline
/// export gets a cached [`pymergetic_metal_reg::ImportRow`][row], filled
/// in by the kernel's connect pass, and a safe-signature wrapper that
/// resolves through [`pymergetic_metal_reg::acquire`]/[`::release`]
/// (refcounted, so the provider cannot unload mid-call) instead of a
/// link-time symbol. `pymergetic_metal_reg` is the registry spine, so
/// depending on it directly here is the one Cargo-dependency exception
/// (see docs/definitions/module.md "Consume foreign modules").
///
/// Calling a wrapper before the provider has loaded/connected is a
/// documented misuse (same contract as calling through a null function
/// pointer) and is reported by an `assert!`, not a synthesized fake
/// return value -- there is no type-generic "half-open" result to invent
/// for an arbitrary return type.
///
/// [row]: pymergetic_metal_reg::ImportRow
pub fn export_proxy(
    module_name: &str,
    base: &str,
    cat: &Catalog,
    human: &str,
    source_sha: &str,
) -> String {
    let mut lines =
        generated_banner("rs", human, &alloc::format!("{}.rs", base), source_sha);
    lines.push(String::new());
    lines.push(String::from("#![allow(dead_code, non_camel_case_types)]"));
    lines.push(String::new());
    lines.extend(emit_types(cat));

    let inline_fns: Vec<&Fn> = cat.fns.iter().filter(|f| f.inline).collect();
    let extern_fns: Vec<&Fn> = cat.fns.iter().filter(|f| !f.inline).collect();
    for fn_ in inline_fns {
        lines.extend(emit_rs_inline_twin(fn_));
    }

    if extern_fns.is_empty() {
        lines.push(alloc::format!("// module {}: empty catalog", module_name));
        return lines.join("\n");
    }

    for fn_ in &extern_fns {
        lines.push(alloc::format!(
            "static __PM_METAL_IMPORT_{}: pymergetic_metal_reg::ImportRow = pymergetic_metal_reg::ImportRow::new(\"{}\", \"{}\");",
            fn_.name, module_name, fn_.name
        ));
    }
    lines.push(String::new());

    for fn_ in &extern_fns {
        let args = rs_args(fn_);
        let ret = rs_ty(&fn_.ret);
        let arg_tys = fn_
            .args
            .iter()
            .map(|a| rs_ty(&a.ty))
            .collect::<Vec<_>>()
            .join(", ");
        let call_args = fn_
            .args
            .iter()
            .map(|a| rs_ident(&a.name))
            .collect::<Vec<_>>()
            .join(", ");
        let row = alloc::format!("__PM_METAL_IMPORT_{}", fn_.name);
        let sig_ret = if ret == "()" {
            String::new()
        } else {
            alloc::format!(" -> {}", ret)
        };
        lines.push(alloc::format!("pub unsafe fn {}({}){} {{", fn_.name, args, sig_ret));
        lines.push(alloc::format!(
            "    let __p = pymergetic_metal_reg::acquire(&{});",
            row
        ));
        lines.push(alloc::format!(
            "    assert!(!__p.is_null(), \"{}: provider {} not connected\");",
            fn_.name, module_name
        ));
        let fn_ty_ret = if ret == "()" {
            String::new()
        } else {
            alloc::format!(" -> {}", ret)
        };
        lines.push(alloc::format!(
            "    let __f: unsafe extern \"C\" fn({}){} = core::mem::transmute(__p);",
            arg_tys, fn_ty_ret
        ));
        lines.push(alloc::format!("    let __r = __f({});", call_args));
        lines.push(alloc::format!(
            "    pymergetic_metal_reg::release(&{});",
            row
        ));
        lines.push(String::from("    __r"));
        lines.push(String::from("}"));
        lines.push(String::new());
    }
    lines.join("\n")
}

