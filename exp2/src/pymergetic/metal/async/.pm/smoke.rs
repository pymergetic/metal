//! Host async throughput bench — ``metal mod test pymergetic/metal/async``.
//! Prints ops/s + p99 wait (us). Lives under `.pm/` only — not main source.
use std::alloc::{alloc, dealloc, Layout};
use std::time::Instant;

/* Same-package bin: pull the rlib so no_mangle C symbols resolve. */
use pymergetic_metal_async as _;
use pymergetic_metal_mem as _;

extern "C" {
    fn pm_metal_async_start(n_cpus: u32) -> i32;
    fn pm_metal_async_ready() -> i32;
    fn pm_metal_async_run_poll_all() -> i32;
    fn pm_metal_async_sleep_us(us: u64) -> u32;
    fn pm_metal_async_status(h: u32) -> u32;
    fn pm_metal_async_completed_u32(v: u32) -> u32;
    fn pm_metal_async_coro_close(h: u32);
}

const DONE: u32 = 2;
const WAVE: usize = 32;
const WAVES: usize = 64;

fn percentile_us(samples: &mut [u64], pct: usize) -> u64 {
    assert!(!samples.is_empty());
    samples.sort_unstable();
    let idx = ((samples.len() * pct) / 100).min(samples.len() - 1);
    samples[idx]
}

fn main() {
    const N: usize = 512 * 1024;
    let layout = Layout::from_size_align(N, 4096).unwrap();
    let base = unsafe { alloc(layout) };
    assert!(!base.is_null());
    unsafe {
        assert_eq!(pymergetic_metal_mem::api::init(base, N), 0);
        assert_eq!(pm_metal_async_start(1), 0);
        assert_eq!(pm_metal_async_ready(), 1);
    }

    // --- empty poll_all ops/s ---
    const POLL_ITERS: u64 = 200_000;
    let t0 = Instant::now();
    for _ in 0..POLL_ITERS {
        unsafe {
            let _ = pm_metal_async_run_poll_all();
        }
    }
    let poll_ns = t0.elapsed().as_nanos().max(1);
    let poll_ops = (POLL_ITERS as u128 * 1_000_000_000) / poll_ns;

    // --- completed_u32 create/status/close ops/s ---
    const DONE_ITERS: u64 = 50_000;
    let t1 = Instant::now();
    for i in 0..DONE_ITERS {
        unsafe {
            let h = pm_metal_async_completed_u32(i as u32);
            assert_ne!(h, 0);
            assert_eq!(pm_metal_async_status(h), DONE);
            pm_metal_async_coro_close(h);
        }
    }
    let done_ns = t1.elapsed().as_nanos().max(1);
    let done_ops = (DONE_ITERS as u128 * 1_000_000_000) / done_ns;

    // --- sleep_us(0) wave: throughput + p99 wait ---
    let mut waits = Vec::with_capacity(WAVE * WAVES);
    let mut handles = [0u32; WAVE];
    let t2 = Instant::now();
    for _ in 0..WAVES {
        let starts: [Instant; WAVE] = core::array::from_fn(|_| Instant::now());
        for i in 0..WAVE {
            unsafe {
                let h = pm_metal_async_sleep_us(0);
                assert_ne!(h, 0, "sleep_us(0) spawn failed");
                handles[i] = h;
            }
        }
        let mut left = WAVE;
        while left > 0 {
            unsafe {
                let _ = pm_metal_async_run_poll_all();
            }
            for i in 0..WAVE {
                let h = handles[i];
                if h == 0 {
                    continue;
                }
                let st = unsafe { pm_metal_async_status(h) };
                if st == DONE {
                    waits.push(starts[i].elapsed().as_micros() as u64);
                    unsafe {
                        pm_metal_async_coro_close(h);
                    }
                    handles[i] = 0;
                    left -= 1;
                } else if st > DONE {
                    panic!("sleep handle error status={st}");
                }
            }
        }
    }
    let sleep_ns = t2.elapsed().as_nanos().max(1);
    let sleep_n = (WAVE * WAVES) as u128;
    let sleep_ops = (sleep_n * 1_000_000_000) / sleep_ns;
    let p99 = percentile_us(&mut waits, 99);

    unsafe {
        dealloc(base, layout);
    }

    println!(
        "async .pm/smoke.rs: poll_all={poll_ops}/s completed={done_ops}/s sleep0={sleep_ops}/s p99_wait_us={p99}"
    );
    println!("async .pm/smoke.rs: PASS");
}
