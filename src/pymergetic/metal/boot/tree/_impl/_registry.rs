//! Static section table — emitters add content via the tree builder API only.

use core::fmt::Write;

use pymergetic_metal_dt::{
    pm_metal_dt_bus_t, pm_metal_dt_class_t, pm_metal_dt_count_class, pm_metal_dt_foreach, DtNode,
};

use super::api::{
    item_name, item_str, pm_metal_boot_tree_blank, pm_metal_boot_tree_enter,
    pm_metal_boot_tree_leave, pm_metal_boot_tree_spacer, pm_metal_boot_tree_status_t,
};
use super::line::LineBuf;

extern "C" {
    fn pm_metal_async_ready() -> i32;
    fn pm_metal_async_n_runners() -> u32;
    fn pm_metal_async_runner_addr(i: u32) -> usize;
    fn pm_metal_async_runner_qlen(i: u32, high: *mut u32, med: *mut u32, low: *mut u32) -> i32;
    fn pm_metal_dev_acpi_cpu_apic_id(index: u32) -> i32;
    fn pm_metal_time_tsc_per_us() -> u64;
    fn pm_metal_mem_map_used() -> usize;
    fn pm_metal_mem_tlsf_used() -> usize;
    fn pm_metal_mem_hole() -> usize;
    fn pm_metal_net_ip_if_count() -> u32;
    fn pm_metal_net_ip_if_status_index(index: u32, dest: *mut u8, dest_cap: u32) -> i32;
    fn pm_metal_net_ssh_listen_port() -> u32;
    fn pm_metal_net_ssh_hostkey_label(buf: *mut u8, buf_len: u32) -> i32;
    fn pm_metal_net_http_autoload_port() -> u16;
    fn pm_metal_fs_vfs_mount_count() -> u32;
    fn pm_metal_fs_vfs_mount_info(
        index: u32,
        target_out: *mut u8,
        target_cap: u32,
        fstype_out: *mut u8,
        fstype_cap: u32,
    ) -> i32;
    fn pm_metal_fs_mount_statfs(index: u32, out: *mut pm_metal_fs_statfs_t) -> i32;
    fn pm_metal_util_size_format(out: *mut u8, cap: usize, bytes: u64) -> i32;
    fn pm_metal_wasm_ready() -> i32;
    fn pm_metal_external_count() -> u32;
    fn pm_metal_external_get(idx: u32, out: *mut pm_metal_external_t) -> i32;
}

#[repr(C)]
struct pm_metal_external_t {
    id: *const u8,
    version: *const u8,
    url: *const u8,
    note: *const u8,
}

#[repr(C)]
struct pm_metal_fs_statfs_t {
    total: u64,
    used: u64,
    flags: u32,
}

const PM_METAL_FS_ST_RDONLY: u32 = 1;

use pm_metal_boot_tree_status_t::{
    PM_METAL_BOOT_TREE_ACCENT, PM_METAL_BOOT_TREE_DIM, PM_METAL_BOOT_TREE_FAIL,
    PM_METAL_BOOT_TREE_OK, PM_METAL_BOOT_TREE_WARN,
};

fn fmt_size(bytes: u64) -> ([u8; 16], usize) {
    let mut buf = [0u8; 16];
    let n = unsafe { pm_metal_util_size_format(buf.as_mut_ptr(), buf.len(), bytes) };
    if n <= 0 {
        buf[0] = b'?';
        return (buf, 1);
    }
    (buf, n as usize)
}

fn detail_buf() -> LineBuf {
    LineBuf::new()
}

fn as_str(buf: &LineBuf) -> &str {
    let n = buf.pos_len();
    core::str::from_utf8(buf.bytes(n)).unwrap_or("")
}

pub type EmitFn = unsafe fn() -> i32;

pub struct Section {
    #[allow(dead_code)]
    pub id: &'static str,
    pub emit: EmitFn,
}

fn class_name(c: pm_metal_dt_class_t) -> &'static str {
    match c {
        pm_metal_dt_class_t::PM_METAL_DT_CLASS_TIME => "time",
        pm_metal_dt_class_t::PM_METAL_DT_CLASS_GFX => "gfx",
        pm_metal_dt_class_t::PM_METAL_DT_CLASS_AUDIO => "audio",
        pm_metal_dt_class_t::PM_METAL_DT_CLASS_INPUT => "input",
        pm_metal_dt_class_t::PM_METAL_DT_CLASS_FS => "fs",
        pm_metal_dt_class_t::PM_METAL_DT_CLASS_STREAM => "stream",
        pm_metal_dt_class_t::PM_METAL_DT_CLASS_NET => "net",
        pm_metal_dt_class_t::PM_METAL_DT_CLASS_RANDOM => "random",
        pm_metal_dt_class_t::PM_METAL_DT_CLASS_BLK => "blk",
        pm_metal_dt_class_t::PM_METAL_DT_CLASS_MEM => "mem",
        _ => "?",
    }
}

fn bus_name(b: pm_metal_dt_bus_t) -> &'static str {
    match b {
        pm_metal_dt_bus_t::PM_METAL_DT_BUS_PLATFORM => "plat",
        pm_metal_dt_bus_t::PM_METAL_DT_BUS_PCI => "pci",
        pm_metal_dt_bus_t::PM_METAL_DT_BUS_ISA => "isa",
    }
}

fn compat_str(p: *const u8) -> &'static str {
    if p.is_null() {
        return "-";
    }
    let mut n = 0usize;
    unsafe {
        while n < 32 && *p.add(n) != 0 {
            n += 1;
        }
        core::str::from_utf8_unchecked(core::slice::from_raw_parts(p, n))
    }
}

/// Matches `pymergetic.metal` `.pm/module` version.
const METAL_VERSION: &str = "0.1.0";

unsafe fn emit_root() -> i32 {
    pm_metal_boot_tree_blank();
    item_str(PM_METAL_BOOT_TREE_ACCENT, "pymergetic metal", METAL_VERSION);
    pm_metal_boot_tree_spacer();
    0
}

unsafe fn emit_area_breakdown() {
    let map = pm_metal_mem_map_used() as u64;
    let hole = pm_metal_mem_hole() as u64;
    let tlsf = pm_metal_mem_tlsf_used() as u64;
    let rows: [(&str, u64); 3] = [("map", map), ("hole", hole), ("tlsf (heap)", tlsf)];
    pm_metal_boot_tree_enter();
    for &(name, bytes) in &rows {
        let (sz, szn) = fmt_size(bytes);
        let human = core::str::from_utf8(&sz[..szn]).unwrap_or("?");
        item_str(PM_METAL_BOOT_TREE_DIM, name, human);
    }
    pm_metal_boot_tree_leave();
}

unsafe extern "C" fn on_mem(node: *const DtNode, _ctx: *mut core::ffi::c_void) -> i32 {
    if node.is_null() {
        return 0;
    }
    let n = &*node;
    if n.class != pm_metal_dt_class_t::PM_METAL_DT_CLASS_MEM {
        return 0;
    }
    let base = ((n.loc[1] as u64) << 32) | (n.loc[0] as u64);
    let size = ((n.loc[3] as u64) << 32) | (n.loc[2] as u64);
    let name = compat_str(n.compat);
    let is_area = name == "area";
    let (sz, szn) = fmt_size(size);
    let human = core::str::from_utf8(&sz[..szn]).unwrap_or("?");
    let mut d = detail_buf();
    let _ = write!(d, "base=0x{:016x} size={}", base, human);
    item_str(PM_METAL_BOOT_TREE_DIM, name, as_str(&d));
    if is_area {
        emit_area_breakdown();
    }
    0
}

unsafe fn emit_mem() -> i32 {
    let n = pm_metal_dt_count_class(pm_metal_dt_class_t::PM_METAL_DT_CLASS_MEM);
    if n == 0 {
        item_str(PM_METAL_BOOT_TREE_FAIL, "mem", "FAIL");
        return -1;
    }
    let mut d = detail_buf();
    let _ = write!(d, "{} region(s)", n);
    item_str(PM_METAL_BOOT_TREE_OK, "mem", as_str(&d));
    pm_metal_boot_tree_enter();
    pm_metal_dt_foreach(Some(on_mem), core::ptr::null_mut());
    pm_metal_boot_tree_leave();
    0
}

unsafe fn emit_cpu() -> i32 {
    let mut n = pm_metal_async_n_runners();
    if n == 0 {
        n = 1;
    }
    let mhz = pm_metal_time_tsc_per_us();
    let mut d = detail_buf();
    let _ = write!(d, "{}  tsc ~{} MHz", n, mhz);
    item_str(PM_METAL_BOOT_TREE_OK, "cpu", as_str(&d));
    pm_metal_boot_tree_enter();
    for i in 0..n {
        let mut name = detail_buf();
        let _ = write!(name, "cpu{}", i);
        let apic = pm_metal_dev_acpi_cpu_apic_id(i);
        if apic >= 0 {
            let mut det = detail_buf();
            let _ = write!(det, "apic={}", apic);
            item_str(PM_METAL_BOOT_TREE_DIM, as_str(&name), as_str(&det));
        } else {
            item_name(PM_METAL_BOOT_TREE_DIM, as_str(&name));
        }
    }
    pm_metal_boot_tree_leave();
    0
}

unsafe extern "C" fn on_dev(node: *const DtNode, _ctx: *mut core::ffi::c_void) -> i32 {
    if node.is_null() {
        return 0;
    }
    let n = &*node;
    if n.class == pm_metal_dt_class_t::PM_METAL_DT_CLASS_MEM {
        return 0;
    }
    let mut name = detail_buf();
    let _ = write!(
        name,
        "{}/{}",
        class_name(n.class),
        compat_str(n.compat)
    );
    let mut det = detail_buf();
    let _ = write!(det, "bus={}", bus_name(n.bus));
    item_str(PM_METAL_BOOT_TREE_DIM, as_str(&name), as_str(&det));
    0
}

unsafe extern "C" fn count_non_mem(node: *const DtNode, ctx: *mut core::ffi::c_void) -> i32 {
    if node.is_null() || ctx.is_null() {
        return 0;
    }
    if (*node).class != pm_metal_dt_class_t::PM_METAL_DT_CLASS_MEM {
        let n = &mut *(ctx as *mut u32);
        *n = n.wrapping_add(1);
    }
    0
}

unsafe fn emit_devices() -> i32 {
    let mut total = 0u32;
    pm_metal_dt_foreach(
        Some(count_non_mem),
        &mut total as *mut u32 as *mut core::ffi::c_void,
    );
    let mut d = detail_buf();
    let _ = write!(d, "{} node(s)", total);
    item_str(PM_METAL_BOOT_TREE_OK, "devices", as_str(&d));
    if total == 0 {
        return 0;
    }
    pm_metal_boot_tree_enter();
    pm_metal_dt_foreach(Some(on_dev), core::ptr::null_mut());
    pm_metal_boot_tree_leave();
    0
}

unsafe fn emit_fs() -> i32 {
    let count = pm_metal_fs_vfs_mount_count();
    if count == 0 {
        item_str(PM_METAL_BOOT_TREE_FAIL, "fs", "FAIL");
        return -1;
    }
    let mut d = detail_buf();
    let _ = write!(d, "{} mount(s)", count);
    item_str(PM_METAL_BOOT_TREE_OK, "fs", as_str(&d));
    pm_metal_boot_tree_enter();
    for index in 0..count {
        let mut target = [0u8; 128];
        let mut fstype = [0u8; 32];
        if pm_metal_fs_vfs_mount_info(
            index,
            target.as_mut_ptr(),
            target.len() as u32,
            fstype.as_mut_ptr(),
            fstype.len() as u32,
        ) != 0
        {
            continue;
        }
        let tlen = target.iter().position(|&b| b == 0).unwrap_or(target.len());
        let flen = fstype.iter().position(|&b| b == 0).unwrap_or(fstype.len());
        let t = core::str::from_utf8_unchecked(&target[..tlen]);
        let f = core::str::from_utf8_unchecked(&fstype[..flen]);
        let mut st = pm_metal_fs_statfs_t {
            total: 0,
            used: 0,
            flags: 0,
        };
        let _ = pm_metal_fs_mount_statfs(index, &mut st);
        let mode = if (st.flags & PM_METAL_FS_ST_RDONLY) != 0 {
            "ro"
        } else {
            "rw"
        };
        let (sz, szn) = fmt_size(st.total);
        let human = core::str::from_utf8(&sz[..szn]).unwrap_or("?");
        let mut name = detail_buf();
        let _ = write!(name, "{:<12}", t);
        let mut det = detail_buf();
        let _ = write!(det, "{:<8} {}  {}", f, human, mode);
        item_str(PM_METAL_BOOT_TREE_DIM, as_str(&name), as_str(&det));
    }
    pm_metal_boot_tree_leave();
    0
}

unsafe fn emit_net() -> i32 {
    let count = pm_metal_net_ip_if_count();
    let ssh_port = pm_metal_net_ssh_listen_port();
    let http_port = pm_metal_net_http_autoload_port() as u32;
    if count == 0 && ssh_port == 0 && http_port == 0 {
        item_str(PM_METAL_BOOT_TREE_WARN, "net", "WARN");
        return 0;
    }

    item_str(PM_METAL_BOOT_TREE_OK, "net", "ok");
    pm_metal_boot_tree_enter();

    for index in 0..count {
        let mut slot = [0u8; 192];
        if pm_metal_net_ip_if_status_index(index, slot.as_mut_ptr(), slot.len() as u32) < 0 {
            continue;
        }
        let len = slot.iter().position(|&b| b == 0).unwrap_or(slot.len());
        let text = core::str::from_utf8_unchecked(&slot[..len]);
        item_name(PM_METAL_BOOT_TREE_DIM, text);
    }
    if ssh_port != 0 {
        let mut hk = [0u8; 32];
        let _ = pm_metal_net_ssh_hostkey_label(hk.as_mut_ptr(), hk.len() as u32);
        let hlen = hk.iter().position(|&b| b == 0).unwrap_or(0);
        let hks = if hlen > 0 {
            core::str::from_utf8_unchecked(&hk[..hlen])
        } else {
            "delay"
        };
        let mut line = detail_buf();
        let _ = write!(
            line,
            "sshd :{} (dropbear, hostkey={})",
            ssh_port, hks
        );
        item_name(PM_METAL_BOOT_TREE_DIM, as_str(&line));
    }
    if http_port != 0 {
        let mut line = detail_buf();
        if super::notes::http_ok() {
            let _ = write!(line, "httpd :{} (health ok)", http_port);
        } else {
            let _ = write!(line, "httpd :{}", http_port);
        }
        item_name(PM_METAL_BOOT_TREE_DIM, as_str(&line));
    }
    pm_metal_boot_tree_leave();
    0
}

unsafe fn emit_async() -> i32 {
    if pm_metal_async_ready() == 0 {
        item_str(PM_METAL_BOOT_TREE_FAIL, "async", "FAIL");
        return -1;
    }
    let n = pm_metal_async_n_runners();
    let show_await = super::notes::await_ok();
    let show_concurrency = super::notes::concurrency_ok();
    let mut d = detail_buf();
    let _ = write!(d, "ok ({} runners)", n);
    item_str(PM_METAL_BOOT_TREE_OK, "async", as_str(&d));
    pm_metal_boot_tree_enter();
    for i in 0..n {
        let addr = pm_metal_async_runner_addr(i);
        let mut high = 0u32;
        let mut med = 0u32;
        let mut low = 0u32;
        let _ = pm_metal_async_runner_qlen(i, &mut high, &mut med, &mut low);
        let mut name = detail_buf();
        let _ = write!(name, "r{}", i);
        let mut det = detail_buf();
        let _ = write!(det, "@0x{:x}  q={}/{}/{}", addr, high, med, low);
        item_str(PM_METAL_BOOT_TREE_DIM, as_str(&name), as_str(&det));
    }
    if show_await {
        item_name(PM_METAL_BOOT_TREE_DIM, "await ok");
    }
    if show_concurrency {
        item_name(PM_METAL_BOOT_TREE_DIM, "concurrency ok");
    }
    pm_metal_boot_tree_leave();
    0
}

unsafe fn emit_wasm() -> i32 {
    /* Runtime ready (product init) or stress proof note. */
    if pm_metal_wasm_ready() != 0 || super::notes::wasm_ok() {
        item_str(PM_METAL_BOOT_TREE_OK, "wasm", "ok");
    } else {
        item_str(PM_METAL_BOOT_TREE_DIM, "wasm", "-");
    }
    0
}

unsafe fn emit_externals() -> i32 {
    let n = pm_metal_external_count();
    if n == 0 {
        item_str(PM_METAL_BOOT_TREE_DIM, "externals", "-");
        return 0;
    }
    let mut d = detail_buf();
    let _ = write!(d, "{}", n);
    item_str(PM_METAL_BOOT_TREE_OK, "externals", as_str(&d));
    pm_metal_boot_tree_enter();
    for i in 0..n {
        let mut e = pm_metal_external_t {
            id: core::ptr::null(),
            version: core::ptr::null(),
            url: core::ptr::null(),
            note: core::ptr::null(),
        };
        if pm_metal_external_get(i, &mut e) != 0 || e.id.is_null() {
            continue;
        }
        let id = compat_str(e.id);
        let ver = if e.version.is_null() {
            ""
        } else {
            compat_str(e.version)
        };
        if ver.is_empty() {
            item_name(PM_METAL_BOOT_TREE_DIM, id);
        } else {
            item_str(PM_METAL_BOOT_TREE_DIM, id, ver);
        }
    }
    pm_metal_boot_tree_leave();
    0
}

unsafe fn emit_ready() -> i32 {
    item_str(PM_METAL_BOOT_TREE_OK, "ready", "ok");
    0
}

pub static SECTIONS: &[Section] = &[
    Section {
        id: "root",
        emit: emit_root,
    },
    Section {
        id: "mem",
        emit: emit_mem,
    },
    Section {
        id: "cpu",
        emit: emit_cpu,
    },
    Section {
        id: "devices",
        emit: emit_devices,
    },
    Section {
        id: "fs",
        emit: emit_fs,
    },
    Section {
        id: "net",
        emit: emit_net,
    },
    Section {
        id: "async",
        emit: emit_async,
    },
    Section {
        id: "wasm",
        emit: emit_wasm,
    },
    Section {
        id: "externals",
        emit: emit_externals,
    },
    Section {
        id: "ready",
        emit: emit_ready,
    },
];
