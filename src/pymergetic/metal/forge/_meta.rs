//! Minimal `.pm/module` JSON reader (type / name / impl only). No serde.

use alloc::string::String;
use alloc::vec::Vec;

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ModuleType {
    Module,
    Package,
    Hidden,
}

#[derive(Clone, Debug)]
pub struct ModuleMeta {
    pub ty: ModuleType,
    pub name: String,
    pub impl_lang: String,
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
    Ok(ModuleMeta {
        ty,
        name,
        impl_lang,
    })
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
