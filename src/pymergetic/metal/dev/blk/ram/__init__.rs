//! In-memory block device (ramdisk). Sector async I/O completes Ready immediately.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(dead_code, non_camel_case_types)]

extern crate alloc;

use alloc::vec::Vec;
use core::ptr::{addr_of, addr_of_mut};
use core::sync::atomic::{AtomicU32, Ordering};

use pymergetic_metal_rt as _;

extern "C" {
    fn pm_metal_async_completed_u32(v: u32) -> u32;
}

pub const PM_METAL_BLK_RAM_SECTOR: u32 = 512;
const MAX_RAM: usize = 8;

struct RamDisk {
    data: Vec<u8>,
    sector: u32,
}

static mut DISKS: [Option<RamDisk>; MAX_RAM] = [const { None }; MAX_RAM];
static NEXT: AtomicU32 = AtomicU32::new(1);

fn done(v: u32) -> u32 {
    unsafe { pm_metal_async_completed_u32(v) }
}

/// Create zeroed ramdisk of `bytes` (rounded up to sectors). Returns handle != 0 or 0.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_blk_ram_create(bytes: usize) -> u32 {
    if bytes == 0 {
        return 0;
    }
    let sector = PM_METAL_BLK_RAM_SECTOR as usize;
    let n = (bytes + sector - 1) / sector * sector;
    let disks = &mut *addr_of_mut!(DISKS);
    let mut id = 1usize;
    while id < MAX_RAM {
        if disks[id].is_none() {
            break;
        }
        id += 1;
    }
    if id >= MAX_RAM {
        return 0;
    }
    let _ = NEXT.fetch_add(1, Ordering::Relaxed);
    disks[id] = Some(RamDisk {
        data: alloc::vec![0u8; n],
        sector: PM_METAL_BLK_RAM_SECTOR,
    });
    id as u32
}

/// Wrap an existing mutable image buffer (copied into owned Vec). Returns handle or 0.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_blk_ram_from_image(src: *const u8, len: usize) -> u32 {
    if src.is_null() || len == 0 {
        return 0;
    }
    let h = pm_metal_dev_blk_ram_create(len);
    if h == 0 {
        return 0;
    }
    let disks = &mut *addr_of_mut!(DISKS);
    let d = disks[h as usize].as_mut().unwrap();
    let n = core::cmp::min(len, d.data.len());
    for i in 0..n {
        d.data[i] = *src.add(i);
    }
    h
}

/// Destroy ramdisk. Returns 0 ok.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_blk_ram_destroy(h: u32) -> i32 {
    let i = h as usize;
    let disks = &mut *addr_of_mut!(DISKS);
    if i == 0 || i >= MAX_RAM || disks[i].is_none() {
        return -1;
    }
    disks[i] = None;
    0
}

/// Capacity in sectors.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_blk_ram_capacity_sectors(h: u32) -> u64 {
    let disks = &*addr_of!(DISKS);
    match disks.get(h as usize).and_then(|d| d.as_ref()) {
        Some(d) => (d.data.len() as u64) / (d.sector as u64),
        None => 0,
    }
}

/// Export raw bytes pointer + length (valid until destroy). Returns 0 ok.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_blk_ram_bytes(
    h: u32,
    out_ptr: *mut *mut u8,
    out_len: *mut usize,
) -> i32 {
    if out_ptr.is_null() || out_len.is_null() {
        return -1;
    }
    let disks = &mut *addr_of_mut!(DISKS);
    match disks.get_mut(h as usize).and_then(|d| d.as_mut()) {
        Some(d) => {
            *out_ptr = d.data.as_mut_ptr();
            *out_len = d.data.len();
            0
        }
        None => -1,
    }
}

/// Async sector read -> completed with 0 ok / nonzero fail. Buffer is host memory.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_blk_ram_read_async(
    h: u32,
    lba: u64,
    buf: *mut u8,
    nsec: u32,
) -> u32 {
    if buf.is_null() || nsec == 0 {
        return done(u32::MAX);
    }
    let disks = &*addr_of!(DISKS);
    let Some(d) = disks.get(h as usize).and_then(|x| x.as_ref()) else {
        return done(u32::MAX);
    };
    let off = (lba as usize).saturating_mul(d.sector as usize);
    let n = (nsec as usize).saturating_mul(d.sector as usize);
    if off + n > d.data.len() {
        return done(u32::MAX);
    }
    for i in 0..n {
        *buf.add(i) = d.data[off + i];
    }
    done(0)
}

/// Async sector write -> completed with 0 ok.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_blk_ram_write_async(
    h: u32,
    lba: u64,
    buf: *const u8,
    nsec: u32,
) -> u32 {
    if buf.is_null() || nsec == 0 {
        return done(u32::MAX);
    }
    let disks = &mut *addr_of_mut!(DISKS);
    let Some(d) = disks.get_mut(h as usize).and_then(|x| x.as_mut()) else {
        return done(u32::MAX);
    };
    let off = (lba as usize).saturating_mul(d.sector as usize);
    let n = (nsec as usize).saturating_mul(d.sector as usize);
    if off + n > d.data.len() {
        return done(u32::MAX);
    }
    for i in 0..n {
        d.data[off + i] = *buf.add(i);
    }
    done(0)
}


use core::ffi::c_void;

use pymergetic_metal_reg::{pm_metal_reg_mod_load, RegMod};

pymergetic_metal_reg::reg_mod! {
    mod ram = "pymergetic.metal.dev.blk.ram";
    exports: [create, from_image, destroy, capacity_sectors, bytes, read_async, write_async];
}

extern "C" fn ram_register_symbols(_ctx: *mut c_void) -> i32 {
    ram::create.publish(pm_metal_dev_blk_ram_create as *const c_void);
    ram::from_image.publish(pm_metal_dev_blk_ram_from_image as *const c_void);
    ram::destroy.publish(pm_metal_dev_blk_ram_destroy as *const c_void);
    ram::capacity_sectors.publish(pm_metal_dev_blk_ram_capacity_sectors as *const c_void);
    ram::bytes.publish(pm_metal_dev_blk_ram_bytes as *const c_void);
    ram::read_async.publish(pm_metal_dev_blk_ram_read_async as *const c_void);
    ram::write_async.publish(pm_metal_dev_blk_ram_write_async as *const c_void);
    0
}

static RAM_MOD: RegMod = RegMod::from_static(
    ram::NAME,
    &ram::STORAGE.exports,
    &ram::STORAGE.imports,
    Some(ram_register_symbols),
);

#[no_mangle]
pub extern "C" fn pm_metal_dev_blk_ram_reg_load() -> i32 {
    if pymergetic_metal_reg::find_mod(ram::NAME).is_some() {
        return 0;
    }
    unsafe { pm_metal_reg_mod_load(&RAM_MOD) }
}

#[inline]
pub fn reg_load() -> i32 {
    pm_metal_dev_blk_ram_reg_load()
}
