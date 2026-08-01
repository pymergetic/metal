//! Boot-tree builder — call sites supply name/status/detail only.
//!
//! Glyphs (`+--` / `` `--``), indent, and last-child choice live here.
//! Print-time fixed node pool (no heap); `flush` renders the buffered tree.
#![allow(non_camel_case_types)]

use core::fmt::Write;

use pymergetic_metal_log::pm_metal_log_style_t;

use super::line::{emit, emit_str, LineBuf};

/// Status for a tree item (maps to log style). C ABI spelling (matches `api.h`).
#[repr(u32)]
#[derive(Clone, Copy, PartialEq, Eq)]
#[allow(non_camel_case_types)]
pub enum pm_metal_boot_tree_status_t {
    PM_METAL_BOOT_TREE_OK = 0,
    PM_METAL_BOOT_TREE_WARN = 1,
    PM_METAL_BOOT_TREE_FAIL = 2,
    PM_METAL_BOOT_TREE_DIM = 3,
    PM_METAL_BOOT_TREE_ACCENT = 4,
}

const NAME_PAD: usize = 13;
const MAX_NODES: usize = 64;
const MAX_DEPTH: usize = 8;
const NAME_CAP: usize = 48;
const DETAIL_CAP: usize = 140;

const KIND_ITEM: u8 = 0;
const KIND_BLANK: u8 = 1;
const KIND_SPACER: u8 = 2;

#[derive(Clone, Copy)]
struct Node {
    kind: u8,
    status: pm_metal_boot_tree_status_t,
    name_len: u8,
    detail_len: u8,
    name: [u8; NAME_CAP],
    detail: [u8; DETAIL_CAP],
    first_child: i16,
    last_child: i16,
    next_sibling: i16,
}

impl Node {
    const fn empty() -> Self {
        Self {
            kind: KIND_ITEM,
            status: pm_metal_boot_tree_status_t::PM_METAL_BOOT_TREE_DIM,
            name_len: 0,
            detail_len: 0,
            name: [0; NAME_CAP],
            detail: [0; DETAIL_CAP],
            first_child: -1,
            last_child: -1,
            next_sibling: -1,
        }
    }
}

struct Tree {
    nodes: [Node; MAX_NODES],
    n: i16,
    /* stack[depth] = parent node index; stack[0] = virtual root (0). */
    stack: [i16; MAX_DEPTH],
    depth: usize,
}

impl Tree {
    const fn new() -> Self {
        Self {
            nodes: [Node::empty(); MAX_NODES],
            n: 0,
            stack: [0; MAX_DEPTH],
            depth: 0,
        }
    }
}

static mut TREE: Tree = Tree::new();

fn bytes_copy(dst: &mut [u8], src: &[u8]) -> u8 {
    let mut n = 0usize;
    for &b in src {
        if n >= dst.len() {
            break;
        }
        if b == 0 {
            break;
        }
        if b < 0x80 {
            dst[n] = b;
            n += 1;
        } else {
            break;
        }
    }
    n as u8
}

fn cstr_slice<'a>(src: *const u8) -> &'a [u8] {
    if src.is_null() {
        return &[];
    }
    let mut n = 0usize;
    unsafe {
        while n < 256 {
            if *src.add(n) == 0 {
                break;
            }
            n += 1;
        }
        core::slice::from_raw_parts(src, n)
    }
}

fn style_of(st: pm_metal_boot_tree_status_t) -> pm_metal_log_style_t {
    match st {
        pm_metal_boot_tree_status_t::PM_METAL_BOOT_TREE_OK => {
            pm_metal_log_style_t::PM_METAL_LOG_STYLE_OK
        }
        pm_metal_boot_tree_status_t::PM_METAL_BOOT_TREE_WARN => {
            pm_metal_log_style_t::PM_METAL_LOG_STYLE_WARN
        }
        pm_metal_boot_tree_status_t::PM_METAL_BOOT_TREE_FAIL => {
            pm_metal_log_style_t::PM_METAL_LOG_STYLE_FAIL
        }
        pm_metal_boot_tree_status_t::PM_METAL_BOOT_TREE_DIM => {
            pm_metal_log_style_t::PM_METAL_LOG_STYLE_DIM
        }
        pm_metal_boot_tree_status_t::PM_METAL_BOOT_TREE_ACCENT => {
            pm_metal_log_style_t::PM_METAL_LOG_STYLE_ACCENT
        }
    }
}

unsafe fn alloc_node() -> Option<i16> {
    let t = &mut *core::ptr::addr_of_mut!(TREE);
    if t.n as usize >= MAX_NODES {
        return None;
    }
    /* Node 0 is the virtual root — first real alloc makes it. */
    if t.n == 0 {
        t.nodes[0] = Node::empty();
        t.n = 1;
        t.stack[0] = 0;
        t.depth = 0;
    }
    let idx = t.n;
    t.nodes[idx as usize] = Node::empty();
    t.n += 1;
    Some(idx)
}

unsafe fn link_child(parent: i16, child: i16) {
    let t = &mut *core::ptr::addr_of_mut!(TREE);
    let last = t.nodes[parent as usize].last_child;
    if last < 0 {
        t.nodes[parent as usize].first_child = child;
    } else {
        t.nodes[last as usize].next_sibling = child;
    }
    t.nodes[parent as usize].last_child = child;
}

unsafe fn add_kind(kind: u8, st: pm_metal_boot_tree_status_t, name: &[u8], detail: &[u8]) {
    let Some(idx) = alloc_node() else {
        return;
    };
    {
        let t = &mut *core::ptr::addr_of_mut!(TREE);
        let n = &mut t.nodes[idx as usize];
        n.kind = kind;
        n.status = st;
        if kind == KIND_ITEM {
            n.name_len = bytes_copy(&mut n.name, name);
            n.detail_len = bytes_copy(&mut n.detail, detail);
        }
        let parent = t.stack[t.depth];
        link_child(parent, idx);
    }
}

fn write_indent(line: &mut LineBuf, cont: &[bool]) {
    for &c in cont {
        if c {
            let _ = write!(line, "|   ");
        } else {
            let _ = write!(line, "    ");
        }
    }
}

fn write_name_detail(line: &mut LineBuf, depth: usize, name: &[u8], detail: &[u8]) {
    if depth == 0 && name.len() < NAME_PAD {
        let _ = write!(line, "{:<width$}", core::str::from_utf8(name).unwrap_or(""), width = NAME_PAD);
    } else {
        let _ = write!(line, "{}", core::str::from_utf8(name).unwrap_or(""));
        if !detail.is_empty() && depth > 0 {
            let _ = write!(line, "  ");
        } else if !detail.is_empty() && depth == 0 && name.len() >= NAME_PAD {
            let _ = write!(line, " ");
        }
    }
    if !detail.is_empty() {
        let _ = write!(line, "{}", core::str::from_utf8(detail).unwrap_or(""));
    }
}

unsafe fn render_item(idx: i16, cont: &mut [bool], depth: usize, is_last: bool) {
    let t = &*core::ptr::addr_of!(TREE);
    let n = &t.nodes[idx as usize];
    match n.kind {
        KIND_BLANK => {
            emit_str(pm_metal_log_style_t::PM_METAL_LOG_STYLE_DEFAULT, "");
        }
        KIND_SPACER => {
            let mut line = LineBuf::new();
            write_indent(&mut line, &cont[..depth]);
            let _ = write!(line, "|");
            emit(pm_metal_log_style_t::PM_METAL_LOG_STYLE_DIM, &mut line);
        }
        _ => {
            let mut line = LineBuf::new();
            write_indent(&mut line, &cont[..depth]);
            let branch = if is_last { "`--" } else { "+--" };
            let _ = write!(line, "{} ", branch);
            let name = &n.name[..n.name_len as usize];
            let detail = &n.detail[..n.detail_len as usize];
            write_name_detail(&mut line, depth, name, detail);
            emit(style_of(n.status), &mut line);
            if n.first_child >= 0 {
                if depth < cont.len() {
                    cont[depth] = !is_last;
                }
                render_children(n.first_child, cont, depth + 1);
            }
        }
    }
}

unsafe fn render_children(first: i16, cont: &mut [bool], depth: usize) {
    let t = &*core::ptr::addr_of!(TREE);
    let mut cur = first;
    while cur >= 0 {
        let next = t.nodes[cur as usize].next_sibling;
        let is_last = next < 0;
        render_item(cur, cont, depth, is_last);
        cur = next;
    }
}

/// Clear the builder (start of `tree_print`).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_boot_tree_reset() {
    let t = &mut *core::ptr::addr_of_mut!(TREE);
    *t = Tree::new();
    /* Virtual root at index 0. */
    t.nodes[0] = Node::empty();
    t.n = 1;
    t.stack[0] = 0;
    t.depth = 0;
}

/// Blank log line (banner / tree separation).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_boot_tree_blank() {
    add_kind(
        KIND_BLANK,
        pm_metal_boot_tree_status_t::PM_METAL_BOOT_TREE_DIM,
        &[],
        &[],
    );
}

/// Dim trunk `|` under the current parent (after root label).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_boot_tree_spacer() {
    add_kind(
        KIND_SPACER,
        pm_metal_boot_tree_status_t::PM_METAL_BOOT_TREE_DIM,
        &[],
        &[],
    );
}

/// Append one item under the current parent.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_boot_tree_item(
    st: pm_metal_boot_tree_status_t,
    name: *const u8,
    detail: *const u8,
) {
    add_kind(KIND_ITEM, st, cstr_slice(name), cstr_slice(detail));
}

/// Descend into children of the most recently added item.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_boot_tree_enter() {
    let t = &mut *core::ptr::addr_of_mut!(TREE);
    if t.n <= 1 {
        return;
    }
    if t.depth + 1 >= MAX_DEPTH {
        return;
    }
    let parent = t.stack[t.depth];
    let last = t.nodes[parent as usize].last_child;
    if last < 0 {
        return;
    }
    if t.nodes[last as usize].kind != KIND_ITEM {
        return;
    }
    t.depth += 1;
    t.stack[t.depth] = last;
}

/// Leave the current child list (return to parent).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_boot_tree_leave() {
    let t = &mut *core::ptr::addr_of_mut!(TREE);
    if t.depth == 0 {
        return;
    }
    t.depth -= 1;
}

/// Render the buffered tree to the log path.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_boot_tree_flush() {
    let t = &*core::ptr::addr_of!(TREE);
    if t.n <= 1 {
        return;
    }
    let mut cont = [false; MAX_DEPTH];
    let first = t.nodes[0].first_child;
    if first >= 0 {
        render_children(first, &mut cont, 0);
    }
}

/* --- Rust helpers -------------------------------------------------------- */

pub fn item_str(st: pm_metal_boot_tree_status_t, name: &str, detail: &str) {
    unsafe {
        add_kind(KIND_ITEM, st, name.as_bytes(), detail.as_bytes());
    }
}

pub fn item_name(st: pm_metal_boot_tree_status_t, name: &str) {
    unsafe {
        add_kind(KIND_ITEM, st, name.as_bytes(), &[]);
    }
}
