//! discover + check / sync / clean / ls over ForgeStore + ForgeSession.
//!
//! Per-face work is [`crate::_convert`] — sync discovers stems and pipes
//! each through convert (with Source-sha freshness).

use alloc::collections::BTreeMap;
use alloc::string::String;
use alloc::vec;
use alloc::vec::Vec;

use crate::_banner::content_has_banner;
use crate::_convert::{convert_paths, convert_stem_slot, import_catalog, FaceAction};
use crate::_gitignore;
use crate::_meta::{
    discover_pm_dirs_under, join_path, parse_module_json, ModuleMeta, ModuleType,
};
use crate::_pool::{
    emit_slots, face_rel, impl_ext, impl_source_exts, pool_slot, slot_from_path, ENTRY_STEM,
    POOL,
};
use crate::_port::{block_on, ForgeSession, ForgeStore};

fn status_sign(s: &str) -> &'static str {
    match s {
        "original" => "§",
        "cleaned" => "X",
        "emitted" => "*",
        "fresh" => ".",
        "marker" => "~",
        "pruned" => "x",
        _ => "-",
    }
}

fn read_text<S: ForgeStore>(store: &mut S, path: &str) -> Result<String, ()> {
    let bytes = block_on(|| store.read_file(path)).map_err(|_| ())?;
    String::from_utf8(bytes).map_err(|_| ())
}

fn mod_dir_rel(mod_dir: &str, roots: &[String]) -> String {
    for root in roots {
        let prefix = if root.ends_with('/') {
            root.clone()
        } else {
            alloc::format!("{}/", root)
        };
        if let Some(rel) = mod_dir.strip_prefix(&prefix) {
            return String::from(rel);
        }
        if mod_dir == root.as_str() {
            return String::new();
        }
    }
    String::from(mod_dir.rsplit('/').next().unwrap_or(mod_dir))
}

fn path_to_py_name(rel: &str) -> String {
    // Python-style module path: pymergetic.metal.async.await
    rel.replace('/', ".")
}

fn stem_kind(stem: &str) -> &'static str {
    // Only directories are Metal modules (.pm/module). Rows are stems:
    // __init__ = package entry (d); other public stems = sibling files (f).
    if stem == ENTRY_STEM {
        "d"
    } else {
        "f"
    }
}

fn stem_id(mod_dir: &str, stem: &str, roots: &[String]) -> String {
    let rel = mod_dir_rel(mod_dir, roots);
    let base = path_to_py_name(&rel);
    if stem == ENTRY_STEM {
        base
    } else if base.is_empty() {
        String::from(stem)
    } else {
        alloc::format!("{}.{}", base, stem)
    }
}

fn load_meta<S: ForgeStore>(store: &mut S, mod_dir: &str) -> Result<ModuleMeta, ()> {
    let path = join_path(&join_path(mod_dir, ".pm"), "module");
    let text = read_text(store, &path)?;
    parse_module_json(&text)
}

fn impl_sources<S: ForgeStore>(store: &mut S, mod_dir: &str, impl_lang: &str) -> Vec<String> {
    let exts = impl_source_exts(impl_lang);
    let names = match block_on(|| store.list_dir(mod_dir)) {
        Ok(n) => n,
        Err(_) => return Vec::new(),
    };
    let mut seen: Vec<String> = Vec::new();
    let mut out: Vec<String> = Vec::new();
    let mut files: Vec<String> = names
        .into_iter()
        .filter(|n| {
            for ext in exts {
                if n.ends_with(&alloc::format!(".{}", ext)) {
                    return true;
                }
            }
            false
        })
        .collect();
    files.sort();
    // prefer __init__ first; then stem name (C/C++ sources are headers only)
    files.sort_by(|a, b| {
        let stem_a = a.rsplit_once('.').map(|(s, _)| s).unwrap_or(a.as_str());
        let stem_b = b.rsplit_once('.').map(|(s, _)| s).unwrap_or(b.as_str());
        let ka = if stem_a == ENTRY_STEM { 0 } else { 1 };
        let kb = if stem_b == ENTRY_STEM { 0 } else { 1 };
        ka.cmp(&kb).then(stem_a.cmp(stem_b))
    });
    for name in files {
        let stem = name.rsplit_once('.').map(|(s, _)| s).unwrap_or(&name);
        // Package entry `__init__` is public even though it starts with `_`.
        if stem != ENTRY_STEM {
            if stem.starts_with('_') {
                continue;
            }
            if stem.ends_with("_bin") {
                continue;
            }
        }
        if seen.iter().any(|s| s == stem) {
            continue;
        }
        let path = join_path(mod_dir, &name);
        if block_on(|| store.is_file(&path)) {
            seen.push(String::from(stem));
            out.push(path);
        }
    }
    out
}

fn remove_marked<S: ForgeStore>(store: &mut S, mod_dir: &str, rel: &str) -> bool {
    let path = join_path(mod_dir, rel);
    if block_on(|| store.is_file(&path)) {
        if let Ok(text) = read_text(store, &path) {
            if content_has_banner(&text) {
                let _ = block_on(|| store.remove_file(&path));
                return true;
            }
        }
    }
    false
}

fn sync_stem<S: ForgeStore>(
    store: &mut S,
    mod_dir: &str,
    meta: &ModuleMeta,
    human_path: &str,
    slots: &[&str],
    roots: &[String],
    force: bool,
) -> Result<(String, String, BTreeMap<String, String>), String> {
    let human_name = human_path.rsplit('/').next().unwrap_or(human_path);
    let stem = human_name
        .rsplit_once('.')
        .map(|(s, _)| s)
        .unwrap_or(human_name);
    let own = pool_slot(&meta.impl_lang).unwrap_or("");
    let text = read_text(store, human_path).map_err(|_| alloc::format!("read {}", human_path))?;
    let src_slot = own;
    let cat = import_catalog(src_slot, &text)
        .map_err(|_| alloc::format!("import {}", human_path))?;

    let mut emit_now: Vec<&str> = slots.to_vec();
    let mut marker_slots: Vec<&str> = Vec::new();
    if !cat.has_border() {
        emit_now.clear();
        for s in slots {
            if *s == "toml" {
                emit_now.push(s);
            } else if *s == "py" && stem == ENTRY_STEM {
                emit_now.push(s);
                marker_slots.push(s);
            }
        }
    }

    let mut actions: BTreeMap<String, FaceAction> = BTreeMap::new();
    for slot in &emit_now {
        match convert_stem_slot(store, mod_dir, meta, human_path, slot, force) {
            Ok(a) => {
                actions.insert(String::from(*slot), a);
            }
            Err(e) => return Err(e),
        }
    }

    // prune unemitted faces for this stem
    let keep: Vec<String> = emit_now.iter().map(|s| face_rel(s, stem)).collect();
    let mut pruned = Vec::new();
    for slot in POOL {
        let rel = face_rel(slot, stem);
        if keep.iter().any(|k| k == &rel) {
            continue;
        }
        if remove_marked(store, mod_dir, &rel) {
            pruned.push(rel);
        }
    }

    let mut status = BTreeMap::new();
    for slot in POOL {
        let st = if slot == own {
            "original"
        } else if marker_slots.iter().any(|s| *s == slot) {
            match actions.get(slot) {
                Some(FaceAction::Fresh) => "fresh",
                _ => "marker",
            }
        } else if let Some(a) = actions.get(slot) {
            match a {
                FaceAction::Written => "emitted",
                FaceAction::Fresh => "fresh",
                FaceAction::Empty => "noop",
            }
        } else if pruned.iter().any(|r| r == &face_rel(slot, stem)) {
            "pruned"
        } else {
            "noop"
        };
        status.insert(String::from(slot), String::from(st));
    }
    Ok((
        stem_id(mod_dir, stem, roots),
        String::from(stem_kind(stem)),
        status,
    ))
}

fn format_pool_table(
    rows: &[(String, String, BTreeMap<String, String>)],
    legend: &str,
) -> String {
    if rows.is_empty() {
        return String::new();
    }
    let mod_w = rows
        .iter()
        .map(|(m, _, _)| m.len())
        .max()
        .unwrap_or(6)
        .max(6);
    let kind_w = 4usize; // "d" | "f"
    let slot_w = 4usize;
    let mut head = alloc::format!("{:width$}", "module", width = mod_w);
    head.push_str(&alloc::format!("  {:^width$}", "kind", width = kind_w));
    for s in POOL {
        head.push_str(&alloc::format!("  {:^width$}", s, width = slot_w));
    }
    let mut lines = vec![head.clone(), "-".repeat(head.len())];
    for (mid, kind, st) in rows {
        let mut row = alloc::format!("{:width$}", mid, width = mod_w);
        row.push_str(&alloc::format!("  {:^width$}", kind, width = kind_w));
        for s in POOL {
            let sign = status_sign(st.get(s).map(|x| x.as_str()).unwrap_or("noop"));
            row.push_str(&alloc::format!("  {:^width$}", sign, width = slot_w));
        }
        lines.push(row);
    }
    lines.push(String::from(legend));
    lines.join("\n")
}

fn discover_modules<S: ForgeStore>(store: &mut S, roots: &[String]) -> Vec<String> {
    let mut dirs = Vec::new();
    for root in roots {
        discover_pm_dirs_under(store, root, &mut dirs);
    }
    dirs.sort();
    dirs.dedup();
    dirs.into_iter()
        .filter(|d| {
            load_meta(store, d)
                .map(|m| m.ty == ModuleType::Module)
                .unwrap_or(false)
        })
        .collect()
}

fn out_line<Sess: ForgeSession>(sess: &mut Sess, s: &str) {
    let _ = block_on(|| sess.out_line(s));
}

fn err_line<Sess: ForgeSession>(sess: &mut Sess, s: &str) {
    let _ = block_on(|| sess.err_line(s));
}

/// Resolve metal checkout roots: `<metal>/src` only (old product under `_old/`).
pub fn default_src_roots(metal_root: &str) -> Vec<String> {
    vec![join_path(metal_root, "src")]
}

pub fn cmd_sync<S: ForgeStore, Sess: ForgeSession>(
    store: &mut S,
    sess: &mut Sess,
    metal_root: &str,
    extra_toml: bool,
    force: bool,
) -> i32 {
    let roots = default_src_roots(metal_root);
    let mods = discover_modules(store, &roots);
    if mods.is_empty() {
        err_line(sess, "forge mod sync: no .pm/module modules found");
        sess.set_exit(1);
        return 1;
    }
    let mut errors = 0;
    let mut rows: Vec<(String, String, BTreeMap<String, String>)> = Vec::new();
    let mut synced = 0;
    for mod_dir in &mods {
        let meta = match load_meta(store, mod_dir) {
            Ok(m) => m,
            Err(_) => {
                err_line(sess, &alloc::format!("forge mod sync: {}: bad meta", mod_dir));
                errors += 1;
                continue;
            }
        };
        if meta.ty != ModuleType::Module {
            continue;
        }
        if impl_ext(&meta.impl_lang).is_none() {
            err_line(
                sess,
                &alloc::format!("forge mod sync: skip {}: bad impl", mod_dir),
            );
            continue;
        }
        let sources = impl_sources(store, mod_dir, &meta.impl_lang);
        if sources.is_empty() {
            err_line(
                sess,
                &alloc::format!("forge mod sync: {}: no human impl sources", mod_dir),
            );
            errors += 1;
            continue;
        }
        let slots = emit_slots(&meta.impl_lang, extra_toml);
        let mut ok = true;
        for human in &sources {
            match sync_stem(store, mod_dir, &meta, human, &slots, &roots, force) {
                Ok(row) => rows.push(row),
                Err(e) => {
                    err_line(sess, &alloc::format!("forge mod sync: {}", e));
                    errors += 1;
                    ok = false;
                    break;
                }
            }
        }
        if ok {
            // prune orphan faces
            let keep: Vec<String> = sources
                .iter()
                .map(|p| {
                    p.rsplit('/')
                        .next()
                        .unwrap_or(p)
                        .rsplit_once('.')
                        .map(|(s, _)| String::from(s))
                        .unwrap_or_else(|| String::from(p.as_str()))
                })
                .collect();
            for rel in _gitignore::list_generated_rels(store, mod_dir) {
                let stem = rel.rsplit_once('.').map(|(s, _)| s).unwrap_or(&rel);
                if !keep.iter().any(|k| k == stem) {
                    let _ = remove_marked(store, mod_dir, &rel);
                }
            }
            _gitignore::update(store, mod_dir);
            synced += 1;
        }
    }
    if !rows.is_empty() {
        out_line(
            sess,
            &format_pool_table(
                &rows,
                "kind=d|f (d=__init__ package entry; f=sibling stem)  §=original  *=emitted  .=fresh  ~=marker  x=pruned  -=noop",
            ),
        );
    }
    out_line(
        sess,
        &alloc::format!(
            "forge mod sync: {} module(s), {} stem(s)",
            synced,
            rows.len()
        ),
    );
    let code = if errors > 0 { 1 } else { 0 };
    sess.set_exit(code);
    code
}

/// One-shot filetype convert: `SRC` -> `DST` (banner + Source-sha + gitignore).
pub fn cmd_convert<S: ForgeStore, Sess: ForgeSession>(
    store: &mut S,
    sess: &mut Sess,
    src: &str,
    dst: &str,
    force: bool,
) -> i32 {
    match convert_paths(store, src, dst, force) {
        Ok(FaceAction::Written) => {
            out_line(sess, &alloc::format!("forge convert: wrote {}", dst));
            sess.set_exit(0);
            0
        }
        Ok(FaceAction::Fresh) => {
            out_line(sess, &alloc::format!("forge convert: fresh {}", dst));
            sess.set_exit(0);
            0
        }
        Ok(FaceAction::Empty) => {
            err_line(sess, "forge convert: exporter produced no content");
            sess.set_exit(1);
            1
        }
        Err(e) => {
            err_line(sess, &alloc::format!("forge convert: {}", e));
            sess.set_exit(1);
            1
        }
    }
}

fn is_border_fn_name(name: &str) -> bool {
    let mut chars = name.chars();
    match chars.next() {
        Some(c) if c == '_' || c.is_ascii_alphabetic() => {}
        _ => return false,
    }
    chars.all(|c| c == '_' || c.is_ascii_alphanumeric())
}

fn catalog_fn_names(cat: &crate::_catalog::Catalog) -> Vec<String> {
    // Drop C-import noise (e.g. `=`, `|=`, `*foo` from macros / fn-ptr types).
    let mut v: Vec<String> = cat
        .fns
        .iter()
        .filter(|f| is_border_fn_name(&f.name))
        .map(|f| f.name.clone())
        .collect();
    v.sort();
    v.dedup();
    v
}

/// Every public border fn on the human stem must appear on each emitted
/// lang-pool face (c / rs / py). Missing faces or name skew = fail.
///
/// `human_path` is the absolute path from [`impl_sources`] (same as sync).
fn check_face_symmetry<S: ForgeStore, Sess: ForgeSession>(
    store: &mut S,
    sess: &mut Sess,
    mod_dir: &str,
    mod_rel: &str,
    human_path: &str,
    meta: &ModuleMeta,
) -> u32 {
    let impl_slot = match pool_slot(&meta.impl_lang) {
        Some(s) => s,
        None => return 0,
    };
    let human_name = human_path.rsplit('/').next().unwrap_or(human_path);
    let text = match read_text(store, human_path) {
        Ok(t) => t,
        Err(_) => {
            err_line(
                sess,
                &alloc::format!("  bad  {}  (read {})", mod_rel, human_name),
            );
            return 1;
        }
    };
    let cat = match import_catalog(impl_slot, &text) {
        Ok(c) => c,
        Err(_) => {
            err_line(
                sess,
                &alloc::format!("  bad  {}  (parse {})", mod_rel, human_name),
            );
            return 1;
        }
    };
    if !cat.has_border() {
        return 0;
    }
    let want = catalog_fn_names(&cat);
    let stem = human_name
        .rsplit_once('.')
        .map(|(s, _)| s)
        .unwrap_or(human_name);
    let mut errors = 0u32;
    for slot in emit_slots(&meta.impl_lang, false) {
        let face = face_rel(slot, stem);
        let fpath = join_path(mod_dir, &face);
        if !block_on(|| store.is_file(&fpath)) {
            err_line(
                sess,
                &alloc::format!(
                    "  bad  {}  missing {} face for {} ({} fns)",
                    mod_rel,
                    slot,
                    human_name,
                    want.len()
                ),
            );
            errors += 1;
            continue;
        }
        let ftext = match read_text(store, &fpath) {
            Ok(t) => t,
            Err(_) => {
                err_line(sess, &alloc::format!("  bad  {}  (read {})", mod_rel, face));
                errors += 1;
                continue;
            }
        };
        let fcat = match import_catalog(slot, &ftext) {
            Ok(c) => c,
            Err(_) => {
                err_line(
                    sess,
                    &alloc::format!("  bad  {}  (parse face {})", mod_rel, face),
                );
                errors += 1;
                continue;
            }
        };
        let got = catalog_fn_names(&fcat);
        if got != want {
            let mut missing = Vec::new();
            let mut extra = Vec::new();
            for n in &want {
                if !got.iter().any(|g| g == n) {
                    missing.push(n.clone());
                }
            }
            for n in &got {
                if !want.iter().any(|w| w == n) {
                    extra.push(n.clone());
                }
            }
            err_line(
                sess,
                &alloc::format!(
                    "  bad  {}  {} vs {} (stem {}): missing {:?} extra {:?}",
                    mod_rel,
                    slot,
                    impl_slot,
                    stem,
                    missing,
                    extra
                ),
            );
            errors += 1;
        }
    }
    errors
}

pub fn cmd_check<S: ForgeStore, Sess: ForgeSession>(
    store: &mut S,
    sess: &mut Sess,
    metal_root: &str,
) -> i32 {
    let roots = default_src_roots(metal_root);
    let mods = discover_modules(store, &roots);
    let mut errors = 0;
    for mod_dir in &mods {
        match load_meta(store, mod_dir) {
            Ok(meta) => {
                if impl_ext(&meta.impl_lang).is_none() {
                    err_line(
                        sess,
                        &alloc::format!("  bad  {}  (bad impl)", mod_dir),
                    );
                    errors += 1;
                    continue;
                }
                let sources = impl_sources(store, mod_dir, &meta.impl_lang);
                if sources.is_empty() {
                    err_line(
                        sess,
                        &alloc::format!("  bad  {}  (no human sources)", mod_dir),
                    );
                    errors += 1;
                    continue;
                }
                let rel_mod = path_to_py_name(&mod_dir_rel(mod_dir, &roots));
                let mut face_errs = 0u32;
                for rel in &sources {
                    face_errs +=
                        check_face_symmetry(store, sess, mod_dir, &rel_mod, rel, &meta);
                }
                errors += face_errs as i32;
                if face_errs == 0 {
                    out_line(
                        sess,
                        &alloc::format!(
                            "  ok  {}  impl={}  stems={}  faces=c/rs/py",
                            rel_mod,
                            meta.impl_lang,
                            sources.len()
                        ),
                    );
                } else {
                    err_line(
                        sess,
                        &alloc::format!(
                            "  bad  {}  impl={}  face symmetry ({} error(s))",
                            rel_mod,
                            meta.impl_lang,
                            face_errs
                        ),
                    );
                }
            }
            Err(_) => {
                err_line(sess, &alloc::format!("  bad  {}  (meta)", mod_dir));
                errors += 1;
            }
        }
    }
    out_line(
        sess,
        &alloc::format!("forge mod check: {} module(s)", mods.len()),
    );
    let code = if errors > 0 { 1 } else { 0 };
    sess.set_exit(code);
    code
}

pub fn cmd_clean<S: ForgeStore, Sess: ForgeSession>(
    store: &mut S,
    sess: &mut Sess,
    metal_root: &str,
) -> i32 {
    let roots = default_src_roots(metal_root);
    let mods = discover_modules(store, &roots);
    let mut n = 0;
    // stem_id -> (kind, slot -> status)
    let mut by_stem: BTreeMap<String, (String, BTreeMap<String, String>)> = BTreeMap::new();
    for mod_dir in &mods {
        for rel in _gitignore::list_generated_rels(store, mod_dir) {
            if !remove_marked(store, mod_dir, &rel) {
                continue;
            }
            n += 1;
            let stem = rel.rsplit_once('.').map(|(s, _)| s).unwrap_or(rel.as_str());
            let Some(slot) = slot_from_path(&rel) else {
                continue;
            };
            let id = stem_id(mod_dir, stem, &roots);
            let kind = String::from(stem_kind(stem));
            let entry = by_stem
                .entry(id)
                .or_insert_with(|| (kind, BTreeMap::new()));
            entry.1.insert(String::from(slot), String::from("pruned"));
        }
        // Do not touch .gitignore — sync owns that; clean only removes faces.
    }
    let rows: Vec<(String, String, BTreeMap<String, String>)> = by_stem
        .into_iter()
        .map(|(id, (kind, st))| (id, kind, st))
        .collect();
    if !rows.is_empty() {
        out_line(
            sess,
            &format_pool_table(
                &rows,
                "kind=d|f (d=__init__; f=sibling)  x=removed  -=noop  §=original  *=emitted  .=fresh  ~=marker",
            ),
        );
    }
    out_line(
        sess,
        &alloc::format!(
            "forge mod clean: removed {} generated file(s) in {} module(s)",
            n,
            mods.len()
        ),
    );
    sess.set_exit(0);
    0
}

pub fn cmd_ls<S: ForgeStore, Sess: ForgeSession>(
    store: &mut S,
    sess: &mut Sess,
    metal_root: &str,
) -> i32 {
    let roots = default_src_roots(metal_root);
    let mods = discover_modules(store, &roots);
    for mod_dir in &mods {
        out_line(
            sess,
            &alloc::format!("{}/", path_to_py_name(&mod_dir_rel(mod_dir, &roots))),
        );
        if let Ok(names) = block_on(|| store.list_dir(mod_dir)) {
            let mut names = names;
            names.sort();
            for name in names {
                if name.starts_with('.') || name == "target" {
                    continue;
                }
                let path = join_path(mod_dir, &name);
                if block_on(|| store.is_file(&path)) {
                    if let Ok(text) = read_text(store, &path) {
                        if content_has_banner(&text) {
                            continue;
                        }
                    }
                }
                let slash = if block_on(|| store.is_dir(&path)) {
                    "/"
                } else {
                    ""
                };
                out_line(sess, &alloc::format!("|-- {}{}", name, slash));
            }
        }
        out_line(sess, "");
    }
    out_line(
        sess,
        &alloc::format!(
            "forge mod ls: {} module(s) (generated hidden)",
            mods.len()
        ),
    );
    sess.set_exit(0);
    0
}
