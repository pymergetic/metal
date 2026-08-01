//! Export: Catalog -> Python `.pyi` face.

use alloc::string::String;
use alloc::vec::Vec;

use crate::_banner::generated_banner;
use crate::_catalog::Catalog;

pub fn export(
    module_name: &str,
    base: &str,
    cat: &Catalog,
    human: &str,
    source_sha: &str,
) -> String {
    let mut lines =
        generated_banner("py", human, &alloc::format!("{}.pyi", base), source_sha);
    lines.push(String::new());
    lines.push(alloc::format!("\"\"\"Stubs for {}.\"\"\"", module_name));
    lines.push(String::new());
    // `static inline` border functions have no externally-linkable
    // definition, so there is nothing for Python's FFI/registry bind to
    // resolve -- stubbing one here would claim a call path that does not
    // exist. Only the module's real (non-inline) border functions are
    // reachable from Python.
    let border_fns: Vec<&crate::_catalog::Fn> = cat.fns.iter().filter(|f| !f.inline).collect();
    for fn_ in &border_fns {
        let args = fn_
            .args
            .iter()
            .map(|a| a.name.as_str())
            .collect::<Vec<_>>()
            .join(", ");
        lines.push(alloc::format!("def {}({}) -> int: ...", fn_.name, args));
    }
    if border_fns.is_empty() {
        lines.push(String::from("# package marker (no exported symbols)"));
    }
    lines.push(String::new());
    lines.join("\n")
}
