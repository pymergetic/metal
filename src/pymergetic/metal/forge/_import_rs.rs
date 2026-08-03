//! Import: Rust source -> Catalog (#[no_mangle] extern "C", repr(C), repr(u32)).

use alloc::string::String;
use alloc::vec::Vec;

use crate::_banner::content_has_banner;
use crate::_catalog::{Arg, Catalog, EnumDef, EnumVariant, Field, Fn, Struct, Typedef};

fn strip_rs_noise(text: &str) -> String {
    let b = text.as_bytes();
    let mut out = String::with_capacity(text.len());
    let mut i = 0;
    while i < b.len() {
        if i + 1 < b.len() && b[i] == b'/' && b[i + 1] == b'/' {
            i += 2;
            while i < b.len() && b[i] != b'\n' {
                i += 1;
            }
            continue;
        }
        if i + 1 < b.len() && b[i] == b'/' && b[i + 1] == b'*' {
            i += 2;
            while i + 1 < b.len() && !(b[i] == b'*' && b[i + 1] == b'/') {
                i += 1;
            }
            i = if i + 1 < b.len() { i + 2 } else { b.len() };
            continue;
        }
        out.push(b[i] as char);
        i += 1;
    }
    out
}

fn rs_type_to_c(ty: &str) -> String {
    let t = collapse_ws(ty);
    // [T; N] -> CType[N] (export_c rewrites "T name[N]")
    if t.starts_with('[') && t.ends_with(']') {
        if let Some(inner) = t.strip_prefix('[').and_then(|s| s.strip_suffix(']')) {
            if let Some((elem, n)) = inner.rsplit_once(';') {
                return alloc::format!("{}[{}]", rs_type_to_c(elem.trim()), n.trim());
            }
        }
    }
    if let Some(rest) = t.strip_prefix("*const ") {
        return alloc::format!("const {} *", rs_type_to_c(rest));
    }
    if let Some(rest) = t.strip_prefix("*mut ") {
        return alloc::format!("{} *", rs_type_to_c(rest));
    }
    match t.as_str() {
        "u8" => String::from("uint8_t"),
        "i8" => String::from("int8_t"),
        "u16" => String::from("uint16_t"),
        "i16" => String::from("int16_t"),
        "u32" => String::from("uint32_t"),
        "i32" => String::from("int32_t"),
        "u64" => String::from("uint64_t"),
        "i64" => String::from("int64_t"),
        "usize" => String::from("size_t"),
        "isize" => String::from("intptr_t"),
        "bool" => String::from("bool"),
        "f32" => String::from("float"),
        "f64" => String::from("double"),
        "c_void" | "core::ffi::c_void" => String::from("void"),
        "c_char" | "core::ffi::c_char" => String::from("char"),
        "!" => String::from("_Noreturn void"),
        _ => {
            if let Some(tail) = t.rsplit("::").next() {
                String::from(tail)
            } else {
                t
            }
        }
    }
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

fn split_args(arglist: &str) -> Vec<Arg> {
    let arglist = arglist.trim();
    if arglist.is_empty() {
        return Vec::new();
    }
    let mut args = Vec::new();
    let mut depth = 0i32;
    let mut cur = String::new();
    for ch in arglist.chars() {
        match ch {
            '<' | '(' | '[' => {
                depth += 1;
                cur.push(ch);
            }
            '>' | ')' | ']' => {
                depth -= 1;
                cur.push(ch);
            }
            ',' if depth == 0 => {
                if let Some(a) = parse_arg(cur.trim()) {
                    args.push(a);
                }
                cur.clear();
            }
            _ => cur.push(ch),
        }
    }
    if let Some(a) = parse_arg(cur.trim()) {
        args.push(a);
    }
    args
}

fn parse_arg(part: &str) -> Option<Arg> {
    let part = part.trim();
    if part.is_empty() {
        return None;
    }
    let (name, ty) = match part.split_once(':') {
        Some((n, t)) => (n.trim().trim_start_matches('_'), t.trim()),
        None => ("a0", part),
    };
    let mut name = String::from(if name.is_empty() { "a0" } else { name });
    if matches!(name.as_str(), "class" | "new" | "template" | "delete") {
        name.push('_');
    }
    Some(Arg {
        name,
        ty: rs_type_to_c(ty),
    })
}

fn find_all<'a>(hay: &'a str, needle: &str) -> Vec<usize> {
    let mut out = Vec::new();
    let mut start = 0;
    while let Some(rel) = hay[start..].find(needle) {
        let at = start + rel;
        out.push(at);
        start = at + needle.len();
    }
    out
}

pub fn import(text: &str) -> Catalog {
    // Banner check on raw text: `//!` lines are stripped as comments below.
    let generated_face = content_has_banner(text);
    let raw = strip_rs_noise(text);
    let mut cat = Catalog::default();

    // #[repr(u32)] ... pub enum Name { VAR = 0, ... }
    for at in find_all(&raw, "#[repr(u32)]") {
        let rest = &raw[at..];
        if let Some(epos) = rest.find("pub enum ") {
            let after = &rest[epos + "pub enum ".len()..];
            let name_end = after
                .find(|c: char| c == ' ' || c == '{')
                .unwrap_or(after.len());
            let name = after[..name_end].trim();
            if let Some(brace) = after.find('{') {
                if let Some(end) = after[brace..].find('}') {
                    let body = &after[brace + 1..brace + end];
                    let mut variants = Vec::new();
                    for part in body.split(',') {
                        let part = part.trim();
                        if let Some((n, v)) = part.split_once('=') {
                            let n = n.trim();
                            let v = v.trim().trim_end_matches(',');
                            if let Ok(val) = parse_int(v) {
                                variants.push(EnumVariant {
                                    name: String::from(n),
                                    value: val,
                                });
                            }
                        }
                    }
                    if !variants.is_empty() {
                        cat.enums.push(EnumDef {
                            name: String::from(name),
                            variants,
                        });
                    }
                }
            }
        }
    }

    for at in find_all(&raw, "#[repr(C)]") {
        let rest = &raw[at..];
        if let Some(epos) = rest.find("pub struct ") {
            let after = &rest[epos + "pub struct ".len()..];
            let name_end = after
                .find(|c: char| c == ' ' || c == '{')
                .unwrap_or(after.len());
            let name = after[..name_end].trim();
            if let Some(brace) = after.find('{') {
                if let Some(end) = matching_brace(&after[brace..]) {
                    let body = &after[brace + 1..brace + end];
                    let mut fields = Vec::new();
                    for line in split_top_level(body, ',') {
                        let line = line.trim();
                        if let Some(rest) = line.strip_prefix("pub ") {
                            if let Some((n, t)) = rest.split_once(':') {
                                fields.push(Field {
                                    name: String::from(n.trim()),
                                    ty: rs_type_to_c(t.trim()),
                                });
                            }
                        }
                    }
                    if !fields.is_empty() {
                        cat.structs.push(Struct {
                            name: String::from(name),
                            fields,
                            is_union: false,
                        });
                    }
                }
            }
        }
    }

    // pub type Foo = Option<unsafe extern "C" fn(...) -> T>;
    // pub type Foo = u32;  (and other primitive / path aliases)
    for at in find_all(&raw, "pub type ") {
        let after = &raw[at + "pub type ".len()..];
        let name_end = after.find('=').unwrap_or(0);
        if name_end == 0 {
            continue;
        }
        let tname = after[..name_end].trim();
        if tname.contains('<') || tname.contains('(') {
            continue;
        }
        let rhs = after[name_end + 1..].trim();
        if rhs.starts_with("Option<") {
            if let Some(fn_at) = rhs.find("extern \"C\" fn") {
                let sig = &rhs[fn_at + "extern \"C\" fn".len()..];
                if let Some(paren) = sig.find('(') {
                    if let Some(close) = matching_paren(&sig[paren..]) {
                        let args_s = &sig[paren + 1..paren + close];
                        let after_args = sig[paren + close + 1..].trim();
                        let ret = if let Some(r) = after_args.strip_prefix("->") {
                            let r = r.trim();
                            /* Multiline Option aliases often end `-> u32,` before `>`. */
                            let r = r
                                .trim_end_matches(|c: char| {
                                    c == ';' || c == ',' || c == '>' || c.is_whitespace()
                                })
                                .split('>')
                                .next()
                                .unwrap_or(r)
                                .trim()
                                .trim_end_matches(|c: char| c == ',' || c.is_whitespace());
                            rs_type_to_c(r)
                        } else {
                            String::from("void")
                        };
                        let mut args_c = Vec::new();
                        for a in split_args(args_s) {
                            args_c.push(alloc::format!("{} {}", a.ty, a.name));
                        }
                        let arg_s = if args_c.is_empty() {
                            String::from("void")
                        } else {
                            args_c.join(", ")
                        };
                        cat.typedefs.push(Typedef {
                            name: String::from(tname),
                            ty: alloc::format!("{} (*)({})", ret, arg_s),
                        });
                    }
                }
            }
            continue;
        }
        /* Simple alias: pub type pm_metal_fs_h = u32; */
        let alias_end = rhs
            .find(|c: char| c == ';' || c == '\n' || c == '{')
            .unwrap_or(rhs.len());
        let alias = rhs[..alias_end].trim();
        if alias.is_empty() || alias.starts_with("fn ") || alias.contains('(') {
            continue;
        }
        let cty = rs_type_to_c(alias);
        if cty == alias && !alias.starts_with("pm_metal_") {
            /* Unknown non-primitive — skip rather than emit garbage. */
            continue;
        }
        cat.typedefs.push(Typedef {
            name: String::from(tname),
            ty: cty,
        });
    }

    for at in find_all(&raw, "#[no_mangle]") {
        let rest = &raw[at..];
        if let Some(fpos) = rest.find("extern \"C\" fn ") {
            let after = &rest[fpos + "extern \"C\" fn ".len()..];
            let name_end = after.find('(').unwrap_or(0);
            if name_end == 0 {
                continue;
            }
            let fname = after[..name_end].trim();
            if let Some(close) = matching_paren(&after[name_end..]) {
                let args_s = &after[name_end + 1..name_end + close];
                let after_args = after[name_end + close + 1..].trim();
                let ret = if let Some(r) = after_args.strip_prefix("->") {
                    let r = r.trim();
                    let end = r.find('{').unwrap_or(r.len());
                    rs_type_to_c(r[..end].trim())
                } else {
                    String::from("void")
                };
                cat.fns.push(Fn {
                    name: String::from(fname),
                    ret,
                    args: split_args(args_s),
                    inline: false,
                });
            }
        }
    }

    // Generated C->rs faces: `extern "C" { pub fn ... }` and `#[inline] pub
    // [unsafe] fn ...` twins. Human Rust uses `extern "C" { }` for *foreign*
    // imports — never treat those as this module's border.
    if !generated_face {
        return cat;
    }
    for at in find_all(&raw, "extern \"C\"") {
        let rest = &raw[at + "extern \"C\"".len()..];
        let rest = rest.trim_start();
        if rest.starts_with("fn ") {
            continue; // handled via #[no_mangle] path above
        }
        if !rest.starts_with('{') {
            continue;
        }
        let Some(close) = matching_brace(rest) else {
            continue;
        };
        let body = &rest[1..close];
        let mut bi = 0;
        let bb = body.as_bytes();
        while bi < bb.len() {
            while bi < bb.len() && (bb[bi] as char).is_whitespace() {
                bi += 1;
            }
            if body[bi..].starts_with("pub") {
                let after = bi + 3;
                if after < bb.len() && (bb[after] as char).is_whitespace() {
                    bi = after;
                    while bi < bb.len() && (bb[bi] as char).is_whitespace() {
                        bi += 1;
                    }
                }
            }
            if !body[bi..].starts_with("fn ") {
                while bi < bb.len() && bb[bi] != b'\n' && bb[bi] != b';' {
                    bi += 1;
                }
                if bi < bb.len() {
                    bi += 1;
                }
                continue;
            }
            bi += 3;
            while bi < bb.len() && (bb[bi] as char).is_whitespace() {
                bi += 1;
            }
            let name_start = bi;
            while bi < bb.len() {
                let c = bb[bi] as char;
                if c == '_' || c.is_ascii_alphanumeric() {
                    bi += 1;
                } else {
                    break;
                }
            }
            if bi == name_start {
                continue;
            }
            let fname = &body[name_start..bi];
            while bi < bb.len() && (bb[bi] as char).is_whitespace() {
                bi += 1;
            }
            if bi >= bb.len() || bb[bi] != b'(' {
                continue;
            }
            let Some(pclose) = matching_paren(&body[bi..]) else {
                break;
            };
            let args_s = &body[bi + 1..bi + pclose];
            bi += pclose + 1;
            let after_args = body[bi..].trim_start();
            let ret = if let Some(r) = after_args.strip_prefix("->") {
                let r = r.trim();
                let end = r
                    .find(|c: char| c == ';' || c == '{')
                    .unwrap_or(r.len());
                rs_type_to_c(r[..end].trim())
            } else {
                String::from("void")
            };
            cat.fns.push(Fn {
                name: String::from(fname),
                ret,
                args: split_args(args_s),
                inline: false,
            });
            while bi < bb.len() && bb[bi] != b';' && bb[bi] != b'\n' {
                bi += 1;
            }
            if bi < bb.len() {
                bi += 1;
            }
        }
    }

    // Face wrappers: `#[inline] pub [unsafe] fn ...` (C static-inline twins)
    // and plain `pub unsafe fn ...` (always-proxy faces). Only the former
    // is catalogued as `inline: true` (excluded from face-symmetry); proxy
    // wrappers must count as real border fns.
    let mut si = 0;
    while si < raw.len() {
        let rest = &raw[si..];
        let rel = match rest.find("pub ") {
            Some(r) => r,
            None => break,
        };
        let pub_at = si + rel;
        let before = raw[..pub_at].trim_end();
        let is_inline_attr = before.ends_with("#[inline]");
        si = pub_at + 4;
        let after_pub = raw[si..].trim_start();
        let after_pub = after_pub
            .strip_prefix("unsafe")
            .map(|s| s.trim_start())
            .unwrap_or(after_pub);
        if !after_pub.starts_with("fn ") {
            continue;
        }
        let fn_body = after_pub["fn ".len()..].trim_start();
        let name_end = fn_body
            .find(|c: char| !(c == '_' || c.is_ascii_alphanumeric()))
            .unwrap_or(0);
        if name_end == 0 {
            continue;
        }
        let fname = &fn_body[..name_end];
        if !is_border_fn_name_local(fname) {
            continue;
        }
        let after_name = fn_body[name_end..].trim_start();
        if !after_name.starts_with('(') {
            continue;
        }
        let Some(pclose) = matching_paren(after_name) else {
            continue;
        };
        let args_s = &after_name[1..pclose];
        let after_args = after_name[pclose + 1..].trim_start();
        let ret = if let Some(r) = after_args.strip_prefix("->") {
            let r = r.trim();
            let end = r.find('{').unwrap_or(r.len());
            rs_type_to_c(r[..end].trim())
        } else {
            String::from("void")
        };
        if cat.fns.iter().any(|f| f.name == fname) {
            continue;
        }
        cat.fns.push(Fn {
            name: String::from(fname),
            ret,
            args: split_args(args_s),
            inline: is_inline_attr,
        });
    }

    cat
}

fn is_border_fn_name_local(name: &str) -> bool {
    let mut chars = name.chars();
    match chars.next() {
        Some(c) if c == '_' || c.is_ascii_alphabetic() => {}
        _ => return false,
    }
    chars.all(|c| c == '_' || c.is_ascii_alphanumeric())
}

fn matching_paren(s: &str) -> Option<usize> {
    matching_delim(s, b'(', b')')
}

fn matching_brace(s: &str) -> Option<usize> {
    matching_delim(s, b'{', b'}')
}

fn matching_delim(s: &str, open: u8, close: u8) -> Option<usize> {
    let b = s.as_bytes();
    if b.first() != Some(&open) {
        return None;
    }
    let mut depth = 0i32;
    let mut angle = 0i32;
    for (i, &c) in b.iter().enumerate() {
        match c {
            b'<' => angle += 1,
            b'>' => {
                if angle > 0 {
                    angle -= 1;
                }
            }
            _ if c == open && angle == 0 => depth += 1,
            _ if c == close && angle == 0 => {
                depth -= 1;
                if depth == 0 {
                    return Some(i);
                }
            }
            _ => {}
        }
    }
    None
}

/// Split `s` on `sep` at top level (paren / angle / brace depth 0).
fn split_top_level(s: &str, sep: char) -> Vec<String> {
    let mut out = Vec::new();
    let mut cur = String::new();
    let mut paren = 0i32;
    let mut angle = 0i32;
    let mut brace = 0i32;
    for c in s.chars() {
        match c {
            '(' => paren += 1,
            ')' => paren -= 1,
            '<' => angle += 1,
            '>' => angle -= 1,
            '{' => brace += 1,
            '}' => brace -= 1,
            _ if c == sep && paren == 0 && angle == 0 && brace == 0 => {
                out.push(core::mem::take(&mut cur));
                continue;
            }
            _ => {}
        }
        cur.push(c);
    }
    if !cur.trim().is_empty() {
        out.push(cur);
    }
    out
}

fn parse_int(s: &str) -> Result<i64, ()> {
    let s = s.trim();
    if let Some(hex) = s.strip_prefix("0x").or_else(|| s.strip_prefix("0X")) {
        i64::from_str_radix(hex, 16).map_err(|_| ())
    } else {
        s.parse().map_err(|_| ())
    }
}
