//! Static section table — emitters only read DT/async state.

use core::fmt::Write;

use pymergetic_metal_dt::{
    pm_metal_dt_bus_t, pm_metal_dt_class_t, pm_metal_dt_count_class, pm_metal_dt_foreach, DtNode,
};
use pymergetic_metal_log::pm_metal_log_style_t;

use super::line::{emit, emit_str, LineBuf};

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
}

#[repr(C)]
struct pm_metal_fs_statfs_t {
    total: u64,
    used: u64,
    flags: u32,
}

const PM_METAL_FS_ST_RDONLY: u32 = 1;

fn fmt_size(bytes: u64) -> ([u8; 16], usize) {
    let mut buf = [0u8; 16];
    let n = unsafe { pm_metal_util_size_format(buf.as_mut_ptr(), buf.len(), bytes) };
    if n <= 0 {
        buf[0] = b'?';
        return (buf, 1);
    }
    (buf, n as usize)
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
    /* Blank row between banner and tree root. */
    emit_str(pm_metal_log_style_t::PM_METAL_LOG_STYLE_DEFAULT, "");
    let mut line = LineBuf::new();
    let _ = write!(line, "+-- pymergetic metal {}", METAL_VERSION);
    emit(
        pm_metal_log_style_t::PM_METAL_LOG_STYLE_ACCENT,
        &mut line,
    );
    /* Spacer under the root label (keeps the trunk `|`). */
    emit_str(pm_metal_log_style_t::PM_METAL_LOG_STYLE_DIM, "|");
    0
}

struct MemCtx {
    line: LineBuf,
    n: u32,
    i: u32,
}

unsafe fn emit_area_breakdown(line: &mut LineBuf) {
    /* Dual-span arena: map grows up, TLSF heap grows down, hole between. */
    let map = pm_metal_mem_map_used() as u64;
    let hole = pm_metal_mem_hole() as u64;
    let tlsf = pm_metal_mem_tlsf_used() as u64;
    let rows: [(&str, u64); 3] = [("map", map), ("hole", hole), ("tlsf (heap)", tlsf)];
    for (i, &(name, bytes)) in rows.iter().enumerate() {
        let (sz, szn) = fmt_size(bytes);
        let human = core::str::from_utf8(&sz[..szn]).unwrap_or("?");
        let branch = if i + 1 == rows.len() { "`--" } else { "+--" };
        line.clear();
        let _ = write!(line, "|       {} {}  {}", branch, name, human);
        emit(pm_metal_log_style_t::PM_METAL_LOG_STYLE_DIM, line);
    }
}

unsafe extern "C" fn on_mem(node: *const DtNode, ctx: *mut core::ffi::c_void) -> i32 {
    if node.is_null() || ctx.is_null() {
        return 0;
    }
    let n = &*node;
    if n.class != pm_metal_dt_class_t::PM_METAL_DT_CLASS_MEM {
        return 0;
    }
    let c = &mut *(ctx as *mut MemCtx);
    c.i = c.i.wrapping_add(1);
    let base = ((n.loc[1] as u64) << 32) | (n.loc[0] as u64);
    let size = ((n.loc[3] as u64) << 32) | (n.loc[2] as u64);
    let name = compat_str(n.compat);
    let is_area = name == "area";
    let last = c.i >= c.n;
    let branch = if last { "`--" } else { "+--" };
    let (sz, szn) = fmt_size(size);
    let human = core::str::from_utf8(&sz[..szn]).unwrap_or("?");
    c.line.clear();
    let _ = write!(
        c.line,
        "|   {} {}  base=0x{:016x} size={}",
        branch, name, base, human
    );
    emit(pm_metal_log_style_t::PM_METAL_LOG_STYLE_DIM, &mut c.line);
    if is_area {
        emit_area_breakdown(&mut c.line);
    }
    0
}

unsafe fn emit_mem() -> i32 {
    let n = pm_metal_dt_count_class(pm_metal_dt_class_t::PM_METAL_DT_CLASS_MEM);
    let mut line = LineBuf::new();
    if n == 0 {
        let _ = write!(line, "+-- mem          FAIL");
        emit(pm_metal_log_style_t::PM_METAL_LOG_STYLE_FAIL, &mut line);
        return -1;
    }
    let _ = write!(line, "+-- mem          {} region(s)", n);
    emit(pm_metal_log_style_t::PM_METAL_LOG_STYLE_OK, &mut line);
    let mut ctx = MemCtx {
        line: LineBuf::new(),
        n,
        i: 0,
    };
    pm_metal_dt_foreach(
        Some(on_mem),
        &mut ctx as *mut MemCtx as *mut core::ffi::c_void,
    );
    0
}

unsafe fn emit_cpu() -> i32 {
    let mut n = pm_metal_async_n_runners();
    if n == 0 {
        n = 1;
    }
    let mhz = pm_metal_time_tsc_per_us(); /* cycles/us ~= MHz */
    let mut line = LineBuf::new();
    let _ = write!(line, "+-- cpu          {}  tsc ~{} MHz", n, mhz);
    emit(pm_metal_log_style_t::PM_METAL_LOG_STYLE_OK, &mut line);
    for i in 0..n {
        line.clear();
        let branch = if i + 1 == n { "`--" } else { "+--" };
        let apic = pm_metal_dev_acpi_cpu_apic_id(i);
        if apic >= 0 {
            let _ = write!(line, "|   {} cpu{}  apic={}", branch, i, apic);
        } else {
            let _ = write!(line, "|   {} cpu{}", branch, i);
        }
        emit(pm_metal_log_style_t::PM_METAL_LOG_STYLE_DIM, &mut line);
    }
    0
}

struct DevCtx {
    line: LineBuf,
    n: u32,
    i: u32,
}

unsafe extern "C" fn on_dev(node: *const DtNode, ctx: *mut core::ffi::c_void) -> i32 {
    if node.is_null() || ctx.is_null() {
        return 0;
    }
    let n = &*node;
    if n.class == pm_metal_dt_class_t::PM_METAL_DT_CLASS_MEM {
        return 0;
    }
    let c = &mut *(ctx as *mut DevCtx);
    c.i = c.i.wrapping_add(1);
    let last = c.i >= c.n;
    let branch = if last { "`--" } else { "+--" };
    c.line.clear();
    let _ = write!(
        c.line,
        "|   {} {}/{}  bus={}",
        branch,
        class_name(n.class),
        compat_str(n.compat),
        bus_name(n.bus)
    );
    emit(pm_metal_log_style_t::PM_METAL_LOG_STYLE_DIM, &mut c.line);
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
    let mut line = LineBuf::new();
    let _ = write!(line, "+-- devices      {} node(s)", total);
    emit(pm_metal_log_style_t::PM_METAL_LOG_STYLE_OK, &mut line);
    if total == 0 {
        return 0;
    }
    let mut ctx = DevCtx {
        line: LineBuf::new(),
        n: total,
        i: 0,
    };
    pm_metal_dt_foreach(
        Some(on_dev),
        &mut ctx as *mut DevCtx as *mut core::ffi::c_void,
    );
    0
}

unsafe fn emit_fs() -> i32 {
    let count = pm_metal_fs_vfs_mount_count();
    let mut line = LineBuf::new();
    if count == 0 {
        let _ = write!(line, "+-- fs           FAIL");
        emit(pm_metal_log_style_t::PM_METAL_LOG_STYLE_FAIL, &mut line);
        return -1;
    }
    let _ = write!(line, "+-- fs           {} mount(s)", count);
    emit(pm_metal_log_style_t::PM_METAL_LOG_STYLE_OK, &mut line);
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
        line.clear();
        let branch = if index + 1 == count { "`--" } else { "+--" };
        let _ = write!(line, "|   {} {:<12} {:<8} {}  {}", branch, t, f, human, mode);
        emit(pm_metal_log_style_t::PM_METAL_LOG_STYLE_DIM, &mut line);
    }
    0
}

unsafe fn emit_net() -> i32 {
    let count = pm_metal_net_ip_if_count();
    let mut line = LineBuf::new();
    if count == 0 {
        let _ = write!(line, "+-- net          WARN");
        emit(pm_metal_log_style_t::PM_METAL_LOG_STYLE_WARN, &mut line);
        return 0;
    }

    let _ = write!(line, "+-- net          ok");
    emit(pm_metal_log_style_t::PM_METAL_LOG_STYLE_OK, &mut line);
    for index in 0..count {
        let mut status = [0u8; 192];
        if pm_metal_net_ip_if_status_index(index, status.as_mut_ptr(), status.len() as u32) < 0 {
            continue;
        }
        let len = status.iter().position(|&b| b == 0).unwrap_or(status.len());
        let text = core::str::from_utf8_unchecked(&status[..len]);
        line.clear();
        let branch = if index + 1 == count { "`--" } else { "+--" };
        let _ = write!(line, "|   {} {}", branch, text);
        emit(pm_metal_log_style_t::PM_METAL_LOG_STYLE_DIM, &mut line);
    }
    0
}

unsafe fn emit_async() -> i32 {
    let mut line = LineBuf::new();
    if pm_metal_async_ready() == 0 {
        let _ = write!(line, "+-- async        FAIL");
        emit(pm_metal_log_style_t::PM_METAL_LOG_STYLE_FAIL, &mut line);
        return -1;
    }
    let n = pm_metal_async_n_runners();
    /* Stackless runners: control-block addr + H/M/L queue depths. */
    let _ = write!(line, "+-- async        ok ({} runners)", n);
    emit(pm_metal_log_style_t::PM_METAL_LOG_STYLE_OK, &mut line);
    for i in 0..n {
        let addr = pm_metal_async_runner_addr(i);
        let mut high = 0u32;
        let mut med = 0u32;
        let mut low = 0u32;
        let _ = pm_metal_async_runner_qlen(i, &mut high, &mut med, &mut low);
        line.clear();
        let branch = if i + 1 == n { "`--" } else { "+--" };
        let _ = write!(
            line,
            "|   {} r{}  @0x{:x}  q={}/{}/{}",
            branch, i, addr, high, med, low
        );
        emit(pm_metal_log_style_t::PM_METAL_LOG_STYLE_DIM, &mut line);
    }
    0
}

unsafe fn emit_ready() -> i32 {
    emit_str(
        pm_metal_log_style_t::PM_METAL_LOG_STYLE_OK,
        "`-- ready        ok",
    );
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
        id: "ready",
        emit: emit_ready,
    },
];
