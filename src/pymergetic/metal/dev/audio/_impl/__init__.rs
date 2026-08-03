//! Audio — null backend (discard PCM, eager drain). Virtio/AC97 later.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use core::cell::Cell;
use core::ffi::c_void;
use core::ptr::addr_of_mut;

use pymergetic_metal_reg::{
    pm_metal_reg_mod_load, publish_entries, RegEntry, RegMod, RegModStatic,
};
use pymergetic_metal_rt as _;

// `async` / `log` are floor modules: consume generated faces, not Cargo deps
// (boot already links their object code). Faces live under gitignored
// `include/`; build.rs stages copies into OUT_DIR (RA skips gitignored
// #[path] targets).
include!(concat!(env!("OUT_DIR"), "/face_mods.rs"));

pub type pm_metal_dev_audio_stream_h = u32;

pub const PM_METAL_DEV_AUDIO_STREAM_INVALID: u32 = 0;
pub const PM_METAL_DEV_AUDIO_FMT_S16LE_STEREO_22050: u32 = 1;

const MAX_STREAMS: usize = 8;

#[derive(Clone, Copy)]
struct Stream {
    used: i32,
    format: u32,
    frames: u32,
}

struct State {
    streams: [Stream; MAX_STREAMS + 1],
    muted: i32,
    volume: u32,
    logged: i32,
}

static mut ST: State = State {
    streams: [Stream {
        used: 0,
        format: 0,
        frames: 0,
    }; MAX_STREAMS + 1],
    muted: 0,
    volume: 100,
    logged: 0,
};

fn ensure_log() {
    unsafe {
        if ST.logged == 0 {
            log_face::pm_metal_log(b"metal-audio: null\0".as_ptr());
            ST.logged = 1;
        }
    }
}

/// 1 if the silent/null path is usable.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_audio_ready() -> i32 {
    ensure_log();
    1
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_audio_open(format: u32, frames_buffered: u32) -> u32 {
    ensure_log();
    if format == 0 {
        return PM_METAL_DEV_AUDIO_STREAM_INVALID;
    }
    let st = &mut *addr_of_mut!(ST);
    let mut i = 1usize;
    while i <= MAX_STREAMS {
        if st.streams[i].used == 0 {
            st.streams[i].used = 1;
            st.streams[i].format = format;
            st.streams[i].frames = frames_buffered;
            return i as u32;
        }
        i += 1;
    }
    PM_METAL_DEV_AUDIO_STREAM_INVALID
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_audio_close(s: u32) {
    if s == 0 || (s as usize) > MAX_STREAMS {
        return;
    }
    let st = &mut *addr_of_mut!(ST);
    st.streams[s as usize] = Stream {
        used: 0,
        format: 0,
        frames: 0,
    };
}

/// Accept PCM for pacing (discard). Returns nbytes accepted, or 0 if bad stream.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_audio_queue(s: u32, pcm: *const u8, nbytes: u32) -> u32 {
    let _ = pcm;
    let st = &*addr_of_mut!(ST);
    if s == 0 || (s as usize) > MAX_STREAMS || st.streams[s as usize].used == 0 {
        return 0;
    }
    let _ = st.muted;
    let _ = st.volume;
    nbytes
}

/// Awaitable drain — completes immediately (null has no backlog).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_audio_drain(s: u32, nbytes: u32) -> u32 {
    let _ = nbytes;
    let st = &*addr_of_mut!(ST);
    if s == 0 || (s as usize) > MAX_STREAMS || st.streams[s as usize].used == 0 {
        return 0;
    }
    async_handle_face::pm_metal_async_completed_u32(0)
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_audio_mute(on: i32) {
    (*addr_of_mut!(ST)).muted = if on != 0 { 1 } else { 0 };
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_audio_muted() -> i32 {
    (*addr_of_mut!(ST)).muted
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_audio_volume_set(pct: u32) {
    let v = if pct > 100 { 100 } else { pct };
    (*addr_of_mut!(ST)).volume = v;
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_audio_volume_get() -> u32 {
    (*addr_of_mut!(ST)).volume
}

/// Write backend name into out (NUL-terminated). 0 ok, -1 bad.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_audio_backend(out: *mut u8, out_cap: u32) -> i32 {
    if out.is_null() || out_cap == 0 {
        return -1;
    }
    let name = b"null\0";
    if (name.len() as u32) > out_cap {
        return -1;
    }
    let mut i = 0usize;
    while i < name.len() {
        *out.add(i) = name[i];
        i += 1;
    }
    0
}

static FLOOR_ENTRIES: RegModStatic<10, 0> = RegModStatic::new(
    [
        RegEntry::new("pm_metal_dev_audio_ready"),
        RegEntry::new("pm_metal_dev_audio_open"),
        RegEntry::new("pm_metal_dev_audio_close"),
        RegEntry::new("pm_metal_dev_audio_queue"),
        RegEntry::new("pm_metal_dev_audio_drain"),
        RegEntry::new("pm_metal_dev_audio_mute"),
        RegEntry::new("pm_metal_dev_audio_muted"),
        RegEntry::new("pm_metal_dev_audio_volume_set"),
        RegEntry::new("pm_metal_dev_audio_volume_get"),
        RegEntry::new("pm_metal_dev_audio_backend"),
    ],
    [],
);

extern "C" fn floor_register_symbols(_ctx: *mut c_void) -> i32 {
    publish_entries(
        &FLOOR_ENTRIES.entries,
        &[
            pm_metal_dev_audio_ready as *const c_void,
            pm_metal_dev_audio_open as *const c_void,
            pm_metal_dev_audio_close as *const c_void,
            pm_metal_dev_audio_queue as *const c_void,
            pm_metal_dev_audio_drain as *const c_void,
            pm_metal_dev_audio_mute as *const c_void,
            pm_metal_dev_audio_muted as *const c_void,
            pm_metal_dev_audio_volume_set as *const c_void,
            pm_metal_dev_audio_volume_get as *const c_void,
            pm_metal_dev_audio_backend as *const c_void,
        ],
    )
}

static FLOOR_MOD: RegMod = RegMod {
    name: "pymergetic.metal.dev.audio",
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

#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_audio_mod_load() -> i32 {
    pm_metal_reg_mod_load(&FLOOR_MOD)
}
