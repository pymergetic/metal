//! Virtual console — ring buffer + viewport attach (concept, not hardware).
//! All I/O drains through manually attached viewports. History from `seq_begin`
//! is replayed when a viewport joins.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use core::ptr::{addr_of, addr_of_mut, null_mut};

use pymergetic_metal_mem as mem;
use pymergetic_metal_rt as _;

#[path = "_style.rs"]
mod style;

/* Max consoles (ids 0 .. CONSOLE_MAX-1). */
const CONSOLE_MAX: usize = 32;
/* Max viewports attached to one console. */
const VP_MAX: usize = 8;
/* Default ring size (bytes); allocated from the host heap. */
const DEFAULT_RING: usize = 64 * 1024;

/// Semantic line style — viewports map SGR / colors.
#[repr(u32)]
#[derive(Clone, Copy)]
pub enum pm_metal_console_style_t {
    PM_METAL_CONSOLE_STYLE_DEFAULT = 0,
    PM_METAL_CONSOLE_STYLE_DIM = 1,
    PM_METAL_CONSOLE_STYLE_OK = 2,
    PM_METAL_CONSOLE_STYLE_WARN = 3,
    PM_METAL_CONSOLE_STYLE_FAIL = 4,
    PM_METAL_CONSOLE_STYLE_ACCENT = 5,
}

pub type pm_metal_console_viewport_write_fn =
    Option<unsafe extern "C" fn(ctx: *mut u8, s: *const u8, n: usize)>;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct pm_metal_console_viewport_ops_t {
    pub write: pm_metal_console_viewport_write_fn,
}

#[derive(Clone, Copy)]
struct Viewport {
    ops: pm_metal_console_viewport_ops_t,
    ctx: *mut u8,
    seq_pos: u64,
    live: bool,
}

struct Console {
    buf: *mut u8,
    cap: usize,
    seq_begin: u64,
    seq_end: u64,
    vps: [Viewport; VP_MAX],
    vp_n: usize,
}

/* Slot table only — each Console + ring is heap-allocated. */
static mut CONSOLES: [*mut Console; CONSOLE_MAX] = [null_mut(); CONSOLE_MAX];

unsafe fn console_ptr(id: u32) -> *mut Console {
    let i = id as usize;
    if i >= CONSOLE_MAX {
        return null_mut();
    }
    (*addr_of!(CONSOLES))[i]
}

unsafe fn push_byte(c: &mut Console, b: u8) {
    if c.cap == 0 {
        return;
    }
    if (c.seq_end - c.seq_begin) as usize >= c.cap {
        c.seq_begin += 1;
        for i in 0..c.vp_n {
            if c.vps[i].live && c.vps[i].seq_pos < c.seq_begin {
                c.vps[i].seq_pos = c.seq_begin;
            }
        }
    }
    *c.buf.add((c.seq_end as usize) % c.cap) = b;
    c.seq_end += 1;
}

unsafe fn drain_vp(c: &mut Console, vi: usize) {
    if vi >= c.vp_n || !c.vps[vi].live {
        return;
    }
    let Some(w) = c.vps[vi].ops.write else {
        return;
    };
    let ctx = c.vps[vi].ctx;
    while c.vps[vi].seq_pos < c.seq_end {
        let seq = c.vps[vi].seq_pos;
        let idx = (seq as usize) % c.cap;
        let left = (c.seq_end - seq) as usize;
        let contig = core::cmp::min(c.cap - idx, left);
        w(ctx, c.buf.add(idx), contig);
        c.vps[vi].seq_pos = seq + contig as u64;
    }
}

unsafe fn drain_all(c: &mut Console) {
    for i in 0..c.vp_n {
        drain_vp(c, i);
    }
}

unsafe fn console_create(id: u32, ring_bytes: usize) -> i32 {
    let i = id as usize;
    if i >= CONSOLE_MAX {
        return -1;
    }
    if !(*addr_of!(CONSOLES))[i].is_null() {
        return 0;
    }
    let cap = if ring_bytes == 0 {
        DEFAULT_RING
    } else {
        ring_bytes
    };
    let c_raw = mem::pm_metal_mem_alloc(core::mem::size_of::<Console>());
    if c_raw.is_null() {
        return -1;
    }
    let buf = mem::pm_metal_mem_alloc(cap);
    if buf.is_null() {
        mem::pm_metal_mem_free(c_raw);
        return -1;
    }
    let c = c_raw as *mut Console;
    *c = Console {
        buf,
        cap,
        seq_begin: 0,
        seq_end: 0,
        vps: [Viewport {
            ops: pm_metal_console_viewport_ops_t { write: None },
            ctx: null_mut(),
            seq_pos: 0,
            live: false,
        }; VP_MAX],
        vp_n: 0,
    };
    (*addr_of_mut!(CONSOLES))[i] = c;
    0
}

/// Create and init console #0 with a heap ring (`ring_bytes` 0 => 64KiB default).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_console_init0(ring_bytes: usize) -> i32 {
    console_create(0, ring_bytes)
}

/// 1 if console #0 exists.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_console_ready() -> i32 {
    if !(*addr_of!(CONSOLES))[0].is_null() {
        1
    } else {
        0
    }
}

unsafe fn write_bytes(c: &mut Console, s: *const u8, n: usize) {
    for i in 0..n {
        let b = *s.add(i);
        if b < 0x80 {
            push_byte(c, b);
        }
    }
}

unsafe fn write_sgr(c: &mut Console, code: &[u8]) {
    /* ESC [ <code> m */
    push_byte(c, 0x1b);
    push_byte(c, b'[');
    for &b in code {
        push_byte(c, b);
    }
    push_byte(c, b'm');
}

/// Append ASCII bytes to console `id` (0 .. 31). Drains live viewports.
/// Style DEFAULT (no SGR wrapper).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_console_write(id: u32, s: *const u8, n: usize) {
    pm_metal_console_write_styled(id, pm_metal_console_style_t::PM_METAL_CONSOLE_STYLE_DEFAULT, s, n)
}

/// Append ASCII bytes with a semantic style (SGR before/after, then ESC[0m).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_console_write_styled(
    id: u32,
    style: pm_metal_console_style_t,
    s: *const u8,
    n: usize,
) {
    if s.is_null() || n == 0 {
        return;
    }
    let c = console_ptr(id);
    if c.is_null() {
        return;
    }
    let c = &mut *c;
    let code = style::sgr_code(style);
    if style as u32 != pm_metal_console_style_t::PM_METAL_CONSOLE_STYLE_DEFAULT as u32 {
        write_sgr(c, code);
    }
    write_bytes(c, s, n);
    if style as u32 != pm_metal_console_style_t::PM_METAL_CONSOLE_STYLE_DEFAULT as u32 {
        write_sgr(c, b"0");
    }
    drain_all(c);
}

/// Manually attach a viewport. Replays from retained `seq_begin`, then live.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_console_attach(
    id: u32,
    ops: *const pm_metal_console_viewport_ops_t,
    ctx: *mut u8,
) -> i32 {
    if ops.is_null() {
        return -1;
    }
    let c = console_ptr(id);
    if c.is_null() {
        return -1;
    }
    let c = &mut *c;
    if c.vp_n >= VP_MAX {
        return -1;
    }
    let vi = c.vp_n;
    c.vps[vi] = Viewport {
        ops: *ops,
        ctx,
        seq_pos: c.seq_begin,
        live: true,
    };
    c.vp_n += 1;
    drain_vp(c, vi);
    0
}
