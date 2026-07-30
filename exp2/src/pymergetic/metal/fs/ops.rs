//! Public fstype driver ABI — host/guest register backends here.
//!
//! Parallel to product `pm_metal_blk_ops_t`: fill the vtable, register a
//! name, mount via vfs with that name + ctx.

use core::ffi::c_void;

/// Async open → handle; result_u32 is `pm_metal_fs_h` or error code.
pub type pm_metal_fs_op_open_fn = Option<
    unsafe extern "C" fn(ctx: *mut c_void, path: *const u8, flags: u32) -> u32,
>;
pub type pm_metal_fs_op_path_fn =
    Option<unsafe extern "C" fn(ctx: *mut c_void, path: *const u8) -> u32>;
pub type pm_metal_fs_op_h_fn = Option<unsafe extern "C" fn(ctx: *mut c_void, h: u32) -> u32>;
pub type pm_metal_fs_op_read_fn = Option<
    unsafe extern "C" fn(ctx: *mut c_void, h: u32, dest: *mut u8, len: u32) -> u32,
>;
pub type pm_metal_fs_op_write_fn = Option<
    unsafe extern "C" fn(ctx: *mut c_void, h: u32, src: *const u8, len: u32) -> u32,
>;
pub type pm_metal_fs_op_pread_fn = Option<
    unsafe extern "C" fn(ctx: *mut c_void, h: u32, off: u32, dest: *mut u8, len: u32) -> u32,
>;
pub type pm_metal_fs_op_pwrite_fn = Option<
    unsafe extern "C" fn(ctx: *mut c_void, h: u32, off: u32, src: *const u8, len: u32) -> u32,
>;
pub type pm_metal_fs_op_stat_fn = Option<
    unsafe extern "C" fn(ctx: *mut c_void, path: *const u8, st_out: *mut u8) -> u32,
>;
pub type pm_metal_fs_op_readdir_fn = Option<
    unsafe extern "C" fn(ctx: *mut c_void, h: u32, name_out: *mut u8, name_cap: u32) -> u32,
>;
pub type pm_metal_fs_op_rename_fn = Option<
    unsafe extern "C" fn(ctx: *mut c_void, old: *const u8, new: *const u8) -> u32,
>;
pub type pm_metal_fs_op_lseek_fn =
    Option<unsafe extern "C" fn(ctx: *mut c_void, h: u32, off: i32, whence: u32) -> i32>;

/// Fstype driver vtable (async handles unless noted).
#[repr(C)]
pub struct pm_metal_fs_ops_t {
    pub name: *const u8,
    pub open: pm_metal_fs_op_open_fn,
    pub close: pm_metal_fs_op_h_fn,
    pub fread: pm_metal_fs_op_read_fn,
    pub fwrite: pm_metal_fs_op_write_fn,
    pub fpread: pm_metal_fs_op_pread_fn,
    pub fpwrite: pm_metal_fs_op_pwrite_fn,
    pub lseek: pm_metal_fs_op_lseek_fn,
    pub stat: pm_metal_fs_op_stat_fn,
    pub readdir: pm_metal_fs_op_readdir_fn,
    pub mkdir: pm_metal_fs_op_path_fn,
    pub unlink: pm_metal_fs_op_path_fn,
    pub rename: pm_metal_fs_op_rename_fn,
    pub fsync: pm_metal_fs_op_h_fn,
}

/* C vtable: raw pointers; single-threaded Metal host / firmware. */
unsafe impl Sync for pm_metal_fs_ops_t {}

const MAX_DRIVERS: usize = 16;

static mut DRIVERS: [Option<&'static pm_metal_fs_ops_t>; MAX_DRIVERS] = [None; MAX_DRIVERS];
static mut DRIVER_N: usize = 0;

/// Register a fstype (`ops.name` is a NUL C string, e.g. `mtar`). Returns 0 ok, -1 full/dup.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_fs_ops_register(ops: *const pm_metal_fs_ops_t) -> i32 {
    if ops.is_null() || (*ops).name.is_null() {
        return -1;
    }
    let n = DRIVER_N;
    if n >= MAX_DRIVERS {
        return -1;
    }
    for i in 0..n {
        if let Some(d) = DRIVERS[i] {
            if cstr_eq(d.name, (*ops).name) {
                return -1;
            }
        }
    }
    DRIVERS[n] = Some(&*ops);
    DRIVER_N = n + 1;
    0
}

/// Look up registered ops by fstype name. NULL if missing.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_fs_ops_lookup(name: *const u8) -> *const pm_metal_fs_ops_t {
    if name.is_null() {
        return core::ptr::null();
    }
    for i in 0..DRIVER_N {
        if let Some(d) = DRIVERS[i] {
            if cstr_eq(d.name, name) {
                return d as *const pm_metal_fs_ops_t;
            }
        }
    }
    core::ptr::null()
}

fn cstr_eq(a: *const u8, b: *const u8) -> bool {
    unsafe {
        let mut i = 0usize;
        loop {
            let ca = *a.add(i);
            let cb = *b.add(i);
            if ca != cb {
                return false;
            }
            if ca == 0 {
                return true;
            }
            i += 1;
            if i > 64 {
                return false;
            }
        }
    }
}
