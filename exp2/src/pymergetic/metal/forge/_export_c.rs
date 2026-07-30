//! Export: Catalog -> C `.h` face.

use alloc::string::String;
use alloc::vec::Vec;

use crate::_banner::generated_banner;
use crate::_catalog::{Catalog, Fn};

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

pub fn export(
    module_name: &str,
    base: &str,
    cat: &Catalog,
    human: &str,
    source_sha: &str,
) -> String {
    let guard = alloc::format!(
        "PM_METAL_{}_H_",
        module_name
            .to_uppercase()
            .replace('.', "_")
            .replace('-', "_")
    );
    let mut lines =
        generated_banner("c", human, &alloc::format!("{}.h", base), source_sha);
    lines.push(String::new());
    lines.push(alloc::format!("#ifndef {}", guard));
    lines.push(alloc::format!("#define {}", guard));
    lines.push(String::new());
    lines.push(String::from("#include <stddef.h>"));
    lines.push(String::from("#include <stdint.h>"));
    lines.push(String::new());
    lines.push(String::from("#ifdef __cplusplus"));
    lines.push(String::from("extern \"C\" {"));
    lines.push(String::from("#endif"));
    lines.push(String::new());
    for st in &cat.structs {
        lines.push(alloc::format!("typedef struct {} {};", st.name, st.name));
        lines.push(String::new());
    }
    for en in &cat.enums {
        lines.push(String::from("typedef enum {"));
        for (i, v) in en.variants.iter().enumerate() {
            let comma = if i + 1 < en.variants.len() { "," } else { "" };
            lines.push(alloc::format!("  {} = {}{}", v.name, v.value, comma));
        }
        lines.push(alloc::format!("}} {};", en.name));
        lines.push(String::new());
    }
    for td in &cat.typedefs {
        if td.ty.contains("(*)") {
            lines.push(alloc::format!(
                "typedef {};",
                td.ty.replacen("(*)", &alloc::format!("(*{})", td.name), 1)
            ));
        } else {
            lines.push(alloc::format!("typedef {} {};", td.ty, td.name));
        }
        lines.push(String::new());
    }
    for st in &cat.structs {
        lines.push(alloc::format!("struct {} {{", st.name));
        for f in &st.fields {
            if f.ty.contains('[') && f.ty.ends_with(']') {
                if let Some((base, n)) = f.ty.rsplit_once('[') {
                    lines.push(alloc::format!(
                        "  {} {}[{}];",
                        base.trim(),
                        f.name,
                        n.trim_end_matches(']')
                    ));
                }
            } else {
                lines.push(alloc::format!("  {} {};", f.ty, f.name));
            }
        }
        lines.push(String::from("};"));
        lines.push(String::new());
    }
    for fn_ in &cat.fns {
        lines.push(alloc::format!("{} {}({});", fn_.ret, fn_.name, c_args(fn_)));
    }
    if cat.is_empty() {
        lines.push(alloc::format!(
            "/* module {}: empty catalog */",
            module_name
        ));
    }
    lines.push(String::new());
    lines.push(String::from("#ifdef __cplusplus"));
    lines.push(String::from("}"));
    lines.push(String::from("#endif"));
    lines.push(String::new());
    lines.push(alloc::format!("#endif /* {} */", guard));
    lines.push(String::new());
    lines.join("\n")
}

