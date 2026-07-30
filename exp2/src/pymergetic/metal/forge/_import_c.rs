//! Import: C source/header -> Catalog (incl. static inline border).

use alloc::string::String;
use alloc::vec::Vec;

use crate::_catalog::{Arg, Catalog, Fn};

fn strip_c_noise(text: &str) -> String {
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
        if b[i] == b'#' && (i == 0 || b[i - 1] == b'\n') {
            while i < b.len() && b[i] != b'\n' {
                i += 1;
            }
            continue;
        }
        out.push(b[i] as char);
        i += 1;
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

fn split_c_args(arglist: &str) -> Vec<Arg> {
    let arglist = arglist.trim();
    if arglist.is_empty() || arglist == "void" {
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
                if let Some(a) = parse_c_arg(cur.trim(), args.len()) {
                    args.push(a);
                }
                cur.clear();
            }
            _ => cur.push(ch),
        }
    }
    if let Some(a) = parse_c_arg(cur.trim(), args.len()) {
        args.push(a);
    }
    args
}

fn parse_c_arg(part: &str, idx: usize) -> Option<Arg> {
    let part = collapse_ws(part);
    if part.is_empty() {
        return None;
    }
    // array: const uint8_t src[4]
    if let Some(br) = part.rfind('[') {
        if part.ends_with(']') {
            let head = part[..br].trim();
            if let Some(sp) = head.rfind(' ') {
                let ty = head[..sp].trim();
                let name = head[sp + 1..].trim();
                let n = &part[br + 1..part.len() - 1];
                return Some(Arg {
                    name: String::from(name),
                    ty: alloc::format!("{}[{}]", ty, n),
                });
            }
        }
    }
    let starred = part.replace('*', " * ");
    let tokens: Vec<&str> = starred.split_whitespace().collect();
    if tokens.is_empty() {
        return Some(Arg {
            name: alloc::format!("a{}", idx),
            ty: String::from("void"),
        });
    }
    let mut name = String::from(*tokens.last().unwrap());
    let mut stars = 0usize;
    while name.starts_with('*') {
        stars += 1;
        name = String::from(&name[1..]);
    }
    let mut ty = tokens[..tokens.len() - 1].join(" ");
    if stars > 0 {
        ty = alloc::format!("{} {}", ty, "*".repeat(stars));
    }
    if name.is_empty() || matches!(name.as_str(), "const" | "struct" | "volatile" | "enum") {
        return Some(Arg {
            name: alloc::format!("a{}", idx),
            ty: part,
        });
    }
    Some(Arg {
        name,
        ty: collapse_ws(&ty),
    })
}

/// Join newlines that sit inside `(...)` so multi-line prototypes parse as one decl.
fn flatten_paren_newlines(raw: &str) -> String {
    let mut out = String::with_capacity(raw.len());
    let mut depth = 0i32;
    for ch in raw.chars() {
        match ch {
            '(' => {
                depth += 1;
                out.push(ch);
            }
            ')' => {
                depth -= 1;
                out.push(ch);
            }
            '\n' | '\r' if depth > 0 => out.push(' '),
            _ => out.push(ch),
        }
    }
    out
}

fn is_call_site_ret(ret: &str) -> bool {
    matches!(
        ret,
        "return" | "if" | "for" | "while" | "switch" | "sizeof" | "case" | "else"
    )
}

fn import_typedefs(raw: &str, cat: &mut Catalog) {
    /* typedef uint8_t name_t[N]; */
    for line in raw.split('\n') {
        let line = line.trim();
        if !line.starts_with("typedef ") || !line.ends_with(';') {
            continue;
        }
        let body = line["typedef ".len()..line.len() - 1].trim();
        if body.starts_with("union") || body.starts_with("struct") || body.starts_with("enum") {
            continue;
        }
        if let Some(br) = body.rfind('[') {
            if body.ends_with(']') {
                let head = body[..br].trim();
                if let Some(sp) = head.rfind(' ') {
                    let ty = head[..sp].trim();
                    let name = head[sp + 1..].trim();
                    let n = &body[br + 1..body.len() - 1];
                    cat.typedefs.push(crate::_catalog::Typedef {
                        name: String::from(name),
                        ty: alloc::format!("{}[{}]", ty, n),
                    });
                }
            }
        }
    }
    /* typedef union tag { fields } name_t; */
    let mut search = 0;
    while let Some(rel) = raw[search..].find("typedef union ") {
        let at = search + rel;
        let after = &raw[at + "typedef union ".len()..];
        if let Some(brace) = after.find('{') {
            if let Some(close_rel) = after[brace..].find('}') {
                let end = brace + close_rel;
                let tail = after[brace + 1..end].trim();
                let after_brace = after[end + 1..].trim_start();
                let name = after_brace
                    .split(|c: char| c == ';' || c.is_whitespace())
                    .next()
                    .unwrap_or("")
                    .trim();
                if !name.is_empty() {
                    let mut fields = Vec::new();
                    for part in tail.split(';') {
                        let part = collapse_ws(part);
                        if part.is_empty() {
                            continue;
                        }
                        if let Some(a) = parse_c_arg(&part, fields.len()) {
                            fields.push(crate::_catalog::Field {
                                name: a.name,
                                ty: a.ty,
                            });
                        }
                    }
                    if !fields.is_empty() {
                        cat.structs.push(crate::_catalog::Struct {
                            name: String::from(name),
                            fields,
                            is_union: true,
                        });
                    }
                }
                search = at + end + 1;
                continue;
            }
        }
        search = at + 1;
    }
}

pub fn import(text: &str) -> Catalog {
    let raw = flatten_paren_newlines(&strip_c_noise(text));
    let mut cat = Catalog::default();
    let mut seen: Vec<String> = Vec::new();
    import_typedefs(&raw, &mut cat);

    // static inline RET NAME(ARGS) {
    let mut search = 0;
    while let Some(rel) = raw[search..].find("static inline ") {
        let at = search + rel;
        let after = &raw[at + "static inline ".len()..];
        if let Some(paren) = after.find('(') {
            let head = collapse_ws(&after[..paren]);
            let parts: Vec<&str> = head.split_whitespace().collect();
            if parts.len() >= 2 {
                let fname = String::from(*parts.last().unwrap());
                let ret = parts[..parts.len() - 1].join(" ");
                if let Some(close) = matching_paren(&after[paren..]) {
                    let args_s = &after[paren + 1..paren + close];
                    let after_args = after[paren + close + 1..].trim_start();
                    if after_args.starts_with('{') && !seen.iter().any(|s| s == &fname) {
                        seen.push(fname.clone());
                        cat.fns.push(Fn {
                            name: fname,
                            ret: collapse_ws(&ret),
                            args: split_c_args(args_s),
                            inline: true,
                        });
                    }
                }
            }
        }
        search = at + 1;
    }

    // Top-level decls ending in ; (headers + rare .c prototypes).
    for line in raw.split('\n') {
        let line = line.trim();
        if !line.ends_with(';') || line.contains("(*") || line.contains("->") {
            continue;
        }
        if line.starts_with("typedef") {
            continue;
        }
        let head = line.split('(').next().unwrap_or("");
        let head_starred = head.replace('*', " * ");
        let toks: Vec<&str> = head_starred.split_whitespace().collect();
        let is_inline = toks.contains(&"static") && toks.contains(&"inline");
        if !is_inline && (toks.contains(&"static") || toks.contains(&"inline")) {
            continue;
        }
        if let Some(paren) = line.find('(') {
            if !line[paren..].contains(')') {
                continue;
            }
            let close = match matching_paren(&line[paren..]) {
                Some(c) => c,
                None => continue,
            };
            let pre = collapse_ws(&line[..paren]);
            let mut parts: Vec<&str> = pre.split_whitespace().collect();
            parts.retain(|t| *t != "static" && *t != "inline");
            if parts.len() < 2 {
                continue;
            }
            let fname = String::from(*parts.last().unwrap());
            if matches!(
                fname.as_str(),
                "if" | "for" | "while" | "switch" | "return" | "sizeof"
            ) {
                continue;
            }
            if seen.iter().any(|s| s == &fname) {
                continue;
            }
            let ret = parts[..parts.len() - 1].join(" ");
            if ret.contains('(') || ret.contains(')') || is_call_site_ret(&ret) {
                continue;
            }
            /* Skip body call-sites / statements mistaken for decls. */
            if fname.starts_with('&') || fname.starts_with('*') {
                continue;
            }
            let args_s = &line[paren + 1..paren + close];
            seen.push(fname.clone());
            cat.fns.push(Fn {
                name: fname,
                ret: collapse_ws(&ret),
                args: split_c_args(args_s),
                inline: is_inline,
            });
        }
    }

    cat
}

fn matching_paren(s: &str) -> Option<usize> {
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
