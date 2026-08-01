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

/// Which pool faces to generate for a module implemented in `impl_lang`.
///
/// `c` and `py` are skipped when they equal the impl's own slot -- a
/// same-language C-to-C or Python-to-Python consumer uses the human
/// source directly (a C consumer `#include`s the human `.h`; Python
/// imports the human `.py` module), so a generated mirror of the impl's
/// own language would be pointless there.
///
/// `rs` is the one exception: every consumer language always gets a
/// generated `include/pymergetic/metal/<mod>/` face, Rust-to-Rust
/// included -- a Rust consumer of an `impl=rs` provider does not get a
/// free pass to Cargo-depend on the provider's `_impl` crate directly
/// (see docs/definitions/module.md "Consume foreign modules"; `mem`/
/// `reg` are the only spine exception, handled outside this pool
/// machinery entirely).
pub fn emit_slots(impl_lang: &str, extra_toml: bool) -> Vec<&'static str> {
    let own = match pool_slot(impl_lang) {
        Some(s) => s,
        None => return Vec::new(),
    };
    let mut out = Vec::new();
    for s in POOL {
        if s == own && s != "rs" {
            continue;
        }
        if s == "toml" && !extra_toml {
            continue;
        }
        out.push(s);
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
        /* C border lives in headers — never scrape .c bodies for binds. */
        "c" => &["h"],
        "cpp" => &["hpp", "h"],
        "rs" => &["rs"],
        "py" => &["py"],
        _ => &[],
    }
}
