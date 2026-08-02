//! Minimal `.pm/module` JSON reader (type / name / impl only). No serde.

use alloc::string::String;
use alloc::vec::Vec;

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ModuleType {
    Module,
    Package,
    Hidden,
}

/// One declared cross-package import: `imports: [{"module": "...", "func": "..."}]`
/// in a `type: package`'s `.pm/module`. Forge cannot scan another
/// package's source tree to auto-detect this (see
/// docs/definitions/module.md "Cross-package import declarations"), so
/// the package declares it explicitly here; the matching real wasm
/// import (`#[link(wasm_import_module = "...")]` in Rust,
/// `PM_METAL_PKG_IMPORT` in C) still lives in the package's own source
/// and is what the compiler actually verifies -- this manifest entry
/// only tells `forge pack` what to embed in its own `.wasm`'s
/// `pm_metal_imports` custom section (see `_wasm_import_section`), for
/// the host loader to read back and register at load time.
#[derive(Clone, Debug)]
pub struct PkgImport {
    pub module: String,
    pub func: String,
}

#[derive(Clone, Debug)]
pub struct ModuleMeta {
    pub ty: ModuleType,
    pub name: String,
    pub impl_lang: String,
    /// Can this provider be reloaded/unloaded without the consumer's Cargo
    /// graph changing? Defaults by `type` (`module` -> false, always
    /// kernel-linked; `package` -> true, a wasm pack) and may be overridden
    /// explicitly (a `package` may set `"unloadable": false` to opt into the
    /// permanent-module fast path -- the "sticky" case).
    pub unloadable: bool,
    /// Does this module's export border cross a *package* boundary that
    /// forge cannot see across (a wasm guest calling into this provider,
    /// or vice versa)? Forge can auto-detect same-package imports by
    /// scanning source; it cannot see into another package's source
    /// tree, so a module opts in explicitly here to get the C face's
    /// dual-branch declaration (`PM_METAL_PKG_IMPORT` on `__wasm__`,
    /// an ordinary prototype natively) instead of the plain one.
    /// Defaults `false` (same-package, auto-detected, no explicit
    /// declaration needed).
    pub guest_surface: bool,
    /// This package's own declared cross-package imports (`type:
    /// package` only; empty for `type: module`). See [`PkgImport`].
    pub imports: Vec<PkgImport>,
}

fn skip_ws(s: &str, mut i: usize) -> usize {
    let b = s.as_bytes();
    while i < b.len() && b[i].is_ascii_whitespace() {
        i += 1;
    }
    i
}

fn parse_string(s: &str, mut i: usize) -> Option<(String, usize)> {
    let b = s.as_bytes();
    i = skip_ws(s, i);
    if i >= b.len() || b[i] != b'"' {
        return None;
    }
    i += 1;
    let mut out = String::new();
    while i < b.len() {
        match b[i] {
            b'"' => return Some((out, i + 1)),
            b'\\' => {
                i += 1;
                if i >= b.len() {
                    return None;
                }
                out.push(b[i] as char);
                i += 1;
            }
            c => {
                out.push(c as char);
                i += 1;
            }
        }
    }
    None
}

/// Extract string value for a top-level `"key": "value"` (flat object only).
fn find_string_field(json: &str, key: &str) -> Option<String> {
    let pat = alloc::format!("\"{}\"", key);
    let mut search_from = 0;
    while let Some(rel) = json[search_from..].find(&pat) {
        let start = search_from + rel + pat.len();
        let mut i = skip_ws(json, start);
        let b = json.as_bytes();
        if i >= b.len() || b[i] != b':' {
            search_from = start;
            continue;
        }
        i = skip_ws(json, i + 1);
        if let Some((v, _)) = parse_string(json, i) {
            return Some(v);
        }
        search_from = start;
    }
    None
}

/// Extract bool value for a top-level `"key": true|false` (flat object only).
fn find_bool_field(json: &str, key: &str) -> Option<bool> {
    let pat = alloc::format!("\"{}\"", key);
    let mut search_from = 0;
    while let Some(rel) = json[search_from..].find(&pat) {
        let start = search_from + rel + pat.len();
        let mut i = skip_ws(json, start);
        let b = json.as_bytes();
        if i >= b.len() || b[i] != b':' {
            search_from = start;
            continue;
        }
        i = skip_ws(json, i + 1);
        if json[i..].starts_with("true") {
            return Some(true);
        }
        if json[i..].starts_with("false") {
            return Some(false);
        }
        search_from = start;
    }
    None
}

/// Find the raw `[...]` span (inclusive) for a top-level `"key": [...]`,
/// tracking bracket depth so nested `[`/`]` inside object values don't
/// end the scan early.
fn find_array_span(json: &str, key: &str) -> Option<(usize, usize)> {
    let pat = alloc::format!("\"{}\"", key);
    let start_key = json.find(&pat)?;
    let mut i = skip_ws(json, start_key + pat.len());
    let b = json.as_bytes();
    if i >= b.len() || b[i] != b':' {
        return None;
    }
    i = skip_ws(json, i + 1);
    if i >= b.len() || b[i] != b'[' {
        return None;
    }
    let open = i;
    let mut depth = 0i32;
    while i < b.len() {
        match b[i] {
            b'[' => depth += 1,
            b']' => {
                depth -= 1;
                if depth == 0 {
                    return Some((open, i + 1));
                }
            }
            b'"' => {
                // skip over a string literal so a bracket inside it is ignored
                i += 1;
                while i < b.len() && b[i] != b'"' {
                    if b[i] == b'\\' {
                        i += 1;
                    }
                    i += 1;
                }
            }
            _ => {}
        }
        i += 1;
    }
    None
}

/// Parse `imports: [{"module": "...", "func": "..."}, ...]`. Malformed
/// or missing entries are silently skipped (best-effort: an empty
/// import list is safe -- forge just generates no trampoline for a
/// package that declared none).
fn parse_imports(json: &str) -> Vec<PkgImport> {
    let mut out = Vec::new();
    let Some((open, close)) = find_array_span(json, "imports") else {
        return out;
    };
    let body = &json[open + 1..close - 1];
    let b = body.as_bytes();
    let mut i = 0;
    while i < b.len() {
        i = skip_ws(body, i);
        if i >= b.len() {
            break;
        }
        if b[i] != b'{' {
            i += 1;
            continue;
        }
        // Find this object's matching close brace (objects here are flat,
        // no nested braces expected -- module/func are plain strings).
        let obj_start = i;
        let mut depth = 0i32;
        let mut j = i;
        while j < b.len() {
            match b[j] {
                b'{' => depth += 1,
                b'}' => {
                    depth -= 1;
                    if depth == 0 {
                        j += 1;
                        break;
                    }
                }
                _ => {}
            }
            j += 1;
        }
        let obj = &body[obj_start..j.min(body.len())];
        if let (Some(module), Some(func)) =
            (find_string_field(obj, "module"), find_string_field(obj, "func"))
        {
            out.push(PkgImport { module, func });
        }
        i = j;
    }
    out
}

pub fn parse_module_json(text: &str) -> Result<ModuleMeta, ()> {
    let ty_s = find_string_field(text, "type").ok_or(())?;
    let ty = match ty_s.as_str() {
        "module" => ModuleType::Module,
        "package" => ModuleType::Package,
        "hidden" => ModuleType::Hidden,
        _ => return Err(()),
    };
    let name = find_string_field(text, "name").unwrap_or_default();
    let impl_lang = find_string_field(text, "impl").unwrap_or_default();
    let default_unloadable = matches!(ty, ModuleType::Package);
    let unloadable = find_bool_field(text, "unloadable").unwrap_or(default_unloadable);
    let guest_surface = find_bool_field(text, "guest_surface").unwrap_or(false);
    let imports = parse_imports(text);
    Ok(ModuleMeta {
        ty,
        name,
        impl_lang,
        unloadable,
        guest_surface,
        imports,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_package_imports() {
        let json = r#"{
            "type": "package",
            "name": "sample.announcer",
            "impl": "rs",
            "imports": [
                {"module": "pymergetic.metal.sample.greeter.words", "func": "hello"},
                {"module": "pymergetic.metal.sample.greeter.numbers", "func": "lucky"}
            ]
        }"#;
        let meta = parse_module_json(json).expect("parse");
        assert_eq!(meta.ty, ModuleType::Package);
        assert_eq!(meta.imports.len(), 2);
        assert_eq!(meta.imports[0].module, "pymergetic.metal.sample.greeter.words");
        assert_eq!(meta.imports[0].func, "hello");
        assert_eq!(meta.imports[1].func, "lucky");
    }

    #[test]
    fn no_imports_field_is_empty() {
        let json = r#"{"type": "module", "name": "pymergetic.metal.dt", "impl": "rs"}"#;
        let meta = parse_module_json(json).expect("parse");
        assert!(meta.imports.is_empty());
    }
}

pub fn join_path(a: &str, b: &str) -> String {
    if a.is_empty() {
        return String::from(b);
    }
    if b.is_empty() {
        return String::from(a);
    }
    if a.ends_with('/') {
        alloc::format!("{}{}", a, b)
    } else {
        alloc::format!("{}/{}", a, b)
    }
}

/// Walk `root` recursively for `.pm/module` files; return module dirs.
pub fn discover_pm_dirs_under<S>(
    store: &mut S,
    root: &str,
    out: &mut Vec<String>,
) where
    S: crate::_port::ForgeStore,
{
    use crate::_port::block_on;

    fn walk<S: crate::_port::ForgeStore>(
        store: &mut S,
        dir: &str,
        out: &mut Vec<String>,
    ) {
        let names = match block_on(|| store.list_dir(dir)) {
            Ok(n) => n,
            Err(_) => return,
        };
        let mut names = names;
        names.sort();
        let pm = join_path(dir, ".pm");
        let module_json = join_path(&pm, "module");
        if block_on(|| store.is_file(&module_json)) {
            out.push(String::from(dir));
        }
        for name in names {
            if name == "."
                || name == ".."
                || name == ".pm"
                || name == ".target"
                || name == "target"
                || name == ".git"
            {
                continue;
            }
            let child = join_path(dir, &name);
            if block_on(|| store.is_dir(&child)) {
                walk(store, &child, out);
            }
        }
    }

    if block_on(|| store.is_dir(root)) {
        walk(store, root, out);
    }
}
