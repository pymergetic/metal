//! Single-face convert: source file -> dest face by pool slot / filetype.
//!
//! `mod sync` is discover + convert each stem to each emit slot. Banner
//! write-gate and `Source-sha` freshness live here.

use alloc::string::String;

use crate::_banner::{content_has_banner, face_matches_source_sha, generated_hint};
use crate::_catalog::Catalog;
use crate::_hash::source_sha_hex;
use crate::_meta::{join_path, parse_module_json, ModuleMeta};
use crate::_pool::{face_rel, pool_slot, slot_from_path, ENTRY_STEM};
use crate::_port::{block_on, ForgeStore};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum FaceAction {
    /// Wrote (or rewrote) the face.
    Written,
    /// Existing face already matches Source-sha of current human source.
    Fresh,
    /// Exporter produced no content (e.g. toml stub).
    Empty,
}

fn read_text<S: ForgeStore>(store: &mut S, path: &str) -> Result<String, ()> {
    let bytes = block_on(|| store.read_file(path)).map_err(|_| ())?;
    String::from_utf8(bytes).map_err(|_| ())
}

fn write_text<S: ForgeStore>(store: &mut S, path: &str, text: &str) -> Result<(), ()> {
    block_on(|| store.write_file(path, text.as_bytes())).map_err(|_| ())
}

pub fn import_catalog(src_slot: &str, text: &str) -> Result<Catalog, ()> {
    match src_slot {
        "rs" => Ok(crate::_import_rs::import(text)),
        "c" => Ok(crate::_import_c::import(text)),
        "py" => crate::_import_py::import(text),
        "toml" => crate::_import_toml::import(text),
        _ => Err(()),
    }
}

fn known_type_name(cat: &Catalog, name: &str) -> bool {
    cat.structs.iter().any(|s| s.name == name)
        || cat.enums.iter().any(|e| e.name == name)
        || cat.typedefs.iter().any(|t| t.name == name)
        || cat.sibling_types.iter().any(|t| t == name)
}

/// Foreign `pm_metal_*_t` identifiers referenced by this catalog's own
/// functions but not defined in it -- mirrors `_export_c.rs`'s own
/// `foreign_type_names` tokenizing.
fn foreign_type_names(cat: &Catalog) -> alloc::vec::Vec<String> {
    let mut out: alloc::vec::Vec<String> = alloc::vec::Vec::new();
    let mut push = |raw: &str| {
        for tok in raw.split(|c: char| {
            c.is_whitespace() || c == '*' || c == ',' || c == '(' || c == ')' || c == '[' || c == ']'
        }) {
            let t = tok.trim();
            /* Mirror `_export_c::looks_like_foreign_type` (keep in sync). */
            let first_ok = t.chars().next().map(|c| c.is_ascii_uppercase()).unwrap_or(false);
            let pm_t = t.starts_with("pm_metal_") && t.ends_with("_t");
            let builtin = matches!(
                t,
                "void"
                    | "char"
                    | "int"
                    | "long"
                    | "float"
                    | "double"
                    | "bool"
                    | "size_t"
                    | "intptr_t"
                    | "uintptr_t"
                    | "const"
                    | "struct"
                    | "enum"
                    | "union"
            ) || ((t.starts_with("uint") || t.starts_with("int")) && t.ends_with("_t"));
            if (pm_t || first_ok)
                && !builtin
                && t.chars().all(|c| c == '_' || c.is_ascii_alphanumeric())
                && !known_type_name(cat, t)
                && !out.iter().any(|x| x == t)
            {
                out.push(String::from(t));
            }
        }
    };
    for f in &cat.fns {
        push(&f.ret);
        for a in &f.args {
            push(&a.ty);
        }
    }
    out
}

/// Sibling stems in the same module directory sometimes own a typedef
/// this stem's own function signatures reference by name (e.g. an
/// `async/task.rs` spawn fn taking `async/coro.rs`'s step-fn typedef).
///
/// - **Rust faces:** pull the real typedef/struct/enum into this catalog
///   (a Rust face must be a self-contained TU; opaque is not viable for
///   by-value fn-pointer typedefs).
/// - **C faces:** record a sibling `#include` instead of inlining or
///   inventing `typedef struct X X` -- co-including the owning face would
///   otherwise redefine enums/typedefs (ssh includes handle+await+coro+task).
fn resolve_foreign_types<S: ForgeStore>(
    store: &mut S,
    mod_dir: &str,
    src_slot: &str,
    human_path: &str,
    cat: &mut Catalog,
    pkg_name: &str,
    for_c_face: bool,
) {
    let missing = foreign_type_names(cat);
    if missing.is_empty() {
        return;
    }
    let impl_dir = join_path(mod_dir, "_impl");
    let src_dir = if block_on(|| store.is_dir(&impl_dir)) {
        impl_dir
    } else {
        String::from(mod_dir)
    };
    let names = match block_on(|| store.list_dir(&src_dir)) {
        Ok(n) => n,
        Err(_) => return,
    };
    let pkg_slash = pkg_name.replace('.', "/");
    for name in names {
        if missing.iter().all(|n| known_type_name(cat, n)) {
            break;
        }
        let path = join_path(&src_dir, &name);
        if path == human_path || !block_on(|| store.is_file(&path)) {
            continue;
        }
        let Ok(text) = read_text(store, &path) else {
            continue;
        };
        let Ok(sib_cat) = import_catalog(src_slot, &text) else {
            continue;
        };
        let sib_stem = name
            .rsplit_once('.')
            .map(|(s, _)| s)
            .unwrap_or(name.as_str());
        if sib_stem.starts_with('_') {
            continue;
        }
        let mut used_sibling = false;
        for want in &missing {
            if known_type_name(cat, want) {
                continue;
            }
            let hit = sib_cat.typedefs.iter().any(|t| &t.name == want)
                || sib_cat.structs.iter().any(|s| &s.name == want)
                || sib_cat.enums.iter().any(|e| &e.name == want);
            if !hit {
                continue;
            }
            if for_c_face {
                used_sibling = true;
                if !cat.sibling_types.iter().any(|t| t == want) {
                    cat.sibling_types.push((*want).clone());
                }
            } else if let Some(td) = sib_cat.typedefs.iter().find(|t| &t.name == want) {
                cat.typedefs.push(td.clone());
            } else if let Some(st) = sib_cat.structs.iter().find(|s| &s.name == want) {
                cat.structs.push(st.clone());
            } else if let Some(en) = sib_cat.enums.iter().find(|e| &e.name == want) {
                cat.enums.push(en.clone());
            }
        }
        if for_c_face && used_sibling {
            let face = if sib_stem == ENTRY_STEM {
                alloc::format!("{}/__init__.h", pkg_slash)
            } else {
                alloc::format!("{}/{}.h", pkg_slash, sib_stem)
            };
            if !cat.includes.iter().any(|i| i == &face) {
                cat.includes.push(face);
            }
        }
    }
}

/// Spine modules keep a plain link-time Rust face: `mem`/`reg` (bootstrap
/// circularity), the kernel root (`pymergetic.metal` -- `pm_metal_kernel_load`
/// must be callable before any registry entry exists), and `async` (reg's
/// unload path path-includes the quiesce face; a proxy face would need
/// `pymergetic_metal_reg::` from *inside* the reg crate).
fn rs_face_is_spine(module_name: &str) -> bool {
    module_name == "pymergetic.metal"
        || module_name == "pymergetic.metal.mem"
        || module_name.starts_with("pymergetic.metal.mem.")
        || module_name == "pymergetic.metal.reg"
        || module_name.starts_with("pymergetic.metal.reg.")
        || module_name == "pymergetic.metal.async"
        || module_name.starts_with("pymergetic.metal.async.")
}

/// Emit the face for `dst_slot`.
///
/// Rust faces are **always-proxy** (cached [`ImportRow`][row], load slot,
/// call) except the spine -- see docs/definitions/module.md "Two face
/// shapes". `provider_unloadable` no longer forks the Rust shape; quiesce
/// made fixed and unloadable call sites identical.
///
/// `c` still forks on `guest_surface` only (wasm import branch vs plain
/// prototype). `py` is unchanged (typing face).
///
/// [row]: pymergetic_metal_reg::ImportRow
pub fn export_face(
    dst_slot: &str,
    name: &str,
    stem: &str,
    cat: &Catalog,
    human: &str,
    source_sha: &str,
    _provider_unloadable: bool,
    guest_surface: bool,
) -> Option<String> {
    match dst_slot {
        "c" => Some(if guest_surface {
            crate::_export_c::export_guest_surface(name, stem, cat, human, source_sha)
        } else {
            crate::_export_c::export(name, stem, cat, human, source_sha)
        }),
        "rs" => Some(if rs_face_is_spine(name) {
            crate::_export_rs::export(name, stem, cat, human, source_sha)
        } else {
            crate::_export_rs::export_proxy(name, stem, cat, human, source_sha)
        }),
        "py" => Some(crate::_export_py::export(name, stem, cat, human, source_sha)),
        "toml" => {
            let s = crate::_export_toml::export(name, stem, cat, human, source_sha);
            if s.is_empty() {
                None
            } else {
                Some(s)
            }
        }
        _ => None,
    }
}

/// Write banner-owned `rel` under `out_dir` (a module's mirrored
/// `include/pymergetic/metal/<mod>/` directory, never the module's own
/// `src/` directory), or refuse human / private paths.
pub fn write_generated<S: ForgeStore>(
    store: &mut S,
    out_dir: &str,
    rel: &str,
    content: &str,
) -> Result<(), String> {
    if !content.contains(generated_hint()) {
        return Err(alloc::format!("codegen refuses content without banner: {}", rel));
    }
    let path = join_path(out_dir, rel);
    let base = rel.rsplit('/').next().unwrap_or(rel);
    let stem = base.rsplit_once('.').map(|(s, _)| s).unwrap_or(base);
    // Package entry `__init__` is public; other `_*.*` stems stay private.
    if stem != ENTRY_STEM && stem.starts_with('_') {
        return Err(alloc::format!("codegen refuses private path: {}", rel));
    }
    if block_on(|| store.is_file(&path)) {
        let existing = read_text(store, &path).unwrap_or_default();
        if !content_has_banner(&existing) {
            return Err(alloc::format!(
                "refusing to overwrite human file (no generated banner): {}",
                path
            ));
        }
    }
    write_text(store, &path, content).map_err(|_| alloc::format!("io write {}", path))
}

/// True if `path` exists, is banner-owned, and Source-sha matches `want`.
pub fn path_is_fresh<S: ForgeStore>(store: &mut S, path: &str, want: &str) -> bool {
    if !block_on(|| store.is_file(path)) {
        return false;
    }
    match read_text(store, path) {
        Ok(text) => face_matches_source_sha(&text, want),
        Err(_) => false,
    }
}

/// Convert human impl at `human_path` into one pool face under `out_dir`
/// (the module's mirrored `include/pymergetic/metal/<mod>/` directory).
/// `mod_dir` (the module's own `src/` directory) is only used for the
/// package-name fallback when `.pm/module`'s `name` is empty.
///
/// When `force` is false and the face's `Source-sha` already matches the
/// human source fingerprint, skip the write (`FaceAction::Fresh`).
pub fn convert_stem_slot<S: ForgeStore>(
    store: &mut S,
    mod_dir: &str,
    out_dir: &str,
    meta: &ModuleMeta,
    human_path: &str,
    dst_slot: &str,
    force: bool,
) -> Result<FaceAction, String> {
    let human_name = human_path.rsplit('/').next().unwrap_or(human_path);
    let stem = human_name
        .rsplit_once('.')
        .map(|(s, _)| s)
        .unwrap_or(human_name);
    let pkg = if meta.name.is_empty() {
        mod_dir.rsplit('/').next().unwrap_or(mod_dir)
    } else {
        meta.name.as_str()
    };
    let name = if stem == ENTRY_STEM {
        String::from(pkg)
    } else {
        alloc::format!("{}.{}", pkg, stem)
    };

    let text = read_text(store, human_path).map_err(|_| alloc::format!("read {}", human_path))?;
    let source_sha = source_sha_hex(text.as_bytes());
    let rel = face_rel(dst_slot, stem);
    let dst_path = join_path(out_dir, &rel);
    if !force && path_is_fresh(store, &dst_path, &source_sha) {
        return Ok(FaceAction::Fresh);
    }

    let src_slot = pool_slot(&meta.impl_lang).unwrap_or("");
    let mut cat = import_catalog(src_slot, &text)
        .map_err(|_| alloc::format!("import {}", human_path))?;
    if dst_slot == "rs" || dst_slot == "c" {
        resolve_foreign_types(
            store,
            mod_dir,
            src_slot,
            human_path,
            &mut cat,
            pkg,
            dst_slot == "c",
        );
    }
    let Some(content) = export_face(
        dst_slot,
        &name,
        stem,
        &cat,
        human_name,
        &source_sha,
        meta.unloadable,
        meta.guest_surface,
    ) else {
        return Ok(FaceAction::Empty);
    };
    write_generated(store, out_dir, &rel, &content)?;
    Ok(FaceAction::Written)
}

fn load_meta_at<S: ForgeStore>(store: &mut S, mod_dir: &str) -> Result<ModuleMeta, ()> {
    let path = join_path(&join_path(mod_dir, ".pm"), "module");
    let text = read_text(store, &path)?;
    parse_module_json(&text)
}

/// Walk parents of `path` for a directory containing `.pm/module`.
pub fn enclosing_module_dir<S: ForgeStore>(store: &mut S, path: &str) -> Option<String> {
    let mut cur = String::from(path);
    if block_on(|| store.is_file(&cur)) {
        if let Some((parent, _)) = cur.rsplit_once('/') {
            cur = String::from(parent);
        } else {
            return None;
        }
    }
    loop {
        let meta = join_path(&join_path(&cur, ".pm"), "module");
        if block_on(|| store.is_file(&meta)) {
            return Some(cur);
        }
        match cur.rsplit_once('/') {
            Some((parent, _)) if !parent.is_empty() => cur = String::from(parent),
            _ => return None,
        }
    }
}

/// Convert absolute `src` -> absolute `dst` by filetype; refresh module gitignore.
///
/// Returns Written / Fresh / Empty. Refuses private stems and human dsts.
pub fn convert_paths<S: ForgeStore>(
    store: &mut S,
    src: &str,
    dst: &str,
    force: bool,
) -> Result<FaceAction, String> {
    let src_slot = slot_from_path(src)
        .ok_or_else(|| alloc::format!("convert: unknown source type: {}", src))?;
    let dst_slot = slot_from_path(dst)
        .ok_or_else(|| alloc::format!("convert: unknown dest type: {}", dst))?;
    if src_slot == dst_slot {
        return Err(String::from("convert: source and dest are the same pool slot"));
    }
    let dst_base = dst.rsplit('/').next().unwrap_or(dst);
    let dst_stem = dst_base
        .rsplit_once('.')
        .map(|(s, _)| s)
        .unwrap_or(dst_base);
    if dst_stem != ENTRY_STEM && dst_stem.starts_with('_') {
        return Err(alloc::format!("convert: refuses private dest: {}", dst));
    }

    let text = read_text(store, src).map_err(|_| alloc::format!("convert: read {}", src))?;
    let source_sha = source_sha_hex(text.as_bytes());
    if !force && path_is_fresh(store, dst, &source_sha) {
        return Ok(FaceAction::Fresh);
    }

    let mod_dir = enclosing_module_dir(store, dst)
        .ok_or_else(|| alloc::format!("convert: no .pm/module above {}", dst))?;
    let meta = load_meta_at(store, &mod_dir)
        .map_err(|_| alloc::format!("convert: bad meta in {}", mod_dir))?;

    let human_name = src.rsplit('/').next().unwrap_or(src);
    let stem = human_name
        .rsplit_once('.')
        .map(|(s, _)| s)
        .unwrap_or(human_name);
    if stem != dst_stem {
        return Err(alloc::format!(
            "convert: stem mismatch {} vs {}",
            stem, dst_stem
        ));
    }

    let pkg = if meta.name.is_empty() {
        mod_dir.rsplit('/').next().unwrap_or(mod_dir.as_str())
    } else {
        meta.name.as_str()
    };
    let name = if stem == ENTRY_STEM {
        String::from(pkg)
    } else {
        alloc::format!("{}.{}", pkg, stem)
    };

    let mut cat = import_catalog(src_slot, &text)
        .map_err(|_| alloc::format!("convert: import {}", src))?;
    if dst_slot == "rs" || dst_slot == "c" {
        resolve_foreign_types(
            store,
            &mod_dir,
            src_slot,
            src,
            &mut cat,
            pkg,
            dst_slot == "c",
        );
    }
    let Some(content) = export_face(
        dst_slot,
        &name,
        stem,
        &cat,
        human_name,
        &source_sha,
        meta.unloadable,
        meta.guest_surface,
    ) else {
        return Ok(FaceAction::Empty);
    };

    // dst may be absolute; write_generated wants mod-relative.
    let rel = if let Some(rel) = dst.strip_prefix(mod_dir.as_str()) {
        let r = rel.trim_start_matches('/');
        if r.is_empty() {
            return Err(String::from("convert: dest is the module dir"));
        }
        String::from(r)
    } else {
        face_rel(dst_slot, stem)
    };
    write_generated(store, &mod_dir, &rel, &content)?;
    Ok(FaceAction::Written)
}
