//! Product freestanding Rust image — one staticlib, no duplicate rt/core.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]

pub use pymergetic_metal_rt as rt;

use core::ffi::c_void;

use pymergetic_metal_async as metal_async;
use pymergetic_metal_dev_blk_ram as metal_dev_blk_ram;
use pymergetic_metal_dt as metal_dt;
use pymergetic_metal_fs as metal_fs;
use pymergetic_metal_fs_embed as metal_fs_embed;
use pymergetic_metal_fs_fat as metal_fs_fat;
use pymergetic_metal_fs_littlefs as metal_fs_littlefs;
use pymergetic_metal_fs_mtar as metal_fs_mtar;
use pymergetic_metal_fs_overlay as metal_fs_overlay;
use pymergetic_metal_fs_tmpfs as metal_fs_tmpfs;
use pymergetic_metal_fs_vfs as metal_fs_vfs;
use pymergetic_metal_fs_wasmmod as metal_fs_wasmmod;
use pymergetic_metal_fs_zip as metal_fs_zip;
use pymergetic_metal_hwtree as metal_hwtree;
use pymergetic_metal_log as metal_log;
use pymergetic_metal_mem as metal_mem;
use pymergetic_metal_mem_arena as metal_mem_arena;
use pymergetic_metal_mem_lock as metal_mem_lock;
use pymergetic_metal_mem_tlsf as metal_mem_tlsf;
use pymergetic_metal_net_ssh as metal_net_ssh;
use pymergetic_metal_reg::{pm_metal_reg_mod_load, find_mod, RegMod};
use pymergetic_metal_util_lz4 as metal_util_lz4;
use pymergetic_metal_util_size as metal_util_size;
use pymergetic_metal_util_tar as metal_util_tar;
use pymergetic_metal_wamr_host as metal_wamr_host;

/* ---- rt / mem.port: declared here to avoid Cargo cycles (reg → rt) ---- */

mod floor_c;
mod floor_dev;
mod floor_frozen;
mod floor_seats;

extern "C" {
    fn pm_metal_rt_halt() -> !;
    fn pm_metal_rt_panic(msg: *const u8) -> !;
    fn pm_metal_rt_panic_at(file: *const u8, line: u32, msg: *const u8) -> !;
    fn pm_metal_mem_alloc(size: usize) -> *mut u8;
    fn pm_metal_mem_free(ptr: *mut u8);
    fn pm_metal_mem_memalign(align: usize, size: usize) -> *mut u8;
    fn pm_metal_mem_realloc(ptr: *mut u8, size: usize) -> *mut u8;
    fn pm_metal_mem_free_bytes() -> usize;

    fn pm_metal_console_ready() -> i32;
    fn pm_metal_console_write(data: *const u8, n: usize) -> usize;
    fn pm_metal_console_init(buf: *mut u8, cap: usize) -> i32;
    fn pm_metal_console_create(id: i32, buf: *mut u8, cap: usize) -> i32;
    fn pm_metal_console_attach(sink: *const c_void, user: *mut c_void) -> i32;
    fn pm_metal_console_detach();
    fn pm_metal_console_seq() -> u64;
    fn pm_metal_console_len() -> usize;
    fn pm_metal_console_copy_tail(out: *mut u8, cap: usize) -> usize;

    fn pm_metal_process_spawn(
        step: *const c_void,
        state_bytes: u32,
        prio: u32,
        mode: u32,
        tag: *const u8,
        teardown: *const c_void,
        userdata: *mut c_void,
    ) -> u32;
    fn pm_metal_process_crown(
        async_handle: u32,
        mode: u32,
        tag: *const u8,
        teardown: *const c_void,
        userdata: *mut c_void,
    ) -> u32;
    fn pm_metal_process_quit(pid: u32, code: i32) -> i32;
    fn pm_metal_process_current() -> u32;
    fn pm_metal_process_list(infos: *mut c_void, max: u32) -> u32;
    fn pm_metal_process_quit_all(code: i32) -> u32;
    fn pm_metal_process_shutting_down() -> i32;
    fn pm_metal_process_set_shutting_down(on: i32);
}

pymergetic_metal_reg::reg_mod! {
    mod rt_mod = "pymergetic.metal.rt";
    exports: [halt, panic, panic_at];
}

extern "C" fn rt_register_symbols(_ctx: *mut c_void) -> i32 {
    rt_mod::halt.publish(pm_metal_rt_halt as *const c_void);
    rt_mod::panic.publish(pm_metal_rt_panic as *const c_void);
    rt_mod::panic_at.publish(pm_metal_rt_panic_at as *const c_void);
    0
}

static RT_MOD: RegMod = RegMod::from_static(
    rt_mod::NAME,
    &rt_mod::STORAGE.exports,
    &rt_mod::STORAGE.imports,
    Some(rt_register_symbols),
);

fn rt_reg_load() -> i32 {
    if find_mod(rt_mod::NAME).is_some() {
        return 0;
    }
    unsafe { pm_metal_reg_mod_load(&RT_MOD) }
}

pymergetic_metal_reg::reg_mod! {
    mod mem_port = "pymergetic.metal.mem.port";
    exports: [alloc, free, memalign, realloc, free_bytes];
}

extern "C" fn mem_port_register_symbols(_ctx: *mut c_void) -> i32 {
    mem_port::alloc.publish(pm_metal_mem_alloc as *const c_void);
    mem_port::free.publish(pm_metal_mem_free as *const c_void);
    mem_port::memalign.publish(pm_metal_mem_memalign as *const c_void);
    mem_port::realloc.publish(pm_metal_mem_realloc as *const c_void);
    mem_port::free_bytes.publish(pm_metal_mem_free_bytes as *const c_void);
    0
}

static MEM_PORT_MOD: RegMod = RegMod::from_static(
    mem_port::NAME,
    &mem_port::STORAGE.exports,
    &mem_port::STORAGE.imports,
    Some(mem_port_register_symbols),
);

fn mem_port_reg_load() -> i32 {
    if find_mod(mem_port::NAME).is_some() {
        return 0;
    }
    unsafe { pm_metal_reg_mod_load(&MEM_PORT_MOD) }
}

pymergetic_metal_reg::reg_mod! {
    mod console = "pymergetic.metal.console";
    exports: [ready, write, init, create, attach, detach, seq, len, copy_tail];
}

extern "C" fn console_register_symbols(_ctx: *mut c_void) -> i32 {
    console::ready.publish(pm_metal_console_ready as *const c_void);
    console::write.publish(pm_metal_console_write as *const c_void);
    console::init.publish(pm_metal_console_init as *const c_void);
    console::create.publish(pm_metal_console_create as *const c_void);
    console::attach.publish(pm_metal_console_attach as *const c_void);
    console::detach.publish(pm_metal_console_detach as *const c_void);
    console::seq.publish(pm_metal_console_seq as *const c_void);
    console::len.publish(pm_metal_console_len as *const c_void);
    console::copy_tail.publish(pm_metal_console_copy_tail as *const c_void);
    0
}

static CONSOLE_MOD: RegMod = RegMod::from_static(
    console::NAME,
    &console::STORAGE.exports,
    &console::STORAGE.imports,
    Some(console_register_symbols),
);

fn console_reg_load() -> i32 {
    if find_mod(console::NAME).is_some() {
        return 0;
    }
    unsafe { pm_metal_reg_mod_load(&CONSOLE_MOD) }
}

pymergetic_metal_reg::reg_mod! {
    mod process = "pymergetic.metal.process";
    exports: [spawn, crown, quit, current, list, quit_all, shutting_down, set_shutting_down];
}

extern "C" fn process_register_symbols(_ctx: *mut c_void) -> i32 {
    process::spawn.publish(pm_metal_process_spawn as *const c_void);
    process::crown.publish(pm_metal_process_crown as *const c_void);
    process::quit.publish(pm_metal_process_quit as *const c_void);
    process::current.publish(pm_metal_process_current as *const c_void);
    process::list.publish(pm_metal_process_list as *const c_void);
    process::quit_all.publish(pm_metal_process_quit_all as *const c_void);
    process::shutting_down.publish(pm_metal_process_shutting_down as *const c_void);
    process::set_shutting_down.publish(pm_metal_process_set_shutting_down as *const c_void);
    0
}

static PROCESS_MOD: RegMod = RegMod::from_static(
    process::NAME,
    &process::STORAGE.exports,
    &process::STORAGE.imports,
    Some(process_register_symbols),
);

fn process_reg_load() -> i32 {
    if find_mod(process::NAME).is_some() {
        return 0;
    }
    unsafe { pm_metal_reg_mod_load(&PROCESS_MOD) }
}

/* Floor RegMods: load permanently-linked modules into the kernel ring. */
#[no_mangle]
pub extern "C" fn pm_metal_reg_floor_load() -> i32 {
    let mut rc = 0i32;
    /* Spine first so soft imports (e.g. ssh → async.yield) can connect. */
    rc |= rt_reg_load();
    rc |= metal_async::reg_load();
    rc |= console_reg_load();
    rc |= metal_mem::reg_load();
    rc |= mem_port_reg_load();
    rc |= metal_mem_tlsf::reg_load();
    rc |= metal_mem_arena::reg_load();
    rc |= metal_mem_lock::reg_load();
    rc |= metal_log::reg_load();
    rc |= metal_dt::reg_load();
    rc |= process_reg_load();
    rc |= metal_fs_vfs::reg_load();
    rc |= metal_fs::reg_load();
    rc |= metal_fs_tmpfs::reg_load();
    rc |= metal_fs_overlay::reg_load();
    rc |= metal_fs_littlefs::reg_load();
    rc |= metal_fs_fat::reg_load();
    rc |= metal_fs_zip::reg_load();
    rc |= metal_fs_mtar::reg_load();
    rc |= metal_fs_embed::reg_load();
    rc |= metal_fs_wasmmod::reg_load();
    rc |= metal_dev_blk_ram::reg_load();
    rc |= metal_util_lz4::reg_load();
    rc |= metal_util_tar::reg_load();
    rc |= metal_util_size::reg_load();
    rc |= metal_hwtree::reg_load();
    rc |= metal_wamr_host::reg_load();
    rc |= metal_net_ssh::reg_load();
    rc |= floor_c::load_all();
    rc |= floor_dev::load_all();
    rc |= floor_frozen::load_all();
    rc |= floor_seats::load_all();
    rc
}

/* net.ip / net.wg RS faces live at path==module:
 *   src/pymergetic/metal/net/{ip,wg}/__init__.rs
 */
