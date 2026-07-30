//! In-memory FAT16/FAT32 with VFAT LFN (format, seed, ops vtable).
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(dead_code, non_camel_case_types, non_snake_case)]

extern crate alloc;

use alloc::string::String;
use alloc::vec::Vec;

use core::ffi::c_void;
use core::ptr::{addr_of, addr_of_mut};
use core::sync::atomic::{AtomicU32, Ordering};

use pymergetic_metal_fs::{
    pm_metal_fs_ops_register, pm_metal_fs_ops_t, pm_metal_fs_set_active_ops, pm_metal_fs_stat_t,
    PM_METAL_FS_INVALID, PM_METAL_FS_O_CREAT, PM_METAL_FS_O_DIRECTORY, PM_METAL_FS_O_RDONLY,
    PM_METAL_FS_O_RDWR, PM_METAL_FS_O_TRUNC, PM_METAL_FS_O_WRONLY, PM_METAL_FS_SEEK_CUR,
    PM_METAL_FS_SEEK_END, PM_METAL_FS_SEEK_SET, PM_METAL_FS_TYPE_DIR, PM_METAL_FS_TYPE_FILE,
};
use pymergetic_metal_rt as _;
use pymergetic_metal_vfs as vfs;

extern "C" {
    fn pm_metal_async_completed_u32(v: u32) -> u32;
    fn pm_metal_dev_blk_ram_bytes(h: u32, out_ptr: *mut *mut u8, out_len: *mut usize) -> i32;
}

const SECTOR: usize = 512;
const ROOT_ENTRIES: u16 = 512;
const ROOT_SECS: u32 = 32;
const NUM_FATS: u8 = 2;
const RESERVED16: u16 = 1;
const RESERVED32: u16 = 32;
const MAX_VOL: usize = 8;
const MAX_OPEN: usize = 32;
const ATTR_RO: u8 = 0x01;
const ATTR_VOL: u8 = 0x08;
const ATTR_DIR: u8 = 0x10;
const ATTR_ARC: u8 = 0x20;
const ATTR_LFN: u8 = 0x0F;
const LFN_TYPE: u8 = 0x00;
const DELETED: u8 = 0xe5;
const DE_END: u8 = 0x00;
const FAT16_EOC: u16 = 0xffff;
const FAT16_BAD: u16 = 0xfff7;
const FAT16_MEDIA: u16 = 0xfff8;
const FAT32_EOC: u32 = 0x0fffffff;
const FAT32_BAD: u32 = 0x0ffffff7;
const FAT32_MEDIA: u32 = 0x0ffffff8;
const FAT32_MASK: u32 = 0x0fffffff;
const FAT16_CLUST_MAX: u32 = 65524;
const FAT32_MIN_BYTES: usize = 32 * 1024 * 1024;
const MAX_LFN_UCS2: usize = 255;
const LFN_CHARS: usize = 13;

#[derive(Clone, Copy)]
struct Layout {
    is_fat32: bool,
    total_secs: u32,
    sec_per_clust: u8,
    fat_secs: u32,
    fat1: usize,
    fat2: usize,
    root_sec: usize,
    root_clust: u32,
    data_sec: u32,
    clust_count: u32,
    root_entries: u16,
}

struct Vol {
    buf: *mut u8,
    len: usize,
    lay: Layout,
}

struct OpenH {
    vol: u32,
    is_dir: bool,
    entry_off: usize,
    first_clust: u32,
    dir_clust: u32,
    dir_slot: u32,
    pos: u32,
    size: u32,
}

static mut VOLS: [Option<Vol>; MAX_VOL] = [const { None }; MAX_VOL];
static mut FILES: [Option<OpenH>; MAX_OPEN] = [const { None }; MAX_OPEN];
static mut OPS_READY: bool = false;
static NEXT_VOL: AtomicU32 = AtomicU32::new(1);
static FAT_NAME: &[u8] = b"fat\0";

static FAT_OPS: pm_metal_fs_ops_t = pm_metal_fs_ops_t {
    name: FAT_NAME.as_ptr(),
    open: Some(op_open),
    close: Some(op_close),
    fread: Some(op_fread),
    fwrite: Some(op_fwrite),
    fpread: Some(op_fpread),
    fpwrite: Some(op_fpwrite),
    lseek: Some(op_lseek),
    stat: Some(op_stat),
    readdir: Some(op_readdir),
    mkdir: Some(op_mkdir),
    unlink: Some(op_unlink),
    rename: None,
    fsync: None,
};

fn done(v: u32) -> u32 {
    unsafe { pm_metal_async_completed_u32(v) }
}

fn cstr<'a>(p: *const u8) -> &'a str {
    unsafe {
        let mut n = 0usize;
        while *p.add(n) != 0 {
            n += 1;
            if n > 512 {
                break;
            }
        }
        core::str::from_utf8_unchecked(core::slice::from_raw_parts(p, n))
    }
}

fn u16le(b: *const u8) -> u16 {
    unsafe { u16::from_le_bytes([*b, *b.add(1)]) }
}

fn u32le(b: *const u8) -> u32 {
    unsafe {
        u32::from_le_bytes([*b, *b.add(1), *b.add(2), *b.add(3)])
    }
}

fn put16(b: *mut u8, v: u16) {
    unsafe {
        *b = (v & 0xff) as u8;
        *b.add(1) = (v >> 8) as u8;
    }
}

fn put32(b: *mut u8, v: u32) {
    unsafe {
        *b = (v & 0xff) as u8;
        *b.add(1) = ((v >> 8) & 0xff) as u8;
        *b.add(2) = ((v >> 16) & 0xff) as u8;
        *b.add(3) = ((v >> 24) & 0xff) as u8;
    }
}

fn norm_path(path: &str) -> String {
    let mut s = String::from(path.trim().trim_start_matches('/'));
    while s.ends_with('/') {
        s.pop();
    }
    s
}

fn split_parent(path: &str) -> (&str, &str) {
    match path.rfind('/') {
        Some(i) => (&path[..i], &path[i + 1..]),
        None => ("", path),
    }
}

fn ascii_eq(a: &str, b: &str) -> bool {
    a.eq_ignore_ascii_case(b)
}

fn utf8_to_ucs2(name: &str) -> Vec<u16> {
    let mut out = Vec::new();
    for c in name.chars() {
        if out.len() >= MAX_LFN_UCS2 {
            break;
        }
        if c.is_ascii() {
            out.push(c as u16);
        } else {
            out.push(b'_' as u16);
        }
    }
    out
}

fn ucs2_to_ascii(out: *mut u8, cap: u32, ucs2: &[u16]) -> u32 {
    if out.is_null() || cap == 0 {
        return 0;
    }
    let mut w = 0usize;
    for &u in ucs2 {
        if u == 0 || u >= 0x80 {
            if u >= 0x80 {
                continue;
            }
            break;
        }
        if w + 1 >= cap as usize {
            break;
        }
        unsafe {
            *out.add(w) = u as u8;
        }
        w += 1;
    }
    unsafe {
        *out.add(w) = 0;
    }
    w as u32
}

fn lfn_checksum(n: &[u8; 8], e: &[u8; 3]) -> u8 {
    let mut s = [0u8; 11];
    s[..8].copy_from_slice(n);
    s[8..11].copy_from_slice(e);
    let mut sum = 0u8;
    for &b in &s {
        let t = (sum as u16 & 1) * 0x80 + (sum as u16 >> 1) + b as u16;
        sum = (t & 0xff) as u8;
    }
    sum
}

fn to83_strict(name: &str) -> Option<([u8; 8], [u8; 3])> {
    let name = name.trim();
    if name.is_empty() || name == "." || name == ".." {
        return None;
    }
    let (base, ext) = match name.rfind('.') {
        Some(i) if i > 0 => (&name[..i], &name[i + 1..]),
        _ => (name, ""),
    };
    if base.is_empty() || base.len() > 8 || ext.len() > 3 {
        return None;
    }
    let mut n = [b' '; 8];
    let mut e = [b' '; 3];
    for (i, c) in base.bytes().enumerate() {
        let u = c.to_ascii_uppercase();
        if !u.is_ascii_alphanumeric() && u != b'_' && u != b'~' {
            return None;
        }
        n[i] = u;
    }
    for (i, c) in ext.bytes().enumerate() {
        let u = c.to_ascii_uppercase();
        if !u.is_ascii_alphanumeric() {
            return None;
        }
        e[i] = u;
    }
    Some((n, e))
}

fn gen_short_alias(base_name: &str, seq: u32) -> ([u8; 8], [u8; 3]) {
    let (stem, ext) = match base_name.rfind('.') {
        Some(i) if i > 0 => (&base_name[..i], &base_name[i + 1..]),
        _ => (base_name, ""),
    };
    let mut stem_u: Vec<u8> = stem
        .bytes()
        .filter(|c| c.is_ascii_alphanumeric())
        .map(|c| c.to_ascii_uppercase())
        .collect();
    if stem_u.is_empty() {
        stem_u.push(b'X');
    }
    let mut n = [b' '; 8];
    let tilde = alloc::format!("~{}", seq);
    let tlen = tilde.len().min(6);
    let blen = 6usize.saturating_sub(tlen).min(stem_u.len());
    for i in 0..blen {
        n[i] = stem_u[i];
    }
    for (i, &c) in tilde.as_bytes().iter().enumerate().take(tlen) {
        n[blen + i] = c;
    }
    let mut e = [b' '; 3];
    let ext_u: Vec<u8> = ext
        .bytes()
        .filter(|c| c.is_ascii_alphanumeric())
        .map(|c| c.to_ascii_uppercase())
        .take(3)
        .collect();
    for (i, &c) in ext_u.iter().enumerate() {
        e[i] = c;
    }
    (n, e)
}

fn name83_out(n: &[u8; 8], e: &[u8; 3], out: *mut u8, cap: u32) -> u32 {
    if out.is_null() || cap == 0 {
        return 0;
    }
    let mut tmp = [0u8; 13];
    let mut w = 0usize;
    for &c in n {
        if c == b' ' {
            break;
        }
        if w < 12 {
            tmp[w] = c;
            w += 1;
        }
    }
    let mut eh = 0usize;
    for &c in e {
        if c != b' ' {
            eh += 1;
        }
    }
    if eh > 0 {
        if w < 12 {
            tmp[w] = b'.';
            w += 1;
        }
        for &c in e {
            if c == b' ' {
                break;
            }
            if w < 12 {
                tmp[w] = c;
                w += 1;
            }
        }
    }
    let copy = core::cmp::min(w, cap as usize - 1);
    unsafe {
        for i in 0..copy {
            *out.add(i) = tmp[i];
        }
        *out.add(copy) = 0;
    }
    copy as u32
}

fn plan_fat16(len: usize, spc: u8) -> Option<Layout> {
    if len < SECTOR * 40 {
        return None;
    }
    let total = (len / SECTOR) as u32;
    let mut fat_secs = 1u32;
    loop {
        let meta = RESERVED16 as u32 + (NUM_FATS as u32) * fat_secs + ROOT_SECS;
        if meta >= total {
            return None;
        }
        let data_secs = total - meta;
        let clust = data_secs / spc as u32;
        if clust < 2 {
            return None;
        }
        let need = ((clust + 2) * 2 + SECTOR as u32 - 1) / SECTOR as u32;
        if need <= fat_secs {
            let fat1 = SECTOR * RESERVED16 as usize;
            let fat2 = fat1 + SECTOR * fat_secs as usize;
            let root = fat2 + SECTOR * fat_secs as usize;
            return Some(Layout {
                is_fat32: false,
                total_secs: total,
                sec_per_clust: spc,
                fat_secs,
                fat1,
                fat2,
                root_sec: root,
                root_clust: 0,
                data_sec: meta,
                clust_count: clust,
                root_entries: ROOT_ENTRIES,
            });
        }
        fat_secs += 1;
        if fat_secs > 256 {
            return None;
        }
    }
}

fn plan_fat32(len: usize, spc: u8) -> Option<Layout> {
    if len < SECTOR * 128 {
        return None;
    }
    let total = (len / SECTOR) as u32;
    let mut fat_secs = 1u32;
    loop {
        let meta = RESERVED32 as u32 + (NUM_FATS as u32) * fat_secs;
        if meta >= total {
            return None;
        }
        let data_secs = total - meta;
        let clust = data_secs / spc as u32;
        if clust < 2 {
            return None;
        }
        let need = ((clust + 2) * 4 + SECTOR as u32 - 1) / SECTOR as u32;
        if need <= fat_secs {
            let fat1 = SECTOR * RESERVED32 as usize;
            let fat2 = fat1 + SECTOR * fat_secs as usize;
            return Some(Layout {
                is_fat32: true,
                total_secs: total,
                sec_per_clust: spc,
                fat_secs,
                fat1,
                fat2,
                root_sec: 0,
                root_clust: 2,
                data_sec: meta,
                clust_count: clust,
                root_entries: 0,
            });
        }
        fat_secs += 1;
        if fat_secs > 4096 {
            return None;
        }
    }
}

fn pick_layout(len: usize) -> Option<Layout> {
    let use32 = len >= FAT32_MIN_BYTES
        || plan_fat16(len, 4)
            .or_else(|| plan_fat16(len, 1))
            .map(|l| l.clust_count > FAT16_CLUST_MAX)
            .unwrap_or(true);
    if use32 {
        plan_fat32(len, 8)
            .or_else(|| plan_fat32(len, 4))
            .or_else(|| plan_fat32(len, 1))
    } else {
        plan_fat16(len, 4).or_else(|| plan_fat16(len, 1))
    }
}

unsafe fn vol_ref(id: u32) -> Option<&'static Vol> {
    let i = id as usize;
    if i == 0 || i >= MAX_VOL {
        return None;
    }
    let vols = &*addr_of!(VOLS);
    vols[i].as_ref()
}

unsafe fn vol_mut(id: u32) -> Option<&'static mut Vol> {
    let i = id as usize;
    if i == 0 || i >= MAX_VOL {
        return None;
    }
    let vols = &mut *addr_of_mut!(VOLS);
    vols[i].as_mut()
}

unsafe fn fat_get(v: &Vol, cl: u32) -> u32 {
    if cl < 2 {
        return 0;
    }
    if v.lay.is_fat32 {
        let off = v.lay.fat1 + (cl as usize) * 4;
        u32le(v.buf.add(off)) & FAT32_MASK
    } else {
        let off = v.lay.fat1 + (cl as usize) * 2;
        u16le(v.buf.add(off)) as u32
    }
}

unsafe fn fat_set(v: &mut Vol, cl: u32, val: u32) {
    if cl < 2 {
        return;
    }
    if v.lay.is_fat32 {
        let o1 = v.lay.fat1 + (cl as usize) * 4;
        let o2 = v.lay.fat2 + (cl as usize) * 4;
        let hi = u32le(v.buf.add(o1)) & !FAT32_MASK;
        put32(v.buf.add(o1), hi | (val & FAT32_MASK));
        put32(v.buf.add(o2), hi | (val & FAT32_MASK));
    } else {
        let v16 = val as u16;
        let o1 = v.lay.fat1 + (cl as usize) * 2;
        let o2 = v.lay.fat2 + (cl as usize) * 2;
        put16(v.buf.add(o1), v16);
        put16(v.buf.add(o2), v16);
    }
}

fn fat_is_eoc(v: &Vol, e: u32) -> bool {
    if v.lay.is_fat32 {
        e >= 0x0ffffff8 && e <= FAT32_EOC
    } else {
        e >= 0xfff8
    }
}

fn fat_is_bad(v: &Vol, e: u32) -> bool {
    if v.lay.is_fat32 {
        e == FAT32_BAD
    } else {
        e == FAT16_BAD as u32
    }
}

unsafe fn clust_sec(v: &Vol, cl: u32) -> u32 {
    v.lay.data_sec + (cl - 2) * v.lay.sec_per_clust as u32
}

unsafe fn clust_off(v: &Vol, cl: u32) -> usize {
    clust_sec(v, cl) as usize * SECTOR
}

unsafe fn clust_used(v: &Vol, cl: u32) -> bool {
    let e = fat_get(v, cl);
    e != 0 && !fat_is_bad(v, e)
}

unsafe fn clust_alloc(v: &mut Vol) -> Option<u32> {
    for cl in 2..=v.lay.clust_count + 1 {
        if !clust_used(v, cl) {
            let eoc = if v.lay.is_fat32 {
                FAT32_EOC
            } else {
                FAT16_EOC as u32
            };
            fat_set(v, cl, eoc);
            return Some(cl);
        }
    }
    None
}

unsafe fn clust_free_chain(v: &mut Vol, start: u32) {
    let mut cl = start;
    let mut guard = 0u32;
    while cl >= 2 && cl <= v.lay.clust_count + 1 && guard < 65536 {
        guard += 1;
        let next = fat_get(v, cl);
        fat_set(v, cl, 0);
        if fat_is_eoc(v, next) {
            break;
        }
        cl = next;
    }
}

unsafe fn clust_extend(v: &mut Vol, tail: u32) -> Option<u32> {
    let nc = clust_alloc(v)?;
    fat_set(v, tail, nc);
    Some(nc)
}

unsafe fn de_attr(v: &Vol, off: usize) -> u8 {
    *v.buf.add(off + 11)
}

unsafe fn de_clust(v: &Vol, off: usize) -> u32 {
    if v.lay.is_fat32 {
        let hi = u16le(v.buf.add(off + 20)) as u32;
        let lo = u16le(v.buf.add(off + 26)) as u32;
        (hi << 16) | lo
    } else {
        u16le(v.buf.add(off + 26)) as u32
    }
}

unsafe fn de_size(v: &Vol, off: usize) -> u32 {
    u32le(v.buf.add(off + 28))
}

unsafe fn de_set_size(v: &mut Vol, off: usize, sz: u32) {
    put32(v.buf.add(off + 28), sz);
}

unsafe fn de_set_clust(v: &mut Vol, off: usize, cl: u32) {
    if v.lay.is_fat32 {
        put16(v.buf.add(off + 20), (cl >> 16) as u16);
        put16(v.buf.add(off + 26), (cl & 0xffff) as u16);
    } else {
        put16(v.buf.add(off + 26), cl as u16);
    }
}

unsafe fn de_read83(v: &Vol, off: usize) -> ([u8; 8], [u8; 3]) {
    let mut n = [0u8; 8];
    let mut e = [0u8; 3];
    let b = v.buf.add(off);
    for i in 0..8 {
        n[i] = *b.add(i);
    }
    for i in 0..3 {
        e[i] = *b.add(i + 8);
    }
    (n, e)
}

unsafe fn de_write(v: &mut Vol, off: usize, n: &[u8; 8], e: &[u8; 3], attr: u8, cl: u32, sz: u32) {
    let b = v.buf.add(off);
    for i in 0..8 {
        *b.add(i) = n[i];
    }
    for i in 0..3 {
        *b.add(i + 8) = e[i];
    }
    *b.add(11) = attr;
    for i in 12..20 {
        *b.add(i) = 0;
    }
    if v.lay.is_fat32 {
        put16(b.add(20), (cl >> 16) as u16);
        for i in 22..26 {
            *b.add(i) = 0;
        }
        put16(b.add(26), (cl & 0xffff) as u16);
    } else {
        for i in 20..26 {
            *b.add(i) = 0;
        }
        put16(b.add(26), cl as u16);
    }
    put32(b.add(28), sz);
}

unsafe fn de_del(v: &mut Vol, off: usize) {
    *v.buf.add(off) = DELETED;
}

unsafe fn de_is_free(v: &Vol, off: usize) -> bool {
    let c0 = *v.buf.add(off);
    c0 == DE_END || c0 == DELETED
}

unsafe fn is_root(v: &Vol, dir_cl: u32) -> bool {
    if v.lay.is_fat32 {
        dir_cl == v.lay.root_clust
    } else {
        dir_cl == 0
    }
}

unsafe fn root_dir_cl(v: &Vol) -> u32 {
    if v.lay.is_fat32 {
        v.lay.root_clust
    } else {
        0
    }
}

unsafe fn dir_slots(v: &Vol, dir_cl: u32) -> u32 {
    if !v.lay.is_fat32 && dir_cl == 0 {
        return v.lay.root_entries as u32;
    }
    let per_cl = (SECTOR / 32) * v.lay.sec_per_clust as usize;
    let mut cl = if dir_cl == 0 {
        v.lay.root_clust
    } else {
        dir_cl
    };
    let mut total = 0u32;
    let mut guard = 0u32;
    loop {
        total += per_cl as u32;
        let n = fat_get(v, cl);
        if fat_is_eoc(v, n) {
            break;
        }
        cl = n;
        guard += 1;
        if guard > 4096 {
            break;
        }
    }
    total
}

unsafe fn dir_entry_off(v: &Vol, dir_cl: u32, slot: u32) -> Option<usize> {
    if !v.lay.is_fat32 && dir_cl == 0 {
        if slot as usize >= v.lay.root_entries as usize {
            return None;
        }
        return Some(v.lay.root_sec + slot as usize * 32);
    }
    let per = SECTOR / 32;
    let slots_per_cl = per * v.lay.sec_per_clust as usize;
    let cl_idx = slot as usize / slots_per_cl;
    let in_cl = slot as usize % slots_per_cl;
    let mut cl = if dir_cl == 0 {
        v.lay.root_clust
    } else {
        dir_cl
    };
    for _ in 0..cl_idx {
        let n = fat_get(v, cl);
        if fat_is_eoc(v, n) {
            return None;
        }
        cl = n;
    }
    let sec = clust_sec(v, cl) + (in_cl / per) as u32;
    Some(sec as usize * SECTOR + (in_cl % per) * 32)
}

unsafe fn dir_zero_cluster(v: &mut Vol, cl: u32) {
    let base = clust_off(v, cl);
    let sz = v.lay.sec_per_clust as usize * SECTOR;
    for i in 0..sz {
        *v.buf.add(base + i) = 0;
    }
}

unsafe fn dir_extend(v: &mut Vol, dir_cl: u32) -> bool {
    if !v.lay.is_fat32 && dir_cl == 0 {
        return false;
    }
    let start = if dir_cl == 0 {
        v.lay.root_clust
    } else {
        dir_cl
    };
    let mut tail = start;
    let mut guard = 0u32;
    loop {
        let n = fat_get(v, tail);
        if fat_is_eoc(v, n) {
            break;
        }
        tail = n;
        guard += 1;
        if guard > 4096 {
            return false;
        }
    }
    let nc = match clust_alloc(v) {
        Some(c) => c,
        None => return false,
    };
    fat_set(v, tail, nc);
    dir_zero_cluster(v, nc);
    true
}

unsafe fn dir_find_free_run_slot(v: &mut Vol, dir_cl: u32, need: u32) -> Option<u32> {
    let mut run = 0u32;
    let mut run_start = 0u32;
    let mut slot = 0u32;
    loop {
        if slot >= dir_slots(v, dir_cl) {
            if !dir_extend(v, dir_cl) {
                return None;
            }
            continue;
        }
        let Some(off) = dir_entry_off(v, dir_cl, slot) else {
            return None;
        };
        if de_is_free(v, off) {
            if run == 0 {
                run_start = slot;
            }
            run += 1;
            if run >= need {
                return Some(run_start);
            }
        } else {
            run = 0;
        }
        slot += 1;
        if slot > 65536 {
            return None;
        }
    }
}

unsafe fn write_lfn_entry(v: &mut Vol, off: usize, seq: u8, chk: u8, chunk: &[u16]) {
    let b = v.buf.add(off);
    *b = seq;
    for i in 0..5 {
        let u = chunk.get(i).copied().unwrap_or(0xffff);
        put16(b.add(1 + i * 2), u);
    }
    *b.add(11) = ATTR_LFN;
    *b.add(12) = LFN_TYPE;
    *b.add(13) = chk;
    for i in 0..6 {
        let u = chunk.get(5 + i).copied().unwrap_or(0xffff);
        put16(b.add(14 + i * 2), u);
    }
    put16(b.add(26), 0);
    for i in 0..2 {
        let u = chunk.get(11 + i).copied().unwrap_or(0xffff);
        put16(b.add(28 + i * 2), u);
    }
}

unsafe fn read_lfn_chunk(v: &Vol, off: usize) -> ([u16; LFN_CHARS], u8, u8) {
    let b = v.buf.add(off);
    let seq = *b;
    let chk = *b.add(13);
    let mut chunk = [0xffffu16; LFN_CHARS];
    for i in 0..5 {
        chunk[i] = u16le(b.add(1 + i * 2));
    }
    for i in 0..6 {
        chunk[5 + i] = u16le(b.add(14 + i * 2));
    }
    for i in 0..2 {
        chunk[11 + i] = u16le(b.add(28 + i * 2));
    }
    (chunk, seq, chk)
}

unsafe fn slot_of_off(v: &Vol, dir_cl: u32, want: usize) -> Option<u32> {
    let slots = dir_slots(v, dir_cl);
    for s in 0..slots {
        if dir_entry_off(v, dir_cl, s) == Some(want) {
            return Some(s);
        }
    }
    None
}

unsafe fn read_lfn_before(v: &Vol, dir_cl: u32, sfn_off: usize) -> Option<Vec<u16>> {
    let (n, e) = de_read83(v, sfn_off);
    let want = lfn_checksum(&n, &e);
    let sfn_slot = slot_of_off(v, dir_cl, sfn_off)?;
    if sfn_slot == 0 {
        return None;
    }
    let mut parts: Vec<(u8, [u16; LFN_CHARS])> = Vec::new();
    let mut slot = sfn_slot;
    loop {
        if slot == 0 {
            break;
        }
        slot -= 1;
        let off = dir_entry_off(v, dir_cl, slot)?;
        if de_attr(v, off) != ATTR_LFN {
            break;
        }
        let (chunk, seq, chk) = read_lfn_chunk(v, off);
        if chk != want {
            break;
        }
        parts.push((seq, chunk));
        if (seq & 0x40) != 0 {
            break;
        }
    }
    if parts.is_empty() {
        return None;
    }
    parts.sort_by_key(|p| p.0 & 0x3f);
    let mut name = Vec::new();
    for (_, chunk) in parts {
        for u in chunk {
            if u == 0 || u == 0xffff {
                return Some(name);
            }
            if name.len() >= MAX_LFN_UCS2 {
                return Some(name);
            }
            name.push(u);
        }
    }
    Some(name)
}

unsafe fn write_dir_entry(v: &mut Vol, dir_cl: u32, long_name: &str, attr: u8, cl: u32, sz: u32) -> Option<usize> {
    /* Volume labels are short-name only (no LFN chain). */
    if (attr & ATTR_VOL) != 0 {
        let (n, e) = to83_strict(long_name).unwrap_or_else(|| gen_short_alias(long_name, 1));
        let start_slot = dir_find_free_run_slot(v, dir_cl, 1)?;
        let off = dir_entry_off(v, dir_cl, start_slot)?;
        de_write(v, off, &n, &e, attr, cl, sz);
        return Some(off);
    }
    let ucs2 = utf8_to_ucs2(long_name);
    let lfn_cnt = if ucs2.is_empty() {
        0
    } else {
        (ucs2.len() + LFN_CHARS - 1) / LFN_CHARS
    };
    let need = lfn_cnt as u32 + 1;
    let mut seq_try = 1u32;
    let (n, e) = loop {
        let alias = gen_short_alias(long_name, seq_try);
        if dir_find_sfn(v, dir_cl, &alias.0, &alias.1).is_none() {
            break alias;
        }
        seq_try += 1;
        if seq_try > 9999 {
            return None;
        }
    };
    let start_slot = dir_find_free_run_slot(v, dir_cl, need)?;
    let sfn_slot = start_slot + lfn_cnt as u32;
    let sfn_off = dir_entry_off(v, dir_cl, sfn_slot)?;
    if lfn_cnt > 0 {
        let chk = lfn_checksum(&n, &e);
        /* On-disk order: highest ordinal first (with 0x40), then down to 1, then SFN. */
        for i in 0..lfn_cnt {
            let ord = (lfn_cnt - i) as u8;
            let seq = if i == 0 { 0x40u8 | ord } else { ord };
            let begin = (ord as usize - 1) * LFN_CHARS;
            let end = core::cmp::min(begin + LFN_CHARS, ucs2.len());
            let mut chunk = [0xffffu16; LFN_CHARS];
            for (j, &u) in ucs2[begin..end].iter().enumerate() {
                chunk[j] = u;
            }
            if end < begin + LFN_CHARS {
                /* NUL terminator after last char; rest 0xFFFF */
                if end - begin < LFN_CHARS {
                    chunk[end - begin] = 0;
                }
            }
            let off = dir_entry_off(v, dir_cl, start_slot + i as u32)?;
            write_lfn_entry(v, off, seq, chk, &chunk);
        }
    }
    de_write(v, sfn_off, &n, &e, attr, cl, sz);
    Some(sfn_off)
}

unsafe fn dir_find_sfn(v: &Vol, dir_cl: u32, n: &[u8; 8], e: &[u8; 3]) -> Option<usize> {
    let slots = dir_slots(v, dir_cl);
    for s in 0..slots {
        let off = dir_entry_off(v, dir_cl, s)?;
        let c0 = *v.buf.add(off);
        if c0 == DE_END {
            break;
        }
        if c0 == DELETED || de_attr(v, off) == ATTR_LFN {
            continue;
        }
        if (*v.buf.add(off + 11) & ATTR_VOL) != 0 {
            continue;
        }
        let (nn, ee) = de_read83(v, off);
        if nn == *n && ee == *e {
            return Some(off);
        }
    }
    None
}

unsafe fn dir_find_name(v: &Vol, dir_cl: u32, name: &str) -> Option<usize> {
    if let Some((n, e)) = to83_strict(name) {
        if let Some(off) = dir_find_sfn(v, dir_cl, &n, &e) {
            return Some(off);
        }
    }
    let slots = dir_slots(v, dir_cl);
    for s in 0..slots {
        let off = dir_entry_off(v, dir_cl, s)?;
        let c0 = *v.buf.add(off);
        if c0 == DE_END {
            break;
        }
        if c0 == DELETED || de_attr(v, off) == ATTR_LFN || (de_attr(v, off) & ATTR_VOL) != 0 {
            continue;
        }
        if let Some(lfn) = read_lfn_before(v, dir_cl, off) {
            let mut ascii = String::new();
            for u in lfn {
                if u < 0x80 {
                    ascii.push(u as u8 as char);
                }
            }
            if ascii_eq(&ascii, name) {
                return Some(off);
            }
        }
    }
    None
}

unsafe fn resolve_parent(v: &Vol, path: &str) -> Option<(u32, String)> {
    let path = norm_path(path);
    if path.is_empty() {
        return None;
    }
    let (parent, leaf) = split_parent(&path);
    let mut dir_cl = root_dir_cl(v);
    if !parent.is_empty() {
        for part in parent.split('/').filter(|p| !p.is_empty()) {
            let off = dir_find_name(v, dir_cl, part)?;
            if (de_attr(v, off) & ATTR_DIR) == 0 {
                return None;
            }
            dir_cl = de_clust(v, off);
        }
    }
    Some((dir_cl, String::from(leaf)))
}

unsafe fn lookup_path(v: &Vol, path: &str) -> Option<usize> {
    let path = norm_path(path);
    if path.is_empty() {
        return None;
    }
    let (dir_cl, leaf) = resolve_parent(v, &path)?;
    dir_find_name(v, dir_cl, &leaf)
}

unsafe fn ensure_dir_chain(v: &mut Vol, path: &str) -> Option<u32> {
    let path = norm_path(path);
    if path.is_empty() {
        return Some(root_dir_cl(v));
    }
    let mut dir_cl = root_dir_cl(v);
    for part in path.split('/').filter(|p| !p.is_empty()) {
        if let Some(off) = dir_find_name(v, dir_cl, part) {
            if (de_attr(v, off) & ATTR_DIR) == 0 {
                return None;
            }
            dir_cl = de_clust(v, off);
            continue;
        }
        let nc = clust_alloc(v)?;
        if !dir_init_sub(v, nc, dir_cl) {
            return None;
        }
        write_dir_entry(v, dir_cl, part, ATTR_DIR | ATTR_ARC, nc, 0)?;
        dir_cl = nc;
    }
    Some(dir_cl)
}

unsafe fn dir_init_sub(v: &mut Vol, cl: u32, parent_cl: u32) -> bool {
    dir_zero_cluster(v, cl);
    let base = clust_off(v, cl);
    de_write(v, base, b".       ", b"   ", ATTR_DIR, cl, 0);
    de_write(v, base + 32, b"..      ", b"   ", ATTR_DIR, parent_cl, 0);
    true
}

unsafe fn mkdir_one(v: &mut Vol, path: &str) -> i32 {
    let path = norm_path(path);
    if path.is_empty() {
        return -1;
    }
    let (parent, leaf) = split_parent(&path);
    let parent_cl = if parent.is_empty() {
        root_dir_cl(v)
    } else if let Some(pc) = ensure_dir_chain(v, parent) {
        pc
    } else {
        return -1;
    };
    if dir_find_name(v, parent_cl, leaf).is_some() {
        return -1;
    }
    let nc = match clust_alloc(v) {
        Some(x) => x,
        None => return -1,
    };
    if !dir_init_sub(v, nc, parent_cl) {
        return -1;
    }
    if write_dir_entry(v, parent_cl, leaf, ATTR_DIR | ATTR_ARC, nc, 0).is_none() {
        return -1;
    }
    0
}

unsafe fn create_file(v: &mut Vol, path: &str, trunc: bool) -> Option<usize> {
    let path = norm_path(path);
    let (dir_cl, leaf) = resolve_parent(v, &path)?;
    if let Some(off) = dir_find_name(v, dir_cl, &leaf) {
        if (de_attr(v, off) & ATTR_DIR) != 0 {
            return None;
        }
        if trunc {
            let cl = de_clust(v, off);
            if cl >= 2 {
                clust_free_chain(v, cl);
            }
            de_set_clust(v, off, 0);
            de_set_size(v, off, 0);
        }
        return Some(off);
    }
    write_dir_entry(v, dir_cl, &leaf, ATTR_ARC, 0, 0)
}

unsafe fn delete_entry(v: &mut Vol, dir_cl: u32, sfn_off: usize) {
    if let Some(sfn_slot) = slot_of_off(v, dir_cl, sfn_off) {
        let mut slot = sfn_slot;
        loop {
            if slot == 0 {
                break;
            }
            slot -= 1;
            let Some(prev) = dir_entry_off(v, dir_cl, slot) else {
                break;
            };
            if de_attr(v, prev) != ATTR_LFN {
                break;
            }
            de_del(v, prev);
        }
    }
    de_del(v, sfn_off);
}

unsafe fn read_clust(v: &Vol, cl: u32, sec_in: u32, dst: *mut u8, off: usize, n: usize) -> usize {
    if dst.is_null() || n == 0 {
        return 0;
    }
    let sec = clust_sec(v, cl) + sec_in;
    let base = sec as usize * SECTOR + off;
    let copy = core::cmp::min(n, v.len.saturating_sub(base));
    for i in 0..copy {
        unsafe {
            *dst.add(i) = *v.buf.add(base + i);
        }
    }
    copy
}

unsafe fn write_clust(v: &mut Vol, cl: u32, sec_in: u32, src: *const u8, off: usize, n: usize) -> usize {
    if src.is_null() || n == 0 {
        return 0;
    }
    let sec = clust_sec(v, cl) + sec_in;
    let base = sec as usize * SECTOR + off;
    if base >= v.len {
        return 0;
    }
    let copy = core::cmp::min(n, v.len - base);
    for i in 0..copy {
        unsafe {
            *v.buf.add(base + i) = *src.add(i);
        }
    }
    copy
}

unsafe fn file_read_at(v: &Vol, start_cl: u32, size: u32, pos: u32, dst: *mut u8, len: u32) -> u32 {
    if start_cl < 2 || pos >= size || dst.is_null() || len == 0 {
        return 0;
    }
    let csize = v.lay.sec_per_clust as u32 * SECTOR as u32;
    let mut cl = start_cl;
    let mut cur = 0u32;
    while cur + csize <= pos {
        let n = fat_get(v, cl);
        if fat_is_eoc(v, n) {
            return 0;
        }
        cl = n;
        cur += csize;
    }
    let mut todo = core::cmp::min(len, size - pos) as usize;
    let mut p = pos - cur;
    let mut wrote = 0usize;
    loop {
        let sec = p / SECTOR as u32;
        let soff = (p % SECTOR as u32) as usize;
        let chunk = core::cmp::min(todo, SECTOR - soff);
        let n = read_clust(v, cl, sec, dst.add(wrote), soff, chunk);
        if n == 0 {
            break;
        }
        wrote += n;
        todo -= n;
        if todo == 0 {
            break;
        }
        let next = fat_get(v, cl);
        if fat_is_eoc(v, next) {
            break;
        }
        cl = next;
        p = 0;
    }
    wrote as u32
}

unsafe fn file_write_at(v: &mut Vol, entry_off: usize, pos: u32, src: *const u8, len: u32) -> u32 {
    if src.is_null() || len == 0 {
        return 0;
    }
    let mut start = de_clust(v, entry_off);
    let mut size = de_size(v, entry_off);
    let csize = v.lay.sec_per_clust as u32 * SECTOR as u32;
    if start < 2 {
        start = match clust_alloc(v) {
            Some(c) => c,
            None => return 0,
        };
        de_set_clust(v, entry_off, start);
    }
    let end = pos.saturating_add(len);
    if end > size {
        size = end;
        de_set_size(v, entry_off, size);
    }
    let mut cl = start;
    let mut cur = 0u32;
    while cur + csize <= pos {
        let n = fat_get(v, cl);
        if fat_is_eoc(v, n) {
            if cur + csize > pos {
                break;
            }
            cl = match clust_extend(v, cl) {
                Some(c) => c,
                None => return 0,
            };
        } else {
            cl = n;
        }
        cur += csize;
    }
    let mut todo = len as usize;
    let mut p = pos - cur;
    let mut done_n = 0usize;
    loop {
        let sec = p / SECTOR as u32;
        let soff = (p % SECTOR as u32) as usize;
        let chunk = core::cmp::min(todo, SECTOR - soff);
        let n = write_clust(v, cl, sec, src.add(done_n), soff, chunk);
        if n == 0 {
            break;
        }
        done_n += n;
        todo -= n;
        if todo == 0 {
            break;
        }
        let next = fat_get(v, cl);
        if fat_is_eoc(v, next) {
            cl = match clust_extend(v, cl) {
                Some(c) => c,
                None => break,
            };
        } else {
            cl = next;
        }
        p = 0;
    }
    done_n as u32
}

unsafe fn write_bpb_common(b: *mut u8, lay: &Layout) {
    *b.add(0) = 0xeb;
    *b.add(1) = 0x3c;
    *b.add(2) = 0x90;
    let oem = b"METAL   ";
    for i in 0..8 {
        *b.add(3 + i) = oem[i];
    }
    put16(b.add(0x0b), SECTOR as u16);
    *b.add(0x0d) = lay.sec_per_clust;
    *b.add(0x15) = 0xf8;
    put16(b.add(0x18), 63);
    put16(b.add(0x1a), 255);
    put32(b.add(0x1c), 0);
    *b.add(0x1fe) = 0x55;
    *b.add(0x1ff) = 0xaa;
}

unsafe fn format_core(buf: *mut u8, len: usize) -> Option<Layout> {
    if buf.is_null() {
        return None;
    }
    let lay = pick_layout(len)?;
    for i in 0..len {
        *buf.add(i) = 0;
    }
    let b = buf;
    write_bpb_common(b, &lay);
    if lay.is_fat32 {
        put16(b.add(0x0e), RESERVED32);
        *b.add(0x10) = NUM_FATS;
        put16(b.add(0x11), 0);
        put16(b.add(0x13), 0);
        put16(b.add(0x16), 0);
        put32(b.add(0x20), lay.total_secs);
        put32(b.add(0x24), lay.fat_secs);
        put32(b.add(0x2c), lay.root_clust);
        put16(b.add(0x30), 1);
        put16(b.add(0x32), 6);
        *b.add(0x40) = 0x80;
        *b.add(0x42) = 0x29;
        put32(b.add(0x43), 0x4d455441);
        let lbl = b"METAL VOL  ";
        for i in 0..11 {
            *b.add(0x47 + i) = lbl[i];
        }
        let fst = b"FAT32   ";
        for i in 0..8 {
            *b.add(0x52 + i) = fst[i];
        }
        put32(buf.add(lay.fat1), FAT32_MEDIA);
        put32(buf.add(lay.fat1 + 4), FAT32_EOC);
        put32(buf.add(lay.fat1 + 8), FAT32_EOC);
        put32(buf.add(lay.fat2), FAT32_MEDIA);
        put32(buf.add(lay.fat2 + 4), FAT32_EOC);
        put32(buf.add(lay.fat2 + 8), FAT32_EOC);
        let mut v = Vol { buf, len, lay };
        dir_init_sub(&mut v, lay.root_clust, 0);
        write_dir_entry(
            &mut v,
            lay.root_clust,
            "METAL VOL",
            ATTR_VOL,
            0,
            0,
        );
    } else {
        put16(b.add(0x0e), RESERVED16);
        *b.add(0x10) = NUM_FATS;
        put16(b.add(0x11), ROOT_ENTRIES);
        if lay.total_secs < 65536 {
            put16(b.add(0x13), lay.total_secs as u16);
        } else {
            put16(b.add(0x13), 0);
        }
        put16(b.add(0x16), lay.fat_secs as u16);
        if lay.total_secs >= 65536 {
            put32(b.add(0x20), lay.total_secs);
        }
        *b.add(0x24) = 0x80;
        *b.add(0x26) = 0x29;
        put32(b.add(0x27), 0x4d455441);
        let lbl = b"METAL VOL  ";
        for i in 0..11 {
            *b.add(0x2b + i) = lbl[i];
        }
        let fst = b"FAT16   ";
        for i in 0..8 {
            *b.add(0x36 + i) = fst[i];
        }
        put16(buf.add(lay.fat1), FAT16_MEDIA);
        put16(buf.add(lay.fat1 + 2), FAT16_EOC);
        put16(buf.add(lay.fat2), FAT16_MEDIA);
        put16(buf.add(lay.fat2 + 2), FAT16_EOC);
        let mut v = Vol { buf, len, lay };
        de_write(
            &mut v,
            lay.root_sec,
            b"METAL   ",
            b"VOL",
            ATTR_VOL,
            0,
            0,
        );
    }
    Some(lay)
}

unsafe fn parse_layout(buf: *const u8, len: usize) -> Option<Layout> {
    if buf.is_null() || len < SECTOR {
        return None;
    }
    if *buf.add(0x1fe) != 0x55 || *buf.add(0x1ff) != 0xaa {
        return None;
    }
    if u16le(buf.add(0x0b)) != SECTOR as u16 {
        return None;
    }
    let spc = *buf.add(0x0d);
    if spc == 0 {
        return None;
    }
    let root_ent = u16le(buf.add(0x11));
    let fat16_sz = u16le(buf.add(0x16));
    let fat32_sz = u32le(buf.add(0x24));
    let rsvd = u16le(buf.add(0x0e));
    let mut total = u16le(buf.add(0x13)) as u32;
    if total == 0 {
        total = u32le(buf.add(0x20));
    }
    if total == 0 || (total as usize) * SECTOR > len {
        return None;
    }
    if root_ent == 0 && fat16_sz == 0 && fat32_sz != 0 {
        let fat_secs = fat32_sz;
        let fat1 = SECTOR * rsvd as usize;
        let fat2 = fat1 + SECTOR * fat_secs as usize;
        let root_clust = u32le(buf.add(0x2c));
        let meta = rsvd as u32 + (NUM_FATS as u32) * fat_secs;
        let data_secs = total.saturating_sub(meta);
        let clust_count = data_secs / spc as u32;
        Some(Layout {
            is_fat32: true,
            total_secs: total,
            sec_per_clust: spc,
            fat_secs,
            fat1,
            fat2,
            root_sec: 0,
            root_clust,
            data_sec: meta,
            clust_count,
            root_entries: 0,
        })
    } else {
        let fat_secs = fat16_sz as u32;
        let fat1 = SECTOR * rsvd as usize;
        let fat2 = fat1 + SECTOR * fat_secs as usize;
        let root = fat2 + SECTOR * fat_secs as usize;
        let root_secs = (root_ent as u32 * 32 + SECTOR as u32 - 1) / SECTOR as u32;
        let meta = rsvd as u32 + (NUM_FATS as u32) * fat_secs + root_secs;
        let data_secs = total.saturating_sub(meta);
        let clust_count = data_secs / spc as u32;
        Some(Layout {
            is_fat32: false,
            total_secs: total,
            sec_per_clust: spc,
            fat_secs,
            fat1,
            fat2,
            root_sec: root,
            root_clust: 0,
            data_sec: meta,
            clust_count,
            root_entries: root_ent,
        })
    }
}

unsafe fn ensure_ops_registered() {
    if !*addr_of!(OPS_READY) {
        let _ = pm_metal_fs_ops_register(&FAT_OPS);
        *addr_of_mut!(OPS_READY) = true;
    }
}

/// Format `buf` as empty FAT16 or FAT32. Returns 0 ok, -1 error.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_fs_fat_format_buf(buf: *mut u8, len: usize) -> i32 {
    match format_core(buf, len) {
        Some(_) => 0,
        None => -1,
    }
}

/// Open in-memory FAT volume. Returns vol id (nonzero) or 0.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_fs_fat_open_buf(buf: *mut u8, len: usize) -> u32 {
    let lay = match parse_layout(buf, len) {
        Some(l) => l,
        None => return 0,
    };
    let vols = &mut *addr_of_mut!(VOLS);
    let mut id = 1usize;
    while id < MAX_VOL {
        if vols[id].is_none() {
            break;
        }
        id += 1;
    }
    if id >= MAX_VOL {
        return 0;
    }
    let _ = NEXT_VOL.fetch_add(1, Ordering::Relaxed);
    vols[id] = Some(Vol { buf, len, lay });
    ensure_ops_registered();
    id as u32
}

/// Close volume. Returns 0 ok, -1 error.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_fs_fat_close(vol: u32) -> i32 {
    let i = vol as usize;
    let vols = &mut *addr_of_mut!(VOLS);
    let files = &mut *addr_of_mut!(FILES);
    if i == 0 || i >= MAX_VOL || vols[i].is_none() {
        return -1;
    }
    for f in files.iter_mut() {
        if let Some(h) = f {
            if h.vol == vol {
                *f = None;
            }
        }
    }
    vols[i] = None;
    0
}

/// Mount FAT buffer at `target` (registers fat ops). Returns 0 ok.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_fs_fat_mount(target: *const u8, buf: *mut u8, len: usize) -> i32 {
    let id = pm_metal_fs_fat_open_buf(buf, len);
    if id == 0 {
        return -1;
    }
    ensure_ops_registered();
    let ctx = id as usize as *mut c_void;
    if vfs::pm_metal_vfs_mount(target, &FAT_OPS as *const _ as *const c_void, ctx) == 0 {
        let _ = pm_metal_fs_fat_close(id);
        return -1;
    }
    0
}

/// Format if needed, then write files (`names` paths may contain `/`).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_fs_fat_seed_simple(
    buf: *mut u8,
    len: usize,
    names: *const *const u8,
    datas: *const *const u8,
    lens: *const u32,
    count: u32,
) -> i32 {
    if buf.is_null() || names.is_null() || datas.is_null() || lens.is_null() {
        return -1;
    }
    if parse_layout(buf, len).is_none() && pm_metal_fs_fat_format_buf(buf, len) != 0 {
        return -1;
    }
    let lay = match parse_layout(buf, len) {
        Some(l) => l,
        None => return -1,
    };
    let mut v = Vol { buf, len, lay };
    for i in 0..count as usize {
        let path = cstr(*names.add(i));
        let src = *datas.add(i);
        let slen = *lens.add(i);
        if src.is_null() {
            return -1;
        }
        let norm = norm_path(path);
        let (parent, _leaf) = split_parent(&norm);
        if !parent.is_empty() {
            if ensure_dir_chain(&mut v, parent).is_none() {
                return -1;
            }
        }
        let off = match create_file(&mut v, path, true) {
            Some(o) => o,
            None => return -1,
        };
        if slen > 0 {
            let n = file_write_at(&mut v, off, 0, src, slen);
            if n != slen {
                return -1;
            }
        }
    }
    0
}

/// Mount FAT volume living in a ramdisk handle at `target`.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_fs_fat_mount_ram(target: *const u8, ram_h: u32) -> i32 {
    let mut ptr: *mut u8 = core::ptr::null_mut();
    let mut len: usize = 0;
    if pm_metal_dev_blk_ram_bytes(ram_h, &mut ptr, &mut len) != 0 || ptr.is_null() || len == 0 {
        return -1;
    }
    pm_metal_fs_fat_mount(target, ptr, len)
}

unsafe extern "C" fn op_open(ctx: *mut c_void, path: *const u8, flags: u32) -> u32 {
    let vol = ctx as u32;
    let files = &mut *addr_of_mut!(FILES);
    let Some(v) = vol_ref(vol) else {
        return done(PM_METAL_FS_INVALID);
    };
    let path = cstr(path);
    let norm = norm_path(path);
    let want_dir = (flags & PM_METAL_FS_O_DIRECTORY) != 0 || norm.is_empty();
    if norm.is_empty() {
        let mut slot = None;
        for i in 0..MAX_OPEN {
            if files[i].is_none() {
                slot = Some(i);
                break;
            }
        }
        let Some(fi) = slot else {
            return done(PM_METAL_FS_INVALID);
        };
        files[fi] = Some(OpenH {
            vol,
            is_dir: true,
            entry_off: if v.lay.is_fat32 {
                clust_off(v, v.lay.root_clust)
            } else {
                v.lay.root_sec
            },
            first_clust: root_dir_cl(v),
            dir_clust: root_dir_cl(v),
            dir_slot: 0,
            pos: 0,
            size: 0,
        });
        pm_metal_fs_set_active_ops(&FAT_OPS, ctx);
        return done(fi as u32);
    }
    let mut entry_off = lookup_path(v, &norm);
    if entry_off.is_none() && (flags & PM_METAL_FS_O_CREAT) != 0 && !want_dir {
        let Some(vm) = vol_mut(vol) else {
            return done(PM_METAL_FS_INVALID);
        };
        entry_off = create_file(vm, &norm, (flags & PM_METAL_FS_O_TRUNC) != 0);
    }
    let Some(off) = entry_off else {
        return done(PM_METAL_FS_INVALID);
    };
    let attr = de_attr(v, off);
    let is_dir = (attr & ATTR_DIR) != 0;
    if want_dir && !is_dir {
        return done(PM_METAL_FS_INVALID);
    }
    if !want_dir && is_dir {
        return done(PM_METAL_FS_INVALID);
    }
    if !is_dir {
        let wr = (flags & (PM_METAL_FS_O_WRONLY | PM_METAL_FS_O_RDWR)) != 0;
        let rd = (flags & (PM_METAL_FS_O_RDONLY | PM_METAL_FS_O_RDWR)) != 0;
        if !wr && !rd {
            return done(PM_METAL_FS_INVALID);
        }
        if (attr & ATTR_RO) != 0 && wr {
            return done(PM_METAL_FS_INVALID);
        }
        if (flags & PM_METAL_FS_O_TRUNC) != 0 && wr {
            if let Some(vm) = vol_mut(vol) {
                let cl = de_clust(vm, off);
                if cl >= 2 {
                    clust_free_chain(vm, cl);
                }
                de_set_clust(vm, off, 0);
                de_set_size(vm, off, 0);
            }
        }
    }
    let mut slot = None;
    for i in 0..MAX_OPEN {
        if files[i].is_none() {
            slot = Some(i);
            break;
        }
    }
    let Some(fi) = slot else {
        return done(PM_METAL_FS_INVALID);
    };
    let dir_clust = if is_dir { de_clust(v, off) } else { 0 };
    files[fi] = Some(OpenH {
        vol,
        is_dir,
        entry_off: off,
        first_clust: de_clust(v, off),
        dir_clust,
        dir_slot: 0,
        pos: 0,
        size: de_size(v, off),
    });
    pm_metal_fs_set_active_ops(&FAT_OPS, ctx);
    done(fi as u32)
}

unsafe extern "C" fn op_close(_ctx: *mut c_void, h: u32) -> u32 {
    if (h as usize) < MAX_OPEN {
        (*addr_of_mut!(FILES))[h as usize] = None;
    }
    done(0)
}

unsafe extern "C" fn op_fread(ctx: *mut c_void, h: u32, dest: *mut u8, len: u32) -> u32 {
    let files = &*addr_of!(FILES);
    let pos = files
        .get(h as usize)
        .and_then(|f| f.as_ref())
        .map(|f| f.pos)
        .unwrap_or(0);
    op_fpread(ctx, h, pos, dest, len)
}

unsafe extern "C" fn op_fwrite(ctx: *mut c_void, h: u32, src: *const u8, len: u32) -> u32 {
    let files = &*addr_of!(FILES);
    let pos = files
        .get(h as usize)
        .and_then(|f| f.as_ref())
        .map(|f| f.pos)
        .unwrap_or(0);
    op_fpwrite(ctx, h, pos, src, len)
}

unsafe extern "C" fn op_fpread(_ctx: *mut c_void, h: u32, off: u32, dest: *mut u8, len: u32) -> u32 {
    let files = &mut *addr_of_mut!(FILES);
    let Some(f) = files.get(h as usize).and_then(|x| x.as_ref()) else {
        return done(0);
    };
    if f.is_dir {
        return done(0);
    }
    let Some(v) = vol_ref(f.vol) else {
        return done(0);
    };
    let n = file_read_at(v, f.first_clust, f.size, off, dest, len);
    if let Some(fm) = files[h as usize].as_mut() {
        fm.pos = off + n;
    }
    done(n)
}

unsafe extern "C" fn op_fpwrite(ctx: *mut c_void, h: u32, off: u32, src: *const u8, len: u32) -> u32 {
    let files = &mut *addr_of_mut!(FILES);
    let Some(f) = files.get(h as usize).and_then(|x| x.as_ref()) else {
        return done(0);
    };
    if f.is_dir {
        return done(0);
    }
    let vol = f.vol;
    let entry_off = f.entry_off;
    let Some(vm) = vol_mut(vol) else {
        return done(0);
    };
    let n = file_write_at(vm, entry_off, off, src, len);
    let sz = de_size(vm, entry_off);
    let cl = de_clust(vm, entry_off);
    if let Some(fm) = files[h as usize].as_mut() {
        fm.pos = off + n;
        fm.size = sz;
        fm.first_clust = cl;
    }
    let _ = ctx;
    done(n)
}

unsafe extern "C" fn op_lseek(_ctx: *mut c_void, h: u32, off: i32, whence: u32) -> i32 {
    let files = &mut *addr_of_mut!(FILES);
    let Some(f) = files.get_mut(h as usize).and_then(|x| x.as_mut()) else {
        return -1;
    };
    let end = if f.is_dir {
        dir_slots(vol_ref(f.vol).unwrap(), f.dir_clust) as i32
    } else {
        f.size as i32
    };
    let cur = f.pos as i32;
    let np = match whence {
        PM_METAL_FS_SEEK_SET => off,
        PM_METAL_FS_SEEK_CUR => cur + off,
        PM_METAL_FS_SEEK_END => end + off,
        _ => return -1,
    };
    if np < 0 {
        return -1;
    }
    if f.is_dir {
        f.dir_slot = np as u32;
    }
    f.pos = np as u32;
    np
}

unsafe extern "C" fn op_stat(ctx: *mut c_void, path: *const u8, st_out: *mut u8) -> u32 {
    let vol = ctx as u32;
    let Some(v) = vol_ref(vol) else {
        return done(PM_METAL_FS_INVALID);
    };
    let path = norm_path(cstr(path));
    let Some(off) = lookup_path(v, &path) else {
        return done(PM_METAL_FS_INVALID);
    };
    if !st_out.is_null() {
        let st = st_out as *mut pm_metal_fs_stat_t;
        let attr = de_attr(v, off);
        (*st).size = de_size(v, off);
        (*st).type_ = if (attr & ATTR_DIR) != 0 {
            PM_METAL_FS_TYPE_DIR
        } else {
            PM_METAL_FS_TYPE_FILE
        };
    }
    done(0)
}

unsafe extern "C" fn op_readdir(ctx: *mut c_void, h: u32, name_out: *mut u8, name_cap: u32) -> u32 {
    let vol = ctx as u32;
    let files = &mut *addr_of_mut!(FILES);
    let Some(f) = files.get(h as usize).and_then(|x| x.as_ref()) else {
        return done(0);
    };
    if !f.is_dir || name_out.is_null() || name_cap == 0 {
        return done(0);
    }
    let Some(v) = vol_ref(f.vol) else {
        return done(0);
    };
    let dir_cl = if f.dir_clust != 0 {
        f.dir_clust
    } else {
        root_dir_cl(v)
    };
    let slots = dir_slots(v, dir_cl);
    let mut s = f.dir_slot;
    while s < slots {
        let Some(off) = dir_entry_off(v, dir_cl, s) else {
            break;
        };
        s += 1;
        let c0 = *v.buf.add(off);
        if c0 == DE_END {
            break;
        }
        if c0 == DELETED {
            continue;
        }
        let attr = de_attr(v, off);
        if attr == ATTR_LFN || (attr & ATTR_VOL) != 0 {
            continue;
        }
        let (n, e) = de_read83(v, off);
        if n[0] == b'.' {
            continue;
        }
        if let Some(lfn) = read_lfn_before(v, dir_cl, off) {
            ucs2_to_ascii(name_out, name_cap, &lfn);
        } else {
            name83_out(&n, &e, name_out, name_cap);
        }
        if let Some(fm) = files[h as usize].as_mut() {
            fm.dir_slot = s;
        }
        let _ = vol;
        return done(1);
    }
    if let Some(fm) = files[h as usize].as_mut() {
        fm.dir_slot = s;
    }
    done(0)
}

unsafe extern "C" fn op_mkdir(ctx: *mut c_void, path: *const u8) -> u32 {
    let vol = ctx as u32;
    let Some(vm) = vol_mut(vol) else {
        return done(PM_METAL_FS_INVALID);
    };
    let path = norm_path(cstr(path));
    if mkdir_one(vm, &path) == 0 {
        done(0)
    } else {
        done(PM_METAL_FS_INVALID)
    }
}

unsafe extern "C" fn op_unlink(ctx: *mut c_void, path: *const u8) -> u32 {
    let vol = ctx as u32;
    let Some(vm) = vol_mut(vol) else {
        return done(PM_METAL_FS_INVALID);
    };
    let path = norm_path(cstr(path));
    let (dir_cl, leaf) = match resolve_parent(vm, &path) {
        Some(x) => x,
        None => return done(PM_METAL_FS_INVALID),
    };
    let off = match dir_find_name(vm, dir_cl, &leaf) {
        Some(o) => o,
        None => return done(PM_METAL_FS_INVALID),
    };
    let attr = de_attr(vm, off);
    if (attr & ATTR_DIR) != 0 {
        let cl = de_clust(vm, off);
        let mut child = 0u32;
        let slots = dir_slots(vm, cl);
        for s in 0..slots {
            if let Some(co) = dir_entry_off(vm, cl, s) {
                let c0 = *vm.buf.add(co);
                if c0 != DE_END && c0 != DELETED && de_attr(vm, co) != ATTR_LFN {
                    child += 1;
                }
            }
        }
        if child > 2 {
            return done(PM_METAL_FS_INVALID);
        }
        clust_free_chain(vm, cl);
    } else {
        let cl = de_clust(vm, off);
        if cl >= 2 {
            clust_free_chain(vm, cl);
        }
    }
    delete_entry(vm, dir_cl, off);
    done(0)
}




#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn seed_long_names_fat16() {
        let mut buf = vec![0u8; 256 * 1024];
        unsafe {
            assert_eq!(pm_metal_fs_fat_format_buf(buf.as_mut_ptr(), buf.len()), 0);
            let n1 = b"hello_world.py\0";
            let d1 = b"py\n";
            let n2 = b"long_subdir_name/my_module.py\0";
            let d2 = b"nested\n";
            let names = [n1.as_ptr(), n2.as_ptr()];
            let datas = [d1.as_ptr(), d2.as_ptr()];
            let lens = [d1.len() as u32, d2.len() as u32];
            assert_eq!(
                pm_metal_fs_fat_seed_simple(
                    buf.as_mut_ptr(),
                    buf.len(),
                    names.as_ptr(),
                    datas.as_ptr(),
                    lens.as_ptr(),
                    2,
                ),
                0
            );
            /* LFN stores UCS-2 in discontinuous dirent fields; probe alias + LFN attr. */
            assert!(buf.windows(8).any(|w| w == b"HELL~1  "));
            assert!(buf.iter().any(|&b| b == ATTR_LFN));
            let lay = parse_layout(buf.as_ptr(), buf.len()).unwrap();
            let vol = Vol {
                buf: buf.as_mut_ptr(),
                len: buf.len(),
                lay,
            };
            assert!(dir_find_name(&vol, 0, "hello_world.py").is_some());
            assert!(dir_find_name(&vol, 0, "long_subdir_name").is_some());
            let dir = de_clust(&vol, dir_find_name(&vol, 0, "long_subdir_name").unwrap());
            assert!(dir_find_name(&vol, dir, "my_module.py").is_some());
        }
    }

    #[test]
    fn seed_long_names_fat32() {
        let mut buf = vec![0u8; 32 * 1024 * 1024];
        unsafe {
            assert_eq!(pm_metal_fs_fat_format_buf(buf.as_mut_ptr(), buf.len()), 0);
            let lay = parse_layout(buf.as_ptr(), buf.len()).unwrap();
            assert!(lay.is_fat32);
            let n1 = b"hello_world.py\0";
            let d1 = b"py\n";
            let names = [n1.as_ptr()];
            let datas = [d1.as_ptr()];
            let lens = [d1.len() as u32];
            assert_eq!(
                pm_metal_fs_fat_seed_simple(
                    buf.as_mut_ptr(),
                    buf.len(),
                    names.as_ptr(),
                    datas.as_ptr(),
                    lens.as_ptr(),
                    1,
                ),
                0
            );
            let vol = Vol {
                buf: buf.as_mut_ptr(),
                len: buf.len(),
                lay,
            };
            assert!(dir_find_name(&vol, lay.root_clust, "hello_world.py").is_some());
        }
    }
}
