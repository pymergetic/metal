//! Internal async engine — handle table, queues, step/await/wake.
//! Public stems expose the C ABI; this file owns the durable state.

use core::sync::atomic::{AtomicU32, Ordering};

pub type Handle = u32;
pub const INVALID: Handle = 0;

pub const MAX_HANDLES: usize = 128;
pub const MAX_RUNNERS: usize = 8;
pub const QUEUE_CAP: usize = 64;
pub const MAX_PIDS: usize = 64;

#[repr(u32)]
#[derive(Clone, Copy, PartialEq, Eq)]
pub enum Status {
    Pending = 0,
    Waiting = 1,
    Done = 2,
    Cancelled = 3,
    Error = 4,
}

#[repr(u32)]
#[derive(Clone, Copy, PartialEq, Eq)]
pub enum Prio {
    High = 0,
    Med = 1,
    Low = 2,
}

pub const PRIO_N: usize = 3;

pub type StepFn = unsafe extern "C" fn(Handle) -> u32;

#[derive(Clone, Copy)]
struct Slot {
    used: bool,
    status: Status,
    step: Option<StepFn>,
    frame: *mut u8,
    frame_len: u32,
    /// Handle waiting on this slot (0 = none).
    waiter: Handle,
    /// Handle this slot is awaiting (0 = none).
    awaiting: Handle,
    is_task: bool,
    prio: Prio,
    runner: u32,
    pid: u32,
    /// In a ready queue (avoid double-enqueue).
    queued: bool,
    /// Opaque u32 result (net awaitables, etc.).
    result_u32: u32,
}

impl Slot {
    const fn empty() -> Self {
        Self {
            used: false,
            status: Status::Pending,
            step: None,
            frame: core::ptr::null_mut(),
            frame_len: 0,
            waiter: INVALID,
            awaiting: INVALID,
            is_task: false,
            prio: Prio::Med,
            runner: 0,
            pid: 0,
            queued: false,
            result_u32: 0,
        }
    }
}

#[derive(Clone, Copy)]
struct Ring {
    buf: [Handle; QUEUE_CAP],
    head: usize,
    tail: usize,
    len: usize,
}

impl Ring {
    const fn empty() -> Self {
        Self {
            buf: [INVALID; QUEUE_CAP],
            head: 0,
            tail: 0,
            len: 0,
        }
    }

    fn push(&mut self, h: Handle) -> bool {
        if self.len >= QUEUE_CAP || h == INVALID {
            return false;
        }
        self.buf[self.tail] = h;
        self.tail = (self.tail + 1) % QUEUE_CAP;
        self.len += 1;
        true
    }

    fn pop(&mut self) -> Handle {
        if self.len == 0 {
            return INVALID;
        }
        let h = self.buf[self.head];
        self.buf[self.head] = INVALID;
        self.head = (self.head + 1) % QUEUE_CAP;
        self.len -= 1;
        h
    }

    fn len(&self) -> usize {
        self.len
    }
}

#[derive(Clone, Copy)]
struct Runner {
    q: [Ring; PRIO_N],
}

impl Runner {
    const fn empty() -> Self {
        Self {
            q: [Ring::empty(), Ring::empty(), Ring::empty()],
        }
    }
}

struct Engine {
    lock: AtomicU32,
    slots: [Slot; MAX_HANDLES + 1],
    runners: [Runner; MAX_RUNNERS],
    n_runners: u32,
    started: bool,
    rr: u32,
    weights: [u32; PRIO_N],
    /// pid -> handle (index 0 unused).
    pids: [Handle; MAX_PIDS + 1],
    next_pid: u32,
    current_runner: u32,
}

impl Engine {
    const fn new() -> Self {
        Self {
            lock: AtomicU32::new(0),
            slots: [Slot::empty(); MAX_HANDLES + 1],
            runners: [Runner::empty(); MAX_RUNNERS],
            n_runners: 0,
            started: false,
            rr: 0,
            weights: [4, 2, 1],
            pids: [INVALID; MAX_PIDS + 1],
            next_pid: 1,
            current_runner: 0,
        }
    }
}

static mut ENG: Engine = Engine::new();

fn lock() {
    unsafe {
        let l = &(*core::ptr::addr_of_mut!(ENG)).lock;
        while l
            .compare_exchange_weak(0, 1, Ordering::Acquire, Ordering::Relaxed)
            .is_err()
        {
            core::hint::spin_loop();
        }
    }
}

fn unlock() {
    unsafe {
        (*core::ptr::addr_of_mut!(ENG))
            .lock
            .store(0, Ordering::Release);
    }
}

fn with_lock<R>(f: impl FnOnce(&mut Engine) -> R) -> R {
    lock();
    let r = unsafe { f(&mut *core::ptr::addr_of_mut!(ENG)) };
    unlock();
    r
}

extern "C" {
    fn pm_metal_mem_alloc(size: usize) -> *mut u8;
    fn pm_metal_mem_free(ptr: *mut u8);
}

fn alloc_frame(n: u32) -> *mut u8 {
    if n == 0 {
        return core::ptr::null_mut();
    }
    unsafe {
        let p = pm_metal_mem_alloc(n as usize);
        if !p.is_null() {
            core::ptr::write_bytes(p, 0, n as usize);
        }
        p
    }
}

fn free_frame(p: *mut u8) {
    if !p.is_null() {
        unsafe { pm_metal_mem_free(p) };
    }
}

fn slot_ok(e: &Engine, h: Handle) -> bool {
    h != INVALID && (h as usize) <= MAX_HANDLES && e.slots[h as usize].used
}

fn enqueue_locked(e: &mut Engine, h: Handle) -> bool {
    if !slot_ok(e, h) {
        return false;
    }
    let s = &mut e.slots[h as usize];
    if !s.is_task || s.queued {
        return s.queued;
    }
    if s.status == Status::Done || s.status == Status::Cancelled || s.status == Status::Error {
        return false;
    }
    let r = s.runner as usize;
    let p = s.prio as usize;
    if r >= e.n_runners as usize {
        return false;
    }
    if e.runners[r].q[p].push(h) {
        s.queued = true;
        true
    } else {
        false
    }
}

fn wake_waiter_locked(e: &mut Engine, done_h: Handle) {
    if !slot_ok(e, done_h) {
        return;
    }
    let waiter = e.slots[done_h as usize].waiter;
    e.slots[done_h as usize].waiter = INVALID;
    if !slot_ok(e, waiter) {
        return;
    }
    let w = &mut e.slots[waiter as usize];
    if w.awaiting == done_h {
        w.awaiting = INVALID;
        if w.status == Status::Waiting {
            w.status = Status::Pending;
        }
        let _ = enqueue_locked(e, waiter);
    }
}

pub fn start(n_cpus: u32) -> i32 {
    with_lock(|e| {
        if e.started {
            return -1;
        }
        let n = if n_cpus == 0 {
            1
        } else if n_cpus > MAX_RUNNERS as u32 {
            MAX_RUNNERS as u32
        } else {
            n_cpus
        };
        e.n_runners = n;
        e.started = true;
        e.rr = 0;
        e.current_runner = 0;
        e.weights = [4, 2, 1];
        0
    })
}

pub fn set_weights(high: u32, med: u32, low: u32) {
    with_lock(|e| {
        e.weights[0] = if high == 0 { 1 } else { high };
        e.weights[1] = if med == 0 { 1 } else { med };
        e.weights[2] = if low == 0 { 1 } else { low };
    });
}

pub fn n_runners() -> u32 {
    with_lock(|e| e.n_runners)
}

pub fn started() -> bool {
    with_lock(|e| e.started)
}

pub fn coro_create(step: StepFn, state_bytes: u32) -> Handle {
    let frame = alloc_frame(state_bytes);
    if state_bytes != 0 && frame.is_null() {
        return INVALID;
    }
    with_lock(|e| {
        let mut h = INVALID;
        for i in 1..=MAX_HANDLES {
            if !e.slots[i].used {
                h = i as Handle;
                break;
            }
        }
        if h == INVALID {
            free_frame(frame);
            return INVALID;
        }
        e.slots[h as usize] = Slot {
            used: true,
            status: Status::Pending,
            step: Some(step),
            frame,
            frame_len: state_bytes,
            waiter: INVALID,
            awaiting: INVALID,
            is_task: false,
            prio: Prio::Med,
            runner: 0,
            pid: 0,
            queued: false,
            result_u32: 0,
        };
        h
    })
}

pub fn set_result_u32(h: Handle, v: u32) {
    with_lock(|e| {
        if slot_ok(e, h) {
            e.slots[h as usize].result_u32 = v;
        }
    });
}

/// Create a handle that is already `Done` with `result_u32 = v` (no runner step).
/// For RAM/sync backends behind an async API — awaiters see DONE immediately.
pub fn completed_u32(v: u32) -> Handle {
    unsafe extern "C" fn noop_step(_h: Handle) -> u32 {
        Status::Done as u32
    }
    let h = coro_create(noop_step, 0);
    if h == INVALID {
        return INVALID;
    }
    with_lock(|e| {
        if slot_ok(e, h) {
            e.slots[h as usize].status = Status::Done;
            e.slots[h as usize].result_u32 = v;
            e.slots[h as usize].is_task = true;
            e.slots[h as usize].queued = false;
        }
    });
    h
}

pub fn result_u32(h: Handle) -> u32 {
    with_lock(|e| {
        if !slot_ok(e, h) {
            0
        } else {
            e.slots[h as usize].result_u32
        }
    })
}

pub fn coro_state(h: Handle) -> *mut u8 {
    with_lock(|e| {
        if !slot_ok(e, h) {
            return core::ptr::null_mut();
        }
        e.slots[h as usize].frame
    })
}

pub fn coro_close(h: Handle) {
    let frame = with_lock(|e| {
        if !slot_ok(e, h) {
            return core::ptr::null_mut();
        }
        let pid = e.slots[h as usize].pid;
        if pid != 0 && (pid as usize) <= MAX_PIDS {
            e.pids[pid as usize] = INVALID;
        }
        let frame = e.slots[h as usize].frame;
        e.slots[h as usize] = Slot::empty();
        frame
    });
    free_frame(frame);
}

pub fn status_of(h: Handle) -> Status {
    with_lock(|e| {
        if !slot_ok(e, h) {
            Status::Error
        } else {
            e.slots[h as usize].status
        }
    })
}

pub fn create_task(h: Handle, prio: Prio) -> i32 {
    with_lock(|e| {
        if !e.started || !slot_ok(e, h) {
            return -1;
        }
        let r = e.rr % e.n_runners;
        e.rr = e.rr.wrapping_add(1);
        {
            let s = &mut e.slots[h as usize];
            s.is_task = true;
            s.prio = prio;
            s.runner = r;
            if s.status == Status::Done || s.status == Status::Cancelled || s.status == Status::Error
            {
                return -1;
            }
            s.status = Status::Pending;
        }
        if enqueue_locked(e, h) {
            0
        } else {
            -1
        }
    })
}

pub fn spawn(step: StepFn, state_bytes: u32, prio: Prio) -> Handle {
    let h = coro_create(step, state_bytes);
    if h == INVALID {
        return INVALID;
    }
    if create_task(h, prio) != 0 {
        coro_close(h);
        return INVALID;
    }
    h
}

pub fn await_child(self_h: Handle, child_h: Handle) -> Status {
    with_lock(|e| {
        if !slot_ok(e, self_h) || !slot_ok(e, child_h) {
            return Status::Error;
        }
        let st = e.slots[child_h as usize].status;
        if st == Status::Done || st == Status::Cancelled || st == Status::Error {
            e.slots[self_h as usize].awaiting = INVALID;
            return st;
        }
        /* Nest: promote child onto the parent's runner if not yet a task. */
        if !e.slots[child_h as usize].is_task {
            let r = e.slots[self_h as usize].runner;
            let p = e.slots[self_h as usize].prio;
            e.slots[child_h as usize].is_task = true;
            e.slots[child_h as usize].runner = r;
            e.slots[child_h as usize].prio = p;
            let _ = enqueue_locked(e, child_h);
        }
        e.slots[child_h as usize].waiter = self_h;
        e.slots[self_h as usize].awaiting = child_h;
        e.slots[self_h as usize].status = Status::Waiting;
        e.slots[self_h as usize].queued = false;
        Status::Waiting
    })
}

fn pop_weighted(e: &mut Engine, runner: usize) -> Handle {
    let w = e.weights;
    for (prio, &weight) in w.iter().enumerate() {
        let mut left = weight;
        while left > 0 {
            if e.runners[runner].q[prio].len() == 0 {
                break;
            }
            let h = e.runners[runner].q[prio].pop();
            left -= 1;
            if h == INVALID {
                continue;
            }
            if slot_ok(e, h) {
                e.slots[h as usize].queued = false;
                return h;
            }
        }
    }
    INVALID
}

fn steal(e: &mut Engine, me: usize) -> Handle {
    let n = e.n_runners as usize;
    if n <= 1 {
        return INVALID;
    }
    for prio in 0..PRIO_N {
        let mut best_r = me;
        let mut best_len = 0usize;
        for r in 0..n {
            if r == me {
                continue;
            }
            let len = e.runners[r].q[prio].len();
            if len > best_len {
                best_len = len;
                best_r = r;
            }
        }
        if best_len == 0 {
            continue;
        }
        let h = e.runners[best_r].q[prio].pop();
        if h == INVALID || !slot_ok(e, h) {
            continue;
        }
        e.slots[h as usize].queued = false;
        e.slots[h as usize].runner = me as u32;
        return h;
    }
    INVALID
}

fn balance_pull(e: &mut Engine, me: usize) {
    let n = e.n_runners as usize;
    if n <= 1 {
        return;
    }
    let mine = e.runners[me].q[0].len();
    let mut richest = me;
    let mut rich_len = mine;
    for r in 0..n {
        if r == me {
            continue;
        }
        let len = e.runners[r].q[0].len();
        if len > rich_len {
            rich_len = len;
            richest = r;
        }
    }
    if rich_len >= mine.saturating_add(2) {
        let h = e.runners[richest].q[0].pop();
        if h != INVALID && slot_ok(e, h) {
            e.slots[h as usize].queued = false;
            e.slots[h as usize].runner = me as u32;
            let _ = enqueue_locked(e, h);
        }
    }
}

fn take_ready(runner: u32) -> Handle {
    with_lock(|e| {
        if !e.started || runner >= e.n_runners {
            return INVALID;
        }
        e.current_runner = runner;
        let me = runner as usize;
        balance_pull(e, me);
        let mut h = pop_weighted(e, me);
        if h == INVALID {
            h = steal(e, me);
        }
        h
    })
}

fn finish_step(h: Handle, raw: u32) {
    with_lock(|e| {
        if !slot_ok(e, h) {
            return;
        }
        let st = match raw {
            0 => Status::Pending,
            1 => Status::Waiting,
            2 => Status::Done,
            3 => Status::Cancelled,
            _ => Status::Error,
        };
        e.slots[h as usize].status = st;
        match st {
            Status::Pending => {
                let _ = enqueue_locked(e, h);
            }
            Status::Waiting => {
                e.slots[h as usize].queued = false;
            }
            Status::Done | Status::Cancelled | Status::Error => {
                e.slots[h as usize].queued = false;
                wake_waiter_locked(e, h);
            }
        }
    });
}

fn step_one(h: Handle) {
    let step = with_lock(|e| {
        if !slot_ok(e, h) || e.slots[h as usize].status == Status::Waiting {
            return None;
        }
        e.slots[h as usize].step
    });
    let Some(step) = step else {
        return;
    };
    let raw = unsafe { step(h) };
    finish_step(h, raw);
}

pub fn run_poll_runner(runner: u32) -> i32 {
    if !started() || runner >= n_runners() {
        return -1;
    }
    let mut ran = 0i32;
    for _ in 0..16 {
        let h = take_ready(runner);
        if h == INVALID {
            break;
        }
        step_one(h);
        ran += 1;
    }
    ran
}

pub fn run_poll() -> i32 {
    let r = with_lock(|e| e.current_runner);
    run_poll_runner(r)
}

pub fn run_poll_all() -> i32 {
    let n = with_lock(|e| e.n_runners);
    if n == 0 {
        return -1;
    }
    let mut total = 0i32;
    for r in 0..n {
        let nrun = run_poll_runner(r);
        if nrun > 0 {
            total += nrun;
        }
    }
    total
}

pub fn process_crown(task_h: Handle) -> u32 {
    with_lock(|e| {
        if !slot_ok(e, task_h) || !e.slots[task_h as usize].is_task {
            return 0;
        }
        if e.slots[task_h as usize].pid != 0 {
            return e.slots[task_h as usize].pid;
        }
        let mut pid = 0u32;
        for i in 1..=MAX_PIDS {
            let idx = ((e.next_pid as usize + i - 1) % MAX_PIDS) + 1;
            if e.pids[idx] == INVALID {
                pid = idx as u32;
                break;
            }
        }
        if pid == 0 {
            return 0;
        }
        e.next_pid = pid.wrapping_add(1);
        if e.next_pid == 0 || e.next_pid as usize > MAX_PIDS {
            e.next_pid = 1;
        }
        e.pids[pid as usize] = task_h;
        e.slots[task_h as usize].pid = pid;
        pid
    })
}

pub fn process_handle(pid: u32) -> Handle {
    with_lock(|e| {
        if pid == 0 || (pid as usize) > MAX_PIDS {
            INVALID
        } else {
            e.pids[pid as usize]
        }
    })
}

pub fn process_kill(pid: u32) -> i32 {
    with_lock(|e| {
        if pid == 0 || (pid as usize) > MAX_PIDS {
            return -1;
        }
        let h = e.pids[pid as usize];
        if !slot_ok(e, h) {
            return -1;
        }
        e.slots[h as usize].status = Status::Cancelled;
        e.slots[h as usize].queued = false;
        wake_waiter_locked(e, h);
        0
    })
}

pub fn prio_from_u32(v: u32) -> Prio {
    match v {
        0 => Prio::High,
        2 => Prio::Low,
        _ => Prio::Med,
    }
}
