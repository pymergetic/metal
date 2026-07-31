//! forge config edit|gen|old — spawn private Kconfiglib scripts.

use alloc::string::String;
use std::path::PathBuf;
use std::process::Command;

use crate::_port::{block_on, ForgeSession};

fn script(metal_root: &str, name: &str) -> PathBuf {
    PathBuf::from(metal_root)
        .join("src/pymergetic/metal/forge/_kconfig")
        .join(name)
}

fn py(metal_root: &str, name: &str) -> i32 {
    let path = script(metal_root, name);
    if !path.is_file() {
        eprintln!("forge config: missing {}", path.display());
        return 2;
    }
    match Command::new("python3")
        .arg(&path)
        .env("METAL_ROOT", metal_root)
        .status()
    {
        Ok(s) => s.code().unwrap_or(1),
        Err(_) => 2,
    }
}

/// Used by forge build before rootfs pack.
pub fn config_gen(metal_root: &str) -> i32 {
    py(metal_root, "confgen.py")
}

pub fn run_config(sess: &mut dyn ForgeSession, metal_root: &str) -> i32 {
    let a1 = String::from(sess.arg(1).unwrap_or(""));
    if a1.is_empty() || a1 == "-h" || a1 == "--help" {
        for line in [
            "forge config - Kconfig",
            "  forge config edit|gen|old [--metal-root DIR]",
        ] {
            let _ = block_on(|| sess.out_line(line));
        }
        sess.set_exit(1);
        return 1;
    }
    let code = match a1.as_str() {
        "edit" => py(metal_root, "menuconfig.py"),
        "gen" => config_gen(metal_root),
        "old" => py(metal_root, "oldconfig.py"),
        other => {
            let msg = alloc::format!("forge config: unknown {other:?}");
            let _ = block_on(|| sess.err_line(&msg));
            2
        }
    };
    sess.set_exit(code);
    code
}
