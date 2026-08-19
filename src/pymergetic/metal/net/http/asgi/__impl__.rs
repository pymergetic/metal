//! pymergetic.metal.net.http.asgi — stackless HTTP/1.0 server on net.ip.
//!
//! Microdot-shaped: `route(method, path, body)` then `listen`. Park via the
//! same WAITING returns as ip (`accept == -2`, `recv == 0`). Default app is
//! GET * → `asgi`. Not a second HTTP client (that stays impl=c).

#![allow(clippy::missing_safety_doc)]
#![allow(non_camel_case_types)]

use core::cell::UnsafeCell;
use core::ffi::c_void;
use core::ptr;

const SOCK_STREAM: i32 = 1;
const WAITING: i32 = 1;
const DONE: i32 = 2;
const ERROR: i32 = 4;
const ACCEPT_WAIT: i32 = -2;
/* Route table is unbounded by design: it grows on demand from the arena (see
 * routes_grow), so adding routes can never overflow a fixed MAX_ROUTE table
 * again. The only limit on route count is arena free space. */
/* Concurrency / buffer budgets. These used to be bare "microcontroller" guesses
 * (4 connections, 1 KiB rx, 160 B headers) that starve a native x64 API server;
 * each is now env-configurable (PM_METAL_ASGI_*) with a larger default. A
 * firmware/intent build that needs to stay small overrides the envs — the card
 * itself is seat-agnostic. */
const MAX_CONN: usize = parse_pm_metal_asgi_max_conn();
const MAX_ASGI: usize = parse_pm_metal_asgi_max_asgi();
const RX_MAX: usize = parse_pm_metal_asgi_rx_max();
const HDR_MAX: usize = parse_pm_metal_asgi_hdr_max();
/* Response body budget for a single dynamic (route_fn) handler. This used to be
 * a fixed 16KiB "microcontroller" guess; this platform is a native x64 API
 * server (async runners, arena, streaming), so the default is 1 MiB and IS
 * configurable per build via the PM_METAL_ASGI_BODY_MAX env. Handlers that
 * outgrow even this should use route_stream_fn (progressive chunks). */
const BODY_MAX: usize = parse_pm_metal_asgi_body_max();

/* option_env! needs a literal at the call site, so each budget has its own thin
 * parser wrapped around one parse helper. */
const fn parse_opt(en: Option<&str>, default: usize) -> usize {
    let b = match en {
        Some(v) => v.as_bytes(),
        None => return default,
    };
    let mut n: usize = 0;
    let mut i = 0usize;
    while i < b.len() {
        let d = b[i];
        if d >= b'0' && d <= b'9' {
            let m = match n.checked_mul(10) {
                Some(x) => x,
                None => return usize::MAX,
            };
            n = match m.checked_add((d - b'0') as usize) {
                Some(x) => x,
                None => return usize::MAX,
            };
        }
        i += 1;
    }
    if n == 0 && b.len() != 0 {
        default
    } else {
        n
    }
}

const fn parse_pm_metal_asgi_max_conn() -> usize {
    parse_opt(option_env!("PM_METAL_ASGI_MAX_CONN"), 16)
}

const fn parse_pm_metal_asgi_max_asgi() -> usize {
    parse_opt(option_env!("PM_METAL_ASGI_MAX_ASGI"), 8)
}

const fn parse_pm_metal_asgi_rx_max() -> usize {
    parse_opt(option_env!("PM_METAL_ASGI_RX_MAX"), 4096)
}

const fn parse_pm_metal_asgi_hdr_max() -> usize {
    parse_opt(option_env!("PM_METAL_ASGI_HDR_MAX"), 1024)
}

const fn parse_pm_metal_asgi_body_max() -> usize {
    parse_opt(option_env!("PM_METAL_ASGI_BODY_MAX"), 1 << 20)
}

type Handler = unsafe extern "C" fn(
    method: *const u8,
    path: *const u8,
    out: *mut u8,
    out_max: u32,
    out_len: *mut u32,
) -> i32;

/* Resident-producer stream handler: the route holds no body; instead a C
 * producer is called repeatedly to feed the response in chunks. This is how a
 * large download (kernel/ELF/EFI, artifact, file) streams without buffering the
 * whole body — the total length comes from the size callback and is set as
 * Content-Length before the first chunk is sent. */
const STREAM_CHUNK: usize = 1 << 14; // 16 KiB transfer slice (not a body cap)
const STREAM_MAX: usize = 1 << 31; // safety upper bound for a streamed response

type StreamSize = unsafe extern "C" fn(ctx: *mut c_void) -> u64;
type StreamProducer = unsafe extern "C" fn(
    ctx: *mut c_void,
    chunk: *mut u8,
    len: *mut u32,
    cap: u32,
    more: *mut i32,
) -> i32;

#[repr(C)]
pub struct pm_util_mem_arena_t {
    _opaque: [u8; 0],
}

/* pymergetic.util.lock — the project's one lock card (RS muscle, C ABI). */
#[repr(C)]
struct pm_util_lock_t {
    locked: u32,
}

#[repr(C)]
struct pm_metal_async_coro_t {
    _opaque: [u8; 0],
}

#[repr(C)]
struct pm_metal_async_task_t {
    _opaque: [u8; 0],
}

unsafe extern "C" {
    fn pm_metal_net_ip_socket(kind: i32) -> i32;
    fn pm_metal_net_ip_close(fd: i32) -> i32;
    fn pm_metal_net_ip_bind(fd: i32, addr: u32, port: u16) -> i32;
    fn pm_util_mem_alloc(arena: *mut pm_util_mem_arena_t, n: usize) -> *mut u8;
    fn pm_util_mem_free(arena: *mut pm_util_mem_arena_t, p: *mut u8);
    fn pm_metal_net_ip_listen(fd: i32, backlog: i32) -> i32;
    fn pm_metal_net_ip_accept(fd: i32) -> i32;
    fn pm_metal_net_ip_send(fd: i32, buf: *const u8, len: u32) -> i32;
    fn pm_metal_net_ip_recv(fd: i32, buf: *mut u8, len: u32) -> i32;
    fn pm_metal_async_coro_create(
        step: unsafe extern "C" fn(*mut pm_metal_async_coro_t) -> i32,
        frame_bytes: usize,
    ) -> *mut pm_metal_async_coro_t;
    fn pm_metal_async_create_task(coro: *mut pm_metal_async_coro_t) -> *mut pm_metal_async_task_t;
    fn pm_metal_services_register(rec: *const pm_metal_service_t) -> i32;
    fn pm_util_lock_acquire(l: *mut pm_util_lock_t);
    fn pm_util_lock_release(l: *mut pm_util_lock_t);
    fn pm_metal_async_current_task() -> *mut pm_metal_async_task_t;
    fn pm_metal_async_post_task(t: *mut pm_metal_async_task_t) -> i32;
    fn pm_metal_async_sleep_us(co: *mut pm_metal_async_coro_t, us: u64) -> i32;
}

/// C mirror of `pm_metal_service_t` (services __types__.h). The asgi RS card
/// self-registers one here so `m.serve()` / `m.services()` see it; the record
/// is shared C-ABI with the services registry in Metal.
#[repr(C)]
struct pm_metal_service_t {
    name: *const u8,
    fqn: *const u8,
    default_addr: u32,
    default_port: u16,
    listen: unsafe extern "C" fn(u32, u16) -> i32,
    count: unsafe extern "C" fn() -> u32,
    status: unsafe extern "C" fn(i32) -> i32,
    stop: unsafe extern "C" fn(i32) -> i32,
}

#[derive(Clone, Copy)]
struct Route {
    used: bool,
    method: [u8; 8],
    path: [u8; 80],
    body: [u8; 256],
    body_len: u32,
    handler: Option<Handler>,
    /* Caller-owned bytes for route_static. Null means use `body`. */
    ext: *const u8,
    /* Caller-owned NUL-terminated Content-Type. Null derives it from the path
     * extension, which cannot work for extension-less or dotted API paths
     * (/health, /inspect/reg/pymergetic.metal.net.ip) — those must say "json"
     * or a browser offers the response as a download. */
    ctype: *const u8,
    /* Streaming route: producer feeds chunks; size (if any) sets Content-Length. */
    stream_ctx: *mut c_void,
    stream_size: Option<StreamSize>,
    stream_prod: Option<StreamProducer>,
    /* Deferred route: the body comes from a renderer on another thread (the
     * language runtime that owns the templates), not from a C callback. */
    defer: bool,
}

#[derive(Clone, Copy)]
struct Conn {
    used: bool,
    step: u32,
    fd: i32,
    rx: [u8; RX_MAX],
    rx_len: u32,
    snd_off: u32,
    hdr_len: u32,
    hdr: [u8; HDR_MAX],
    /* Body staging buffer (heap, via the arena) for a dynamic or deferred
     * handler. Allocated once a handler route matches, freed on release, so
     * MAX_CONN does not scale BODY_MAX across .bss. Null while idle. */
    body_buf: *mut u8,
    body: *const u8,
    body_len: u32,
    /* Content-Type declared by the matched route; null = derive from path. */
    ctype: *const u8,
    /* Active stream state (set when a stream route is matched). */
    stream_ctx: *mut c_void,
    stream_size: Option<StreamSize>,
    stream_prod: Option<StreamProducer>,
    stream_sent: u64,
    stream_chunk: [u8; STREAM_CHUNK],
    stream_chunk_len: u32,
    /* Deferred-body bookkeeping: polls spent parked, and the renderer's verdict. */
    defer_waits: u32,
    defer_ready: bool,
}

struct Mut<T>(UnsafeCell<T>);
unsafe impl<T> Sync for Mut<T> {}

static ARENA: Mut<*mut pm_util_mem_arena_t> = Mut(UnsafeCell::new(ptr::null_mut()));
static LISTEN_FDS: Mut<[i32; MAX_ASGI]> = Mut(UnsafeCell::new([-1; MAX_ASGI]));
static LISTEN_ADDRS: Mut<[u32; MAX_ASGI]> = Mut(UnsafeCell::new([0; MAX_ASGI]));
static LISTEN_PORTS: Mut<[u16; MAX_ASGI]> = Mut(UnsafeCell::new([0; MAX_ASGI]));

unsafe fn listen_fds() -> *mut i32 {
    LISTEN_FDS.0.get() as *mut i32
}
/* Route table storage. Not a fixed MAX_ROUTE array: routes are appended on
 * demand from the arena (grow-only within a session), so the table can never
 * overflow a hard cap. ROUTES_PTR is null until the first route is registered
 * after pm_net_http_asgi_init sets the arena. */
static ROUTES_PTR: Mut<*mut Route> = Mut(UnsafeCell::new(ptr::null_mut()));
static ROUTES_N: Mut<usize> = Mut(UnsafeCell::new(0));
static CONNS: Mut<[Conn; MAX_CONN]> = Mut(UnsafeCell::new([Conn {
    used: false,
    step: 0,
    fd: -1,
    rx: [0; RX_MAX],
    rx_len: 0,
    snd_off: 0,
    hdr_len: 0,
    hdr: [0; HDR_MAX],
    body_buf: ptr::null_mut(),
    body: ptr::null(),
    body_len: 0,
    ctype: ptr::null(),
    stream_ctx: ptr::null_mut(),
    stream_size: None,
    stream_prod: None,
    stream_sent: 0,
    stream_chunk: [0; STREAM_CHUNK],
    stream_chunk_len: 0,
    defer_waits: 0,
    defer_ready: false,
}; MAX_CONN]));
static DEFAULT_BODY: &[u8] = b"asgi";
static DEFER_BUSY_BODY: &[u8] = b"no renderer";

/* Deferred-request queue. A connection coroutine (async runner thread) enqueues
 * here and parks; the renderer thread — the one that owns the template engine,
 * e.g. the MicroPython thread — drains with defer_next and answers with
 * defer_reply. Both sides touch it, so it is the one part of this card under a
 * real lock (pymergetic.util.lock, the project's single lock card).
 *
 * defer_next hands out the pending path and remembers it as *current*, so the
 * renderer needs no request id: one drainer at a time, which is what a render
 * pump is. */
const MAX_DEFER: usize = MAX_CONN;
/* A parked request sleeps in slices rather than waiting on nothing: the reply
 * posts the task for an immediate wake, and the timer bounds the wait when no
 * renderer is draining, so a missing pump answers instead of wedging the slot
 * (with MAX_CONN this small, a stuck request would take the server down). */
const DEFER_SLICE_US: u64 = 1_000;
const DEFER_MAX_WAITS: u32 = 5_000;

#[derive(Clone, Copy)]
struct Defer {
    used: bool,
    taken: bool,
    conn: u32,
    path: [u8; 160],
    /* The parked coroutine's task. A WAITING task only runs again when someone
     * posts it back to the ready ring, so the reply must do exactly that. */
    waiter: *mut pm_metal_async_task_t,
}

static DEFERS: Mut<[Defer; MAX_DEFER]> = Mut(UnsafeCell::new([Defer {
    used: false,
    taken: false,
    conn: 0,
    path: [0; 160],
    waiter: ptr::null_mut(),
}; MAX_DEFER]));
/* Slot handed out by the last defer_next, or -1 when the renderer is idle. */
static DEFER_CUR: Mut<i32> = Mut(UnsafeCell::new(-1));
static DEFER_LOCK: Mut<pm_util_lock_t> = Mut(UnsafeCell::new(pm_util_lock_t { locked: 0 }));

unsafe fn defers() -> &'static mut [Defer; MAX_DEFER] {
    unsafe { &mut *DEFERS.0.get() }
}

unsafe fn defer_lock() -> *mut pm_util_lock_t {
    DEFER_LOCK.0.get()
}

/// Self-registration record: httpd default on ANY :8090, driven through the
/// asgi multi-instance listen/count/status/stop exports.
static HTTPD_SVC: Mut<pm_metal_service_t> = Mut(UnsafeCell::new(pm_metal_service_t {
    name: b"httpd\0".as_ptr(),
    fqn: b"pymergetic.metal.net.http.asgi\0".as_ptr(),
    default_addr: 0,
    default_port: 8090,
    listen: pm_metal_net_http_asgi_listen,
    count: pm_metal_net_http_asgi_count,
    status: pm_metal_net_http_asgi_status,
    stop: pm_metal_net_http_asgi_stop,
}));

/* Live route entries, as a slice. Length is the allocated capacity; entries
 * past ROUTES_N are zeroed at grow time. Safe to call before any route is
 * registered (base is null → an empty slice). */
unsafe fn routes() -> &'static mut [Route] {
    let n = unsafe { *ROUTES_N.0.get() };
    if n == 0 {
        // No routes registered yet: never build a slice from a null base.
        return &mut [];
    }
    let base = unsafe { *ROUTES_PTR.0.get() };
    unsafe { core::slice::from_raw_parts_mut(base, n) }
}

/// Reset an arena-allocated Route slot so it is safe to reuse or to report as
/// free.
unsafe fn route_reset_storage(r: *mut Route) {
    unsafe {
        (*r).used = false;
        (*r).method = [0; 8];
        (*r).path = [0; 80];
        (*r).body = [0; 256];
        (*r).body_len = 0;
        (*r).handler = None;
        (*r).ext = ptr::null();
        (*r).ctype = ptr::null();
        (*r).stream_ctx = ptr::null_mut();
        (*r).stream_size = None;
        (*r).stream_prod = None;
        (*r).defer = false;
    }
}

/* Route capacity grows on demand from the arena (doubling), so there is no
 * fixed MAX_ROUTE to overflow. Requires the arena (set by init). Returns the
 * current capacity after the (possibly empty) grow. */
unsafe fn routes_grow(need: usize) -> usize {
    let arena = unsafe { *ARENA.0.get() };
    if arena.is_null() {
        return 0;
    }
    let old = unsafe { *ROUTES_PTR.0.get() };
    let old_n = unsafe { *ROUTES_N.0.get() };
    if old_n >= need {
        return old_n;
    }
    let mut new_n = old_n.max(1);
    while new_n < need {
        new_n = new_n.saturating_mul(2);
    }
    let bytes = core::mem::size_of::<Route>().saturating_mul(new_n);
    if bytes == 0 {
        return 0;
    }
    /* pm_util_mem_alloc returns *mut u8; cast to *mut Route for element math. */
    let new = unsafe { pm_util_mem_alloc(arena, bytes).cast::<Route>() };
    if new.is_null() {
        return 0;
    }
    // Zero the whole new block so every slot is a clean "unused" Route before
    // we overwrite the live prefix; avoids leaking the old entries' fields
    // into reuse and keeps append-initialization uniform.
    for i in 0..new_n {
        unsafe { route_reset_storage(new.add(i)) };
    }
    if !old.is_null() {
        unsafe { ptr::copy_nonoverlapping(old, new, old_n) };
        unsafe { pm_util_mem_free(arena, old.cast::<u8>()) };
    }
    unsafe {
        *ROUTES_PTR.0.get() = new;
        *ROUTES_N.0.get() = new_n;
    }
    new_n
}

/// Find a free route slot, growing the table when none is free. Returns None
/// only when the arena is unavailable/OOM — never because a fixed cap was hit.
unsafe fn routes_next_slot() -> Option<usize> {
    let n = unsafe { *ROUTES_N.0.get() };
    let base = unsafe { *ROUTES_PTR.0.get() };
    unsafe {
        if !base.is_null() {
            for i in 0..n {
                if !(*base.add(i)).used {
                    return Some(i);
                }
            }
        }
        if routes_grow(n + 1) > n {
            // The freshly grown slot n is guaranteed unused (route_reset_storage).
            Some(n)
        } else {
            None
        }
    }
}

unsafe fn conns() -> &'static mut [Conn; MAX_CONN] {
    unsafe { &mut *CONNS.0.get() }
}

fn cstr_copy(dst: &mut [u8], src: *const u8) -> bool {
    if src.is_null() {
        return false;
    }
    dst.fill(0);
    let mut i = 0usize;
    unsafe {
        while i + 1 < dst.len() {
            let b = *src.add(i);
            if b == 0 {
                break;
            }
            dst[i] = b;
            i += 1;
        }
    }
    true
}

fn cstr_eq(stored: &[u8], got: &[u8]) -> bool {
    let mut n = 0usize;
    while n < stored.len() && stored[n] != 0 {
        n += 1;
    }
    n == got.len() && stored[..n] == got[..]
}

/// Route match. A stored route ending in `*` is a prefix wildcard (no
/// trailing-slash requirement): the handler still receives the full request
/// path, so the C face can parse `/inspect/reg/<module>/<func>` segments and
/// page/query params itself. Everything else is an exact match.
fn path_matches(stored: &[u8], got: &[u8]) -> bool {
    let mut n = 0usize;
    while n < stored.len() && stored[n] != 0 {
        n += 1;
    }
    let prefix = &stored[..n];
    if prefix.len() >= 2 && prefix[prefix.len() - 1] == b'*' && prefix[prefix.len() - 2] == b'/' {
        let base = &prefix[..prefix.len() - 1];
        got.len() >= base.len() && got[..base.len()] == base[..]
    } else {
        n == got.len() && prefix == got
    }
}

fn find_headers_end(buf: &[u8]) -> Option<usize> {
    buf.windows(4).position(|w| w == b"\r\n\r\n").map(|i| i + 4)
}

fn parse_req<'a>(buf: &'a [u8]) -> Option<(&'a [u8], &'a [u8])> {
    let line_end = buf.windows(2).position(|w| w == b"\r\n")?;
    let line = &buf[..line_end];
    let mut it = line.split(|b| *b == b' ');
    let method = it.next()?;
    let path = it.next()?;
    Some((method, path))
}

fn path_only(path: &[u8]) -> &[u8] {
    match path.iter().position(|&b| b == b'?') {
        Some(i) => &path[..i],
        None => path,
    }
}

/// Clear a slot's optional extras so a reused route never inherits the previous
/// registration's stream callbacks or declared Content-Type.
fn route_reset_opts(r: &mut Route) {
    r.stream_ctx = ptr::null_mut();
    r.stream_size = None;
    r.stream_prod = None;
    r.ctype = ptr::null();
    r.defer = false;
}

fn copy_cstr(dst: &mut [u8], src: &[u8]) {
    dst.fill(0);
    let n = src.len().min(dst.len().saturating_sub(1));
    dst[..n].copy_from_slice(&src[..n]);
}

/// Resolve a request to a body. Returns true when the match is a deferred
/// route, meaning no body was produced and the caller must park the connection.
fn lookup_into(c: &mut Conn, method: &[u8], path: &[u8]) -> bool {
    let match_path = path_only(path);
    c.ctype = ptr::null();
    unsafe {
        for r in routes().iter() {
            if !(r.used && cstr_eq(&r.method, method) && path_matches(&r.path, match_path)) {
                continue;
            }
            c.ctype = r.ctype;
            if r.defer {
                /* Body belongs to a renderer on another thread: hand the path
                 * over and let the caller park until defer_reply lands. */
                return true;
            }
            if let Some(h) = r.handler {
                let mut mbuf = [0u8; 8];
                let mut pbuf = [0u8; 160];
                copy_cstr(&mut mbuf, method);
                copy_cstr(&mut pbuf, path);
                let arena = *ARENA.0.get();
                if arena.is_null() {
                    return false;
                }
                let dst = pm_util_mem_alloc(arena, BODY_MAX);
                if dst.is_null() {
                    return false;
                }
                let mut n = 0u32;
                let rc = h(
                    mbuf.as_ptr(),
                    pbuf.as_ptr(),
                    dst,
                    BODY_MAX as u32,
                    &mut n,
                );
                if rc == 0 && n as usize <= BODY_MAX {
                    c.body_buf = dst;
                    c.body = dst;
                    c.body_len = n;
                    return false;
                }
                pm_util_mem_free(arena, dst);
                return false;
            } else if let Some(prod) = r.stream_prod {
                /* Streaming route: Content-Length comes from the size callback
                 * (clamped to STREAM_MAX so a bad producer can't advertise an
                 * absurd length); the body bytes are pulled from the producer
                 * one chunk at a time during step 2. */
                let total = match r.stream_size {
                    Some(sz) => sz(r.stream_ctx).min(STREAM_MAX as u64),
                    None => STREAM_MAX as u64,
                };
                c.stream_ctx = r.stream_ctx;
                c.stream_size = r.stream_size;
                c.stream_prod = Some(prod);
                c.stream_sent = 0;
                c.stream_chunk_len = 0;
                c.body = c.stream_chunk.as_mut_ptr();
                c.body_len = total as u32;
                return false;
            } else if !r.ext.is_null() {
                c.body = r.ext;
                c.body_len = r.body_len;
                return false;
            } else {
                c.body = r.body.as_ptr();
                c.body_len = r.body_len;
                return false;
            }
        }
    }
    c.ctype = ptr::null();
    c.body = DEFAULT_BODY.as_ptr();
    c.body_len = DEFAULT_BODY.len() as u32;
    false
}

fn conn_slot() -> Option<usize> {
    unsafe { conns().iter().position(|c| !c.used) }
}

/// Free a connection's heap body buffer and close its fd. Call on every
/// teardown path so a dynamic/deferred handler's arena allocation is always
/// returned; the static Conn (rx/hdr/stream_chunk) stays for reuse.
unsafe fn release_conn(c: &mut Conn) {
    unsafe {
        if c.fd >= 0 {
            pm_metal_net_ip_close(c.fd);
            c.fd = -1;
        }
        let arena = *ARENA.0.get();
        if !c.body_buf.is_null() && !arena.is_null() {
            pm_util_mem_free(arena, c.body_buf);
            c.body_buf = ptr::null_mut();
        }
    }
}


/// Queue a parked connection's path for the renderer. False when the queue is
/// full, in which case the caller must answer instead of parking.
fn defer_enqueue(conn: u32, path: &[u8]) -> bool {
    unsafe {
        pm_util_lock_acquire(defer_lock());
        let out = match defers().iter().position(|d| !d.used) {
            Some(i) => {
                let d = &mut defers()[i];
                d.used = true;
                d.taken = false;
                d.conn = conn;
                d.waiter = pm_metal_async_current_task();
                d.path.fill(0);
                let n = path.len().min(d.path.len() - 1);
                d.path[..n].copy_from_slice(&path[..n]);
                true
            }
            None => false,
        };
        pm_util_lock_release(defer_lock());
        out
    }
}

/// Forget a queued request (the connection gave up waiting).
fn defer_drop(conn: u32) {
    unsafe {
        pm_util_lock_acquire(defer_lock());
        for (i, d) in defers().iter_mut().enumerate() {
            if d.used && d.conn == conn {
                d.used = false;
                d.taken = false;
                d.waiter = ptr::null_mut();
                if *DEFER_CUR.0.get() == i as i32 {
                    *DEFER_CUR.0.get() = -1;
                }
            }
        }
        pm_util_lock_release(defer_lock());
    }
}

/// Take the next queued request, remembering it as current. Returns its path, or
/// a null pointer when nothing is pending. The returned bytes stay valid until
/// the next `defer_next` call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_metal_net_http_asgi_defer_next() -> *const u8 {
    unsafe {
        pm_util_lock_acquire(defer_lock());
        let mut out = ptr::null();
        if let Some(i) = defers().iter().position(|d| d.used && !d.taken) {
            defers()[i].taken = true;
            *DEFER_CUR.0.get() = i as i32;
            out = defers()[i].path.as_ptr();
        }
        pm_util_lock_release(defer_lock());
        out
    }
}

/// Answer the request handed out by the last `defer_next` and wake its
/// connection. Returns -1 when no request is current or the body does not fit.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_metal_net_http_asgi_defer_reply(body: *const u8, len: u32) -> i32 {
    unsafe { pm_metal_net_http_asgi_defer_reply_ct(body, len, ptr::null()) }
}

/// Answer like `defer_reply`, but the caller may override the route's declared
/// Content-Type per response. A deferred route registers one type for all of
/// its bodies (the pump does not know the type when it claims the slot), so raw
/// replies — a card's C/Rust source, an octet-stream section slice — need the
/// per-request escape hatch. Pass a null `ctype` to keep the route's declared
/// type. `ctype`, when non-null, must be a static NUL-terminated string that
/// outlives the reply (the header is copied while the connection is current).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_metal_net_http_asgi_defer_reply_ct(
    body: *const u8,
    len: u32,
    ctype: *const u8,
) -> i32 {
    unsafe {
        pm_util_lock_acquire(defer_lock());
        let cur = *DEFER_CUR.0.get();
        let mut rc = -1;
        if cur >= 0 && (cur as usize) < MAX_DEFER {
            let d = defers()[cur as usize];
            let fits = (len as usize) <= BODY_MAX && (len == 0 || !body.is_null());
            if d.used && fits && (d.conn as usize) < MAX_CONN {
                let c = &mut conns()[d.conn as usize];
                if len != 0 {
                    let arena = *ARENA.0.get();
                    if arena.is_null() {
                        return -1;
                    }
                    let dst = pm_util_mem_alloc(arena, BODY_MAX);
                    if dst.is_null() {
                        return -1;
                    }
                    ptr::copy_nonoverlapping(body, dst, len as usize);
                    c.body_buf = dst;
                    c.body = dst;
                }
                c.body_len = len;
                /* Override the route's declared type when the reply says so
                 * (the raw-source / octet-stream fallback). */
                if !ctype.is_null() {
                    c.ctype = ctype;
                }
                /* Set last: the parked coroutine reads this to move on. */
                c.defer_ready = true;
                let waiter = d.waiter;
                defers()[cur as usize].used = false;
                defers()[cur as usize].taken = false;
                defers()[cur as usize].waiter = ptr::null_mut();
                *DEFER_CUR.0.get() = -1;
                if !waiter.is_null() {
                    pm_metal_async_post_task(waiter);
                }
                rc = 0;
            }
        }
        pm_util_lock_release(defer_lock());
        rc
    }
}

/// Register a page whose body an external renderer produces (`ctype` required —
/// a deferred path has no extension to derive one from). GET only: this is for
/// server-rendered pages, and the renderer runs wherever the templates live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_metal_net_http_asgi_route_defer(
    path: *const u8,
    ctype: *const u8,
) -> i32 {
    if path.is_null() || ctype.is_null() {
        return -1;
    }
    unsafe {
        let Some(slot) = routes_next_slot() else {
            return -1;
        };
        let r = &mut routes()[slot];
        if !cstr_copy(&mut r.method, b"GET\0".as_ptr()) || !cstr_copy(&mut r.path, path) {
            return -1;
        }
        r.body.fill(0);
        r.body_len = 0;
        r.handler = None;
        r.ext = ptr::null();
        route_reset_opts(r);
        r.ctype = ctype;
        r.defer = true;
        r.used = true;
    }
    0
}

fn ctype_of(path: &[u8]) -> &'static [u8] {
    let mut end = path.len();
    while end > 0 && path[end - 1] == 0 {
        end -= 1;
    }
    let p = &path[..end];
    // Landing/index routes without a file extension (e.g. `/`) are HTML.
    if p == b"/" {
        return b"text/html; charset=utf-8";
    }
    let dot = p.iter().rposition(|&b| b == b'.');
    match dot.map(|i| &p[i..]) {
        Some(b".html") | Some(b".htm") => b"text/html; charset=utf-8",
        Some(b".css") => b"text/css; charset=utf-8",
        Some(b".js") => b"application/javascript",
        Some(b".json") => b"application/json",
        Some(b".svg") => b"image/svg+xml",
        Some(b".png") => b"image/png",
        _ => b"application/octet-stream",
    }
}

fn build_hdr(c: &mut Conn, path: &[u8]) {
    c.hdr.fill(0);
    let mut n = 0usize;
    let status = b"HTTP/1.0 200 OK\r\nContent-Type: ";
    c.hdr[n..n + status.len()].copy_from_slice(status);
    n += status.len();
    // A route may declare its own Content-Type (JSON APIs, whose paths carry no
    // usable extension); otherwise fall back to the path's file extension.
    if !c.ctype.is_null() {
        let mut i = 0usize;
        unsafe {
            while i + 1 < HDR_MAX - n {
                let b = *c.ctype.add(i);
                if b == 0 {
                    break;
                }
                c.hdr[n + i] = b;
                i += 1;
            }
        }
        n += i;
    } else {
        let ct = ctype_of(path);
        c.hdr[n..n + ct.len()].copy_from_slice(ct);
        n += ct.len();
    }
    let prefix = b"\r\nContent-Length: ";
    c.hdr[n..n + prefix.len()].copy_from_slice(prefix);
    n += prefix.len();
    n += put_u64(&mut c.hdr[n..], c.body_len as u64);
    let tail = b"\r\n\r\n";
    c.hdr[n..n + tail.len()].copy_from_slice(tail);
    n += tail.len();
    c.hdr_len = n as u32;
}

fn put_u64(dst: &mut [u8], mut v: u64) -> usize {
    let mut tmp = [0u8; 20];
    let mut i = 0usize;
    loop {
        tmp[i] = b'0' + (v % 10) as u8;
        v /= 10;
        i += 1;
        if v == 0 {
            break;
        }
    }
    let mut o = 0usize;
    while i > 0 {
        i -= 1;
        dst[o] = tmp[i];
        o += 1;
    }
    o
}

/// Must match `struct pm_metal_async_coro` in async/__types__.h.
#[repr(C)]
struct CoroHead {
    step: *mut c_void,
    awaiting: *mut c_void,
    waiter: *mut c_void,
    task: *mut c_void,
    status: u32,
}

/// Conn frame: C coro header then our slot index.
#[repr(C)]
struct ConnFrame {
    _coro: CoroHead,
    slot: u32,
}

    unsafe extern "C" fn step_conn_frame(self_: *mut pm_metal_async_coro_t) -> i32 {
    let f = self_ as *mut ConnFrame;    let slot = unsafe { (*f).slot as usize };
    if slot >= MAX_CONN {
        return ERROR;
    }
    let c = unsafe { &mut conns()[slot] };
    if c.step == 0 {
        let room = RX_MAX as u32 - c.rx_len;
        if room == 0 {
            unsafe { release_conn(c) };
            c.used = false;
            return ERROR;
        }
        let n = unsafe { pm_metal_net_ip_recv(c.fd, c.rx.as_mut_ptr().add(c.rx_len as usize), room) };
        if n == 0 {
            return WAITING;
        }
        if n < 0 {
            unsafe { release_conn(c) };
            c.used = false;
            return ERROR;
        }
        c.rx_len += n as u32;
        let end = match find_headers_end(&c.rx[..c.rx_len as usize]) {
            Some(e) => e,
            None => return WAITING,
        };
        let (method, path) = match parse_req(&c.rx[..end]) {
            Some(v) => v,
            None => {
                unsafe { release_conn(c) };
                c.used = false;
                return ERROR;
            }
        };
        let mut mbuf = [0u8; 8];
        let mut pbuf = [0u8; 160];
        let mlen = method.len().min(7);
        let plen = path.len().min(159);
        copy_cstr(&mut mbuf, method);
        copy_cstr(&mut pbuf, path);
        if lookup_into(c, &mbuf[..mlen], &pbuf[..plen]) {
            /* Deferred route: park until the renderer answers. The header waits
             * too — Content-Length is only known once the body lands. */
            c.defer_waits = 0;
            c.defer_ready = false;
            if !defer_enqueue(slot as u32, &pbuf[..plen]) {
                /* Queue full: answer rather than park forever. */
                c.ctype = ptr::null();
                c.body = DEFER_BUSY_BODY.as_ptr();
                c.body_len = DEFER_BUSY_BODY.len() as u32;
                build_hdr(c, &pbuf[..plen]);
                c.snd_off = 0;
                c.step = 1;
            } else {
                c.step = 3;
                return unsafe { pm_metal_async_sleep_us(self_, DEFER_SLICE_US) };
            }
        } else {
            build_hdr(c, &pbuf[..plen]);
            c.snd_off = 0;
            c.step = 1;
        }
    }
    if c.step == 3 {
        if !c.defer_ready {
            c.defer_waits += 1;
            if c.defer_waits <= DEFER_MAX_WAITS {
                return unsafe { pm_metal_async_sleep_us(self_, DEFER_SLICE_US) };
            }
            /* Nobody is draining the queue (no render pump running). Drop the
             * request from the queue and say so instead of hanging the client. */
            defer_drop(slot as u32);
            c.ctype = ptr::null();
            c.body = DEFER_BUSY_BODY.as_ptr();
            c.body_len = DEFER_BUSY_BODY.len() as u32;
        }
        build_hdr(c, b"");
        c.snd_off = 0;
        c.step = 1;
    }
    if c.step == 1 {
        let off = c.snd_off as usize;
        let left = c.hdr_len as usize - off;
        let n = unsafe { pm_metal_net_ip_send(c.fd, c.hdr.as_ptr().add(off), left as u32) };
        if n == 0 {
            return WAITING;
        }
        if n < 0 {
            unsafe { release_conn(c) };
            c.used = false;
            return ERROR;
        }
        c.snd_off += n as u32;
        if c.snd_off < c.hdr_len {
            return WAITING;
        }
        c.snd_off = 0;
        c.step = 2;
    }
    if c.step == 2 {
        if let Some(prod) = c.stream_prod {
            /* Streaming body: pull chunks from the producer, sending each out as
             * it's produced. Content-Length was fixed at lookup from the size
             * callback, so the client sees a normal response; internally we never
             * buffer the whole body. The loop keeps pulling chunks until the
             * window parks us (partial send), the producer runs dry, or the body
             * is fully sent — only then may the connection close. */
            loop {
                if c.stream_sent >= c.body_len as u64 {
                    c.step = 3;
                    break;
                }
                if c.snd_off >= c.stream_chunk_len {
                    let mut want = c.body_len as u64 - c.stream_sent;
                    if want as usize > STREAM_CHUNK {
                        want = STREAM_CHUNK as u64;
                    }
                    let mut more = 1i32;
                    let mut len = want as u32;
                    let rc = unsafe {
                        prod(
                            c.stream_ctx,
                            c.stream_chunk.as_mut_ptr(),
                            &mut len,
                            STREAM_CHUNK as u32,
                            &mut more,
                        )
                    };
                    if rc != 0 {
                        unsafe { release_conn(c) };
                        c.used = false;
                        return ERROR;
                    }
                    c.stream_chunk_len = len;
                    c.snd_off = 0;
                    if len == 0 {
                        /* Producer finished early; treat the declared body as sent. */
                        c.stream_sent = c.body_len as u64;
                        c.step = 3;
                        break;
                    }
                }
                let off = c.snd_off as usize;
                let left = (c.stream_chunk_len as usize - off) as u32;
                let n = unsafe { pm_metal_net_ip_send(c.fd, c.stream_chunk.as_ptr().add(off), left) };
                if n == 0 {
                    return WAITING;
                }
                if n < 0 {
                    unsafe { release_conn(c) };
                    c.used = false;
                    return ERROR;
                }
                c.snd_off += n as u32;
                c.stream_sent += n as u64;
                if c.snd_off < c.stream_chunk_len {
                    /* Window parked the send mid-chunk; resume when an ACK frees space. */
                    return WAITING;
                }
            }
        } else {
            let off = c.snd_off as usize;
            let left = c.body_len as usize - off;
            if left == 0 {
                c.step = 3;
            } else {
                let n = unsafe { pm_metal_net_ip_send(c.fd, c.body.add(off), left as u32) };
                if n == 0 {
                    return WAITING;
                }
                if n < 0 {
                    unsafe { release_conn(c) };
                    c.used = false;
                    return ERROR;
                }
                c.snd_off += n as u32;
                if c.snd_off < c.body_len {
                    return WAITING;
                }
                c.step = 3;
            }
        }
    }
    unsafe { release_conn(c) };
    c.used = false;
    DONE
}

fn spawn_conn(fd: i32) -> i32 {
    let Some(slot) = conn_slot() else {
        unsafe { pm_metal_net_ip_close(fd) };
        return -1;
    };
    unsafe {
        let c = &mut conns()[slot];
        *c = Conn {
            used: true,
            step: 0,
            fd,
            rx: [0; RX_MAX],
            rx_len: 0,
            snd_off: 0,
            hdr_len: 0,
            hdr: [0; HDR_MAX],
            body_buf: ptr::null_mut(),
            body: ptr::null(),
            body_len: 0,
            ctype: ptr::null(),
            stream_ctx: ptr::null_mut(),
            stream_size: None,
            stream_prod: None,
            stream_sent: 0,
            stream_chunk: [0; STREAM_CHUNK],
            stream_chunk_len: 0,
            defer_waits: 0,
            defer_ready: false,
        };
        let coro = pm_metal_async_coro_create(step_conn_frame, core::mem::size_of::<ConnFrame>());
        if coro.is_null() {
            c.used = false;
            pm_metal_net_ip_close(fd);
            return -1;
        }
        (*(coro as *mut ConnFrame)).slot = slot as u32;
        if pm_metal_async_create_task(coro).is_null() {
            c.used = false;
            pm_metal_net_ip_close(fd);
            return -1;
        }
    }
    0
}

#[repr(C)]
struct ListenFrame {
    _coro: CoroHead,
    slot: u32,
}

unsafe fn listen_at(slot: usize) -> i32 {
    unsafe { *listen_fds().add(slot) }
}
unsafe fn listen_at_set(slot: usize, v: i32) {
    unsafe { *listen_fds().add(slot) = v; }
}
unsafe fn listen_addrs() -> *mut u32 {
    LISTEN_ADDRS.0.get() as *mut u32
}
unsafe fn listen_ports() -> *mut u16 {
    LISTEN_PORTS.0.get() as *mut u16
}
unsafe fn listen_addr_set(slot: usize, addr: u32, port: u16) {
    unsafe {
        *listen_addrs().add(slot) = addr;
        *listen_ports().add(slot) = port;
    }
}
unsafe fn listen_same(slot: usize, addr: u32, port: u16) -> bool {
    unsafe {
        listen_at(slot) >= 0 && *listen_addrs().add(slot) == addr && *listen_ports().add(slot) == port
    }
}

unsafe extern "C" fn step_listen(self_: *mut pm_metal_async_coro_t) -> i32 {
    let f = self_ as *mut ListenFrame;
    let slot = unsafe { (*f).slot as usize };
    if slot >= MAX_ASGI {
        return ERROR;
    }
    let fd = unsafe { listen_at(slot) };
    if fd < 0 {
        return ERROR;
    }
    loop {
        let a = unsafe { pm_metal_net_ip_accept(fd) };
        if a == ACCEPT_WAIT {
            return WAITING;
        }
        if a < 0 {
            return ERROR;
        }
        if spawn_conn(a) != 0 {
            return ERROR;
        }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_metal_net_http_asgi_init(arena: *mut pm_util_mem_arena_t) -> i32 {
    if arena.is_null() {
        return -1;
    }
    unsafe {
        *ARENA.0.get() = arena;
        for slot in 0..MAX_ASGI {
            listen_at_set(slot, -1);
            listen_addr_set(slot, 0, 0);
        }
        for r in routes().iter_mut() {
            r.used = false;
        }
        for c in conns().iter_mut() {
            c.used = false;
        }
        // Self-register the httpd service so m.serve()/m.services() see it.
        pm_metal_services_register(HTTPD_SVC.0.get() as *const pm_metal_service_t);
    }
    0
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_metal_net_http_asgi_deinit() {
    unsafe {
        for slot in 0..MAX_ASGI {
            let fd = listen_at(slot);
            if fd >= 0 {
                pm_metal_net_ip_close(fd);
            }
            listen_at_set(slot, -1);
        }
        for c in conns().iter_mut() {
            if c.used {
                release_conn(c);
            }
            c.used = false;
        }
        // Free the dynamic route table so a re-init starts clean and cannot
        // double-register over stale entries.
        let arena = *ARENA.0.get();
        let rp = *ROUTES_PTR.0.get();
        if !arena.is_null() && !rp.is_null() {
            pm_util_mem_free(arena, rp.cast::<u8>());
        }
        *ROUTES_PTR.0.get() = ptr::null_mut();
        *ROUTES_N.0.get() = 0;
        *ARENA.0.get() = ptr::null_mut();
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_metal_net_http_asgi_route(
    method: *const u8,
    path: *const u8,
    body: *const u8,
    body_len: u32,
) -> i32 {
    if method.is_null() || path.is_null() {
        return -1;
    }
    if body_len as usize > 256 {
        return -1;
    }
    unsafe {
        let Some(slot) = routes_next_slot() else {
            return -1;
        };
        let r = &mut routes()[slot];
        if !cstr_copy(&mut r.method, method) || !cstr_copy(&mut r.path, path) {
            return -1;
        }
        r.body.fill(0);
        if body_len != 0 && !body.is_null() {
            ptr::copy_nonoverlapping(body, r.body.as_mut_ptr(), body_len as usize);
        }
        r.body_len = body_len;
        r.handler = None;
        r.ext = ptr::null();
        route_reset_opts(r);
        r.used = true;
    }
    0
}

/// Claim a slot for a handler route. `ctype` may be null (derive the response
/// type from the path extension). Returns the slot, or None only on invalid args
/// or arena OOM — the table grows on demand, so there is no fixed upper bound.
unsafe fn route_fn_claim(
    method: *const u8,
    path: *const u8,
    handler: Option<Handler>,
    ctype: *const u8,
) -> Option<usize> {
    if method.is_null() || path.is_null() || handler.is_none() {
        return None;
    }
    unsafe {
        let slot = routes_next_slot()?;
        let r = &mut routes()[slot];
        if !cstr_copy(&mut r.method, method) || !cstr_copy(&mut r.path, path) {
            return None;
        }
        r.body.fill(0);
        r.body_len = 0;
        r.handler = handler;
        r.ext = ptr::null();
        route_reset_opts(r);
        r.ctype = ctype;
        r.used = true;
        Some(slot)
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_metal_net_http_asgi_route_fn(
    method: *const u8,
    path: *const u8,
    handler: Option<Handler>,
) -> i32 {
    match unsafe { route_fn_claim(method, path, handler, ptr::null()) } {
        Some(_) => 0,
        None => -1,
    }
}

/// Same as `route_fn`, plus the Content-Type the handler emits. Required for API
/// routes whose path has no usable extension (`/health`) or whose trailing
/// segment is a dotted module name (`/inspect/reg/pymergetic.metal.net.ip`):
/// without it the response falls back to octet-stream and a browser downloads
/// the body instead of showing it. `ctype` must outlive the route.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_metal_net_http_asgi_route_fn_ct(
    method: *const u8,
    path: *const u8,
    handler: Option<Handler>,
    ctype: *const u8,
) -> i32 {
    if ctype.is_null() {
        return -1;
    }
    match unsafe { route_fn_claim(method, path, handler, ctype) } {
        Some(_) => 0,
        None => -1,
    }
}

/// Claim a slot serving caller-owned bytes. `ctype` may be null (derive the
/// response type from the path extension).
unsafe fn route_static_claim(
    url: *const u8,
    body: *const u8,
    body_len: u32,
    ctype: *const u8,
) -> Option<usize> {
    if url.is_null() || (body_len != 0 && body.is_null()) {
        return None;
    }
    unsafe {
        let slot = routes_next_slot()?;
        let r = &mut routes()[slot];
        if !cstr_copy(&mut r.method, b"GET\0".as_ptr()) || !cstr_copy(&mut r.path, url) {
            return None;
        }
        r.body.fill(0);
        r.body_len = body_len;
        r.handler = None;
        r.ext = body;
        route_reset_opts(r);
        r.ctype = ctype;
        r.used = true;
        Some(slot)
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_metal_net_http_asgi_route_static(
    url: *const u8,
    body: *const u8,
    body_len: u32,
) -> i32 {
    match unsafe { route_static_claim(url, body, body_len, ptr::null()) } {
        Some(_) => 0,
        None => -1,
    }
}

/// Same as `route_static`, plus the Content-Type. Needed for URLs with no file
/// extension — a directory index mounted at `/inspect` would otherwise be typed
/// octet-stream and download instead of render. `ctype` must outlive the route.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_metal_net_http_asgi_route_static_ct(
    url: *const u8,
    body: *const u8,
    body_len: u32,
    ctype: *const u8,
) -> i32 {
    if ctype.is_null() {
        return -1;
    }
    match unsafe { route_static_claim(url, body, body_len, ctype) } {
        Some(_) => 0,
        None => -1,
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_metal_net_http_asgi_route_static_copy(
    url: *const u8,
    body: *const u8,
    body_len: u32,
) -> i32 {
    /* Runtime-render wiring: copy the given bytes into a stable arena allocation
     * (survives the caller; the seat renders live templates into a buffer, then
     * hands the bytes off here) and serve them as a GET static route. Empty is
     * not mountable. Nothing is retained between calls. */
    if url.is_null() || body.is_null() || body_len == 0 {
        return -1;
    }
    unsafe {
        let arena = *ARENA.0.get();
        if arena.is_null() {
            return -1;
        }
        let Some(slot) = routes_next_slot() else {
            return -1;
        };
        let n = body_len as usize;
        let dst = pm_util_mem_alloc(arena, n);
        if dst.is_null() {
            return -1;
        }
        ptr::copy_nonoverlapping(body, dst, n);
        let r = &mut routes()[slot];
        if !cstr_copy(&mut r.method, b"GET\0".as_ptr()) || !cstr_copy(&mut r.path, url) {
            return -1;
        }
        r.body.fill(0);
        r.body_len = body_len;
        r.handler = None;
        r.ext = dst;
        route_reset_opts(r);
        r.used = true;
    }
    0
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_metal_net_http_asgi_route_stream_fn(
    method: *const u8,
    path: *const u8,
    ctx: *mut c_void,
    size: Option<StreamSize>,
    producer: Option<StreamProducer>,
) -> i32 {
    if method.is_null() || path.is_null() || producer.is_none() || ctx.is_null() {
        return -1;
    }
    unsafe {
        let total = match size {
            Some(sz) => sz(ctx).min(STREAM_MAX as u64),
            None => STREAM_MAX as u64,
        };
        if total == 0 {
            return -1;
        }
        let Some(slot) = routes_next_slot() else {
            return -1;
        };
        let r = &mut routes()[slot];
        if !cstr_copy(&mut r.method, method) || !cstr_copy(&mut r.path, path) {
            return -1;
        }
        r.body.fill(0);
        r.body_len = 0;
        r.handler = None;
        r.ext = ptr::null();
        r.stream_ctx = ctx;
        r.stream_size = size;
        r.stream_prod = producer;
        r.used = true;
    }
    0
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_metal_net_http_asgi_listen(addr: u32, port: u16) -> i32 {
    unsafe {
        if (*ARENA.0.get()).is_null() {
            return -1;
        }
        for slot in 0..MAX_ASGI {
            // Idempotent: an existing instance on this exact addr:port is it.
            if listen_same(slot, addr, port) {
                return slot as i32;
            }
            if listen_at(slot) >= 0 {
                continue;
            }
            let fd = pm_metal_net_ip_socket(SOCK_STREAM);
            if fd < 0 || pm_metal_net_ip_bind(fd, addr, port) != 0
                || pm_metal_net_ip_listen(fd, 4) != 0
            {
                if fd >= 0 {
                    pm_metal_net_ip_close(fd);
                }
                return -1;
            }
            listen_at_set(slot, fd);
            listen_addr_set(slot, addr, port);
            let coro = pm_metal_async_coro_create(step_listen, core::mem::size_of::<ListenFrame>());
            if coro.is_null() || pm_metal_async_create_task(coro).is_null() {
                pm_metal_net_ip_close(fd);
                listen_at_set(slot, -1);
                return -1;
            }
            (*(coro as *mut ListenFrame)).slot = slot as u32;
            return slot as i32;
        }
    }
    -1
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_metal_net_http_asgi_count() -> u32 {
    unsafe {
        let mut n = 0u32;
        for slot in 0..MAX_ASGI {
            if listen_at(slot) >= 0 {
                n += 1;
            }
        }
        n
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_metal_net_http_asgi_status(id: i32) -> i32 {
    if id < 0 || id as usize >= MAX_ASGI {
        return -1;
    }
    unsafe { if listen_at(id as usize) >= 0 { 1 } else { 0 } }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_metal_net_http_asgi_stop(id: i32) -> i32 {
    if id < 0 || id as usize >= MAX_ASGI {
        return -1;
    }
    unsafe {
        if listen_at(id as usize) >= 0 {
            pm_metal_net_ip_close(listen_at(id as usize));
            listen_at_set(id as usize, -1);
            listen_addr_set(id as usize, 0, 0);
        }
    }
    0
}

pymergetic_wasmmod::PM_MOD_EXPORT_RS!(
    "pymergetic.metal.net.http.asgi",
    pm_metal_net_http_asgi_init,
    "int32_t(pm_util_mem_arena_t *)"
);
pymergetic_wasmmod::PM_MOD_EXPORT_RS!(
    "pymergetic.metal.net.http.asgi",
    pm_metal_net_http_asgi_deinit,
    "void(void)"
);
pymergetic_wasmmod::PM_MOD_EXPORT_RS!(
    "pymergetic.metal.net.http.asgi",
    pm_metal_net_http_asgi_route,
    "int32_t(const char *, const char *, const uint8_t *, uint32_t)"
);
pymergetic_wasmmod::PM_MOD_EXPORT_RS!(
    "pymergetic.metal.net.http.asgi",
    pm_metal_net_http_asgi_route_fn,
    "int32_t(const char *, const char *, pm_metal_net_http_asgi_handler_t)"
);
pymergetic_wasmmod::PM_MOD_EXPORT_RS!(
    "pymergetic.metal.net.http.asgi",
    pm_metal_net_http_asgi_route_fn_ct,
    "int32_t(const char *, const char *, pm_metal_net_http_asgi_handler_t, const char *)"
);
pymergetic_wasmmod::PM_MOD_EXPORT_RS!(
    "pymergetic.metal.net.http.asgi",
    pm_metal_net_http_asgi_route_static,
    "int32_t(const char *, const uint8_t *, uint32_t)"
);
pymergetic_wasmmod::PM_MOD_EXPORT_RS!(
    "pymergetic.metal.net.http.asgi",
    pm_metal_net_http_asgi_route_defer,
    "int32_t(const char *, const char *)"
);
pymergetic_wasmmod::PM_MOD_EXPORT_RS!(
    "pymergetic.metal.net.http.asgi",
    pm_metal_net_http_asgi_defer_next,
    "const char *(void)"
);
pymergetic_wasmmod::PM_MOD_EXPORT_RS!(
    "pymergetic.metal.net.http.asgi",
    pm_metal_net_http_asgi_defer_reply,
    "int32_t(const uint8_t *, uint32_t)"
);
pymergetic_wasmmod::PM_MOD_EXPORT_RS!(
    "pymergetic.metal.net.http.asgi",
    pm_metal_net_http_asgi_defer_reply_ct,
    "int32_t(const uint8_t *, uint32_t, const char *)"
);
pymergetic_wasmmod::PM_MOD_EXPORT_RS!(
    "pymergetic.metal.net.http.asgi",
    pm_metal_net_http_asgi_route_static_ct,
    "int32_t(const char *, const uint8_t *, uint32_t, const char *)"
);
pymergetic_wasmmod::PM_MOD_EXPORT_RS!(
    "pymergetic.metal.net.http.asgi",
    pm_metal_net_http_asgi_route_static_copy,
    "int32_t(const char *, const uint8_t *, uint32_t)"
);
pymergetic_wasmmod::PM_MOD_EXPORT_RS!(
    "pymergetic.metal.net.http.asgi",
    pm_metal_net_http_asgi_route_stream_fn,
    "int32_t(const char *, const char *, void *, pm_metal_net_http_asgi_stream_size_t, pm_metal_net_http_asgi_stream_producer_t)"
);
pymergetic_wasmmod::PM_MOD_EXPORT_RS!(
    "pymergetic.metal.net.http.asgi",
    pm_metal_net_http_asgi_listen,
    "int32_t(uint32_t, uint16_t)"
);
pymergetic_wasmmod::PM_MOD_EXPORT_RS!(
    "pymergetic.metal.net.http.asgi",
    pm_metal_net_http_asgi_count,
    "uint32_t(void)"
);
pymergetic_wasmmod::PM_MOD_EXPORT_RS!(
    "pymergetic.metal.net.http.asgi",
    pm_metal_net_http_asgi_status,
    "int32_t(int32_t)"
);
pymergetic_wasmmod::PM_MOD_EXPORT_RS!(
    "pymergetic.metal.net.http.asgi",
    pm_metal_net_http_asgi_stop,
    "int32_t(int32_t)"
);
pymergetic_wasmmod::PM_MOD_BOOT_RS!(
    "pymergetic.metal.net.http.asgi",
    pm_metal_net_http_asgi_init,
    pm_metal_net_http_asgi_deinit
);
pymergetic_wasmmod::PM_MOD_BOOTDEP_RS!(
    "pymergetic.metal.net.http.asgi",
    "pymergetic.metal.net.http"
);
