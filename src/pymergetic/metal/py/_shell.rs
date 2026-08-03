//! REPL-as-boot-shell -- a cooperative async task that drains UART bytes
//! into the *same* session `_loop.rs` already owns
//! (`pm_metal_py_loop_step`/`feed`/`reset`), echoes what it read, and
//! reprints the `>>> ` / `... ` prompt once a submitted line has
//! actually run. Shared/default context only -- there is no separate
//! shell-only interpreter state, and no command-registration / `pmcmd`
//! surface: the REPL is the only interactive shell.
//!
//! Prompt bookkeeping without a persistent frame: a step only ever
//! prints a prompt when the bytes it just drained *this tick* contained
//! a newline (`had_newline`) -- `_loop.rs::step()` only ever runs
//! `exec_line` on a tick that saw one, so `had_newline` alone
//! disambiguates "just executed / need-more" from "still mid-line,
//! nothing to report yet". Printing on every idle tick would spam the
//! prompt every scheduler pass; this task is stateless across ticks by
//! construction (`state_bytes = 0`).
//!
//! Echo/prompt bytes go straight to `pm_metal_dev_serial_write`
//! (immediate, unbuffered) rather than through `mp_hal_stdout_tx_strn`
//! (which line-buffers for `pm_metal_log` -- see `mphalport.c` doc) --
//! an interactive prompt has to appear before the next newline, not
//! after. Evaluated-expression output still goes through the existing
//! `_loop.rs` -> `mp_hal_stdout_tx_strn` -> `pm_metal_log` path,
//! unchanged; both land on the same physical UART, in order, because
//! everything here runs synchronously within one step.

use core::sync::atomic::{AtomicBool, Ordering};

use pymergetic_metal_dev_serial::{pm_metal_dev_serial_try_read, pm_metal_dev_serial_write};

use crate::repl_loop;

// Floor / product C ABI: resolved at final link (boot + deps). Do not use
// OUT_DIR-staged ImportRow faces here -- rust-analyzer expands
// `env!("OUT_DIR")` from a prior build-script run and then fails when that
// hash directory is gone (common when analyzing py via boot's target/).
extern "C" {
    fn pm_metal_async_spawn(
        step: Option<unsafe extern "C" fn(u32) -> u32>,
        state_bytes: u32,
        prio: u32,
    ) -> u32;
    fn pm_metal_log(line: *const u8);
    fn pm_metal_util_ascii_log_rainbow(text: *const u8);
}

const ASYNC_PRIO_MED: u32 = 1;
const ASYNC_PENDING: u32 = 0;

/// Bytes drained from serial per tick -- matches `_loop.rs`'s own
/// per-step drain cap (`MAX_BYTES_PER_STEP`) so one shell tick never
/// hands the loop more than one of its own steps would drain anyway.
const MAX_BYTES_PER_TICK: usize = 64;

static RUNNING: AtomicBool = AtomicBool::new(false);

fn write_raw(bytes: &[u8]) {
    if bytes.is_empty() {
        return;
    }
    unsafe { pm_metal_dev_serial_write(bytes.as_ptr(), bytes.len()) };
}

fn is_backspace(b: u8) -> bool {
    b == 0x08 || b == 0x7f
}

unsafe extern "C" fn shell_step(_self_h: u32) -> u32 {
    let mut buf = [0u8; MAX_BYTES_PER_TICK];
    let n = pm_metal_dev_serial_try_read(buf.as_mut_ptr(), buf.len());
    if n <= 0 {
        /* Nothing waiting (or no UART at all, e.g. host) -- reschedule,
         * never block (see metal-no-long-running-ops). */
        return ASYNC_PENDING;
    }
    let n = n as usize;
    let mut feed = [0u8; MAX_BYTES_PER_TICK];
    let mut feed_n = 0usize;
    let mut had_newline = false;
    for i in 0..n {
        let mut b = buf[i];
        if b == b'\r' {
            b = b'\n';
        }
        if is_backspace(b) {
            /* Visual erase; feed BS so the line buffer pops. */
            write_raw(b"\x08 \x08");
            feed[feed_n] = 0x08;
            feed_n += 1;
            continue;
        }
        write_raw(&[b]);
        feed[feed_n] = b;
        feed_n += 1;
        if b == b'\n' {
            had_newline = true;
        }
    }
    let _ = repl_loop::feed(feed.as_ptr(), feed_n);
    if !had_newline {
        /* Still mid-line -- nothing for the loop to run yet, no prompt
         * to reprint. */
        return ASYNC_PENDING;
    }
    let rc = repl_loop::step();
    if rc == repl_loop::STEP_NEED_MORE {
        write_raw(b"... ");
    } else {
        /* STEP_IDLE (line executed) or STEP_ERROR (buffer reset) both
         * return to a fresh top-level prompt. */
        write_raw(b">>> ");
    }
    ASYNC_PENDING
}

fn print_banner() {
    unsafe {
        pm_metal_util_ascii_log_rainbow(b"MetalPython\0".as_ptr());
        pm_metal_log(
            b"\x1b[1;35mMetal Python\x1b[0m -- persistent REPL, shared globals.\0".as_ptr(),
        );
        /* Externals identity lives in the boot tree only (no banner dup). */
        pm_metal_log(b"\0".as_ptr());
    }
}

/// Spawn the boot shell task. `0` on success; `-1` if already running,
/// the session reset failed, or the async spawn failed (e.g. async
/// hasn't been started -- the host smoke path, which does not call this
/// at all; see `.pm/smoke.rs`).
pub fn start() -> i32 {
    if RUNNING.swap(true, Ordering::AcqRel) {
        return -1;
    }
    if repl_loop::reset() != 0 {
        RUNNING.store(false, Ordering::Release);
        return -1;
    }
    print_banner();
    write_raw(b">>> ");
    let h = unsafe { pm_metal_async_spawn(Some(shell_step), 0, ASYNC_PRIO_MED) };
    if h == 0 {
        RUNNING.store(false, Ordering::Release);
        return -1;
    }
    0
}

/// `1` iff [`start`] succeeded and hasn't been undone by a later failed
/// `start()` call, else `0`.
pub fn running() -> i32 {
    i32::from(RUNNING.load(Ordering::Acquire))
}
