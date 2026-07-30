//! Unified image builders: mtar / fat(ramdisk) / zip / embed / nest.
//! Same host face for every container type (pack, empty, list, nest, embed).

use alloc::string::String;
use alloc::vec::Vec;
use std::path::Path;

use crate::_port::{block_on, ForgeSession, ForgeStore};

/* Force-link Metal builder crates (C ABI symbols used below). */
use pymergetic_metal_async as _;
use pymergetic_metal_dev_blk_ram as _;
use pymergetic_metal_fs as _;
use pymergetic_metal_fs_embed as _;
use pymergetic_metal_fs_fat as _;
use pymergetic_metal_fs_mtar as _;
use pymergetic_metal_fs_zip as _;
use pymergetic_metal_util_lz4 as _;
use pymergetic_metal_util_tar as _;
use pymergetic_metal_vfs as _;

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

fn usage() -> [&'static str; 16] {
    [
        "forge img - build mtar / fat ramdisk / zip images (same shape)",
        "",
        "  forge img mtar pack DIR -o OUT.mtar",
        "  forge img mtar empty -o OUT.mtar",
        "  forge img fat create --size SIZE [--seed DIR] -o OUT.img",
        "  forge img fat empty --size SIZE -o OUT.img",
        "  forge img zip pack DIR -o OUT.zip",
        "  forge img zip empty -o OUT.zip",
        "  forge img embed FILE --name SYM [-o OUT.inc.c|--rs]",
        "  forge img nest fat-in-mtar --fat FILE.img -o OUT.mtar",
        "  forge img nest mtar-in-fat --mtar FILE.mtar --size SIZE -o OUT.img",
        "  forge img nest zip-in-mtar --zip FILE.zip -o OUT.mtar",
        "  forge img nest mtar-in-zip --mtar FILE.mtar -o OUT.zip",
        "  forge img nest fat-in-zip --fat FILE.img -o OUT.zip",
        "  forge img nest zip-in-fat --zip FILE.zip --size SIZE -o OUT.img",
        "",
    ]
}

/// Dispatch `forge img ...`. Returns process exit code.
pub fn run_img<S: ForgeStore, Sess: ForgeSession>(
    _store: &mut S,
    sess: &mut Sess,
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

