//! Dumb Linux forge app (outside the Metal module tree).
//! Wires solo FS/stdio port + checkout root discovery; all codegen lives in
//! `pymergetic_metal_forge`.

use pymergetic_metal_forge::{run_cli, SoloSession, SoloStore};
use std::path::PathBuf;

fn main() {
    let mut store = SoloStore::new();
    let mut sess = SoloSession::from_env();
    let root = guess_metal_root();
    let code = run_cli(&mut store, &mut sess, &root);
    std::process::exit(code);
}

fn guess_metal_root() -> String {
    if let Ok(exe) = std::env::current_exe() {
        if let Some(r) = walk_for_root(exe) {
            return r;
        }
    }
    if let Ok(cwd) = std::env::current_dir() {
        if let Some(r) = walk_for_root(cwd) {
            return r;
        }
    }
    String::from(".")
}

fn walk_for_root(mut p: PathBuf) -> Option<String> {
    for _ in 0..12 {
        if p.join("tools/metal").is_dir() && p.join("exp2").is_dir() {
            return Some(p.to_string_lossy().into_owned());
        }
        if !p.pop() {
            break;
        }
    }
    None
}
