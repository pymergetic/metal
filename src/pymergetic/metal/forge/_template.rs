//! forge's own tiny template engine for codegen: `{{ path.to.field }}`
//! substitution, `{% if [not] path %}` / `{% else %}` / `{% endif %}`, and
//! `{% for x in path %}` / `{% endfor %}` over a small [`Value`] tree.
//! Nothing else -- no filters, no expressions, no includes.
//!
//! Why hand-rolled instead of a vendored library: forge/.pm/Cargo.toml is
//! deliberately "zero crates.io deps", and every Apache-2.0/MIT
//! candidate surveyed either drags in `serde`+`serde_json`
//! unconditionally (TinyTemplate) or is a young, small-userbase crate
//! not worth depending on for a build-time tool (Minilate) -- see
//! docs/definitions/module.md "Codegen templates". This supports
//! exactly what `_export_c.rs` / `_export_rs.rs` / `_export_py.rs` /
//! `_export_toml.rs` actually need (line/block emission driven by a
//! catalog entry's fields and its list of exports/imports) and nothing
//! speculative on top.
//!
//! `Value` scopes are owned, not borrowed, when rendering a `{% for %}`
//! body: pushing a fresh per-iteration scope onto a `Vec<&Value>` would
//! need that scope to outlive the loop, which a stack-local iteration
//! variable cannot do. Cloning small metadata `Value`s per loop
//! iteration is a non-issue for a code generator (a handful of fields,
//! not a hot path).

use alloc::format;
use alloc::string::String;
use alloc::string::ToString;
use alloc::vec::Vec;

/// A template value: a scalar, or a nested list/map for `{% for %}` and
/// dotted-path lookups (`{{ entry.name }}`).
#[derive(Clone, Debug)]
pub enum Value {
    Str(String),
    Bool(bool),
    List(Vec<Value>),
    Map(Vec<(String, Value)>),
}

impl Value {
    pub fn str(s: impl Into<String>) -> Self {
        Value::Str(s.into())
    }

    /// Build a `Map` from `(key, value)` pairs, in the given order
    /// (lookup is a short linear scan -- these maps hold a handful of
    /// fields, never enough for a hash table to pay for itself).
    pub fn map(pairs: Vec<(&str, Value)>) -> Self {
        Value::Map(pairs.into_iter().map(|(k, v)| (k.to_string(), v)).collect())
    }

    pub fn list(items: Vec<Value>) -> Self {
        Value::List(items)
    }

    /// `Value::list(items.iter().map(f).collect())`, spelled once. Every
    /// exporter's context builder needs this exact shape at every catalog
    /// level (structs, fields, enums, variants, typedefs, fns, args) --
    /// this was the one piece of genuinely repeated code across
    /// `_export_c.rs` / `_export_rs.rs` / `_export_toml.rs` / `_export_py.rs`
    /// (the *values* each closure computes differ per language -- a C
    /// declaration string vs. a Rust type vs. a quoted TOML literal -- but
    /// the "walk a slice into a `Value::List`" wrapper around them doesn't).
    /// Only for plain `.iter().map(f)`; an index-needing walk (C's enum
    /// variant trailing-comma calc) still spells out
    /// `Value::list(items.iter().enumerate()...)` directly -- not common
    /// enough here to earn a second, indexed sibling.
    pub fn list_map<T>(items: &[T], f: impl FnMut(&T) -> Value) -> Self {
        Value::List(items.iter().map(f).collect())
    }

    fn get(&self, key: &str) -> Option<&Value> {
        match self {
            Value::Map(pairs) => pairs.iter().find(|(k, _)| k == key).map(|(_, v)| v),
            _ => None,
        }
    }

    fn truthy(&self) -> bool {
        match self {
            Value::Bool(b) => *b,
            Value::Str(s) => !s.is_empty(),
            Value::List(l) => !l.is_empty(),
            Value::Map(m) => !m.is_empty(),
        }
    }
}

#[derive(Debug)]
enum Node {
    Text(String),
    Var(Vec<String>),
    If {
        negate: bool,
        cond: Vec<String>,
        then_body: Vec<Node>,
        else_body: Vec<Node>,
    },
    For {
        var: String,
        list: Vec<String>,
        body: Vec<Node>,
    },
}

#[derive(Debug)]
enum Tok {
    Text(String),
    Var(Vec<String>),
    If(bool, Vec<String>),
    Else,
    EndIf,
    For(String, Vec<String>),
    EndFor,
}

/// A parsed template, ready to render against any [`Value`] context --
/// parse once, render many times (e.g. once per exported function).
#[derive(Debug)]
pub struct Template {
    nodes: Vec<Node>,
}

impl Template {
    pub fn parse(src: &str) -> Result<Self, String> {
        let toks = tokenize(src)?;
        let mut pos = 0usize;
        let nodes = parse_nodes(&toks, &mut pos)?;
        if pos != toks.len() {
            return Err(format!("{:?} has no matching {{% if %}}/{{% for %}}", toks[pos]));
        }
        Ok(Self { nodes })
    }

    pub fn render(&self, ctx: Value) -> Result<String, String> {
        let mut out = String::new();
        let mut scopes: Vec<Value> = alloc::vec![ctx];
        render_nodes(&self.nodes, &mut scopes, &mut out)?;
        Ok(out)
    }
}

/// Parse + render in one shot, for a template only used once.
pub fn render_str(src: &str, ctx: Value) -> Result<String, String> {
    Template::parse(src)?.render(ctx)
}

fn split_path(inner: &str) -> Result<Vec<String>, String> {
    if inner.is_empty() {
        return Err(String::from("empty {{ }} / condition path"));
    }
    let path: Vec<String> = inner.split('.').map(String::from).collect();
    for seg in &path {
        if seg.is_empty() || !seg.chars().all(|c| c.is_ascii_alphanumeric() || c == '_') {
            return Err(format!("invalid path segment {seg:?} in \"{inner}\""));
        }
    }
    Ok(path)
}

fn parse_tag(inner: &str) -> Result<Tok, String> {
    if let Some(rest) = inner.strip_prefix("if ") {
        let rest = rest.trim();
        if let Some(cond) = rest.strip_prefix("not ") {
            return Ok(Tok::If(true, split_path(cond.trim())?));
        }
        return Ok(Tok::If(false, split_path(rest)?));
    }
    if inner == "else" {
        return Ok(Tok::Else);
    }
    if inner == "endif" {
        return Ok(Tok::EndIf);
    }
    if let Some(rest) = inner.strip_prefix("for ") {
        let rest = rest.trim();
        let mid = rest
            .find(" in ")
            .ok_or_else(|| format!("malformed {{% for %}}: \"{inner}\" (expected \"for X in Y\")"))?;
        let var = rest[..mid].trim().to_string();
        let list = split_path(rest[mid + 4..].trim())?;
        if var.is_empty() || !var.chars().all(|c| c.is_ascii_alphanumeric() || c == '_') {
            return Err(format!("invalid loop variable \"{var}\" in {{% {inner} %}}"));
        }
        return Ok(Tok::For(var, list));
    }
    if inner == "endfor" {
        return Ok(Tok::EndFor);
    }
    Err(format!("unknown tag {{% {inner} %}}"))
}

fn tokenize(src: &str) -> Result<Vec<Tok>, String> {
    let mut toks = Vec::new();
    let mut text = String::new();
    let mut chars = src.char_indices().peekable();

    while let Some((idx, ch)) = chars.next() {
        let rest = &src[idx..];
        if rest.starts_with("\\{{") || rest.starts_with("\\{%") || rest.starts_with("\\{#") {
            text.push('{');
            text.push(rest.as_bytes()[2] as char);
            chars.next();
            chars.next();
            continue;
        }
        if ch == '{' {
            let kind = chars.peek().map(|&(_, c)| c);
            if let Some(kind @ ('{' | '%' | '#')) = kind {
                if !text.is_empty() {
                    toks.push(Tok::Text(core::mem::take(&mut text)));
                }
                chars.next();
                let close = match kind {
                    '{' => "}}",
                    '%' => "%}",
                    _ => "#}",
                };
                let content_start = idx + 2;
                let after = &src[content_start..];
                let rel = after
                    .find(close)
                    .ok_or_else(|| format!("unterminated tag starting at byte {idx}"))?;
                let inner = after[..rel].trim();
                match kind {
                    '#' => {}
                    '{' => toks.push(Tok::Var(split_path(inner)?)),
                    _ => toks.push(parse_tag(inner)?),
                }
                let consumed_end = content_start + rel + close.len();
                while let Some(&(i2, _)) = chars.peek() {
                    if i2 < consumed_end {
                        chars.next();
                    } else {
                        break;
                    }
                }
                continue;
            }
        }
        text.push(ch);
    }
    if !text.is_empty() {
        toks.push(Tok::Text(text));
    }
    Ok(toks)
}

/// Consume `toks[*pos..]` into a node list, stopping (without consuming)
/// at the next `Else`/`EndIf`/`EndFor` so the caller (the enclosing
/// `if`/`for` handler, or `Template::parse` at the top level) decides
/// whether that token is expected here.
fn parse_nodes(toks: &[Tok], pos: &mut usize) -> Result<Vec<Node>, String> {
    let mut nodes = Vec::new();
    while *pos < toks.len() {
        match &toks[*pos] {
            Tok::Text(s) => {
                nodes.push(Node::Text(s.clone()));
                *pos += 1;
            }
            Tok::Var(path) => {
                nodes.push(Node::Var(path.clone()));
                *pos += 1;
            }
            Tok::If(negate, cond) => {
                let negate = *negate;
                let cond = cond.clone();
                *pos += 1;
                let then_body = parse_nodes(toks, pos)?;
                let else_body = if matches!(toks.get(*pos), Some(Tok::Else)) {
                    *pos += 1;
                    parse_nodes(toks, pos)?
                } else {
                    Vec::new()
                };
                if !matches!(toks.get(*pos), Some(Tok::EndIf)) {
                    return Err(String::from("missing {% endif %}"));
                }
                *pos += 1;
                nodes.push(Node::If { negate, cond, then_body, else_body });
            }
            Tok::For(var, list) => {
                let var = var.clone();
                let list = list.clone();
                *pos += 1;
                let body = parse_nodes(toks, pos)?;
                if !matches!(toks.get(*pos), Some(Tok::EndFor)) {
                    return Err(String::from("missing {% endfor %}"));
                }
                *pos += 1;
                nodes.push(Node::For { var, list, body });
            }
            Tok::Else | Tok::EndIf | Tok::EndFor => break,
        }
    }
    Ok(nodes)
}

fn lookup<'a>(scopes: &'a [Value], path: &[String]) -> Option<&'a Value> {
    let (head, tail) = path.split_first()?;
    for scope in scopes.iter().rev() {
        if let Some(mut cur) = scope.get(head) {
            for seg in tail {
                cur = cur.get(seg)?;
            }
            return Some(cur);
        }
    }
    None
}

fn render_nodes(nodes: &[Node], scopes: &mut Vec<Value>, out: &mut String) -> Result<(), String> {
    for node in nodes {
        match node {
            Node::Text(s) => out.push_str(s),
            Node::Var(path) => {
                let v = lookup(scopes, path)
                    .ok_or_else(|| format!("undefined variable {{{{ {} }}}}", path.join(".")))?;
                match v {
                    Value::Str(s) => out.push_str(s),
                    Value::Bool(b) => out.push_str(if *b { "true" } else { "false" }),
                    Value::List(_) | Value::Map(_) => {
                        return Err(format!("cannot print non-scalar {{{{ {} }}}}", path.join(".")));
                    }
                }
            }
            Node::If { negate, cond, then_body, else_body } => {
                // Unlike {{ }}, a missing condition path is falsy, not an
                // error -- "if this optional field is set" is the common
                // case a codegen template needs.
                let truthy = lookup(scopes, cond).map(Value::truthy).unwrap_or(false);
                if truthy != *negate {
                    render_nodes(then_body, scopes, out)?;
                } else {
                    render_nodes(else_body, scopes, out)?;
                }
            }
            Node::For { var, list, body } => {
                let items = match lookup(scopes, list) {
                    Some(Value::List(items)) => items.clone(),
                    Some(_) => {
                        return Err(format!("{{% for {var} in {} %}}: not a list", list.join(".")));
                    }
                    None => {
                        return Err(format!("{{% for {var} in {} %}}: undefined", list.join(".")));
                    }
                };
                for item in items {
                    scopes.push(Value::Map(alloc::vec![(var.clone(), item)]));
                    let r = render_nodes(body, scopes, out);
                    scopes.pop();
                    r?;
                }
            }
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn substitutes_flat_and_nested_paths() {
        let ctx = Value::map(alloc::vec![
            ("name", Value::str("greeter")),
            ("entry", Value::map(alloc::vec![("func", Value::str("hello"))])),
        ]);
        let out = render_str("mod {{ name }} :: {{ entry.func }}();", ctx).expect("render");
        assert_eq!(out, "mod greeter :: hello();");
    }

    #[test]
    fn if_else_and_not() {
        let tpl = Template::parse("{% if unloadable %}dyn{% else %}fixed{% endif %}").unwrap();
        let yes = Value::map(alloc::vec![("unloadable", Value::Bool(true))]);
        let no = Value::map(alloc::vec![("unloadable", Value::Bool(false))]);
        assert_eq!(tpl.render(yes).unwrap(), "dyn");
        assert_eq!(tpl.render(no).unwrap(), "fixed");

        let tpl2 = Template::parse("{% if not unloadable %}fixed{% endif %}").unwrap();
        let yes2 = Value::map(alloc::vec![("unloadable", Value::Bool(true))]);
        assert_eq!(tpl2.render(yes2).unwrap(), "");
    }

    #[test]
    fn missing_if_condition_is_falsy_not_an_error() {
        let tpl = Template::parse("{% if maybe %}x{% endif %}").unwrap();
        assert_eq!(tpl.render(Value::map(Vec::new())).unwrap(), "");
    }

    #[test]
    fn list_map_wraps_iter_map_collect() {
        let names = ["a".to_string(), "b".to_string()];
        let ctx = Value::map(alloc::vec![(
            "items",
            Value::list_map(&names, |n| Value::str(n.to_uppercase())),
        )]);
        let out = render_str("{% for x in items %}[{{ x }}]{% endfor %}", ctx).unwrap();
        assert_eq!(out, "[A][B]");
    }

    #[test]
    fn for_over_scalar_list() {
        let ctx = Value::map(alloc::vec![(
            "names",
            Value::list(alloc::vec![Value::str("a"), Value::str("b"), Value::str("c")]),
        )]);
        let out = render_str("{% for n in names %}[{{ n }}]{% endfor %}", ctx).unwrap();
        assert_eq!(out, "[a][b][c]");
    }

    #[test]
    fn for_over_maps_sees_outer_scope_too() {
        let ctx = Value::map(alloc::vec![
            ("mod_name", Value::str("greeter")),
            (
                "entries",
                Value::list(alloc::vec![
                    Value::map(alloc::vec![("func", Value::str("hello"))]),
                    Value::map(alloc::vec![("func", Value::str("bye"))]),
                ]),
            ),
        ]);
        let out = render_str(
            "{% for e in entries %}{{ mod_name }}.{{ e.func }};{% endfor %}",
            ctx,
        )
        .unwrap();
        assert_eq!(out, "greeter.hello;greeter.bye;");
    }

    #[test]
    fn escapes_literal_tag_open_marker() {
        let ctx = Value::map(alloc::vec![("x", Value::str("REAL"))]);
        let out = render_str("literal \\{{ not a tag }} then {{ x }}", ctx).unwrap();
        assert_eq!(out, "literal {{ not a tag }} then REAL");
    }

    #[test]
    fn undefined_var_in_output_errors() {
        let err = render_str("{{ missing }}", Value::map(Vec::new())).unwrap_err();
        assert!(err.contains("undefined variable"), "{err}");
    }

    #[test]
    fn unterminated_tag_errors() {
        let err = Template::parse("{{ oops").unwrap_err();
        assert!(err.contains("unterminated"), "{err}");
    }

    #[test]
    fn unbalanced_endif_errors() {
        let err = Template::parse("{% endif %}").unwrap_err();
        assert!(err.contains("no matching"), "{err}");
    }
}
