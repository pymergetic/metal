//! Cooperative REPL execution loop -- line buffer + persistent session
//! globals, driven one short tick at a time by `pm_metal_py_loop_step`.
//!
//! `step()` never blocks and never loops for long (see
//! `metal-no-long-running-ops`): each call drains at most
//! [`MAX_BYTES_PER_STEP`] input bytes into the line buffer, and only once
//! a submitted line closes with `\n` does it hand the accumulated buffer
//! to [`repl::exec_line`]. Multi-line continuation (`def f():` etc.) is
//! entirely `exec_line`'s own `continue_with_input` check -- this loop
//! just keeps appending fresh lines to the same buffer across
//! `NeedMore` steps until a complete statement/expression is ready, then
//! clears it.
//!
//! Two input sources feed the line buffer, drained in this order:
//! 1. the internal feed ring (`pm_metal_py_loop_feed` -- for tests and
//!    hosts with no Metal stdio stream attached);
//! 2. `mp_hal_stdin_rx_chr` -- py's own `port/mphalport.c` (same module,
//!    not a foreign ABI), which pulls from the attached Metal stdio
//!    stream when one exists.
//!
//! Output goes through `mp_hal_stdout_tx_strn` (same port file), which in
//! turn writes to Metal log -- see `mphalport.c`'s module doc.
//!
//! Every submitted line is Python -- there is no shell/`pmcmd` meta-line
//! escape. Registry callables belong in the Python module tree later, not
//! as a parallel `!module.func` command surface.

use core::cell::UnsafeCell;
use core::sync::atomic::{AtomicBool, Ordering};

use crate::upy::py::compile;
use crate::upy::py::obj::{self, MpObj};
use crate::upy::py::objects::objdict;
use crate::upy::py::repl::{self, ReplMode, ReplResult};

/// One assembled REPL submission -- bounded (a paste larger than this is
/// a real, checkable error, not silent truncation).
const LINE_CAP: usize = 1024;
/// Internal feed ring (`pm_metal_py_loop_feed`) capacity.
const FEED_CAP: usize = 256;
/// Bytes drained per `step()` tick -- keeps each call short; a burst
/// bigger than this drains over several ticks instead of one loop that
/// could run long.
const MAX_BYTES_PER_STEP: usize = 64;

/// `step()` return codes (documented on the C ABI face too, see
/// `pm_metal_py_loop_step` in `__init__.rs`).
pub const STEP_IDLE: i32 = 0;
pub const STEP_NEED_MORE: i32 = 1;
pub const STEP_ERROR: i32 = -1;

extern "C" {
    fn mp_hal_stdin_rx_chr() -> i32;
    fn mp_hal_stdout_tx_strn(str_: *const u8, len: usize);
}

struct Spin {
    state: AtomicBool,
}

impl Spin {
    const fn new() -> Self {
        Self {
            state: AtomicBool::new(false),
        }
    }
    fn lock(&self) {
        while self
            .state
            .compare_exchange_weak(false, true, Ordering::Acquire, Ordering::Relaxed)
            .is_err()
        {
            core::hint::spin_loop();
        }
    }
    fn unlock(&self) {
        self.state.store(false, Ordering::Release);
    }
}

struct FeedRing {
    buf: [u8; FEED_CAP],
    head: usize,
    len: usize,
}

impl FeedRing {
    const fn new() -> Self {
        Self {
            buf: [0; FEED_CAP],
            head: 0,
            len: 0,
        }
    }
    fn push(&mut self, b: u8) -> bool {
        if self.len == FEED_CAP {
            return false;
        }
        let tail = (self.head + self.len) % FEED_CAP;
        self.buf[tail] = b;
        self.len += 1;
        true
    }
    fn pop(&mut self) -> Option<u8> {
        if self.len == 0 {
            return None;
        }
        let b = self.buf[self.head];
        self.head = (self.head + 1) % FEED_CAP;
        self.len -= 1;
        Some(b)
    }
    fn clear(&mut self) {
        self.head = 0;
        self.len = 0;
    }
}

struct Session {
    line: [u8; LINE_CAP],
    line_len: usize,
    globals: MpObj,
    feed: FeedRing,
    /// Last auto-printed expression's small-int value, if the last
    /// executed submission was one (see `pm_metal_py_loop_last_result_*`
    /// -- an honest two-part seam, not one sentinel value that could be
    /// confused with a real result).
    last_value: Option<isize>,
}

impl Session {
    const fn new() -> Self {
        Self {
            line: [0; LINE_CAP],
            line_len: 0,
            globals: obj::OBJ_NULL,
            feed: FeedRing::new(),
            last_value: None,
        }
    }
}

struct SessionCell {
    inner: UnsafeCell<Session>,
}

// Safety: `inner` is only touched while `LOCK` is held.
unsafe impl Sync for SessionCell {}

impl SessionCell {
    const fn new() -> Self {
        Self {
            inner: UnsafeCell::new(Session::new()),
        }
    }
}

static LOCK: Spin = Spin::new();
static SESSION: SessionCell = SessionCell::new();

fn next_byte(s: &mut Session) -> i32 {
    if let Some(b) = s.feed.pop() {
        return b as i32;
    }
    unsafe { mp_hal_stdin_rx_chr() }
}

fn ensure_globals(s: &mut Session) {
    if s.globals != obj::OBJ_NULL {
        return;
    }
    s.globals = unsafe { objdict::new(16) };
    if s.globals == obj::OBJ_NULL {
        return;
    }
    /* Core builtins as real natives (same set as `import builtins`). */
    unsafe {
        crate::upy::py::builtin::modbuiltins::seed_callables_into_dict(s.globals);
    }
}

fn write_stdout(bytes: &[u8]) {
    if bytes.is_empty() {
        return;
    }
    unsafe { mp_hal_stdout_tx_strn(bytes.as_ptr(), bytes.len()) };
}

/// One short, non-blocking tick. See module doc for the input/output
/// wiring and `pm_metal_py_loop_step` for the documented return codes.
pub fn step() -> i32 {
    LOCK.lock();
    let s = unsafe { &mut *SESSION.inner.get() };
    ensure_globals(s);

    let mut got_newline = false;
    for _ in 0..MAX_BYTES_PER_STEP {
        let c = next_byte(s);
        if c < 0 {
            break;
        }
        /* BS / DEL -- erase last buffered char (shell echoes visually). */
        if c == 0x08 || c == 0x7f {
            if s.line_len > 0 {
                s.line_len -= 1;
            }
            continue;
        }
        if s.line_len >= LINE_CAP {
            s.line_len = 0;
            LOCK.unlock();
            return STEP_ERROR;
        }
        s.line[s.line_len] = c as u8;
        s.line_len += 1;
        if c == b'\n' as i32 {
            got_newline = true;
            break;
        }
    }
    if !got_newline {
        LOCK.unlock();
        return STEP_IDLE;
    }

    let globals = s.globals;
    let rc = match repl::exec_line(&s.line[..s.line_len], ReplMode::Single, globals) {
        Ok(ReplResult::NeedMore) => STEP_NEED_MORE,
        Ok(ReplResult::Executed) => {
            s.line_len = 0;
            s.last_value = None;
            STEP_IDLE
        }
        Ok(ReplResult::Value(v)) => {
            s.line_len = 0;
            s.last_value = obj::small_int_value_checked(v.obj);
            /* Match CPython interactive: a lone expression that is None
             * (e.g. `print(...)`'s return) is not echoed. */
            if !crate::upy::py::objects::objnone::is_none(v.obj) {
                write_stdout(v.repr());
                write_stdout(b"\n");
            }
            STEP_IDLE
        }
        Err(e) => {
            s.line_len = 0;
            s.last_value = None;
            let msg: &[u8] = match e {
                repl::ReplError::Parse(_) => b"Error: parse\n",
                repl::ReplError::Compile(compile::CompileError::Unsupported { .. }) => {
                    b"Error: unsupported\n"
                }
                repl::ReplError::Compile(compile::CompileError::OutOfMemory) => {
                    b"Error: out of memory\n"
                }
                repl::ReplError::Compile(compile::CompileError::TooManyLocals) => {
                    b"Error: too many locals\n"
                }
                repl::ReplError::Compile(compile::CompileError::NotAFuncdef) => {
                    b"Error: unsupported\n"
                }
                repl::ReplError::Exception => b"Error: exception\n",
            };
            write_stdout(msg);
            STEP_ERROR
        }
    };
    LOCK.unlock();
    rc
}

/// Inject bytes for the loop to read on later `step()` calls (tests /
/// non-stream hosts). Returns the number of bytes actually queued (may
/// be less than `len` if the internal ring is full) or `-1` for a null
/// `ptr`.
///
/// # Safety
/// `ptr` must be valid for `len` bytes.
pub unsafe fn feed(ptr: *const u8, len: usize) -> i32 {
    if ptr.is_null() {
        return -1;
    }
    let slice = core::slice::from_raw_parts(ptr, len);
    LOCK.lock();
    let s = &mut *SESSION.inner.get();
    let mut n = 0usize;
    for &b in slice {
        if !s.feed.push(b) {
            break;
        }
        n += 1;
    }
    LOCK.unlock();
    n as i32
}

/// Clear the line buffer, feed ring, and session globals (frees the
/// globals dict if one was allocated). Always returns 0.
pub fn reset() -> i32 {
    LOCK.lock();
    let s = unsafe { &mut *SESSION.inner.get() };
    if s.globals != obj::OBJ_NULL {
        unsafe { objdict::free(s.globals) };
    }
    s.globals = obj::OBJ_NULL;
    s.line_len = 0;
    s.feed.clear();
    s.last_value = None;
    LOCK.unlock();
    0
}

/// Last auto-printed expression's small-int value -- only meaningful
/// when [`last_result_valid`] is nonzero (see `Session::last_value`
/// doc).
pub fn last_result_i32() -> i32 {
    LOCK.lock();
    let s = unsafe { &*SESSION.inner.get() };
    let v = s.last_value.unwrap_or(0) as i32;
    LOCK.unlock();
    v
}

/// `1` iff the last executed submission was an auto-printed small-int
/// value (i.e. [`last_result_i32`] holds a real result), else `0`.
pub fn last_result_valid() -> i32 {
    LOCK.lock();
    let s = unsafe { &*SESSION.inner.get() };
    let v = i32::from(s.last_value.is_some());
    LOCK.unlock();
    v
}
