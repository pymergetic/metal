//! Lang-pool slot math (mirror tools/metal lang_pool.py).

use alloc::format;
use alloc::string::String;
use alloc::vec::Vec;

pub const POOL: [&str; 4] = ["c", "rs", "py", "toml"];
pub const ENTRY_STEM: &str = "__init__";

pub fn pool_slot(impl_lang: &str) -> Option<&'static str> {
    match impl_lang {
        "c" | "cpp" => Some("c"),
        "rs" => Some("rs"),
        "py" => Some("py"),
        _ => None,
    }
}

pub fn face_rel(slot: &str, base: &str) -> String {
    match slot {
        "c" => format!("{}.h", base),
        "rs" => format!("{}.rs", base),
        "py" => format!("{}.pyi", base),
        "toml" => format!("{}.toml", base),
        _ => format!("{}.unknown", base),
    }
}

/// Pool slot inferred from a file path extension (import or export).
pub fn slot_from_path(path: &str) -> Option<&'static str> {
    let name = path.rsplit('/').next().unwrap_or(path);
    let ext = name.rsplit_once('.').map(|(_, e)| e)?;
    match ext {
        "h" | "c" | "hpp" | "cpp" => Some("c"),
        "rs" => Some("rs"),
        "py" | "pyi" => Some("py"),
        "toml" => Some("toml"),
        _ => None,
    }
}

pub fn emit_slots(impl_lang: &str, extra_toml: bool) -> Vec<&'static str> {
    let own = match pool_slot(impl_lang) {
        Some(s) => s,
        None => return Vec::new(),
    };
    let mut out = Vec::new();
    for s in POOL {
        if s == own {
            continue;
        }
        if s == "toml" && !extra_toml {
            continue;
        }
        if s == "toml" || s == "c" || s == "rs" || s == "py" {
            out.push(s);
        }
    }
    out
}

pub fn impl_ext(impl_lang: &str) -> Option<&'static str> {
    match impl_lang {
        "rs" => Some("rs"),
        "c" => Some("c"),
        "cpp" => Some("cpp"),
        "py" => Some("py"),
        _ => None,
    }
}

pub fn impl_source_exts(impl_lang: &str) -> &'static [&'static str] {
    match impl_lang {
        "c" => &["h", "c"],
        "cpp" => &["hpp", "h", "cpp"],
        "rs" => &["rs"],
        "py" => &["py"],
        _ => &[],
    }
}
