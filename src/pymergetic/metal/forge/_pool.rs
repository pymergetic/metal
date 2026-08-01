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
/// Fully language-agnostic: every consumer language (`c`/`rs`/`py`) always
/// gets a generated `include/pymergetic/metal/<mod>/` face, including a
/// mirror in the impl's *own* language (a C-impl module still gets a
/// generated `.h` under `include/`, not just its hand-authored one in
/// `src/`; a Rust-impl module gets a generated `.rs` connector even for
/// Rust-to-Rust). No consumer -- same-language included -- gets a free
/// pass to reach into `src/**/_impl/` (or a module root) directly; every
/// cross-module call, in every language, goes through `include/` (`mem`/
/// `reg` are the only spine exception, handled outside this pool
/// machinery entirely). This keeps the rule uniform: "input in language
/// X, output in all languages."
///
/// `toml` is always emitted too: a raw catalog dump (fns/structs/enums/
/// typedefs) landing in the same generated `include/` tree costs nothing
/// and doubles as a debug hint -- if a face looks wrong, the `.toml` next
/// to it shows exactly what the importer actually detected.
pub fn emit_slots(impl_lang: &str) -> Vec<&'static str> {
    if pool_slot(impl_lang).is_none() {
        return Vec::new();
    }
    POOL.to_vec()
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
