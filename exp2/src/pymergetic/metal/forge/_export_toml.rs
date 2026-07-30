//! Export: Catalog -> `{base}.toml` face (same dialect as Python `emit_catalog_toml`).

use alloc::string::String;

use crate::_banner::generated_banner;
use crate::_catalog::Catalog;

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

/// Serialize catalog IR with GENERATED banner (`{base}.toml` face).
pub fn export(
    _module_name: &str,
    base: &str,
    cat: &Catalog,
    human: &str,
    source_sha: &str,
) -> String {
    let mut lines = generated_banner("toml", human, &alloc::format!("{}.toml", base), source_sha);
    lines.push(String::new());

    for st in &cat.structs {
        lines.push(String::from("[[struct]]"));
        lines.push(alloc::format!("name = {}", toml_str(&st.name)));
        if !st.fields.is_empty() {
            lines.push(String::from("fields = ["));
            for f in &st.fields {
                lines.push(alloc::format!(
                    "  {{ name = {}, ty = {} }},",
                    toml_str(&f.name),
                    toml_str(&f.ty)
                ));
            }
            lines.push(String::from("]"));
        }
        lines.push(String::new());
    }
    for td in &cat.typedefs {
        lines.push(String::from("[[typedef]]"));
        lines.push(alloc::format!("name = {}", toml_str(&td.name)));
        lines.push(alloc::format!("ty = {}", toml_str(&td.ty)));
        lines.push(String::new());
    }
    for en in &cat.enums {
        lines.push(String::from("[[enum]]"));
        lines.push(alloc::format!("name = {}", toml_str(&en.name)));
        lines.push(String::from("variants = ["));
        for v in &en.variants {
            lines.push(alloc::format!(
                "  {{ name = {}, value = {} }},",
                toml_str(&v.name),
                v.value
            ));
        }
        lines.push(String::from("]"));
        lines.push(String::new());
    }
    for fn_ in &cat.fns {
        lines.push(String::from("[[fn]]"));
        lines.push(alloc::format!("name = {}", toml_str(&fn_.name)));
        lines.push(alloc::format!("ret = {}", toml_str(&fn_.ret)));
        if fn_.inline {
            lines.push(String::from("inline = true"));
        }
        if !fn_.args.is_empty() {
            lines.push(String::from("args = ["));
            for a in &fn_.args {
                lines.push(alloc::format!(
                    "  {{ name = {}, ty = {} }},",
                    toml_str(&a.name),
                    toml_str(&a.ty)
                ));
            }
            lines.push(String::from("]"));
        }
        lines.push(String::new());
    }
    if cat.is_empty() {
        lines.push(String::from("# empty catalog"));
        lines.push(String::new());
    }
    lines.join("\n")
}
