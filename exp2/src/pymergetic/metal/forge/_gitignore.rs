//! Managed `# BEGIN metal-generated` … `# END metal-generated` block in module `.gitignore`.

use alloc::string::String;
use alloc::vec;
use alloc::vec::Vec;

use crate::_banner::content_has_banner;
use crate::_meta::join_path;
use crate::_pool::ENTRY_STEM;
use crate::_port::{block_on, ForgeStore};

pub const BEGIN: &str = "# BEGIN metal-generated";
pub const END: &str = "# END metal-generated";

fn read_text<S: ForgeStore>(store: &mut S, path: &str) -> Result<String, ()> {
    let bytes = block_on(|| store.read_file(path)).map_err(|_| ())?;
    String::from_utf8(bytes).map_err(|_| ())
}

fn write_text<S: ForgeStore>(store: &mut S, path: &str, text: &str) -> Result<(), ()> {
    block_on(|| store.write_file(path, text.as_bytes())).map_err(|_| ())
}

fn is_private_face_name(name: &str) -> bool {
    let stem = name.rsplit_once('.').map(|(s, _)| s).unwrap_or(name);
    // Package entry `__init__.*` is public; other `_*.*` stems stay private.
    stem != ENTRY_STEM && stem.starts_with('_')
}

/// Top-level files in `mod_dir` that carry the ownership banner.
///
/// Matches Python `list_generated_rels`: banner-owned faces only. Private
/// `_foo.*` stems are skipped; `__init__.*` is not.
pub fn list_generated_rels<S: ForgeStore>(store: &mut S, mod_dir: &str) -> Vec<String> {
    let names = match block_on(|| store.list_dir(mod_dir)) {
        Ok(n) => n,
        Err(_) => return Vec::new(),
    };
    let mut out = Vec::new();
    for name in names {
        if is_private_face_name(&name) {
            continue;
        }
        let path = join_path(mod_dir, &name);
        if block_on(|| store.is_file(&path)) {
            if let Ok(text) = read_text(store, &path) {
                if content_has_banner(&text) {
                    out.push(name);
                }
            }
        }
    }
    out.sort();
    out
}

/// Rewrite the managed ignore block from banner-owned faces on disk.
pub fn update<S: ForgeStore>(store: &mut S, mod_dir: &str) {
    let generated = list_generated_rels(store, mod_dir);
    let mut patterns = vec![
        String::from(".target/"),
        String::from("target/"),
        String::from(".generated/"),
    ];
    for rel in generated {
        if !patterns.iter().any(|p| p == &rel) {
            patterns.push(rel);
        }
    }
    let mut block = String::from(BEGIN);
    block.push('\n');
    for p in &patterns {
        block.push_str(p);
        block.push('\n');
    }
    block.push_str(END);
    block.push('\n');

    let gi = join_path(mod_dir, ".gitignore");
    let text = if block_on(|| store.is_file(&gi)) {
        let mut text = read_text(store, &gi).unwrap_or_default();
        if text.contains(BEGIN) && text.contains(END) {
            let pre = text.split(BEGIN).next().unwrap_or("");
            let post = text.split(END).nth(1).unwrap_or("");
            let post = post.strip_prefix('\n').unwrap_or(post);
            text = alloc::format!("{}{}{}", pre, block, post);
        } else {
            if !text.is_empty() && !text.ends_with('\n') {
                text.push('\n');
            }
            text.push_str(&block);
        }
        text
    } else {
        block
    };
    let _ = write_text(store, &gi, &text);
}

/// Remove the managed block; delete `.gitignore` if empty afterward.
pub fn clear<S: ForgeStore>(store: &mut S, mod_dir: &str) {
    let gi = join_path(mod_dir, ".gitignore");
    if !block_on(|| store.is_file(&gi)) {
        return;
    }
    let Ok(text) = read_text(store, &gi) else {
        return;
    };
    if !text.contains(BEGIN) || !text.contains(END) {
        return;
    }
    let pre = text.split(BEGIN).next().unwrap_or("");
    let post = text.split(END).nth(1).unwrap_or("");
    let post = post.strip_prefix('\n').unwrap_or(post);
    let new_text = alloc::format!("{}{}", pre, post);
    if new_text.trim().is_empty() {
        let _ = block_on(|| store.remove_file(&gi));
    } else {
        let _ = write_text(store, &gi, &new_text);
    }
}
