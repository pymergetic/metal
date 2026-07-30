//! Import: `{base}.toml` catalog dump -> Catalog.
//!
//! Minimal parser for the dialect emitted by `_export_toml` / Python
//! `emit_catalog_toml` — not a general TOML library.

use alloc::string::String;
use alloc::vec::Vec;

use crate::_catalog::{Arg, Catalog, EnumDef, EnumVariant, Field, Fn, Struct, Typedef};

fn skip_ws(s: &str, mut i: usize) -> usize {
    let b = s.as_bytes();
    while i < b.len() && b[i].is_ascii_whitespace() {
        i += 1;
    }
    i
}

fn parse_string(s: &str, mut i: usize) -> Option<(String, usize)> {
    let b = s.as_bytes();
    i = skip_ws(s, i);
    if i >= b.len() || b[i] != b'"' {
        return None;
    }
    i += 1;
    let mut out = String::new();
    while i < b.len() {
        match b[i] {
            b'"' => return Some((out, i + 1)),
            b'\\' => {
                i += 1;
                if i >= b.len() {
                    return None;
                }
                out.push(b[i] as char);
                i += 1;
            }
            c => {
                out.push(c as char);
                i += 1;
            }
        }
    }
    None
}

fn parse_i64(s: &str, mut i: usize) -> Option<(i64, usize)> {
    let b = s.as_bytes();
    i = skip_ws(s, i);
    let start = i;
    if i < b.len() && (b[i] == b'-' || b[i] == b'+') {
        i += 1;
    }
    let digits = i;
    while i < b.len() && b[i].is_ascii_digit() {
        i += 1;
    }
    if i == digits {
        return None;
    }
    let n: i64 = s[start..i].parse().ok()?;
    Some((n, i))
}

fn parse_bool(s: &str, mut i: usize) -> Option<(bool, usize)> {
    i = skip_ws(s, i);
    if s[i..].starts_with("true") {
        Some((true, i + 4))
    } else if s[i..].starts_with("false") {
        Some((false, i + 5))
    } else {
        None
    }
}

fn parse_key(s: &str, mut i: usize) -> Option<(String, usize)> {
    let b = s.as_bytes();
    i = skip_ws(s, i);
    let start = i;
    while i < b.len() && (b[i].is_ascii_alphanumeric() || b[i] == b'_' || b[i] == b'-') {
        i += 1;
    }
    if i == start {
        return None;
    }
    Some((String::from(&s[start..i]), i))
}

/// Parse `{ name = "...", ty = "..." }` or `{ name = "...", value = N }`.
fn parse_inline_table(s: &str, mut i: usize) -> Option<(Vec<(String, Val)>, usize)> {
    let b = s.as_bytes();
    i = skip_ws(s, i);
    if i >= b.len() || b[i] != b'{' {
        return None;
    }
    i += 1;
    let mut pairs = Vec::new();
    loop {
        i = skip_ws(s, i);
        if i < b.len() && b[i] == b'}' {
            return Some((pairs, i + 1));
        }
        let (key, j) = parse_key(s, i)?;
        i = skip_ws(s, j);
        if i >= b.len() || b[i] != b'=' {
            return None;
        }
        i += 1;
        i = skip_ws(s, i);
        let (val, j) = if i < b.len() && b[i] == b'"' {
            let (v, j) = parse_string(s, i)?;
            (Val::Str(v), j)
        } else if let Some((n, j)) = parse_i64(s, i) {
            (Val::Int(n), j)
        } else if let Some((v, j)) = parse_bool(s, i) {
            (Val::Bool(v), j)
        } else {
            return None;
        };
        pairs.push((key, val));
        i = skip_ws(s, j);
        if i < b.len() && b[i] == b',' {
            i += 1;
            continue;
        }
        if i < b.len() && b[i] == b'}' {
            return Some((pairs, i + 1));
        }
        return None;
    }
}

enum Val {
    Str(String),
    Int(i64),
    Bool(bool),
}

fn table_get_str(pairs: &[(String, Val)], key: &str) -> Option<String> {
    for (k, v) in pairs {
        if k == key {
            if let Val::Str(s) = v {
                return Some(s.clone());
            }
        }
    }
    None
}

fn table_get_i64(pairs: &[(String, Val)], key: &str) -> Option<i64> {
    for (k, v) in pairs {
        if k == key {
            if let Val::Int(n) = v {
                return Some(*n);
            }
        }
    }
    None
}

/// Parse `fields = [ {…}, … ]` spanning the rest of `text` from `i`.
fn parse_inline_table_array(s: &str, mut i: usize) -> Option<(Vec<Vec<(String, Val)>>, usize)> {
    let b = s.as_bytes();
    i = skip_ws(s, i);
    if i >= b.len() || b[i] != b'[' {
        return None;
    }
    i += 1;
    let mut out = Vec::new();
    loop {
        i = skip_ws(s, i);
        // allow newlines inside array
        while i < b.len() && (b[i] == b'\n' || b[i] == b'\r') {
            i += 1;
            i = skip_ws(s, i);
        }
        if i < b.len() && b[i] == b']' {
            return Some((out, i + 1));
        }
        let (tbl, j) = parse_inline_table(s, i)?;
        out.push(tbl);
        i = skip_ws(s, j);
        if i < b.len() && b[i] == b',' {
            i += 1;
        }
    }
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum Section {
    Struct,
    Typedef,
    Enum,
    Fn,
}

/// Parse a TOML catalog dump into a Catalog.
pub fn import(text: &str) -> Result<Catalog, ()> {
    let mut cat = Catalog::default();
    let mut section: Option<Section> = None;
    let mut cur_struct: Option<Struct> = None;
    let mut cur_typedef: Option<Typedef> = None;
    let mut cur_enum: Option<EnumDef> = None;
    let mut cur_fn: Option<Fn> = None;

    let flush = |section: &mut Option<Section>,
                 cur_struct: &mut Option<Struct>,
                 cur_typedef: &mut Option<Typedef>,
                 cur_enum: &mut Option<EnumDef>,
                 cur_fn: &mut Option<Fn>,
                 cat: &mut Catalog| {
        match section.take() {
            Some(Section::Struct) => {
                if let Some(st) = cur_struct.take() {
                    if !st.name.is_empty() {
                        cat.structs.push(st);
                    }
                }
            }
            Some(Section::Typedef) => {
                if let Some(td) = cur_typedef.take() {
                    if !td.name.is_empty() {
                        cat.typedefs.push(td);
                    }
                }
            }
            Some(Section::Enum) => {
                if let Some(en) = cur_enum.take() {
                    if !en.name.is_empty() {
                        cat.enums.push(en);
                    }
                }
            }
            Some(Section::Fn) => {
                if let Some(fn_) = cur_fn.take() {
                    if !fn_.name.is_empty() {
                        cat.fns.push(fn_);
                    }
                }
            }
            None => {}
        }
    };

    let mut i = 0;
    let b = text.as_bytes();
    while i < b.len() {
        // skip blank / full-line comments
        i = skip_ws(text, i);
        if i >= b.len() {
            break;
        }
        if b[i] == b'#' {
            while i < b.len() && b[i] != b'\n' {
                i += 1;
            }
            continue;
        }

        // section header
        if text[i..].starts_with("[[struct]]") {
            flush(
                &mut section,
                &mut cur_struct,
                &mut cur_typedef,
                &mut cur_enum,
                &mut cur_fn,
                &mut cat,
            );
            section = Some(Section::Struct);
            cur_struct = Some(Struct {
                name: String::new(),
                fields: Vec::new(),
                is_union: false,
            });
            i += "[[struct]]".len();
            continue;
        }
        if text[i..].starts_with("[[typedef]]") {
            flush(
                &mut section,
                &mut cur_struct,
                &mut cur_typedef,
                &mut cur_enum,
                &mut cur_fn,
                &mut cat,
            );
            section = Some(Section::Typedef);
            cur_typedef = Some(Typedef {
                name: String::new(),
                ty: String::new(),
            });
            i += "[[typedef]]".len();
            continue;
        }
        if text[i..].starts_with("[[enum]]") {
            flush(
                &mut section,
                &mut cur_struct,
                &mut cur_typedef,
                &mut cur_enum,
                &mut cur_fn,
                &mut cat,
            );
            section = Some(Section::Enum);
            cur_enum = Some(EnumDef {
                name: String::new(),
                variants: Vec::new(),
            });
            i += "[[enum]]".len();
            continue;
        }
        if text[i..].starts_with("[[fn]]") {
            flush(
                &mut section,
                &mut cur_struct,
                &mut cur_typedef,
                &mut cur_enum,
                &mut cur_fn,
                &mut cat,
            );
            section = Some(Section::Fn);
            cur_fn = Some(Fn {
                name: String::new(),
                ret: String::from("void"),
                args: Vec::new(),
                inline: false,
            });
            i += "[[fn]]".len();
            continue;
        }

        let (key, j) = parse_key(text, i).ok_or(())?;
        i = skip_ws(text, j);
        if i >= b.len() || b[i] != b'=' {
            return Err(());
        }
        i += 1;
        i = skip_ws(text, i);

        match key.as_str() {
            "name" => {
                let (v, j) = parse_string(text, i).ok_or(())?;
                i = j;
                match section {
                    Some(Section::Struct) => {
                        if let Some(st) = cur_struct.as_mut() {
                            st.name = v;
                        }
                    }
                    Some(Section::Typedef) => {
                        if let Some(td) = cur_typedef.as_mut() {
                            td.name = v;
                        }
                    }
                    Some(Section::Enum) => {
                        if let Some(en) = cur_enum.as_mut() {
                            en.name = v;
                        }
                    }
                    Some(Section::Fn) => {
                        if let Some(fn_) = cur_fn.as_mut() {
                            fn_.name = v;
                        }
                    }
                    None => return Err(()),
                }
            }
            "ty" => {
                let (v, j) = parse_string(text, i).ok_or(())?;
                i = j;
                if let Some(td) = cur_typedef.as_mut() {
                    td.ty = v;
                } else {
                    return Err(());
                }
            }
            "ret" => {
                let (v, j) = parse_string(text, i).ok_or(())?;
                i = j;
                if let Some(fn_) = cur_fn.as_mut() {
                    fn_.ret = v;
                } else {
                    return Err(());
                }
            }
            "inline" => {
                let (v, j) = parse_bool(text, i).ok_or(())?;
                i = j;
                if let Some(fn_) = cur_fn.as_mut() {
                    fn_.inline = v;
                } else {
                    return Err(());
                }
            }
            "fields" => {
                let (rows, j) = parse_inline_table_array(text, i).ok_or(())?;
                i = j;
                if let Some(st) = cur_struct.as_mut() {
                    for row in rows {
                        let name = table_get_str(&row, "name").ok_or(())?;
                        let ty = table_get_str(&row, "ty").ok_or(())?;
                        st.fields.push(Field { name, ty });
                    }
                } else {
                    return Err(());
                }
            }
            "variants" => {
                let (rows, j) = parse_inline_table_array(text, i).ok_or(())?;
                i = j;
                if let Some(en) = cur_enum.as_mut() {
                    for row in rows {
                        let name = table_get_str(&row, "name").ok_or(())?;
                        let value = table_get_i64(&row, "value").ok_or(())?;
                        en.variants.push(EnumVariant { name, value });
                    }
                } else {
                    return Err(());
                }
            }
            "args" => {
                let (rows, j) = parse_inline_table_array(text, i).ok_or(())?;
                i = j;
                if let Some(fn_) = cur_fn.as_mut() {
                    for row in rows {
                        let name = table_get_str(&row, "name").ok_or(())?;
                        let ty = table_get_str(&row, "ty").ok_or(())?;
                        fn_.args.push(Arg { name, ty });
                    }
                } else {
                    return Err(());
                }
            }
            _ => return Err(()),
        }
    }

    flush(
        &mut section,
        &mut cur_struct,
        &mut cur_typedef,
        &mut cur_enum,
        &mut cur_fn,
        &mut cat,
    );
    Ok(cat)
}
