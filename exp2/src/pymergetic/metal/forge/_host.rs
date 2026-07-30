//! Thin host process helpers for forge solo tooling (build / run / config).

use alloc::string::String;
use alloc::vec::Vec;
use std::path::{Path, PathBuf};
use std::process::{Command, Output, Stdio};

pub fn which(cmd: &str) -> Option<PathBuf> {
    let out = Command::new("sh")
        .args(["-c", &alloc::format!("command -v {}", cmd)])
        .output()
        .ok()?;
    if !out.status.success() {
        return None;
    }
    let s = String::from_utf8_lossy(&out.stdout).trim().to_string();
    if s.is_empty() {
        None
    } else {
        Some(PathBuf::from(s))
    }
}

pub fn run(cmd: &mut Command) -> Result<(), String> {
    let status = cmd.status().map_err(|e| alloc::format!("spawn: {e}"))?;
    if status.success() {
        Ok(())
    } else {
        Err(alloc::format!("failed: {:?}", cmd))
    }
}

pub fn output(cmd: &mut Command) -> Result<Output, String> {
    cmd.output().map_err(|e| alloc::format!("spawn: {e}"))
}

pub fn run_args(bin: impl AsRef<Path>, args: &[&str]) -> Result<(), String> {
    run(Command::new(bin.as_ref()).args(args))
}

pub fn sess_flag(sess: &dyn crate::_port::ForgeSession, name: &str) -> bool {
    (0..sess.arg_count()).any(|i| sess.arg(i) == Some(name))
}

pub fn sess_positional(sess: &dyn crate::_port::ForgeSession, from: usize, default: &str) -> String {
    for i in from..sess.arg_count() {
        if let Some(a) = sess.arg(i) {
            if !a.starts_with('-') {
                return String::from(a);
            }
        }
    }
    String::from(default)
}

pub fn null_stdio(cmd: &mut Command) -> &mut Command {
    cmd.stdout(Stdio::null()).stderr(Stdio::null())
}

/// Collect argv from a `Command` for error messages (Debug is enough elsewhere).
pub fn ensure_file(path: &Path, hint: &str) -> Result<(), String> {
    if path.is_file() {
        Ok(())
    } else {
        Err(alloc::format!("missing {} ({})", path.display(), hint))
    }
}

pub fn ensure_dir(path: &Path) -> Result<(), String> {
    std::fs::create_dir_all(path).map_err(|_| alloc::format!("mkdir {}", path.display()))
}

pub fn push_flags(dst: &mut Vec<String>, flags: &[&str]) {
    for f in flags {
        dst.push(String::from(*f));
    }
}

pub fn push_includes(dst: &mut Vec<String>, dirs: &[PathBuf]) {
    for d in dirs {
        dst.push(alloc::format!("-I{}", d.display()));
    }
}
