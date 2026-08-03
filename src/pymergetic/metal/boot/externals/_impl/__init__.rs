//! Third-party externals registry — static seed + runtime register.
//! Not the mod registry; not Metal's own authors/about record.
#![allow(non_camel_case_types)]

use core::cell::Cell;
use core::ffi::c_void;
use core::sync::atomic::{AtomicBool, AtomicU32, Ordering};

use pymergetic_metal_reg::{
    pm_metal_reg_mod_load, publish_entries, RegEntry, RegMod, RegModStatic,
};

/// One registered external. Pointers are static literals or dyn-slot buffers.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct pm_metal_external_t {
    pub id: *const u8,
    pub version: *const u8,
    pub url: *const u8,
    pub note: *const u8,
}

const DYN_MAX: usize = 64;
const ID_CAP: usize = 32;
const VER_CAP: usize = 48;
const URL_CAP: usize = 96;
const NOTE_CAP: usize = 96;

struct DynSlot {
    id: [u8; ID_CAP],
    version: [u8; VER_CAP],
    url: [u8; URL_CAP],
    note: [u8; NOTE_CAP],
}

impl DynSlot {
    const fn empty() -> Self {
        Self {
            id: [0; ID_CAP],
            version: [0; VER_CAP],
            url: [0; URL_CAP],
            note: [0; NOTE_CAP],
        }
    }
}

struct Seed {
    id: &'static [u8],
    version: &'static [u8],
    url: &'static [u8],
    note: &'static [u8],
}

/// Stacks actually linked in the product image (versions from current submodules).
static SEEDS: &[Seed] = &[
    Seed {
        id: b"micropython\0",
        version: b"1.28.0\0",
        url: b"https://github.com/micropython/micropython\0",
        note: b"embedded Python REPL\0",
    },
    Seed {
        id: b"wamr\0",
        version: b"2.4.5\0",
        url: b"https://github.com/bytecodealliance/wasm-micro-runtime\0",
        note: b"wasm interpreter / AOT guest runner\0",
    },
    Seed {
        id: b"lwip\0",
        version: b"2.2.1\0",
        url: b"https://savannah.nongnu.org/projects/lwip/\0",
        note: b"TCP/IP stack\0",
    },
    Seed {
        id: b"dropbear\0",
        version: b"2024.85\0",
        url: b"https://github.com/mkj/dropbear\0",
        note: b"SSH server (sshd)\0",
    },
    Seed {
        id: b"tlsf\0",
        version: b"3.1\0",
        url: b"http://tlsf.baisoku.org\0",
        note: b"Two-Level Segregated Fit host heap\0",
    },
    Seed {
        id: b"monocypher\0",
        version: b"4.0.2\0",
        url: b"https://monocypher.org\0",
        note: b"crypto (trust / verify)\0",
    },
];

static SEEDED: AtomicBool = AtomicBool::new(false);
static DYN_N: AtomicU32 = AtomicU32::new(0);
static mut DYN: [DynSlot; DYN_MAX] = [const { DynSlot::empty() }; DYN_MAX];

fn cstr_eq(a: *const u8, b: &[u8]) -> bool {
    if a.is_null() || b.is_empty() {
        return false;
    }
    let mut i = 0usize;
    unsafe {
        loop {
            let ca = *a.add(i);
            let cb = if i < b.len() { b[i] } else { 0 };
            if ca != cb {
                return false;
            }
            if ca == 0 {
                return true;
            }
            i += 1;
            if i > 256 {
                return false;
            }
        }
    }
}

fn copy_cstr(dst: &mut [u8], src: *const u8) {
    dst.fill(0);
    if src.is_null() || dst.is_empty() {
        return;
    }
    let mut i = 0usize;
    unsafe {
        while i + 1 < dst.len() {
            let c = *src.add(i);
            dst[i] = c;
            if c == 0 {
                return;
            }
            i += 1;
        }
    }
    dst[dst.len() - 1] = 0;
}

fn seed_row(i: usize) -> pm_metal_external_t {
    let s = &SEEDS[i];
    pm_metal_external_t {
        id: s.id.as_ptr(),
        version: s.version.as_ptr(),
        url: s.url.as_ptr(),
        note: s.note.as_ptr(),
    }
}

fn ensure_seeded() {
    if SEEDED.swap(true, Ordering::AcqRel) {
        return;
    }
    /* Seeds are compile-time; flag only gates dyn-table init order. */
}

/// Number of registered externals (static seeds + dyn).
#[no_mangle]
pub extern "C" fn pm_metal_external_count() -> u32 {
    ensure_seeded();
    SEEDS.len() as u32 + DYN_N.load(Ordering::Acquire)
}

/// Fill `out` with the external at flat `idx`. `0` ok, `-1` bad.
///
/// # Safety
/// `out` must be valid for write.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_external_get(idx: u32, out: *mut pm_metal_external_t) -> i32 {
    ensure_seeded();
    if out.is_null() {
        return -1;
    }
    let static_n = SEEDS.len() as u32;
    if idx < static_n {
        *out = seed_row(idx as usize);
        return 0;
    }
    let di = (idx - static_n) as usize;
    let n = DYN_N.load(Ordering::Acquire) as usize;
    if di >= n {
        return -1;
    }
    let slot = &*core::ptr::addr_of!(DYN[di]);
    *out = pm_metal_external_t {
        id: slot.id.as_ptr(),
        version: slot.version.as_ptr(),
        url: slot.url.as_ptr(),
        note: slot.note.as_ptr(),
    };
    0
}

/// Find by id (dyn last-wins, then static). `0` ok, `-1` miss.
///
/// # Safety
/// `id` NUL C string; `out` valid for write.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_external_find(
    id: *const u8,
    out: *mut pm_metal_external_t,
) -> i32 {
    ensure_seeded();
    if id.is_null() || out.is_null() || *id == 0 {
        return -1;
    }
    let n = DYN_N.load(Ordering::Acquire) as usize;
    for i in 0..n {
        let slot = &*core::ptr::addr_of!(DYN[i]);
        if cstr_eq(id, &slot.id) {
            *out = pm_metal_external_t {
                id: slot.id.as_ptr(),
                version: slot.version.as_ptr(),
                url: slot.url.as_ptr(),
                note: slot.note.as_ptr(),
            };
            return 0;
        }
    }
    for (i, s) in SEEDS.iter().enumerate() {
        if cstr_eq(id, s.id) {
            *out = seed_row(i);
            return 0;
        }
    }
    -1
}

/// Runtime registration — copies strings into a dyn slot (same id updates).
/// url/note may be null. Returns `0` ok, `-1` full/invalid.
///
/// # Safety
/// String args are NUL C strings when non-null.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_external_register(
    id: *const u8,
    version: *const u8,
    url: *const u8,
    note: *const u8,
) -> i32 {
    ensure_seeded();
    if id.is_null() || *id == 0 {
        return -1;
    }
    let ver = if version.is_null() {
        b"\0".as_ptr()
    } else {
        version
    };
    let u = if url.is_null() { b"\0".as_ptr() } else { url };
    let nte = if note.is_null() {
        b"\0".as_ptr()
    } else {
        note
    };

    let n = DYN_N.load(Ordering::Acquire) as usize;
    let mut slot_i: Option<usize> = None;
    for i in 0..n {
        let slot = &*core::ptr::addr_of!(DYN[i]);
        if cstr_eq(id, &slot.id) {
            slot_i = Some(i);
            break;
        }
    }
    let i = match slot_i {
        Some(i) => i,
        None => {
            if n >= DYN_MAX {
                return -1;
            }
            DYN_N.store((n + 1) as u32, Ordering::Release);
            n
        }
    };
    let slot = &mut *core::ptr::addr_of_mut!(DYN[i]);
    copy_cstr(&mut slot.id, id);
    copy_cstr(&mut slot.version, ver);
    copy_cstr(&mut slot.url, u);
    copy_cstr(&mut slot.note, nte);
    0
}

static FLOOR_ENTRIES: RegModStatic<4, 0> = RegModStatic::new(
    [
        RegEntry::new("pm_metal_external_count"),
        RegEntry::new("pm_metal_external_get"),
        RegEntry::new("pm_metal_external_find"),
        RegEntry::new("pm_metal_external_register"),
    ],
    [],
);

extern "C" fn floor_register_symbols(_ctx: *mut c_void) -> i32 {
    publish_entries(
        &FLOOR_ENTRIES.entries,
        &[
            pm_metal_external_count as *const c_void,
            pm_metal_external_get as *const c_void,
            pm_metal_external_find as *const c_void,
            pm_metal_external_register as *const c_void,
        ],
    )
}

static FLOOR_MOD: RegMod = RegMod {
    name: "pymergetic.metal.boot.externals",
    unloadable: false,
    parent: None,
    ctx: core::ptr::null_mut(),
    on_load: None,
    register_symbols: Some(floor_register_symbols),
    connect_symbols: None,
    on_registrations_updated: None,
    deregister_symbols: None,
    on_unload: None,
    entries: &FLOOR_ENTRIES.entries,
    imports: &[],
    raw_next: Cell::new(core::ptr::null()),
    raw_prev: Cell::new(core::ptr::null()),
};

/// Load the externals floor module and seed the static catalog.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_boot_externals_mod_load() -> i32 {
    ensure_seeded();
    pm_metal_reg_mod_load(&FLOOR_MOD)
}
