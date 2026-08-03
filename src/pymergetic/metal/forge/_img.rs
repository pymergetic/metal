//! Unified image builders: mtar / fat(ramdisk) / zip / embed / nest.
//! Same host face for every container type (pack, empty, list, nest, embed).

use alloc::string::String;
use alloc::vec::Vec;
use std::path::{Path, PathBuf};

use crate::_port::{block_on, ForgeSession, ForgeStore};

/* Force-link Metal builder crates (C ABI symbols used below). */
use pymergetic_metal_async as _;
use pymergetic_metal_dev_blk_ram as _;
use pymergetic_metal_fs as _;
use pymergetic_metal_fs_embed as _;
use pymergetic_metal_fs_fat as _;
use pymergetic_metal_fs_littlefs as _;
use pymergetic_metal_fs_mtar as _;
use pymergetic_metal_fs_zip as _;
use pymergetic_metal_util_lz4 as _;
use pymergetic_metal_util_tar as _;
use pymergetic_metal_fs_vfs as _;

extern "C" {
    fn pm_metal_fs_mtar_pack_simple(
        names: *const *const u8,
        datas: *const *const u8,
        lens: *const u32,
        count: u32,
        out: *mut u8,
        out_cap: usize,
        out_len: *mut usize,
    ) -> i32;
    fn pm_metal_fs_mtar_empty(out: *mut u8, out_cap: usize, out_len: *mut usize) -> i32;
    fn pm_metal_fs_mtar_open_blob(blob: *const u8, len: usize) -> u32;

    fn pm_metal_fs_zip_pack_simple(
        names: *const *const u8,
        datas: *const *const u8,
        lens: *const u32,
        count: u32,
        out: *mut u8,
        out_cap: usize,
        out_len: *mut usize,
    ) -> i32;
    fn pm_metal_fs_zip_empty(out: *mut u8, out_cap: usize, out_len: *mut usize) -> i32;

    fn pm_metal_fs_fat_format_buf(buf: *mut u8, len: usize) -> i32;
    fn pm_metal_fs_fat_seed_simple(
        buf: *mut u8,
        len: usize,
        names: *const *const u8,
        datas: *const *const u8,
        lens: *const u32,
        count: u32,
    ) -> i32;

    fn pm_metal_fs_embed_c(
        name: *const u8,
        data: *const u8,
        len: usize,
        out: *mut u8,
        out_cap: usize,
        out_len: *mut usize,
    ) -> i32;
    fn pm_metal_fs_embed_rs(
        name: *const u8,
        data: *const u8,
        len: usize,
        out: *mut u8,
        out_cap: usize,
        out_len: *mut usize,
    ) -> i32;
}

struct FileEntry {
    name: String,
    name_z: Vec<u8>,
    data: Vec<u8>,
}

fn flag(sess: &dyn ForgeSession, long: &str) -> Option<String> {
    let n = sess.arg_count();
    let eq = alloc::format!("{}=", long);
    for i in 0..n {
        match sess.arg(i) {
            Some(a) if a == long => return sess.arg(i + 1).map(String::from),
            Some(a) if a.starts_with(eq.as_str()) => return Some(String::from(&a[eq.len()..])),
            _ => {}
        }
    }
    None
}

fn parse_size(s: &str) -> Option<usize> {
    let s = s.trim();
    if s.is_empty() {
        return None;
    }
    let (num, mul) = if let Some(n) = s.strip_suffix(['K', 'k']) {
        (n, 1024usize)
    } else if let Some(n) = s.strip_suffix(['M', 'm']) {
        (n, 1024 * 1024)
    } else if let Some(n) = s.strip_suffix(['G', 'g']) {
        (n, 1024 * 1024 * 1024)
    } else {
        (s, 1usize)
    };
    num.parse::<usize>().ok().map(|v| v.saturating_mul(mul))
}

fn walk_files(root: &Path) -> Result<Vec<FileEntry>, String> {
    let mut out = Vec::new();
    let mut stack = vec![root.to_path_buf()];
    while let Some(dir) = stack.pop() {
        let rd = std::fs::read_dir(&dir).map_err(|_| alloc::format!("read_dir {}", dir.display()))?;
        for ent in rd {
            let ent = ent.map_err(|_| String::from("readdir"))?;
            let p = ent.path();
            if p.is_dir() {
                stack.push(p);
                continue;
            }
            if !p.is_file() {
                continue;
            }
            let rel = p
                .strip_prefix(root)
                .map_err(|_| String::from("strip"))?
                .to_string_lossy()
                .replace('\\', "/");
            let data = std::fs::read(&p).map_err(|_| alloc::format!("read {}", p.display()))?;
            let mut name_z = rel.clone().into_bytes();
            name_z.push(0);
            out.push(FileEntry {
                name: rel,
                name_z,
                data,
            });
        }
    }
    out.sort_by(|a, b| a.name.cmp(&b.name));
    Ok(out)
}

/// Stage `METAL_EXT_APPS=name=dir[:…]` as `apps/<name>/…` under `stage_root`.
/// Returns total bytes copied (0 if unset).
fn stage_ext_apps_into_apps_prefix(stage_root: &Path) -> Result<usize, String> {
    let Ok(raw) = std::env::var("METAL_EXT_APPS") else {
        return Ok(0);
    };
    if raw.is_empty() {
        return Ok(0);
    }
    let mut total = 0usize;
    for entry in raw.split(':') {
        if entry.is_empty() {
            continue;
        }
        let Some((name, src)) = entry.split_once('=') else {
            eprintln!("ext-apps: skip malformed entry '{entry}'");
            continue;
        };
        if name.is_empty() || src.is_empty() {
            continue;
        }
        let src_p = PathBuf::from(src);
        if !src_p.is_dir() {
            eprintln!("ext-apps: skip {name} -- missing dir {src}");
            continue;
        }
        let dest = stage_root.join("apps").join(name);
        let _ = std::fs::remove_dir_all(&dest);
        std::fs::create_dir_all(&dest).map_err(|_| String::from("mkdir apps"))?;
        let mut stack = vec![src_p.clone()];
        while let Some(dir) = stack.pop() {
            let rd = std::fs::read_dir(&dir).map_err(|_| alloc::format!("read {}", dir.display()))?;
            for ent in rd.flatten() {
                let p = ent.path();
                let rel = p
                    .strip_prefix(&src_p)
                    .map_err(|_| String::from("strip"))?
                    .to_path_buf();
                let out = dest.join(&rel);
                if p.is_dir() {
                    std::fs::create_dir_all(&out).map_err(|_| String::from("mkdir"))?;
                    stack.push(p);
                } else if p.is_file() {
                    if let Some(parent) = out.parent() {
                        std::fs::create_dir_all(parent).map_err(|_| String::from("mkdir"))?;
                    }
                    let data = std::fs::read(&p).map_err(|_| alloc::format!("read {}", p.display()))?;
                    total = total.saturating_add(data.len());
                    std::fs::write(&out, &data).map_err(|_| String::from("write app file"))?;
                }
            }
        }
        eprintln!("ext-apps: staged {name} from {src} -> apps/{name}/ (mods mtar)");
    }
    Ok(total)
}

fn pack_ptrs(files: &[FileEntry]) -> (Vec<*const u8>, Vec<*const u8>, Vec<u32>) {
    let names: Vec<*const u8> = files.iter().map(|f| f.name_z.as_ptr()).collect();
    let datas: Vec<*const u8> = files.iter().map(|f| f.data.as_ptr()).collect();
    let lens: Vec<u32> = files.iter().map(|f| f.data.len() as u32).collect();
    (names, datas, lens)
}

fn write_out(path: &str, data: &[u8]) -> Result<(), String> {
    if let Some(parent) = Path::new(path).parent() {
        if !parent.as_os_str().is_empty() {
            std::fs::create_dir_all(parent).map_err(|_| String::from("mkdir"))?;
        }
    }
    std::fs::write(path, data).map_err(|_| alloc::format!("write {}", path))
}

fn pack_mtar(files: &[FileEntry]) -> Result<Vec<u8>, String> {
    let (names, datas, lens) = pack_ptrs(files);
    let mut cap = 64 * 1024 + files.iter().map(|f| f.data.len() + 1024).sum::<usize>();
    for _ in 0..8 {
        let mut out = alloc::vec![0u8; cap];
        let mut olen = 0usize;
        let rc = unsafe {
            pm_metal_fs_mtar_pack_simple(
                if files.is_empty() {
                    core::ptr::null()
                } else {
                    names.as_ptr()
                },
                if files.is_empty() {
                    core::ptr::null()
                } else {
                    datas.as_ptr()
                },
                if files.is_empty() {
                    core::ptr::null()
                } else {
                    lens.as_ptr()
                },
                files.len() as u32,
                out.as_mut_ptr(),
                out.len(),
                &mut olen,
            )
        };
        if rc == 0 {
            out.truncate(olen);
            return Ok(out);
        }
        cap *= 2;
    }
    Err(String::from("mtar pack failed (buffer?)"))
}

fn pack_zip(files: &[FileEntry]) -> Result<Vec<u8>, String> {
    let (names, datas, lens) = pack_ptrs(files);
    let mut cap = 64 * 1024 + files.iter().map(|f| f.data.len() + 512).sum::<usize>();
    for _ in 0..8 {
        let mut out = alloc::vec![0u8; cap];
        let mut olen = 0usize;
        let rc = unsafe {
            pm_metal_fs_zip_pack_simple(
                if files.is_empty() {
                    core::ptr::null()
                } else {
                    names.as_ptr()
                },
                if files.is_empty() {
                    core::ptr::null()
                } else {
                    datas.as_ptr()
                },
                if files.is_empty() {
                    core::ptr::null()
                } else {
                    lens.as_ptr()
                },
                files.len() as u32,
                out.as_mut_ptr(),
                out.len(),
                &mut olen,
            )
        };
        if rc == 0 {
            out.truncate(olen);
            return Ok(out);
        }
        cap *= 2;
    }
    Err(String::from("zip pack failed (buffer?)"))
}

fn pack_fat(size: usize, files: &[FileEntry]) -> Result<Vec<u8>, String> {
    if size < 64 * 1024 {
        return Err(String::from("fat size too small (need >= 64K)"));
    }
    let mut buf = alloc::vec![0u8; size];
    if unsafe { pm_metal_fs_fat_format_buf(buf.as_mut_ptr(), buf.len()) } != 0 {
        return Err(String::from("fat format failed"));
    }
    if !files.is_empty() {
        let (names, datas, lens) = pack_ptrs(files);
        let rc = unsafe {
            pm_metal_fs_fat_seed_simple(
                buf.as_mut_ptr(),
                buf.len(),
                names.as_ptr(),
                datas.as_ptr(),
                lens.as_ptr(),
                files.len() as u32,
            )
        };
        if rc != 0 {
            return Err(String::from("fat seed failed"));
        }
    }
    Ok(buf)
}

fn empty_mtar() -> Result<Vec<u8>, String> {
    let mut out = alloc::vec![0u8; 4096];
    let mut olen = 0usize;
    let rc = unsafe { pm_metal_fs_mtar_empty(out.as_mut_ptr(), out.len(), &mut olen) };
    if rc != 0 {
        return Err(String::from("mtar empty failed"));
    }
    out.truncate(olen);
    Ok(out)
}

fn empty_zip() -> Result<Vec<u8>, String> {
    let mut out = alloc::vec![0u8; 64];
    let mut olen = 0usize;
    let rc = unsafe { pm_metal_fs_zip_empty(out.as_mut_ptr(), out.len(), &mut olen) };
    if rc != 0 {
        return Err(String::from("zip empty failed"));
    }
    out.truncate(olen);
    Ok(out)
}

fn embed_bytes(style: &str, name: &str, data: &[u8]) -> Result<Vec<u8>, String> {
    let mut nz = name.as_bytes().to_vec();
    nz.push(0);
    let mut cap = data.len() * 6 + 512;
    for _ in 0..6 {
        let mut out = alloc::vec![0u8; cap];
        let mut olen = 0usize;
        let rc = unsafe {
            if style == "rs" {
                pm_metal_fs_embed_rs(
                    nz.as_ptr(),
                    data.as_ptr(),
                    data.len(),
                    out.as_mut_ptr(),
                    out.len(),
                    &mut olen,
                )
            } else {
                pm_metal_fs_embed_c(
                    nz.as_ptr(),
                    data.as_ptr(),
                    data.len(),
                    out.as_mut_ptr(),
                    out.len(),
                    &mut olen,
                )
            }
        };
        if rc == 0 {
            out.truncate(olen);
            return Ok(out);
        }
        cap *= 2;
    }
    Err(String::from("embed failed"))
}

fn usage() -> [&'static str; 13] {
    [
        "forge img - build mtar / fat ramdisk / zip / rootfs images",
        "",
        "  forge img mtar pack DIR -o OUT.mtar",
        "  forge img mtar empty -o OUT.mtar",
        "  forge img fat create --size SIZE [--seed DIR] -o OUT.img",
        "  forge img fat empty --size SIZE -o OUT.img",
        "  forge img zip pack DIR -o OUT.zip",
        "  forge img zip empty -o OUT.zip",
        "  forge img embed FILE --name SYM [-o OUT.inc.c|--rs]",
        "  forge img nest fat-in-mtar|mtar-in-fat|... (see prior help)",
        "  forge img rootfs [--metal-root DIR]   # pack boot Stage-A blobs",
        "  forge img littlefs create --size SIZE [--seed DIR] -o OUT.img",
        "",
    ]
}

fn cfg_y(config_sh: &str, key: &str) -> bool {
    let needle = alloc::format!("export {}=y", key);
    config_sh.lines().any(|l| l.trim() == needle)
}

fn cfg_str(config_sh: &str, key: &str, default: &str) -> String {
    let prefix = alloc::format!("export {}=", key);
    for line in config_sh.lines() {
        let t = line.trim();
        if let Some(rest) = t.strip_prefix(&prefix) {
            let v = rest.trim().trim_matches('"');
            return String::from(v);
        }
    }
    String::from(default)
}

fn cfg_u32(config_sh: &str, key: &str, default: u32) -> u32 {
    let s = cfg_str(config_sh, key, "");
    if s.is_empty() {
        return default;
    }
    s.parse().unwrap_or(default)
}

fn file_has_banner(path: &Path, hint: &str) -> bool {
    match std::fs::read_to_string(path) {
        Ok(s) => s.contains(hint),
        Err(_) => false,
    }
}

fn should_skip_src_path(rel: &str) -> bool {
    rel.contains("/.pm/target/")
        || rel.contains("/.target/")
        || rel.contains("/target/")
        || rel.ends_with("Cargo.lock")
        || rel.ends_with(".inc.c")
        || rel.ends_with("_blobs.rs")
        || rel.ends_with(".o")
        || rel.ends_with(".a")
}

/// Pack kernel root FAT + mods/src mtars; emit `_blobs.rs` + writable `_root_fat.c`.
pub fn embed_rootfs(metal_root: &str) -> Result<String, String> {
    let tree = PathBuf::from(metal_root);
    let config_sh_path = tree.join("build/config.sh");
    if !config_sh_path.is_file() {
        let rc = crate::_config::config_gen(metal_root);
        if rc != 0 {
            return Err(String::from("forge config gen failed"));
        }
    }
    let config_sh = std::fs::read_to_string(&config_sh_path)
        .map_err(|_| String::from("read config.sh"))?;

    let mod_id = cfg_str(&config_sh, "CONFIG_PM_METAL_FS_KERNEL_MODULE_ID", "pymergetic.metal");
    let root_mib = cfg_u32(&config_sh, "CONFIG_PM_METAL_FS_ROOT_SIZE_MIB", 4);
    let log_mounts = cfg_y(&config_sh, "CONFIG_PM_METAL_LOG_BOOT_MOUNTS");
    let mount_mods = cfg_y(&config_sh, "CONFIG_PM_METAL_FS_MOUNT_KERNEL_MODS");
    let mount_src = cfg_y(&config_sh, "CONFIG_PM_METAL_FS_MOUNT_KERNEL_SRC");
    let src_none = cfg_y(&config_sh, "CONFIG_PM_METAL_FS_SRC_MODE_NONE");
    let src_all = cfg_y(&config_sh, "CONFIG_PM_METAL_FS_SRC_MODE_ALL");
    let mods_seed = cfg_str(&config_sh, "CONFIG_PM_METAL_FS_KERNEL_MODS_SEED", "");

    let rf_out = tree.join("build/rootfs");
    let seed_root = rf_out.join("seed_root");
    for sub in ["etc", "tmp", "mods", "src"] {
        std::fs::create_dir_all(seed_root.join(sub)).map_err(|_| String::from("mkdir seed"))?;
        let keep = seed_root.join(sub).join(".keep");
        if !keep.is_file() {
            std::fs::write(&keep, b"").map_err(|_| String::from("write keep"))?;
        }
    }
    // Stage B fstab seed (tmpfs at /tmp).
    let fstab = seed_root.join("etc/fstab");
    if !fstab.is_file() {
        std::fs::write(
            &fstab,
            b"# Stage B mounts (exp2)\nnone /tmp tmpfs defaults 0 0\n",
        )
        .map_err(|_| String::from("write fstab"))?;
    }

    /*
     * METAL_EXT_APPS=name=dir[:…] -> mods mtar as apps/<name>/…
     * (FAT seed cannot hold ~28MiB WADs reliably; mtar can.)
     * Guest path: /mods/<KERNEL_MODULE_ID>/apps/<name>/…
     */
    let ext_stage = rf_out.join("ext_apps_stage");
    let _ = std::fs::remove_dir_all(&ext_stage);
    let ext_bytes = stage_ext_apps_into_apps_prefix(&ext_stage)?;

    let root_size = (root_mib as usize) * 1024 * 1024;
    eprintln!("forge img rootfs: root FAT {}MiB", root_mib);
    let seed_files = walk_files(&seed_root)?;
    let root_img = pack_fat(root_size, &seed_files)?;
    let root_img_path = rf_out.join("root.img");
    write_out(root_img_path.to_str().unwrap(), &root_img)?;

    let mods_mtar_path = rf_out.join("mods.mtar");
    let have_mods = if mount_mods {
        let mut files: Vec<FileEntry> = Vec::new();
        if !mods_seed.is_empty() {
            let p = tree.join(&mods_seed);
            if p.is_dir() {
                eprintln!("forge img rootfs: mods mtar seed {}", mods_seed);
                files.extend(walk_files(&p)?);
            }
        }
        if ext_bytes > 0 && ext_stage.is_dir() {
            eprintln!("forge img rootfs: mods mtar + METAL_EXT_APPS ({} bytes staged)", ext_bytes);
            files.extend(walk_files(&ext_stage)?);
        }
        let bytes = if files.is_empty() {
            eprintln!("forge img rootfs: mods mtar (empty)");
            empty_mtar()?
        } else {
            files.sort_by(|a, b| a.name.cmp(&b.name));
            pack_mtar(&files)?
        };
        write_out(mods_mtar_path.to_str().unwrap(), &bytes)?;
        true
    } else {
        write_out(mods_mtar_path.to_str().unwrap(), b"")?;
        false
    };

    let src_mtar_path = rf_out.join("src.mtar");
    let have_src = if !src_none && mount_src {
        let src_tree = tree.join("src/pymergetic/metal");
        let stage = rf_out.join("stage_src");
        let _ = std::fs::remove_dir_all(&stage);
        std::fs::create_dir_all(&stage).map_err(|_| String::from("mkdir stage_src"))?;
        let mode_all = src_all;
        let banner = "DO NOT HAND-EDIT THIS FILE.";
        eprintln!(
            "forge img rootfs: staging src ({})",
            if mode_all { "all" } else { "human" }
        );
        let mut stack = vec![src_tree.clone()];
        while let Some(dir) = stack.pop() {
            let rd = match std::fs::read_dir(&dir) {
                Ok(r) => r,
                Err(_) => continue,
            };
            for ent in rd.flatten() {
                let p = ent.path();
                if p.is_dir() {
                    let name = p.file_name().and_then(|s| s.to_str()).unwrap_or("");
                    if name == "target" || name == ".target" || name == ".pm" {
                        // still descend into .pm for module sources, but skip target dirs
                        if name == "target" || name == ".target" {
                            continue;
                        }
                    }
                    if p.ends_with(".pm/target") {
                        continue;
                    }
                    stack.push(p);
                    continue;
                }
                if !p.is_file() {
                    continue;
                }
                let rel = match p.strip_prefix(&src_tree) {
                    Ok(r) => r.to_string_lossy().replace('\\', "/"),
                    Err(_) => continue,
                };
                if should_skip_src_path(&rel) {
                    continue;
                }
                if !mode_all && file_has_banner(&p, banner) {
                    continue;
                }
                let dest = stage.join(&rel);
                if let Some(parent) = dest.parent() {
                    std::fs::create_dir_all(parent).map_err(|_| String::from("mkdir dest"))?;
                }
                std::fs::copy(&p, &dest).map_err(|_| alloc::format!("copy {}", rel))?;
            }
        }
        let staged = walk_files(&stage)?;
        let bytes = if staged.is_empty() {
            eprintln!("forge img rootfs: warning: staged src empty; packing empty mtar");
            empty_mtar()?
        } else {
            pack_mtar(&staged)?
        };
        write_out(src_mtar_path.to_str().unwrap(), &bytes)?;
        true
    } else {
        write_out(src_mtar_path.to_str().unwrap(), b"")?;
        false
    };

    let root_c = tree.join("src/pymergetic/metal/boot/rootfs/_root_fat.c");
    let embed_tmp = rf_out.join("root_fat.embed.c");
    let emb = embed_bytes("c", "pm_metal_boot_root_fat", &root_img)?;
    write_out(embed_tmp.to_str().unwrap(), &emb)?;
    let emb_txt = std::fs::read_to_string(&embed_tmp).map_err(|_| String::from("read embed"))?;
    let mut out_c = String::from(
        "/* AUTO-GENERATED by forge img rootfs — do not edit */\n#include <stdint.h>\n",
    );
    for line in emb_txt.lines() {
        if line.starts_with("/* generated by pm_metal_fs_embed_c") {
            continue;
        }
        let mut l = line.to_string();
        if l.starts_with("static const uint8_t ") {
            l = l.replacen("static const uint8_t ", "uint8_t ", 1);
        }
        if l.starts_with("static const unsigned long ") {
            l = l.replacen("static const unsigned long ", "unsigned long long ", 1);
        }
        out_c.push_str(&l);
        out_c.push('\n');
    }
    write_out(root_c.to_str().unwrap(), out_c.as_bytes())?;

    let blobs_rs = tree.join("src/pymergetic/metal/boot/rootfs/_blobs.rs");
    let rel_mods = "../../../../../build/rootfs/mods.mtar";
    let rel_src = "../../../../../build/rootfs/src.mtar";
    let mut blobs = String::from("// AUTO-GENERATED by forge img rootfs — do not edit\n\n");
    blobs.push_str(&alloc::format!("pub const KERNEL_MODULE_ID: &str = \"{}\";\n", mod_id));
    blobs.push_str(&alloc::format!(
        "pub const LOG_BOOT_MOUNTS: bool = {};\n",
        if log_mounts { "true" } else { "false" }
    ));
    blobs.push_str(&alloc::format!(
        "pub const HAVE_MODS: bool = {};\n",
        if have_mods { "true" } else { "false" }
    ));
    blobs.push_str(&alloc::format!(
        "pub const HAVE_SRC: bool = {};\n\n",
        if have_src { "true" } else { "false" }
    ));
    if have_mods {
        blobs.push_str(&alloc::format!(
            "pub static MODS_MTAR: &[u8] = include_bytes!(\"{}\");\n",
            rel_mods
        ));
    } else {
        blobs.push_str("pub static MODS_MTAR: &[u8] = &[];\n");
    }
    if have_src {
        blobs.push_str(&alloc::format!(
            "pub static SRC_MTAR: &[u8] = include_bytes!(\"{}\");\n",
            rel_src
        ));
    } else {
        blobs.push_str("pub static SRC_MTAR: &[u8] = &[];\n");
    }
    write_out(blobs_rs.to_str().unwrap(), blobs.as_bytes())?;

    Ok(alloc::format!(
        "forge img rootfs: wrote {} + {}",
        blobs_rs.display(),
        root_c.display()
    ))
}

fn cmd_rootfs(metal_root: &str) -> Result<String, String> {
    embed_rootfs(metal_root)
}

fn cmd_littlefs(sess: &dyn ForgeSession) -> Result<String, String> {
    let verb = String::from(sess.arg(2).unwrap_or(""));
    let out = flag(sess, "-o").ok_or_else(|| String::from("need -o OUT"))?;
    let size_s = flag(sess, "--size").ok_or_else(|| String::from("need --size"))?;
    let size = parse_size(&size_s).ok_or_else(|| String::from("bad --size"))?;
    if verb != "create" && verb != "empty" {
        return Err(String::from("littlefs: create|empty --size SIZE -o OUT [--seed DIR]"));
    }
    extern "C" {
        fn pm_metal_fs_littlefs_format_buf(buf: *mut u8, len: usize) -> i32;
        fn pm_metal_fs_littlefs_seed_simple(
            buf: *mut u8,
            len: usize,
            names: *const *const u8,
            datas: *const *const u8,
            lens: *const u32,
            count: u32,
        ) -> i32;
    }
    if size < 64 * 1024 {
        return Err(String::from("littlefs size too small (need >= 64K)"));
    }
    let mut buf = alloc::vec![0u8; size];
    if unsafe { pm_metal_fs_littlefs_format_buf(buf.as_mut_ptr(), buf.len()) } != 0 {
        return Err(String::from("littlefs format failed"));
    }
    let seed = flag(sess, "--seed");
    let files = if let Some(d) = seed {
        walk_files(Path::new(&d))?
    } else {
        Vec::new()
    };
    if !files.is_empty() {
        let (names, datas, lens) = pack_ptrs(&files);
        let rc = unsafe {
            pm_metal_fs_littlefs_seed_simple(
                buf.as_mut_ptr(),
                buf.len(),
                names.as_ptr(),
                datas.as_ptr(),
                lens.as_ptr(),
                files.len() as u32,
            )
        };
        if rc != 0 {
            return Err(String::from("littlefs seed failed"));
        }
    }
    write_out(&out, &buf)?;
    Ok(alloc::format!(
        "wrote {} (littlefs, {} files, {} bytes)",
        out,
        files.len(),
        buf.len()
    ))
}

/// Dispatch `forge img ...`. Returns process exit code.
pub fn run_img<S: ForgeStore, Sess: ForgeSession>(
    _store: &mut S,
    sess: &mut Sess,
    metal_root: &str,
) -> i32 {
    let kind = String::from(sess.arg(1).unwrap_or(""));
    if kind.is_empty() || kind == "-h" || kind == "--help" {
        for line in usage() {
            if line.is_empty() {
                continue;
            }
            let _ = block_on(|| sess.out_line(line));
        }
        sess.set_exit(1);
        return 1;
    }

    let result = match kind.as_str() {
        "mtar" => cmd_mtar(sess),
        "fat" => cmd_fat(sess),
        "zip" => cmd_zip(sess),
        "embed" => cmd_embed(sess),
        "nest" => cmd_nest(sess),
        "rootfs" => cmd_rootfs(metal_root),
        "littlefs" => cmd_littlefs(sess),
        _ => Err(alloc::format!("forge img: unknown kind {:?}", kind)),
    };
    match result {
        Ok(msg) => {
            if !msg.is_empty() {
                let _ = block_on(|| sess.out_line(&msg));
            }
            sess.set_exit(0);
            0
        }
        Err(e) => {
            let _ = block_on(|| sess.err_line(&e));
            sess.set_exit(2);
            2
        }
    }
}

fn cmd_mtar(sess: &dyn ForgeSession) -> Result<String, String> {
    let verb = String::from(sess.arg(2).unwrap_or(""));
    let out = flag(sess, "-o").ok_or_else(|| String::from("need -o OUT"))?;
    match verb.as_str() {
        "empty" => {
            let bytes = empty_mtar()?;
            write_out(&out, &bytes)?;
            Ok(alloc::format!("wrote {} ({} bytes)", out, bytes.len()))
        }
        "pack" => {
            let dir = String::from(sess.arg(3).unwrap_or(""));
            if dir.is_empty() {
                return Err(String::from("mtar pack DIR -o OUT"));
            }
            let files = walk_files(Path::new(&dir))?;
            let bytes = pack_mtar(&files)?;
            write_out(&out, &bytes)?;
            Ok(alloc::format!(
                "wrote {} ({} files, {} bytes)",
                out,
                files.len(),
                bytes.len()
            ))
        }
        _ => Err(String::from("mtar: pack|empty")),
    }
}

fn cmd_fat(sess: &dyn ForgeSession) -> Result<String, String> {
    let verb = String::from(sess.arg(2).unwrap_or(""));
    let out = flag(sess, "-o").ok_or_else(|| String::from("need -o OUT"))?;
    let size_s = flag(sess, "--size").ok_or_else(|| String::from("need --size"))?;
    let size = parse_size(&size_s).ok_or_else(|| String::from("bad --size"))?;
    match verb.as_str() {
        "empty" | "create" => {
            let seed = flag(sess, "--seed");
            let files = if let Some(d) = seed {
                walk_files(Path::new(&d))?
            } else if verb == "create" {
                /* optional positional seed dir */
                let d = String::from(sess.arg(3).unwrap_or(""));
                if !d.is_empty() && !d.starts_with('-') {
                    walk_files(Path::new(&d))?
                } else {
                    Vec::new()
                }
            } else {
                Vec::new()
            };
            let bytes = pack_fat(size, &files)?;
            write_out(&out, &bytes)?;
            let kind = if size >= 32 * 1024 * 1024 { "fat32" } else { "fat16" };
            Ok(alloc::format!(
                "wrote {} ({}, {} files, {} bytes)",
                out,
                kind,
                files.len(),
                bytes.len()
            ))
        }
        _ => Err(String::from("fat: create|empty --size SIZE [-o OUT] [--seed DIR]")),
    }
}

fn cmd_zip(sess: &dyn ForgeSession) -> Result<String, String> {
    let verb = String::from(sess.arg(2).unwrap_or(""));
    let out = flag(sess, "-o").ok_or_else(|| String::from("need -o OUT"))?;
    match verb.as_str() {
        "empty" => {
            let bytes = empty_zip()?;
            write_out(&out, &bytes)?;
            Ok(alloc::format!("wrote {} ({} bytes)", out, bytes.len()))
        }
        "pack" => {
            let dir = String::from(sess.arg(3).unwrap_or(""));
            if dir.is_empty() {
                return Err(String::from("zip pack DIR -o OUT"));
            }
            let files = walk_files(Path::new(&dir))?;
            let bytes = pack_zip(&files)?;
            write_out(&out, &bytes)?;
            Ok(alloc::format!(
                "wrote {} ({} files, {} bytes)",
                out,
                files.len(),
                bytes.len()
            ))
        }
        _ => Err(String::from("zip: pack|empty")),
    }
}

fn cmd_embed(sess: &dyn ForgeSession) -> Result<String, String> {
    let file = String::from(sess.arg(2).unwrap_or(""));
    if file.is_empty() {
        return Err(String::from("embed FILE --name SYM -o OUT"));
    }
    let name = flag(sess, "--name").ok_or_else(|| String::from("need --name"))?;
    let out = flag(sess, "-o").ok_or_else(|| String::from("need -o OUT"))?;
    let mut style = "c";
    let n = sess.arg_count();
    for i in 0..n {
        if sess.arg(i) == Some("--rs") {
            style = "rs";
        }
    }
    if out.ends_with(".rs") {
        style = "rs";
    }
    let data = std::fs::read(&file).map_err(|_| alloc::format!("read {}", file))?;
    let text = embed_bytes(style, &name, &data)?;
    write_out(&out, &text)?;
    Ok(alloc::format!("wrote {} ({} style, {} bytes in)", out, style, data.len()))
}

fn cmd_nest(sess: &dyn ForgeSession) -> Result<String, String> {
    let mode = String::from(sess.arg(2).unwrap_or(""));
    let out = flag(sess, "-o").ok_or_else(|| String::from("need -o OUT"))?;
    match mode.as_str() {
        "fat-in-mtar" => {
            let fat = flag(sess, "--fat").ok_or_else(|| String::from("need --fat"))?;
            let data = std::fs::read(&fat).map_err(|_| alloc::format!("read {}", fat))?;
            let mut name_z = b"disk.img\0".to_vec();
            let files = [FileEntry {
                name: String::from("disk.img"),
                name_z: name_z.clone(),
                data,
            }];
            let _ = &mut name_z;
            let bytes = pack_mtar(&files)?;
            write_out(&out, &bytes)?;
            Ok(alloc::format!("wrote {} (fat-in-mtar)", out))
        }
        "zip-in-mtar" => {
            let zip = flag(sess, "--zip").ok_or_else(|| String::from("need --zip"))?;
            let data = std::fs::read(&zip).map_err(|_| alloc::format!("read {}", zip))?;
            let files = [FileEntry {
                name: String::from("pack.zip"),
                name_z: b"pack.zip\0".to_vec(),
                data,
            }];
            let bytes = pack_mtar(&files)?;
            write_out(&out, &bytes)?;
            Ok(alloc::format!("wrote {} (zip-in-mtar)", out))
        }
        "mtar-in-fat" => {
            let mtar = flag(sess, "--mtar").ok_or_else(|| String::from("need --mtar"))?;
            let size_s = flag(sess, "--size").ok_or_else(|| String::from("need --size"))?;
            let size = parse_size(&size_s).ok_or_else(|| String::from("bad --size"))?;
            let data = std::fs::read(&mtar).map_err(|_| alloc::format!("read {}", mtar))?;
            let files = [FileEntry {
                name: String::from("pack.mtar"),
                name_z: b"pack.mtar\0".to_vec(),
                data,
            }];
            let bytes = pack_fat(size, &files)?;
            write_out(&out, &bytes)?;
            Ok(alloc::format!("wrote {} (mtar-in-fat)", out))
        }
        "zip-in-fat" => {
            let zip = flag(sess, "--zip").ok_or_else(|| String::from("need --zip"))?;
            let size_s = flag(sess, "--size").ok_or_else(|| String::from("need --size"))?;
            let size = parse_size(&size_s).ok_or_else(|| String::from("bad --size"))?;
            let data = std::fs::read(&zip).map_err(|_| alloc::format!("read {}", zip))?;
            let files = [FileEntry {
                name: String::from("pack.zip"),
                name_z: b"pack.zip\0".to_vec(),
                data,
            }];
            let bytes = pack_fat(size, &files)?;
            write_out(&out, &bytes)?;
            Ok(alloc::format!("wrote {} (zip-in-fat)", out))
        }
        "mtar-in-zip" => {
            let mtar = flag(sess, "--mtar").ok_or_else(|| String::from("need --mtar"))?;
            let data = std::fs::read(&mtar).map_err(|_| alloc::format!("read {}", mtar))?;
            let files = [FileEntry {
                name: String::from("pack.mtar"),
                name_z: b"pack.mtar\0".to_vec(),
                data,
            }];
            let bytes = pack_zip(&files)?;
            write_out(&out, &bytes)?;
            Ok(alloc::format!("wrote {} (mtar-in-zip)", out))
        }
        "fat-in-zip" => {
            let fat = flag(sess, "--fat").ok_or_else(|| String::from("need --fat"))?;
            let data = std::fs::read(&fat).map_err(|_| alloc::format!("read {}", fat))?;
            let files = [FileEntry {
                name: String::from("disk.img"),
                name_z: b"disk.img\0".to_vec(),
                data,
            }];
            let bytes = pack_zip(&files)?;
            write_out(&out, &bytes)?;
            Ok(alloc::format!("wrote {} (fat-in-zip)", out))
        }
        _ => Err(String::from(
            "nest: fat-in-mtar|mtar-in-fat|zip-in-mtar|mtar-in-zip|fat-in-zip|zip-in-fat",
        )),
    }
}

