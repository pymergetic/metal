//! W11.6 cross-lang / wasm call stress under load (host smoke + firmware proof).

use core::fmt::Write;
use core::sync::atomic::{AtomicU32, Ordering};

use crate::{
    pm_metal_wasm_call0, pm_metal_wasm_load_register, pm_metal_wasm_unload,
};

extern "C" {
    fn pm_metal_reg_call0(full_module: *const u8, func: *const u8) -> i32;
    fn pm_metal_async_ready() -> i32;
    fn pm_metal_async_start(n_cpus: u32) -> i32;
    fn pm_metal_async_spawn(
        step: Option<unsafe extern "C" fn(u32) -> u32>,
        state_bytes: u32,
        prio: u32,
    ) -> u32;
    fn pm_metal_async_coro_state(h: u32) -> *mut u8;
    fn pm_metal_async_coro_close(h: u32);
    fn pm_metal_async_run_poll_all() -> i32;
    fn pm_metal_async_status(h: u32) -> u32;
    fn pm_metal_async_mono_us() -> u64;
    fn pm_metal_log(line: *const u8);
}

const STATUS_DONE: u32 = 2;
const STATUS_PENDING: u32 = 0;
const STATUS_ERROR: u32 = 4;
const PRIO_MED: u32 = 1;
const INVALID: u32 = 0;

static SAMPLE_GREETER: &[u8] =
    include_bytes!("../../../../build/packs/sample.greeter.wasm");
static SAMPLE_ANNOUNCER: &[u8] =
    include_bytes!("../../../../build/packs/sample.announcer.wasm");

static GREETER: &[u8] = b"sample.greeter\0";
static ANNOUNCER: &[u8] = b"sample.announcer\0";
static ANNOUNCE: &[u8] = b"announce\0";

static CALL_OK: AtomicU32 = AtomicU32::new(0);
static CALL_ERR: AtomicU32 = AtomicU32::new(0);
static RELOAD_OK: AtomicU32 = AtomicU32::new(0);

#[repr(C)]
struct StressFrame {
    left: u32,
}

unsafe extern "C" fn stress_call_step(self_h: u32) -> u32 {
    let p = pm_metal_async_coro_state(self_h) as *mut StressFrame;
    if p.is_null() {
        return STATUS_ERROR;
    }
    if (*p).left == 0 {
        return STATUS_DONE;
    }
    (*p).left = (*p).left.saturating_sub(1);
    let rc = pm_metal_wasm_call0(ANNOUNCER.as_ptr(), ANNOUNCE.as_ptr());
    if rc == 12 {
        CALL_OK.fetch_add(1, Ordering::Relaxed);
    } else {
        /* Under unload, -1/-2 is expected; count as err but keep going. */
        CALL_ERR.fetch_add(1, Ordering::Relaxed);
    }
    STATUS_PENDING
}

struct LineBuf {
    buf: [u8; 192],
    pos: usize,
}

impl LineBuf {
    fn new() -> Self {
        Self {
            buf: [0; 192],
            pos: 0,
        }
    }

    fn as_cstr(&mut self) -> *const u8 {
        if self.pos >= self.buf.len() {
            self.pos = self.buf.len() - 1;
        }
        self.buf[self.pos] = 0;
        self.buf.as_ptr()
    }
}

impl Write for LineBuf {
    fn write_str(&mut self, s: &str) -> core::fmt::Result {
        for &b in s.as_bytes() {
            if self.pos + 1 >= self.buf.len() {
                break;
            }
            if b < 0x80 {
                self.buf[self.pos] = b;
                self.pos += 1;
            }
        }
        Ok(())
    }
}

unsafe fn ensure_async() -> bool {
    if pm_metal_async_ready() != 0 {
        return true;
    }
    pm_metal_async_start(4) == 0 && pm_metal_async_ready() != 0
}

unsafe fn load_pair() -> Result<(), i32> {
    let n = pm_metal_wasm_load_register(
        GREETER.as_ptr(),
        SAMPLE_GREETER.as_ptr(),
        SAMPLE_GREETER.len() as u32,
    );
    if n < 1 {
        return Err(-2);
    }
    let n = pm_metal_wasm_load_register(
        ANNOUNCER.as_ptr(),
        SAMPLE_ANNOUNCER.as_ptr(),
        SAMPLE_ANNOUNCER.len() as u32,
    );
    if n < 1 {
        return Err(-3);
    }
    Ok(())
}

unsafe fn expect_announce(want: i32) -> bool {
    let a = pm_metal_wasm_call0(ANNOUNCER.as_ptr(), ANNOUNCE.as_ptr());
    let b = pm_metal_reg_call0(ANNOUNCER.as_ptr(), ANNOUNCE.as_ptr());
    a == want && b == want
}

/// Cross-lang/wasm stress: host+reg call0, wasm↔wasm via announcer, concurrent
/// callers, unload/reload under traffic. Logs one ASCII metrics line.
///
/// Negative codes: -2 greeter load, -3 announcer load, -4 announce, -5 async,
/// -6 spawn, -7 hang, -8 post-reload, -9 metrics.
///
/// C ABI lives on `__init__.rs` (`pm_metal_wasm_proof_stress`) -- underscore
/// stems are not face-synced.
pub(crate) unsafe fn proof_stress() -> i32 {
    CALL_OK.store(0, Ordering::Relaxed);
    CALL_ERR.store(0, Ordering::Relaxed);
    RELOAD_OK.store(0, Ordering::Relaxed);

    /* Drop any prior sample pair from a previous proof run. */
    let _ = pm_metal_wasm_unload(GREETER.as_ptr());
    let _ = pm_metal_wasm_unload(ANNOUNCER.as_ptr());

    if let Err(rc) = load_pair() {
        return rc;
    }
    if !expect_announce(12) {
        return -4;
    }

    let t0 = pm_metal_async_mono_us();
    const SERIAL: u32 = 256;
    for _ in 0..SERIAL {
        if pm_metal_wasm_call0(ANNOUNCER.as_ptr(), ANNOUNCE.as_ptr()) != 12 {
            return -4;
        }
        if pm_metal_reg_call0(ANNOUNCER.as_ptr(), ANNOUNCE.as_ptr()) != 12 {
            return -4;
        }
        CALL_OK.fetch_add(2, Ordering::Relaxed);
    }

    if !ensure_async() {
        return -5;
    }

    const TASKS: usize = 4;
    const PER_TASK: u32 = 64;
    let mut hs = [INVALID; TASKS];
    for h in hs.iter_mut() {
        let th = pm_metal_async_spawn(
            Some(stress_call_step),
            core::mem::size_of::<StressFrame>() as u32,
            PRIO_MED,
        );
        if th == INVALID {
            return -6;
        }
        let p = pm_metal_async_coro_state(th) as *mut StressFrame;
        if p.is_null() {
            return -6;
        }
        (*p).left = PER_TASK;
        *h = th;
    }

    /* Traffic while unloading/reloading greeter (quiesce must not hang). */
    const RELOADS: u32 = 4;
    for _ in 0..RELOADS {
        if pm_metal_wasm_unload(GREETER.as_ptr()) != 0 {
            return -8;
        }
        /* announcer→greeter fwd must degrade, not hang */
        let degraded = pm_metal_wasm_call0(ANNOUNCER.as_ptr(), ANNOUNCE.as_ptr());
        if degraded != -2 && degraded != -1 {
            return -8;
        }
        let n = pm_metal_wasm_load_register(
            GREETER.as_ptr(),
            SAMPLE_GREETER.as_ptr(),
            SAMPLE_GREETER.len() as u32,
        );
        if n < 1 {
            return -8;
        }
        if pm_metal_wasm_call0(ANNOUNCER.as_ptr(), ANNOUNCE.as_ptr()) != 12 {
            return -8;
        }
        RELOAD_OK.fetch_add(1, Ordering::Relaxed);

        for _ in 0..64 {
            let _ = pm_metal_async_run_poll_all();
        }
    }

    let mut spins = 0u32;
    loop {
        let mut all_done = true;
        for &h in hs.iter() {
            let st = pm_metal_async_status(h);
            if st != STATUS_DONE && st != STATUS_ERROR {
                all_done = false;
                break;
            }
        }
        if all_done {
            break;
        }
        let _ = pm_metal_async_run_poll_all();
        spins = spins.wrapping_add(1);
        if spins > 50_000_000 {
            return -7;
        }
    }
    for &h in hs.iter() {
        pm_metal_async_coro_close(h);
    }

    if !expect_announce(12) {
        return -8;
    }

    let ok = CALL_OK.load(Ordering::Relaxed);
    let err = CALL_ERR.load(Ordering::Relaxed);
    let reloads = RELOAD_OK.load(Ordering::Relaxed);
    let dt = pm_metal_async_mono_us().saturating_sub(t0);
    if ok < SERIAL * 2 {
        return -9;
    }
    if reloads != RELOADS {
        return -9;
    }

    let mut line = LineBuf::new();
    let _ = write!(
        line,
        "wasm stress: ok={} err={} reloads={} us={} serial={} tasks={} ok",
        ok,
        err,
        reloads,
        dt,
        SERIAL,
        TASKS as u32 * PER_TASK
    );
    pm_metal_log(line.as_cstr());

    let _ = pm_metal_wasm_unload(GREETER.as_ptr());
    let _ = pm_metal_wasm_unload(ANNOUNCER.as_ptr());
    0
}

