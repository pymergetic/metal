//! Import: Python / `.pyi` stub source -> Catalog (fn names for face check).

use alloc::string::String;
use alloc::vec::Vec;

use crate::_catalog::{Arg, Catalog, Fn};

fn is_ident_start(c: char) -> bool {
    c == '_' || c.is_ascii_alphabetic()
}

fn is_ident_cont(c: char) -> bool {
    c == '_' || c.is_ascii_alphanumeric()
}

/// Strip `# ...` comments and `"""..."""` / `'''...'''` string blocks.
fn strip_py_noise(text: &str) -> String {
    let b = text.as_bytes();
    let mut out = String::with_capacity(text.len());
    let mut i = 0;
    while i < b.len() {
        if b[i] == b'#' {
            while i < b.len() && b[i] != b'\n' {
                i += 1;
            }
            continue;
        }
        if i + 2 < b.len()
            && ((b[i] == b'"' && b[i + 1] == b'"' && b[i + 2] == b'"')
                || (b[i] == b'\'' && b[i + 1] == b'\'' && b[i + 2] == b'\''))
        {
            let q = b[i];
            i += 3;
            while i + 2 < b.len() && !(b[i] == q && b[i + 1] == q && b[i + 2] == q) {
                i += 1;
            }
            i = if i + 2 < b.len() { i + 3 } else { b.len() };
            continue;
        }
        out.push(b[i] as char);
        i += 1;
    }
    out
}

fn parse_def_args(arglist: &str) -> Vec<Arg> {
    let arglist = arglist.trim();
    if arglist.is_empty() {
        return Vec::new();
    }
    let mut args = Vec::new();
    let mut depth = 0i32;
    let mut cur = String::new();
    for ch in arglist.chars() {
        match ch {
            '(' | '[' | '{' => {
                depth += 1;
                cur.push(ch);
            }
            ')' | ']' | '}' => {
                depth -= 1;
                cur.push(ch);
            }
            ',' if depth == 0 => {
                if let Some(a) = parse_one_arg(cur.trim()) {
                    args.push(a);
                }
                cur.clear();
            }
            _ => cur.push(ch),
        }
    }
    if let Some(a) = parse_one_arg(cur.trim()) {
        args.push(a);
    }
    args
}

fn parse_one_arg(part: &str) -> Option<Arg> {
    let part = part.trim();
    if part.is_empty() || part == "*" || part == "/" {
        return None;
    }
    // name: Type = default  |  name = default  |  name
    let head = part.split('=').next().unwrap_or(part).trim();
    let name = head.split(':').next().unwrap_or(head).trim();
    if name.is_empty() || !name.chars().next().is_some_and(is_ident_start) {
        return None;
    }
    if !name.chars().all(|c| is_ident_cont(c)) {
        return None;
    }
    if name == "self" || name == "cls" {
        return None;
    }
    Some(Arg {
        name: String::from(name),
        ty: String::from("Any"),
    })
}

/// Parse `def name(...)->...:` / `async def` stub lines into a catalog.
pub fn import(text: &str) -> Result<Catalog, ()> {
    let cleaned = strip_py_noise(text);
    let mut cat = Catalog::default();
    let bytes = cleaned.as_bytes();
    let mut i = 0;
    while i < bytes.len() {
        // optional leading whitespace / "async "
        while i < bytes.len() && (bytes[i] as char).is_whitespace() {
            i += 1;
        }
        if cleaned[i..].starts_with("async") {
            let after = i + 5;
            if after < bytes.len() && (bytes[after] as char).is_whitespace() {
                i = after;
                while i < bytes.len() && (bytes[i] as char).is_whitespace() {
                    i += 1;
                }
            }
        }
        if !cleaned[i..].starts_with("def") {
            // advance to next line
            while i < bytes.len() && bytes[i] != b'\n' {
                i += 1;
            }
            if i < bytes.len() {
                i += 1;
            }
            continue;
        }
        let after_def = i + 3;
        if after_def >= bytes.len() || !(bytes[after_def] as char).is_whitespace() {
            i += 1;
            continue;
        }
        i = after_def;
        while i < bytes.len() && (bytes[i] as char).is_whitespace() {
            i += 1;
        }
        let name_start = i;
        if i >= bytes.len() || !is_ident_start(bytes[i] as char) {
            while i < bytes.len() && bytes[i] != b'\n' {
                i += 1;
            }
            if i < bytes.len() {
                i += 1;
            }
            continue;
        }
        i += 1;
        while i < bytes.len() && is_ident_cont(bytes[i] as char) {
            i += 1;
        }
        let name = &cleaned[name_start..i];
        while i < bytes.len() && (bytes[i] as char).is_whitespace() {
            i += 1;
        }
        if i >= bytes.len() || bytes[i] != b'(' {
            while i < bytes.len() && bytes[i] != b'\n' {
                i += 1;
            }
            if i < bytes.len() {
                i += 1;
            }
            continue;
        }
        i += 1;
        let args_start = i;
        let mut depth = 1i32;
        while i < bytes.len() && depth > 0 {
            match bytes[i] {
                b'(' => depth += 1,
                b')' => depth -= 1,
                _ => {}
            }
            if depth > 0 {
                i += 1;
            }
        }
        if depth != 0 {
            break;
        }
        let arglist = &cleaned[args_start..i];
        i += 1; // ')'
        // skip rest of signature until ':' or end of line
        while i < bytes.len() && bytes[i] != b':' && bytes[i] != b'\n' {
            i += 1;
        }
        if i < bytes.len() && bytes[i] == b':' {
            i += 1;
        }
        cat.fns.push(Fn {
            name: String::from(name),
            ret: String::from("int"),
            args: parse_def_args(arglist),
            inline: false,
        });
        while i < bytes.len() && bytes[i] != b'\n' {
            i += 1;
        }
        if i < bytes.len() {
            i += 1;
        }
    }
    Ok(cat)
}
