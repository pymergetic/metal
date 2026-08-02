//! Export: Catalog -> `{base}.toml` face (same dialect as Python `emit_catalog_toml`).

use alloc::string::String;

use crate::_banner::generated_banner;
use crate::_catalog::Catalog;
use crate::_template::{self, Value};

fn toml_str(s: &str) -> String {
    let mut out = String::from("\"");
    for c in s.chars() {
        match c {
            '\\' => out.push_str("\\\\"),
            '"' => out.push_str("\\\""),
            _ => out.push(c),
        }
    }
    out.push('"');
    out
}

/// Structural shape mirrors [`crate::_export_c::export`]'s per-section
/// loops one-to-one (struct/typedef/enum/fn, in that order) -- see this
/// module's `TEMPLATE` constant. Scalar text (quoting/escaping a name or
/// type string) stays ordinary Rust (`toml_str`); only the "for each
/// item, emit this shape" assembly moves into the template.
const TEMPLATE: &str = include_str!("templates/export_toml.tpl");

fn name_ty_pair(name: &str, ty: &str) -> Value {
    Value::map(alloc::vec![("name", Value::str(toml_str(name))), ("ty", Value::str(toml_str(ty)))])
}

fn ctx(cat: &Catalog) -> Value {
    let structs = Value::list_map(&cat.structs, |st| {
        let fields = Value::list_map(&st.fields, |f| name_ty_pair(&f.name, &f.ty));
        Value::map(alloc::vec![("name", Value::str(toml_str(&st.name))), ("fields", fields)])
    });
    let typedefs = Value::list_map(&cat.typedefs, |td| name_ty_pair(&td.name, &td.ty));
    let enums = Value::list_map(&cat.enums, |en| {
        let variants = Value::list_map(&en.variants, |v| {
            Value::map(alloc::vec![
                ("name", Value::str(toml_str(&v.name))),
                ("value", Value::str(alloc::format!("{}", v.value))),
            ])
        });
        Value::map(alloc::vec![("name", Value::str(toml_str(&en.name))), ("variants", variants)])
    });
    let fns = Value::list_map(&cat.fns, |f| {
        let args = Value::list_map(&f.args, |a| name_ty_pair(&a.name, &a.ty));
        Value::map(alloc::vec![
            ("name", Value::str(toml_str(&f.name))),
            ("ret", Value::str(toml_str(&f.ret))),
            ("inline", Value::Bool(f.inline)),
            ("args", args),
        ])
    });
    Value::map(alloc::vec![
        ("structs", structs),
        ("typedefs", typedefs),
        ("enums", enums),
        ("fns", fns),
        ("empty", Value::Bool(cat.is_empty())),
    ])
}

/// Serialize catalog IR with GENERATED banner (`{base}.toml` face).
pub fn export(
    _module_name: &str,
    base: &str,
    cat: &Catalog,
    human: &str,
    source_sha: &str,
) -> String {
    let mut out = String::new();
    for line in generated_banner("toml", human, &alloc::format!("{}.toml", base), source_sha) {
        out.push_str(&line);
        out.push('\n');
    }
    out.push('\n');
    out.push_str(&_template::render_str(TEMPLATE, ctx(cat)).expect("toml template"));
    if out.ends_with('\n') {
        out.pop();
    }
    out
}
