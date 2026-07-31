//! Subcommand dispatch over Store + Session (no clap).

use alloc::string::String;

use crate::_port::{ForgeSession, ForgeStore};
use crate::_sync::{cmd_check, cmd_clean, cmd_convert, cmd_ls, cmd_sync};

fn find_flag_value(sess: &dyn ForgeSession, long: &str) -> Option<String> {
    let n = sess.arg_count();
    let mut i = 0;
    let eq = alloc::format!("{}=", long);
    while i < n {
        match sess.arg(i) {
            Some(a) if a == long => {
                return sess.arg(i + 1).map(String::from);
            }
            Some(a) if a.starts_with(eq.as_str()) => {
                return Some(String::from(&a[eq.len()..]));
            }
            _ => {}
        }
        i += 1;
    }
    None
}

fn has_flag(sess: &dyn ForgeSession, name: &str) -> bool {
    let n = sess.arg_count();
    for i in 0..n {
        if sess.arg(i) == Some(name) {
            return true;
        }
    }
    false
}

fn usage_lines() -> [&'static str; 13] {
    [
        "forge - Metal module codegen + image builders + firmware build (solo host tool)",
        "",
        "Usage:",
        "  forge mod sync [--emit toml] [--force] [--metal-root DIR]",
        "  forge mod check|clean|ls [--metal-root DIR]",
        "  forge convert SRC DST [--force]",
        "  forge img mtar|fat|zip|embed|nest|rootfs|littlefs ...",
        "  forge config edit|gen|old [--metal-root DIR]",
        "  forge build [bios|efi|all] [--stress] [--metal-root DIR]",
        "  forge run   [bios|efi|all] [--metal-root DIR]",
        "  forge stress [--metal-root DIR]",
        "  forge version | --version | -V",
        "",
    ]
}

/// Run CLI against the given store/session. Returns process exit code.
pub fn run<S: ForgeStore, Sess: ForgeSession>(
    store: &mut S,
    sess: &mut Sess,
    default_metal_root: &str,
) -> i32 {
    let metal_root = find_flag_value(sess, "--metal-root")
        .unwrap_or_else(|| String::from(default_metal_root));
    let force = has_flag(sess, "--force");

    let a0 = String::from(sess.arg(0).unwrap_or(""));
    if a0 == "version" || a0 == "--version" || a0 == "-V" {
        let _ = crate::_port::block_on(|| {
            sess.out_line(crate::pm_metal_forge_version_str())
        });
        sess.set_exit(0);
        return 0;
    }
    if a0 == "-h" || a0 == "--help" || a0.is_empty() {
        for line in usage_lines() {
            if line.is_empty() {
                continue;
            }
            let _ = crate::_port::block_on(|| sess.out_line(line));
        }
        sess.set_exit(if a0.is_empty() { 1 } else { 0 });
        return sess.exit_code();
    }
    if a0 == "convert" {
        let src = String::from(sess.arg(1).unwrap_or(""));
        let dst = String::from(sess.arg(2).unwrap_or(""));
        if src.is_empty() || dst.is_empty() {
            let _ = crate::_port::block_on(|| {
                sess.err_line("forge convert: need SRC DST")
            });
            sess.set_exit(2);
            return 2;
        }
        return cmd_convert(store, sess, &src, &dst, force);
    }
    if a0 == "config" {
        #[cfg(feature = "solo")]
        {
            return crate::_config::run_config(sess, &metal_root);
        }
        #[cfg(not(feature = "solo"))]
        {
            let _ = crate::_port::block_on(|| sess.err_line("forge config: need solo feature"));
            sess.set_exit(2);
            return 2;
        }
    }
    if a0 == "build" {
        #[cfg(feature = "solo")]
        {
            return crate::_build::run_build(sess, &metal_root);
        }
        #[cfg(not(feature = "solo"))]
        {
            let _ = crate::_port::block_on(|| sess.err_line("forge build: need solo feature"));
            sess.set_exit(2);
            return 2;
        }
    }
    if a0 == "run" {
        #[cfg(feature = "solo")]
        {
            return crate::_run::run_qemu(sess, &metal_root);
        }
        #[cfg(not(feature = "solo"))]
        {
            let _ = crate::_port::block_on(|| sess.err_line("forge run: need solo feature"));
            sess.set_exit(2);
            return 2;
        }
    }
    if a0 == "stress" {
        #[cfg(feature = "solo")]
        {
            return crate::_run::run_stress(sess, &metal_root);
        }
        #[cfg(not(feature = "solo"))]
        {
            let _ = crate::_port::block_on(|| sess.err_line("forge stress: need solo feature"));
            sess.set_exit(2);
            return 2;
        }
    }
    if a0 == "img" {
        #[cfg(feature = "builders")]
        {
            return crate::_img::run_img(store, sess, &metal_root);
        }
        #[cfg(not(feature = "builders"))]
        {
            let _ = crate::_port::block_on(|| {
                sess.err_line("forge img: rebuild forge-cli with feature builders")
            });
            sess.set_exit(2);
            return 2;
        }
    }
    if a0 != "mod" {
        let _ = crate::_port::block_on(|| {
            sess.err_line(
                "forge: expected mod|convert|img|config|build|run|stress (see forge --help)",
            )
        });
        sess.set_exit(2);
        return 2;
    }
    let a1 = String::from(sess.arg(1).unwrap_or(""));
    let mut extra_toml = false;
    let n = sess.arg_count();
    for i in 2..n {
        if sess.arg(i) == Some("--emit") && sess.arg(i + 1) == Some("toml") {
            extra_toml = true;
        }
        if matches!(sess.arg(i), Some(a) if a == "--emit=toml") {
            extra_toml = true;
        }
    }
    match a1.as_str() {
        "sync" => cmd_sync(store, sess, &metal_root, extra_toml, force),
        "check" => cmd_check(store, sess, &metal_root),
        "clean" => cmd_clean(store, sess, &metal_root),
        "ls" => cmd_ls(store, sess, &metal_root),
        "-h" | "--help" | "" => {
            for line in usage_lines() {
                if line.is_empty() {
                    continue;
                }
                let _ = crate::_port::block_on(|| sess.out_line(line));
            }
            sess.set_exit(1);
            1
        }
        other => {
            let msg = alloc::format!("forge mod: unknown subcommand {:?}", other);
            let _ = crate::_port::block_on(|| sess.err_line(&msg));
            sess.set_exit(2);
            2
        }
    }
}
