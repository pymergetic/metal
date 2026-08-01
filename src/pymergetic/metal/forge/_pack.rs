//! forge pack — compile `type=package` modules to `.wasm` (not kernel-linked).
//!
//! W6: `impl=rs` via cargo `wasm32-unknown-unknown` cdylib;
//! `impl=c` via clang + wasm-ld.

use alloc::string::String;
use alloc::vec::Vec;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

use crate::_host::{ensure_dir, run, sess_positional, which};
use crate::_meta::{parse_module_json, ModuleMeta, ModuleType};
use crate::_port::{block_on, ForgeSession};

const WASM_TARGET: &str = "wasm32-unknown-unknown";

fn out_line(sess: &mut dyn ForgeSession, s: &str) {
    let _ = block_on(|| sess.out_line(s));
}

fn err_line(sess: &mut dyn ForgeSession, s: &str) {
    let _ = block_on(|| sess.err_line(s));
}

fn flag_value(sess: &dyn ForgeSession, long: &str) -> Option<String> {
    let n = sess.arg_count();
    let eq = alloc::format!("{long}=");
    let mut i = 0;
    while i < n {
        match sess.arg(i) {
            Some(a) if a == long => return sess.arg(i + 1).map(String::from),
            Some(a) if a.starts_with(eq.as_str()) => return Some(String::from(&a[eq.len()..])),
            _ => {}
        }
        i += 1;
    }
    None
}

fn usage(sess: &mut dyn ForgeSession) {
    for line in [
        "Usage:",
        "  forge pack DIR|NAME [-o OUT.wasm] [--metal-root DIR]",
        "  forge pack all [--metal-root DIR]",
        "",
        "Packs live under tests/ (type=package). Output default:",
        "  build/packs/<name>.wasm",
    ] {
        out_line(sess, line);
    }
}

fn load_meta_path(mod_dir: &Path) -> Result<ModuleMeta, String> {
    let p = mod_dir.join(".pm/module");
    let text = fs::read_to_string(&p).map_err(|e| alloc::format!("read {}: {e}", p.display()))?;
    parse_module_json(&text).map_err(|_| alloc::format!("bad .pm/module in {}", mod_dir.display()))
}

fn discover_packages(metal_root: &str) -> Result<Vec<PathBuf>, String> {
    let tests = PathBuf::from(metal_root).join("tests");
    if !tests.is_dir() {
        return Ok(Vec::new());
    }
    let mut out = Vec::new();
    walk_packages(&tests, &mut out)?;
    out.sort();
    Ok(out)
}

fn walk_packages(dir: &Path, out: &mut Vec<PathBuf>) -> Result<(), String> {
    let entries = fs::read_dir(dir).map_err(|e| alloc::format!("read_dir {}: {e}", dir.display()))?;
    for ent in entries {
        let ent = ent.map_err(|e| alloc::format!("readdir: {e}"))?;
        let path = ent.path();
        if !path.is_dir() {
            continue;
        }
        let name = ent.file_name();
        let name = name.to_string_lossy();
        if name.starts_with('.') {
            continue;
        }
        let module = path.join(".pm/module");
        if module.is_file() {
            match load_meta_path(&path) {
                Ok(m) if m.ty == ModuleType::Package => out.push(path),
                Ok(_) => {}
                Err(e) => return Err(e),
            }
        } else {
            walk_packages(&path, out)?;
        }
    }
    Ok(())
}

fn resolve_package(metal_root: &str, spec: &str) -> Result<PathBuf, String> {
    let as_path = PathBuf::from(spec);
    let candidates = [
        as_path.clone(),
        PathBuf::from(metal_root).join(spec),
        PathBuf::from(metal_root).join("tests").join(spec),
    ];
    for c in &candidates {
        if c.join(".pm/module").is_file() {
            let meta = load_meta_path(c)?;
            if meta.ty != ModuleType::Package {
                return Err(alloc::format!("{}: not type=package", c.display()));
            }
            return Ok(c.canonicalize().unwrap_or_else(|_| c.clone()));
        }
    }
    for p in discover_packages(metal_root)? {
        let meta = load_meta_path(&p)?;
        if meta.name == spec {
            return Ok(p);
        }
    }
    Err(alloc::format!("forge pack: package not found: {spec}"))
}

fn default_out(metal_root: &str, name: &str) -> PathBuf {
    PathBuf::from(metal_root)
        .join("build/packs")
        .join(alloc::format!("{name}.wasm"))
}

fn crate_name_from_manifest(manifest: &Path) -> Result<String, String> {
    let text = fs::read_to_string(manifest).map_err(|e| alloc::format!("read Cargo.toml: {e}"))?;
    for line in text.lines() {
        let t = line.trim();
        if let Some(rest) = t.strip_prefix("name") {
            let rest = rest.trim().trim_start_matches('=').trim();
            if rest.starts_with('"') {
                let v = rest.trim_matches('"');
                if !v.is_empty() {
                    return Ok(String::from(v.replace('-', "_")));
                }
            }
        }
    }
    Err(String::from("Cargo.toml: missing [package] name"))
}

fn pack_rs(mod_dir: &Path, out: &Path) -> Result<(), String> {
    let manifest = mod_dir.join(".pm/Cargo.toml");
    if !manifest.is_file() {
        return Err(alloc::format!("missing {}", manifest.display()));
    }
    let target_dir = mod_dir.join(".pm/.target");
    ensure_dir(&target_dir)?;

    let mut cmd = Command::new("cargo");
    cmd.args([
        "build",
        "--manifest-path",
        manifest.to_str().ok_or("manifest path")?,
        "--release",
        "--target",
        WASM_TARGET,
    ]);
    cmd.env("CARGO_TARGET_DIR", &target_dir);
    run(&mut cmd)?;

    let crate_name = crate_name_from_manifest(&manifest)?;
    let artifact = target_dir
        .join(WASM_TARGET)
        .join("release")
        .join(alloc::format!("{crate_name}.wasm"));
    if !artifact.is_file() {
        return Err(alloc::format!("missing wasm artifact {}", artifact.display()));
    }
    if let Some(parent) = out.parent() {
        ensure_dir(parent)?;
    }
    fs::copy(&artifact, out).map_err(|e| alloc::format!("copy wasm: {e}"))?;
    Ok(())
}

fn find_wasm_ld() -> Result<PathBuf, String> {
    if let Some(p) = which("wasm-ld") {
        return Ok(p);
    }
    if let Some(p) = which("wasm-ld-18") {
        return Ok(p);
    }
    /* rustup ships wasm-ld under rustlib/.../gcc-ld/ */
    let out = Command::new("rustc")
        .args(["--print", "sysroot"])
        .output()
        .map_err(|e| alloc::format!("rustc --print sysroot: {e}"))?;
    if !out.status.success() {
        return Err(String::from("rustc --print sysroot failed"));
    }
    let sys = String::from_utf8_lossy(&out.stdout).trim().to_string();
    let root = PathBuf::from(sys).join("lib/rustlib");
    if let Ok(walker) = fs::read_dir(&root) {
        for host in walker.flatten() {
            let cand = host.path().join("bin/gcc-ld/wasm-ld");
            if cand.is_file() {
                return Ok(cand);
            }
        }
    }
    Err(String::from(
        "forge pack: need wasm-ld (install lld, or rustup wasm target)",
    ))
}

fn pack_c(metal_root: &str, mod_dir: &Path, out: &Path) -> Result<(), String> {
    let src = mod_dir.join("__init__.c");
    if !src.is_file() {
        return Err(alloc::format!("missing {}", src.display()));
    }
    let wasm_ld = find_wasm_ld()?;
    let work = mod_dir.join(".pm/.target");
    ensure_dir(&work)?;
    let obj = work.join("pack.o");
    let libc = PathBuf::from(metal_root).join("src/pymergetic/metal/libc");
    if !libc.join("stdint.h").is_file() {
        return Err(alloc::format!("missing metal libc at {}", libc.display()));
    }

    let mut cc = Command::new("clang");
    cc.args([
        "--target=wasm32",
        "-ffreestanding",
        "-nostdlib",
        "-nostdinc",
        "-O2",
        "-c",
    ]);
    cc.arg(alloc::format!("-I{}", libc.display()));
    cc.arg(src.to_str().ok_or("src path")?);
    cc.arg("-o");
    cc.arg(obj.to_str().ok_or("obj path")?);
    run(&mut cc)?;

    if let Some(parent) = out.parent() {
        ensure_dir(parent)?;
    }

    let mut link = Command::new("clang");
    link.args([
        "--target=wasm32",
        "-nostdlib",
        "-Wl,--no-entry",
        "-Wl,--export=ready",
        "-Wl,--allow-undefined",
    ]);
    link.arg(alloc::format!("-fuse-ld={}", wasm_ld.display()));
    link.arg("-o");
    link.arg(out);
    link.arg(&obj);
    run(&mut link)?;
    Ok(())
}

fn pack_one(metal_root: &str, mod_dir: &Path, out_opt: Option<&Path>) -> Result<PathBuf, String> {
    let meta = load_meta_path(mod_dir)?;
    if meta.ty != ModuleType::Package {
        return Err(alloc::format!("{}: not type=package", mod_dir.display()));
    }
    if meta.name.is_empty() {
        return Err(String::from("package .pm/module missing name"));
    }
    let out = match out_opt {
        Some(p) => p.to_path_buf(),
        None => default_out(metal_root, &meta.name),
    };
    match meta.impl_lang.as_str() {
        "rs" => pack_rs(mod_dir, &out)?,
        "c" | "cpp" => pack_c(metal_root, mod_dir, &out)?,
        other => {
            return Err(alloc::format!("forge pack: unsupported impl={other}"));
        }
    }
    Ok(out)
}

/// Pack every `type=package` under tests/. Returns count.
pub fn pack_all(metal_root: &str) -> Result<usize, String> {
    let pkgs = discover_packages(metal_root)?;
    if pkgs.is_empty() {
        return Ok(0);
    }
    let mut n = 0;
    for p in pkgs {
        let out = pack_one(metal_root, &p, None)?;
        eprintln!("forge pack: {} -> {}", p.display(), out.display());
        n += 1;
    }
    Ok(n)
}

/// CLI: `forge pack ...`
pub fn run_pack(sess: &mut dyn ForgeSession, metal_root: &str) -> i32 {
    let spec = sess_positional(sess, 1, "");
    if spec.is_empty() || spec == "-h" || spec == "--help" {
        usage(sess);
        sess.set_exit(if spec.is_empty() { 1 } else { 0 });
        return sess.exit_code();
    }

    let out_path = flag_value(sess, "-o").map(PathBuf::from);

    if spec == "all" {
        if out_path.is_some() {
            err_line(sess, "forge pack all: -o not supported (per-pack defaults)");
            sess.set_exit(2);
            return 2;
        }
        match discover_packages(metal_root) {
            Ok(pkgs) if pkgs.is_empty() => {
                err_line(sess, "forge pack all: no type=package under tests/");
                sess.set_exit(1);
                return 1;
            }
            Ok(_pkgs) => {
                match pack_all(metal_root) {
                    Ok(n) => {
                        out_line(sess, &alloc::format!("forge pack: {n} package(s)"));
                        sess.set_exit(0);
                        return 0;
                    }
                    Err(e) => {
                        err_line(sess, &alloc::format!("forge pack: {e}"));
                        sess.set_exit(1);
                        return 1;
                    }
                }
            }
            Err(e) => {
                err_line(sess, &e);
                sess.set_exit(1);
                return 1;
            }
        }
    }

    match resolve_package(metal_root, &spec)
        .and_then(|dir| pack_one(metal_root, &dir, out_path.as_deref()))
    {
        Ok(out) => {
            out_line(sess, &alloc::format!("forge pack: ok -> {}", out.display()));
            sess.set_exit(0);
            0
        }
        Err(e) => {
            err_line(sess, &e);
            sess.set_exit(1);
            1
        }
    }
}
