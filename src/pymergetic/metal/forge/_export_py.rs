//! Export: Catalog -> Python `.pyi` face.

use alloc::string::String;
use alloc::vec::Vec;

use crate::_banner::generated_banner;
use crate::_catalog::Catalog;
use crate::_template::{self, Value};

/// `static inline` border functions have no externally-linkable
/// definition, so there is nothing for Python's FFI/registry bind to
/// resolve -- stubbing one here would claim a call path that does not
/// exist. Only the module's real (non-inline) border functions are
/// reachable from Python.
const TEMPLATE: &str = include_str!("templates/export_py.tpl");

pub fn export(
    module_name: &str,
    base: &str,
    cat: &Catalog,
    human: &str,
    source_sha: &str,
) -> String {
    let border_fns: Vec<&crate::_catalog::Fn> = cat.fns.iter().filter(|f| !f.inline).collect();
    let fns = Value::list_map(&border_fns, |f| {
        let args = f
            .args
            .iter()
            .map(|a| a.name.as_str())
            .collect::<Vec<_>>()
            .join(", ");
        Value::map(alloc::vec![("name", Value::str(f.name.clone())), ("args", Value::str(args))])
    });
    let ctx = Value::map(alloc::vec![
        ("module_name", Value::str(module_name)),
        ("fns", fns),
    ]);

    let mut out = String::new();
    for line in generated_banner("py", human, &alloc::format!("{}.pyi", base), source_sha) {
        out.push_str(&line);
        out.push('\n');
    }
    out.push('\n');
    out.push_str(&_template::render_str(TEMPLATE, ctx).expect("py template"));
    if out.ends_with('\n') {
        out.pop();
    }
    out
}
