//! Compile Dropbear + Metal ssh glue into libmetal_net_ssh.a (clang, no crates.io cc).
use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

fn main() {
    let manifest = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let mod_dir = manifest.join("..").canonicalize().expect("net/ssh dir");
    let metal_dir = mod_dir.join("../..").canonicalize().expect("metal dir");
    let pkg_root = mod_dir
        .join("../../../../..")
        .canonicalize()
        .expect("package root");
    let out_dir = PathBuf::from(env::var("OUT_DIR").unwrap());
    let target = env::var("TARGET").unwrap_or_default();
    let freestanding = target.contains("none") || target.contains("uefi");
    let uefi = target.contains("uefi");

    let db = pkg_root.join("external/dropbear");
    let src = db.join("src");
    if !src.join("session.h").is_file() {
        panic!(
            "missing Dropbear: run ./scripts/setup dropbear ({})",
            src.display()
        );
    }
    if !grep_file_contains(&src.join("session.h"), "DROPBEAR_METAL") {
        panic!("Dropbear missing DROPBEAR_METAL patch — re-run ./scripts/setup dropbear");
    }

    let stubs = mod_dir.join("dropbear_stubs");
    let metal_db = mod_dir.join("dropbear_metal");
    let libc = metal_dir.join("libc");
    let host_stubs = metal_dir.join("runtime/mem/host_stubs");
    let clang_res = clang_resource_include();

    let obj_dir = out_dir.join("obj");
    fs::create_dir_all(&obj_dir).expect("obj dir");

    let mut objs: Vec<PathBuf> = Vec::new();

    let db_srcs = [
        "atomicio.c",
        "buffer.c",
        "dbhelpers.c",
        "dbmalloc.c",
        "dbutil.c",
        "dbrandom.c",
        "bignum.c",
        "curve25519.c",
        "ed25519.c",
        "sk-ed25519.c",
        "signkey.c",
        "rsa.c",
        "dss.c",
        "ecc.c",
        "ecdsa.c",
        "sk-ecdsa.c",
        "ltc_prng.c",
        "crypto_desc.c",
        "gensignkey.c",
        "gened25519.c",
        "genrsa.c",
        "gendss.c",
        "queue.c",
        "compat.c",
        "fake-rfc2553.c",
        "common-session.c",
        "packet.c",
        "common-algo.c",
        "common-kex.c",
        "common-channel.c",
        "common-chansession.c",
        "termcodes.c",
        "loginrec.c",
        "tcp-accept.c",
        "listener.c",
        "process-packet.c",
        "dh_groups.c",
        "common-runopts.c",
        "circbuffer.c",
        "list.c",
        "netio.c",
        "chachapoly.c",
        "gcm.c",
        "svr-kex.c",
        "svr-auth.c",
        "sshpty.c",
        "svr-authpasswd.c",
        "svr-authpubkey.c",
        "svr-authsslcert.c",
        "svr-session.c",
        "svr-service.c",
        "svr-chansession.c",
        "svr-runopts.c",
        "svr-tcpfwd.c",
    ];

    for f in db_srcs {
        let path = src.join(f);
        if !path.is_file() {
            continue;
        }
        let stem = path.file_stem().unwrap().to_string_lossy();
        let obj = obj_dir.join(format!("db-{stem}.o"));
        compile(
            &path,
            &obj,
            CompileKind::Dropbear,
            CompileCtx {
                freestanding,
                uefi,
                stubs: &stubs,
                metal_db: &metal_db,
                src: &src,
                db: &db,
                libc: &libc,
                host_stubs: &host_stubs,
                clang_res: clang_res.as_deref(),
                pkg_root: &pkg_root,
                metal_dir: &metal_dir,
                mod_dir: &mod_dir,
                ltc: false,
            },
        );
        objs.push(obj);
    }

    /* libtommath */
    let mut ltm: Vec<PathBuf> = fs::read_dir(db.join("libtommath"))
        .expect("libtommath")
        .filter_map(|e| e.ok())
        .map(|e| e.path())
        .filter(|p| {
            p.extension().and_then(|x| x.to_str()) == Some("c")
                && p.file_name()
                    .and_then(|n| n.to_str())
                    .map(|n| n.starts_with("bn_"))
                    .unwrap_or(false)
        })
        .collect();
    ltm.sort();
    for path in ltm {
        let stem = path.file_stem().unwrap().to_string_lossy();
        let obj = obj_dir.join(format!("ltm-{stem}.o"));
        compile(
            &path,
            &obj,
            CompileKind::Dropbear,
            CompileCtx {
                freestanding,
                uefi,
                stubs: &stubs,
                metal_db: &metal_db,
                src: &src,
                db: &db,
                libc: &libc,
                host_stubs: &host_stubs,
                clang_res: clang_res.as_deref(),
                pkg_root: &pkg_root,
                metal_dir: &metal_dir,
                mod_dir: &mod_dir,
                ltc: false,
            },
        );
        objs.push(obj);
    }

    /* libtomcrypt (skip *tab.c) */
    let mut ltc_files: Vec<PathBuf> = Vec::new();
    collect_c_files(&db.join("libtomcrypt/src"), &mut ltc_files);
    ltc_files.sort();
    for path in ltc_files {
        let stem = path.file_stem().unwrap().to_string_lossy();
        if stem.ends_with("tab") {
            continue;
        }
        let hash = short_hash(&path);
        let obj = obj_dir.join(format!("ltc-{stem}-{hash}.o"));
        compile(
            &path,
            &obj,
            CompileKind::Dropbear,
            CompileCtx {
                freestanding,
                uefi,
                stubs: &stubs,
                metal_db: &metal_db,
                src: &src,
                db: &db,
                libc: &libc,
                host_stubs: &host_stubs,
                clang_res: clang_res.as_deref(),
                pkg_root: &pkg_root,
                metal_dir: &metal_dir,
                mod_dir: &mod_dir,
                ltc: true,
            },
        );
        objs.push(obj);
    }

    /* Metal glue */
    let glue = [
        "dropbear_posix.c",
        "dropbear_fd.c",
        "dropbear_crt.c",
        "ssh_dropbear.c",
        "ssh_config.c",
        "ssh_server.c",
    ];
    for f in glue {
        let path = mod_dir.join(f);
        let stem = path.file_stem().unwrap().to_string_lossy();
        let obj = obj_dir.join(format!("glue-{stem}.o"));
        compile(
            &path,
            &obj,
            CompileKind::Glue,
            CompileCtx {
                freestanding,
                uefi,
                stubs: &stubs,
                metal_db: &metal_db,
                src: &src,
                db: &db,
                libc: &libc,
                host_stubs: &host_stubs,
                clang_res: clang_res.as_deref(),
                pkg_root: &pkg_root,
                metal_dir: &metal_dir,
                mod_dir: &mod_dir,
                ltc: false,
            },
        );
        objs.push(obj);
    }

    let lib = out_dir.join("libmetal_net_ssh.a");
    let ar = env::var("AR").unwrap_or_else(|_| String::from("ar"));
    let _ = fs::remove_file(&lib);
    let mut ar_cmd = Command::new(&ar);
    ar_cmd.arg("rcs").arg(&lib);
    for o in &objs {
        ar_cmd.arg(o);
    }
    run(&mut ar_cmd, "ar rcs");

    redefine_symbols(&ar, &lib, &out_dir);

    println!("cargo:rustc-link-search=native={}", out_dir.display());
    println!("cargo:rustc-link-lib=static=metal_net_ssh");
    println!("cargo:rerun-if-changed={}", mod_dir.join("dropbear_posix.c").display());
    println!("cargo:rerun-if-changed={}", mod_dir.join("dropbear_fd.c").display());
    println!("cargo:rerun-if-changed={}", mod_dir.join("ssh_dropbear.c").display());
    println!("cargo:rerun-if-changed={}", mod_dir.join("ssh_config.c").display());
    println!("cargo:rerun-if-changed={}", mod_dir.join("ssh_server.c").display());
    println!("cargo:rerun-if-changed={}", metal_db.join("localoptions.h").display());
    println!("cargo:rerun-if-changed={}", src.join("session.h").display());
}

struct CompileCtx<'a> {
    freestanding: bool,
    uefi: bool,
    stubs: &'a Path,
    metal_db: &'a Path,
    src: &'a Path,
    db: &'a Path,
    libc: &'a Path,
    host_stubs: &'a Path,
    clang_res: Option<&'a Path>,
    pkg_root: &'a Path,
    metal_dir: &'a Path,
    mod_dir: &'a Path,
    ltc: bool,
}

enum CompileKind {
    Dropbear,
    Glue,
}

fn compile(src: &Path, obj: &Path, kind: CompileKind, ctx: CompileCtx<'_>) {
    let mut cmd = Command::new("clang");
    cmd.arg("-c").arg(src).arg("-o").arg(obj);
    cmd.args([
        "-std=gnu11",
        "-ffreestanding",
        "-fno-stack-protector",
        "-Os",
        "-Wall",
        "-Wno-error",
        "-Wno-unused-parameter",
        "-Wno-unused-variable",
        "-Wno-unused-function",
        "-Wno-invalid-noreturn",
        "-Wno-sign-compare",
        "-Wno-missing-field-initializers",
        "-Wno-pointer-sign",
        "-Wno-format",
        "-fno-strict-aliasing",
        "-DDROPBEAR_SERVER=1",
        "-DDROPBEAR_CLIENT=0",
        "-DLOCALOPTIONS_H_EXISTS=1",
        "-DDROPBEAR_METAL=1",
        "-DBUNDLED_LIBTOM=1",
        "-DUSE_LTM",
        "-DLTM_DESC",
        "-U__linux__",
        "-Ulinux",
        "-U__gnu_linux__",
    ]);
    if ctx.ltc {
        cmd.arg("-DLTC_SOURCE");
    }
    if ctx.uefi {
        cmd.args([
            "--target=x86_64-unknown-windows-gnu",
            "-fshort-wchar",
            "-mno-red-zone",
            "-fPIC",
            "-U_WIN32",
            "-UWIN32",
            "-U__MINGW32__",
            "-U__MINGW64__",
        ]);
    } else if ctx.freestanding {
        cmd.args(["-m64", "-mno-red-zone", "-fno-pic", "-fno-pie"]);
    }

    match kind {
        CompileKind::Dropbear => {
            if ctx.freestanding {
                cmd.arg("-nostdinc");
                cmd.arg("-isystem").arg(ctx.stubs);
                if ctx.host_stubs.is_dir() {
                    cmd.arg("-isystem").arg(ctx.host_stubs);
                }
                if let Some(res) = ctx.clang_res {
                    cmd.arg("-isystem").arg(res);
                }
                cmd.arg("-isystem").arg(ctx.libc);
            } else {
                cmd.arg("-I").arg(ctx.stubs);
            }
            cmd.arg("-I").arg(ctx.metal_db);
            cmd.arg("-I").arg(ctx.src);
            cmd.arg("-I").arg(ctx.db.join("libtomcrypt/src/headers"));
            cmd.arg("-I").arg(ctx.db.join("libtommath"));
            cmd.arg("-I").arg(ctx.pkg_root.join("include"));
            cmd.arg("-I").arg(ctx.metal_dir);
            cmd.arg("-I").arg(ctx.pkg_root.join("src"));
        }
        CompileKind::Glue => {
            if ctx.freestanding {
                cmd.arg("-nostdinc");
                cmd.arg("-isystem").arg(ctx.stubs);
                if ctx.host_stubs.is_dir() {
                    cmd.arg("-isystem").arg(ctx.host_stubs);
                }
                if let Some(res) = ctx.clang_res {
                    cmd.arg("-isystem").arg(res);
                }
                cmd.arg("-isystem").arg(ctx.libc);
            } else {
                cmd.arg("-I").arg(ctx.stubs);
            }
            cmd.arg("-I").arg(ctx.mod_dir);
            cmd.arg("-I").arg(ctx.metal_db);
            cmd.arg("-I").arg(ctx.src);
            cmd.arg("-I").arg(ctx.pkg_root.join("src"));
            cmd.arg("-I").arg(ctx.metal_dir);
        }
    }

    run(&mut cmd, &format!("clang {}", src.display()));
}

fn redefine_symbols(ar: &str, lib: &Path, out_dir: &Path) {
    let tmp = out_dir.join("redef");
    let _ = fs::remove_dir_all(&tmp);
    fs::create_dir_all(&tmp).expect("redef dir");
    run(
        Command::new(ar).current_dir(&tmp).arg("x").arg(lib),
        "ar x",
    );
    let entries = fs::read_dir(&tmp).expect("redef read");
    for e in entries.flatten() {
        let p = e.path();
        if p.extension().and_then(|x| x.to_str()) != Some("o") {
            continue;
        }
        let newp = p.with_extension("o.new");
        run(
            Command::new("objcopy")
                .args([
                    "--redefine-sym",
                    "m_malloc=db_m_malloc",
                    "--redefine-sym",
                    "m_realloc=db_m_realloc",
                    "--redefine-sym",
                    "m_free=db_m_free",
                    "--redefine-sym",
                    "m_strdup=db_m_strdup",
                    "--redefine-sym",
                    "m_calloc=db_m_calloc",
                    "--redefine-sym",
                    "sha256_init=ltc_sha256_init",
                    "--redefine-sym",
                    "sha256_process=ltc_sha256_process",
                    "--redefine-sym",
                    "sha256_done=ltc_sha256_done",
                    "--redefine-sym",
                    "sha256_final=ltc_sha256_final",
                    "--redefine-sym",
                    "mp_init=ltm_mp_init",
                ])
                .arg(&p)
                .arg(&newp),
            "objcopy redefine",
        );
        fs::rename(&newp, &p).expect("rename obj");
    }
    let _ = fs::remove_file(lib);
    let mut ar_cmd = Command::new(ar);
    ar_cmd.arg("rcs").arg(lib);
    let mut objs: Vec<PathBuf> = fs::read_dir(&tmp)
        .expect("redef")
        .filter_map(|e| e.ok())
        .map(|e| e.path())
        .filter(|p| p.extension().and_then(|x| x.to_str()) == Some("o"))
        .collect();
    objs.sort();
    for o in &objs {
        ar_cmd.arg(o);
    }
    run(&mut ar_cmd, "ar rcs redefined");
    let _ = fs::remove_dir_all(&tmp);
}

fn collect_c_files(dir: &Path, out: &mut Vec<PathBuf>) {
    let Ok(rd) = fs::read_dir(dir) else {
        return;
    };
    for e in rd.flatten() {
        let p = e.path();
        if p.is_dir() {
            collect_c_files(&p, out);
        } else if p.extension().and_then(|x| x.to_str()) == Some("c") {
            out.push(p);
        }
    }
}

fn short_hash(path: &Path) -> String {
    use std::collections::hash_map::DefaultHasher;
    use std::hash::{Hash, Hasher};
    let mut h = DefaultHasher::new();
    path.to_string_lossy().hash(&mut h);
    format!("{:06x}", h.finish() & 0xffffff)
}

fn clang_resource_include() -> Option<PathBuf> {
    let out = Command::new("clang")
        .args(["-m64", "-print-resource-dir"])
        .output()
        .ok()?;
    if !out.status.success() {
        return None;
    }
    let s = String::from_utf8_lossy(&out.stdout).trim().to_string();
    if s.is_empty() {
        return None;
    }
    Some(PathBuf::from(s).join("include"))
}

fn grep_file_contains(path: &Path, needle: &str) -> bool {
    fs::read_to_string(path)
        .map(|s| s.contains(needle))
        .unwrap_or(false)
}

fn run(cmd: &mut Command, what: &str) {
    let status = cmd
        .status()
        .unwrap_or_else(|e| panic!("{what} failed to spawn: {e}"));
    if !status.success() {
        panic!("{what} failed");
    }
}
