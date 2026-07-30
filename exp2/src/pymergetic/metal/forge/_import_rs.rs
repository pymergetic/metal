//! Import: Rust source -> Catalog (#[no_mangle] extern "C", repr(C), repr(u32)).

use alloc::string::String;
use alloc::vec::Vec;

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
                if let Some(end) = after[brace..].find('}') {
                    let body = &after[brace + 1..brace + end];
                    let mut fields = Vec::new();
                    for line in body.split(',') {
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
    for at in find_all(&raw, "pub type ") {
        let after = &raw[at + "pub type ".len()..];
        let name_end = after.find('=').unwrap_or(0);
        if name_end == 0 {
            continue;
        }
        let tname = after[..name_end].trim();
        let rhs = after[name_end + 1..].trim();
        if !rhs.starts_with("Option<") {
            continue;
        }
        if let Some(fn_at) = rhs.find("extern \"C\" fn") {
            let sig = &rhs[fn_at + "extern \"C\" fn".len()..];
            if let Some(paren) = sig.find('(') {
                if let Some(close) = matching_paren(&sig[paren..]) {
                    let args_s = &sig[paren + 1..paren + close];
                    let after_args = sig[paren + close + 1..].trim();
                    let ret = if let Some(r) = after_args.strip_prefix("->") {
                        let r = r.trim();
                        let r = r.split('>').next().unwrap_or(r).trim();
                        rs_type_to_c(r.trim_end_matches(|c: char| c == ';' || c == ' '))
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

    cat
}

fn matching_paren(s: &str) -> Option<usize> {
    // s starts with '('
    let b = s.as_bytes();
    if b.first() != Some(&b'(') {
        return None;
    }
    let mut depth = 0i32;
    for (i, &c) in b.iter().enumerate() {
        match c {
            b'(' => depth += 1,
            b')' => {
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

fn parse_int(s: &str) -> Result<i64, ()> {
    let s = s.trim();
    if let Some(hex) = s.strip_prefix("0x").or_else(|| s.strip_prefix("0X")) {
        i64::from_str_radix(hex, 16).map_err(|_| ())
    } else {
        s.parse().map_err(|_| ())
    }
}
