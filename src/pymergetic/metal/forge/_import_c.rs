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

/// `ret_type *fn_name(...)` with no space before the name -- common C
/// style -- leaves the leading `*`(s) glued to the last whitespace-split
/// token. Move them back onto the return type so the catalog holds a
/// clean function name (a name still starting with `*` produced either a
/// corrupted export -- `pub unsafe fn *foo(...)` is invalid Rust -- or,
/// worse, got silently dropped by callers that skip anything star-led as
/// a call-site false match, which for a bare `.h` prototype it never is).
fn split_leading_stars(tok: &str) -> (usize, &str) {
    let mut n = 0usize;
    let mut s = tok;
    while let Some(rest) = s.strip_prefix('*') {
        n += 1;
        s = rest;
    }
    (n, s)
}

fn is_call_site_ret(ret: &str) -> bool {
    matches!(
        ret,
        "return" | "if" | "for" | "while" | "switch" | "sizeof" | "case" | "else"
    )
}

/// Object-like `#define NAME value` from the human header (before `#` strip).
/// Function-like macros (`#define FOO(x)`) are skipped.
fn import_defines(text: &str, cat: &mut Catalog) {
    for line in text.split('\n') {
        let line = line.trim();
        let rest = match line.strip_prefix("#define ") {
            Some(r) => r.trim_start(),
            None => continue,
        };
        let mut name = String::new();
        let mut chars = rest.chars().peekable();
        while let Some(&c) = chars.peek() {
            if c == '_' || c.is_ascii_alphanumeric() {
                name.push(c);
                chars.next();
            } else {
                break;
            }
        }
        if name.is_empty() {
            continue;
        }
        /* Function-like: NAME( */
        if matches!(chars.peek(), Some('(')) {
            continue;
        }
        let value_raw = chars.collect::<String>();
        let value = String::from(value_raw.trim());
        if value.is_empty() {
            continue;
        }
        if cat.defines.iter().any(|d| d.name == name) {
            continue;
        }
        cat.defines.push(crate::_catalog::Define { name, value });
    }
}

/// Replace `[MACRO]` array sizes with the define's integer RHS when known
/// (`uint8_t[PM_METAL_STREAM_NCCS]` -> `uint8_t[32]`). Keeps C call-site
/// macros available via emitted `#define`s; makes Rust faces sized arrays.
fn expand_define_array_sizes(cat: &mut Catalog) {
    let rewrite = |ty: &str, defs: &[crate::_catalog::Define]| -> String {
        let Some(br) = ty.rfind('[') else {
            return String::from(ty);
        };
        if !ty.ends_with(']') {
            return String::from(ty);
        }
        let inner = ty[br + 1..ty.len() - 1].trim();
        if inner.is_empty()
            || inner.chars().all(|c| c.is_ascii_digit())
            || !inner.chars().all(|c| c == '_' || c.is_ascii_alphanumeric())
        {
            return String::from(ty);
        }
        let Some(d) = defs.iter().find(|d| d.name == inner) else {
            return String::from(ty);
        };
        /* Strip C integer suffixes (u/ul/ull/U...). */
        let mut v = d.value.as_str();
        while let Some(c) = v.chars().last() {
            if matches!(c, 'u' | 'U' | 'l' | 'L') {
                v = &v[..v.len() - c.len_utf8()];
            } else {
                break;
            }
        }
        let v = v.trim();
        if v.is_empty() || !v.chars().all(|c| c.is_ascii_digit()) {
            return String::from(ty);
        }
        alloc::format!("{}[{}]", &ty[..br], v)
    };
    let defs = cat.defines.clone();
    for st in &mut cat.structs {
        for f in &mut st.fields {
            f.ty = rewrite(&f.ty, &defs);
        }
    }
    for td in &mut cat.typedefs {
        td.ty = rewrite(&td.ty, &defs);
    }
    for fn_ in &mut cat.fns {
        fn_.ret = rewrite(&fn_.ret, &defs);
        for a in &mut fn_.args {
            a.ty = rewrite(&a.ty, &defs);
        }
    }
}

fn import_typedefs(raw: &str, cat: &mut Catalog) {
    /* typedef uint8_t name_t[N];  /  typedef uint32_t name_t; */
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
                continue;
            }
        }
        /* Plain `typedef <type...> <name>;` (e.g. `typedef uint32_t pm_metal_stream_h`). */
        if let Some(sp) = body.rfind(' ') {
            let ty = body[..sp].trim();
            let name = body[sp + 1..].trim();
            if !ty.is_empty()
                && !name.is_empty()
                && name.chars().all(|c| c == '_' || c.is_ascii_alphanumeric())
            {
                cat.typedefs.push(crate::_catalog::Typedef {
                    name: String::from(name),
                    ty: String::from(ty),
                });
            }
        }
    }
    /* typedef {struct|union} [tag] { fields } name_t; */
    for (kw, is_union) in [("typedef struct ", false), ("typedef union ", true)] {
        let mut search = 0;
        while let Some(rel) = raw[search..].find(kw) {
            let at = search + rel;
            let after = &raw[at + kw.len()..];
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
                                is_union,
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
}

pub fn import(text: &str) -> Catalog {
    let mut cat = Catalog::default();
    import_defines(text, &mut cat);
    let raw = flatten_paren_newlines(&strip_c_noise(text));
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
                let (stars, bare) = split_leading_stars(parts.last().unwrap());
                let fname = String::from(bare);
                let mut ret = parts[..parts.len() - 1].join(" ");
                if stars > 0 {
                    ret = alloc::format!("{} {}", ret, "*".repeat(stars));
                }
                if let Some(close) = matching_paren(&after[paren..]) {
                    let args_s = &after[paren + 1..paren + close];
                    let after_args = after[paren + close + 1..].trim_start();
                    if !fname.is_empty()
                        && after_args.starts_with('{')
                        && !seen.iter().any(|s| s == &fname)
                    {
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
    //
    // Brace-depth tracked across lines: a `static inline` (or struct/union)
    // body can contain its own semicolon-terminated statements (e.g.
    // `dst[i] = (v >> (8 * i)) & 0xff;`) that are not decls at all -- only
    // a line seen while depth == 0 (outside any real `{ }` body) is a real
    // candidate. Without this, an assignment/compound-expression statement
    // gets misread as a prototype named after its own operator token
    // (`= ` / `|= `), producing an invalid extern decl in every generated
    // face (`pub fn =(...)`) instead of being silently and correctly
    // ignored as inline-body-only code.
    //
    // `extern "C" { ... }` linkage-spec wrappers are *transparent* scope
    // (every real C header wraps its whole border in one): a `{` whose
    // preceding text on the line is exactly `extern "C"` pushes a
    // transparent marker instead of a real one, so declarations inside
    // still count as top-level.
    let mut scope_stack: Vec<bool> = Vec::new();
    for line in raw.split('\n') {
        let line = line.trim();
        let at_top = scope_stack.iter().all(|transparent| *transparent);
        for (i, ch) in line.char_indices() {
            match ch {
                '{' => {
                    let transparent = line[..i].trim_end().ends_with("extern \"C\"");
                    scope_stack.push(transparent);
                }
                '}' => {
                    scope_stack.pop();
                }
                _ => {}
            }
        }
        if !at_top {
            continue;
        }
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
            /* Skip body call-sites / statements mistaken for decls. */
            if parts.last().unwrap().starts_with('&') {
                continue;
            }
            let (stars, bare) = split_leading_stars(parts.last().unwrap());
            let fname = String::from(bare);
            if fname.is_empty()
                || matches!(
                    fname.as_str(),
                    "if" | "for" | "while" | "switch" | "return" | "sizeof"
                )
            {
                continue;
            }
            if seen.iter().any(|s| s == &fname) {
                continue;
            }
            let mut ret = parts[..parts.len() - 1].join(" ");
            if ret.contains('(') || ret.contains(')') || is_call_site_ret(&ret) {
                continue;
            }
            if stars > 0 {
                ret = alloc::format!("{} {}", ret, "*".repeat(stars));
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

    expand_define_array_sizes(&mut cat);
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
