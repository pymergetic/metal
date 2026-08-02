//! Export: Catalog -> C `.h` face.

use alloc::string::String;
use alloc::vec::Vec;

use crate::_banner::generated_banner;
use crate::_catalog::{Catalog, Fn};
use crate::_template::{self, Value};

fn c_args(fn_: &Fn) -> String {
    if fn_.args.is_empty() {
        return String::from("void");
    }
    fn_.args
        .iter()
        .map(|a| alloc::format!("{} {}", a.ty, a.name))
        .collect::<Vec<_>>()
        .join(", ")
}

fn header_guard(module_name: &str) -> String {
    alloc::format!(
        "PM_METAL_{}_H_",
        module_name.to_uppercase().replace(['.', '-'], "_")
    )
}

/// Single face for a module's `.h`: types (struct forward decls, enums,
/// typedefs, foreign forward decls, struct bodies) are byte-identical
/// regardless of `guest_surface`; only the function-declaration shape
/// forks on it -- a same-package consumer gets a plain prototype either
/// way, a `guest_surface` provider's declaration additionally carries the
/// `__wasm__` import branch (see [`export`] vs [`export_guest_surface`]).
const TEMPLATE: &str = include_str!("templates/export_c.tpl");

fn known_type_name(cat: &Catalog, name: &str) -> bool {
    cat.structs.iter().any(|s| s.name == name)
        || cat.enums.iter().any(|e| e.name == name)
        || cat.typedefs.iter().any(|t| t.name == name)
}

fn foreign_type_names(cat: &Catalog) -> Vec<String> {
    let mut out = Vec::new();
    let mut push = |raw: &str| {
        for tok in raw.split(|c: char| {
            c.is_whitespace() || c == '*' || c == ',' || c == '(' || c == ')' || c == '[' || c == ']'
        }) {
            let t = tok.trim();
            if t.starts_with("pm_metal_") && t.ends_with("_t") && !known_type_name(cat, t) && !out.iter().any(|x| x == t) {
                out.push(String::from(t));
            }
        }
    };
    for fn_ in &cat.fns {
        push(&fn_.ret);
        for a in &fn_.args {
            push(&a.ty);
        }
    }
    for st in &cat.structs {
        for f in &st.fields {
            push(&f.ty);
        }
    }
    for td in &cat.typedefs {
        push(&td.ty);
    }
    out
}

/// `struct`/union field declaration RHS (no leading indent, no trailing
/// `;`) -- array fields (`char name[N]`) split the array suffix off the
/// element type; everything else is `{ty} {name}`.
fn field_line(ty: &str, name: &str) -> String {
    if ty.contains('[') && ty.ends_with(']') {
        if let Some((base, n)) = ty.rsplit_once('[') {
            return alloc::format!("{} {}[{}]", base.trim(), name, n.trim_end_matches(']'));
        }
    }
    alloc::format!("{} {}", ty, name)
}

/// `typedef` RHS (no leading `typedef `, no trailing `;`) -- function
/// pointers splice the name into the `(*)` slot; everything else is
/// `{ty} {name}`.
fn typedef_line(ty: &str, name: &str) -> String {
    if ty.contains("(*)") {
        return ty.replacen("(*)", &alloc::format!("(*{})", name), 1);
    }
    alloc::format!("{} {}", ty, name)
}

/// Build the `structs` / `enums` / `typedefs` / `foreign_types` context
/// entries shared by [`export`] and [`export_guest_surface`].
fn types_ctx(cat: &Catalog) -> Vec<(&'static str, Value)> {
    let structs = Value::list_map(&cat.structs, |st| {
        let fields = Value::list_map(&st.fields, |f| {
            Value::map(alloc::vec![("line", Value::str(field_line(&f.ty, &f.name)))])
        });
        Value::map(alloc::vec![("name", Value::str(st.name.clone())), ("fields", fields)])
    });
    let enums = Value::list_map(&cat.enums, |en| {
        let n = en.variants.len();
        let variants = Value::list(
            en.variants
                .iter()
                .enumerate()
                .map(|(i, v)| {
                    let comma = if i + 1 < n { "," } else { "" };
                    Value::map(alloc::vec![
                        ("name", Value::str(v.name.clone())),
                        ("value", Value::str(alloc::format!("{}", v.value))),
                        ("comma", Value::str(comma)),
                    ])
                })
                .collect(),
        );
        Value::map(alloc::vec![("name", Value::str(en.name.clone())), ("variants", variants)])
    });
    let typedefs = Value::list_map(&cat.typedefs, |td| {
        Value::map(alloc::vec![("line", Value::str(typedef_line(&td.ty, &td.name)))])
    });
    let foreign_types = Value::list_map(&foreign_type_names(cat), |name| Value::str(name.clone()));
    alloc::vec![
        ("structs", structs),
        ("enums", enums),
        ("typedefs", typedefs),
        ("foreign_types", foreign_types),
    ]
}

fn fns_ctx(cat: &Catalog) -> Value {
    let extern_fns: Vec<&Fn> = cat.fns.iter().filter(|f| !f.inline).collect();
    Value::list_map(&extern_fns, |f| {
        Value::map(alloc::vec![
            ("ret", Value::str(f.ret.clone())),
            ("name", Value::str(f.name.clone())),
            ("args", Value::str(c_args(f))),
        ])
    })
}

fn render(
    module_name: &str,
    base: &str,
    cat: &Catalog,
    human: &str,
    source_sha: &str,
    guest_surface: bool,
) -> String {
    let guard = header_guard(module_name);
    let mut ctx_fields = types_ctx(cat);
    ctx_fields.push(("fns", fns_ctx(cat)));
    ctx_fields.push(("empty", Value::Bool(cat.is_empty())));
    ctx_fields.push(("module_name", Value::str(module_name)));
    ctx_fields.push(("guard", Value::str(guard)));
    ctx_fields.push(("guest_surface", Value::Bool(guest_surface)));
    let ctx = Value::map(ctx_fields);

    let mut out = String::new();
    for line in generated_banner("c", human, &alloc::format!("{}.h", base), source_sha) {
        out.push_str(&line);
        out.push('\n');
    }
    out.push('\n');
    out.push_str(&_template::render_str(TEMPLATE, ctx).expect("c template"));
    if out.ends_with('\n') {
        out.pop();
    }
    out
}

pub fn export(module_name: &str, base: &str, cat: &Catalog, human: &str, source_sha: &str) -> String {
    render(module_name, base, cat, human, source_sha, false)
}

/// Dual-branch face for a module marked `"guest_surface": true` in
/// `.pm/module`: every non-inline export gets a real wasm import
/// declaration on `__wasm__` (`PM_METAL_PKG_IMPORT`, see
/// `include/pymergetic/metal/pkg_import.h`) and an ordinary prototype
/// on the native side -- same shape a same-package consumer's plain
/// [`export`] face already gives it (resolved once by `connect_symbols`
/// through the registry, not a link-time symbol), just declared under
/// `#else` instead of unconditionally. Only the function declarations
/// fork; types/structs/enums are identical on both sides of the
/// boundary (plain data has no ABI difference at a wasm import).
pub fn export_guest_surface(
    module_name: &str,
    base: &str,
    cat: &Catalog,
    human: &str,
    source_sha: &str,
) -> String {
    render(module_name, base, cat, human, source_sha, true)
}
