//! Linux solo forge binary (hidden module `forge/cli`).
//! Wires solo FS/stdio port + checkout root discovery; codegen lives in
//! `pymergetic_metal_forge`. Launched via package-root `./forge-cli`.

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
    for _ in 0..16 {
        let launcher = p.join("forge-cli");
        if p.join("src/pymergetic/metal").is_dir() && launcher.is_file() {
            return Some(p.to_string_lossy().into_owned());
        }
        if !p.pop() {
            break;
        }
    }
    None
}
