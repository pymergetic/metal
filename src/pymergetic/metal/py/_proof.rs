//! Boot / host proofs for py edge (await + W11.5 concurrency metrics).

use core::fmt::Write;

use crate::upy::extmod::asyncio::{self, run_until, sleep_ms, Task};

extern "C" {
    fn pm_metal_async_ready() -> i32;
    fn pm_metal_async_start(n_cpus: u32) -> i32;
    fn pm_metal_async_n_runners() -> u32;
    fn pm_metal_async_run_poll_all() -> i32;
    fn pm_metal_async_spawn(
        step: Option<unsafe extern "C" fn(u32) -> u32>,
        state_bytes: u32,
        prio: u32,
    ) -> u32;
    fn pm_metal_async_coro_state(h: u32) -> *mut u8;
    fn pm_metal_async_await(self_h: u32, child_h: u32) -> u32;
    fn pm_metal_async_sleep_us(us: u64) -> u32;
    fn pm_metal_async_coro_close(h: u32);
    fn pm_metal_async_metric_reset();
    fn pm_metal_async_metric_spawns() -> u64;
    fn pm_metal_async_metric_awaits() -> u64;
    fn pm_metal_async_metric_steps(runner: u32) -> u64;
    fn pm_metal_async_metric_starve_max_us() -> u64;
    fn pm_metal_log(line: *const u8);
}

const STATUS_DONE: u32 = 2;
const STATUS_WAITING: u32 = 1;
const STATUS_ERROR: u32 = 4;
const PRIO_MED: u32 = 1;
const INVALID: u32 = 0;

#[repr(C)]
struct NestFrame {
    phase: u32,
    child: u32,
}

unsafe extern "C" fn nest_await_step(self_h: u32) -> u32 {
    let p = pm_metal_async_coro_state(self_h) as *mut NestFrame;
    if p.is_null() {
        return STATUS_ERROR;
    }
    match (*p).phase {
        0 => {
            (*p).child = pm_metal_async_sleep_us(0);
            if (*p).child == INVALID {
                return STATUS_ERROR;
            }
            (*p).phase = 1;
            let st = pm_metal_async_await(self_h, (*p).child);
            if st == STATUS_WAITING || st == STATUS_DONE {
                st
            } else {
                STATUS_ERROR
            }
        }
        _ => STATUS_DONE,
    }
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

/// W4.2 proof: park on Metal sleep (asyncio.sleep_ms). Silent -- tree shows await.
pub unsafe fn proof_await() -> i32 {
    let Some(t) = sleep_ms(1) else {
        return -1;
    };
    let st0 = asyncio::core::status(t.handle);
    if st0 == asyncio::core::STATUS_ERROR {
        t.cancel();
        return -1;
    }
    if !run_until(t.handle) {
        t.cancel();
        return -1;
    }
    if asyncio::core::status(t.handle) != asyncio::core::STATUS_DONE {
        t.cancel();
        return -1;
    }
    t.cancel();
    0
}

/// Poll until every handle is terminal (no helper sleeps -- keeps handle table free).
unsafe fn gather_poll(handles: &[u32]) -> bool {
    let mut spins = 0u32;
    loop {
        let mut all = true;
        for &h in handles {
            if h == INVALID {
                return false;
            }
            if !asyncio::core::is_done(h) {
                all = false;
                break;
            }
        }
        if all {
            return handles
                .iter()
                .all(|&h| asyncio::core::status(h) == asyncio::core::STATUS_DONE);
        }
        let _ = pm_metal_async_run_poll_all();
        spins = spins.wrapping_add(1);
        if spins > 50_000_000 {
            return false;
        }
    }
}

/// W11.5: many concurrent asyncio sleeps across N runners + nest awaits; log metrics.
///
/// Negative codes (host smoke): -2 start, -3 n_runners, -4 sleep spawn,
/// -5 gather, -6 nest spawn, -7 nest run, -8 spawns, -9 awaits,
/// -10 step total, -11 single-runner-only under n>1.
pub unsafe fn proof_concurrency() -> i32 {
    if pm_metal_async_ready() == 0 {
        /* Host smoke may not have started runners yet; firmware bringup has. */
        if pm_metal_async_start(4) != 0 {
            return -2;
        }
    }
    let n = pm_metal_async_n_runners();
    if n == 0 {
        return -3;
    }

    pm_metal_async_metric_reset();

    const N_TASKS: usize = 32;
    let mut tasks = [Task::from_handle(INVALID); N_TASKS];
    let mut handles = [INVALID; N_TASKS];
    for (i, t) in tasks.iter_mut().enumerate() {
        let Some(s) = sleep_ms(0) else {
            return -4;
        };
        handles[i] = s.handle;
        *t = s;
    }
    if !gather_poll(&handles) {
        for t in tasks.iter() {
            t.cancel();
        }
        return -5;
    }
    for t in tasks.iter() {
        t.cancel();
    }

    /* Nest await: one parent per runner so metric_awaits > 0 and RR still used. */
    let nest_n = if n > 8 { 8 } else { n } as usize;
    for _ in 0..nest_n {
        let h = pm_metal_async_spawn(
            Some(nest_await_step),
            core::mem::size_of::<NestFrame>() as u32,
            PRIO_MED,
        );
        if h == INVALID {
            return -6;
        }
        if !run_until(h) {
            pm_metal_async_coro_close(h);
            return -7;
        }
        pm_metal_async_coro_close(h);
    }

    let spawns = pm_metal_async_metric_spawns();
    let awaits = pm_metal_async_metric_awaits();
    let starve = pm_metal_async_metric_starve_max_us();
    if spawns < N_TASKS as u64 {
        return -8;
    }
    if awaits == 0 {
        return -9;
    }

    /* Steal can idle a late runner in a short wave (early runners drain
     * up to 16 steps incl. steals). Prove multi-runner progress + totals. */
    let mut total_steps = 0u64;
    let mut active = 0u32;
    let mut line = LineBuf::new();
    let _ = write!(
        line,
        "py concurrency: runners={} tasks={} spawns={} awaits={}",
        n, N_TASKS, spawns, awaits
    );
    for r in 0..n {
        let steps = pm_metal_async_metric_steps(r);
        total_steps = total_steps.saturating_add(steps);
        if steps > 0 {
            active = active.saturating_add(1);
        }
        let _ = write!(line, " r{}={}", r, steps);
    }
    if total_steps < N_TASKS as u64 {
        return -10;
    }
    if n > 1 && active < 2 {
        return -11;
    }
    let _ = write!(
        line,
        " active={} starve_max_us={} ok",
        active, starve
    );
    pm_metal_log(line.as_cstr());
    0
}
