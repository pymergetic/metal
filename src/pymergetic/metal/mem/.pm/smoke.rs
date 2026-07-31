//! Host smoke — ``metal mod test mem`` / ``cargo run --bin smoke``.
use std::alloc::{alloc, dealloc, Layout};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::thread;

use pymergetic_metal_mem::lock::{Mutex, MutexCell, Spin};

static SPIN: Spin = Spin::new();
static SPIN_COUNT: AtomicUsize = AtomicUsize::new(0);

fn main() {
    // --- spin / mutex (uncontended) ---
    let s = Spin::new();
    assert!(s.try_lock());
    assert!(!s.try_lock());
    s.unlock();
    s.lock();
    s.unlock();

    let m = Mutex::new();
    assert!(m.try_lock());
    assert!(!m.try_lock());
    m.unlock();
    m.lock();
    m.unlock();

    let cell = MutexCell::new(7u32);
    {
        let mut g = cell.lock();
        assert_eq!(*g, 7);
        *g = 9;
    }
    assert_eq!(*cell.lock(), 9);

    // --- spin contention (multi-thread) ---
    SPIN_COUNT.store(0, Ordering::SeqCst);
    let mut handles = Vec::new();
    for _ in 0..4 {
        handles.push(thread::spawn(|| {
            for _ in 0..2500 {
                SPIN.lock();
                let v = SPIN_COUNT.load(Ordering::Relaxed);
                SPIN_COUNT.store(v + 1, Ordering::Relaxed);
                SPIN.unlock();
            }
        }));
    }
    for h in handles {
        h.join().unwrap();
    }
    assert_eq!(SPIN_COUNT.load(Ordering::SeqCst), 10_000);

    // Smaller arena so TLSF seed exhausts and grow_and_add_pool runs.
    // Host-only seed buffer (fake). On iron this range comes from
    // BIOS/UEFI/DT RAM — not from pymergetic_metal_mem::api::alloc (needs init first).
    const N: usize = 512 * 1024;
    let layout = Layout::from_size_align(N, 4096).unwrap();
    let base = unsafe { alloc(layout) };
    assert!(!base.is_null());

    unsafe {
        assert_eq!(pymergetic_metal_mem::api::init(base, N), 0);

        // --- malloc / free ---
        let a = pymergetic_metal_mem::api::alloc(64);
        let b = pymergetic_metal_mem::api::alloc(128);
        assert!(!a.is_null() && !b.is_null());
        pymergetic_metal_mem::api::free(b);
        pymergetic_metal_mem::api::free(a);

        // --- realloc (grow in place or move+copy) ---
        let r = pymergetic_metal_mem::api::alloc(32);
        assert!(!r.is_null());
        r.write_bytes(0xA5, 32);
        let r2 = pymergetic_metal_mem::api::realloc(r, 256);
        assert!(!r2.is_null());
        assert_eq!(*r2, 0xA5);
        let r3 = pymergetic_metal_mem::api::realloc(r2, 0);
        assert!(r3.is_null());

        // --- memalign ---
        let al = pymergetic_metal_mem::api::memalign(64, 100);
        assert!(!al.is_null());
        assert_eq!(al as usize % 64, 0);
        pymergetic_metal_mem::api::free(al);
        assert!(pymergetic_metal_mem::api::memalign(3, 16).is_null()); // not power of two

        // --- map LIFO ---
        let p0 = pymergetic_metal_mem::api::map(4096);
        let p1 = pymergetic_metal_mem::api::map(4096);
        assert!(!p0.is_null() && !p1.is_null());
        assert_eq!(p0 as usize % 4096, 0);
        assert_eq!(pymergetic_metal_mem::api::unmap(p0, 4096), -1); // not top
        assert_eq!(pymergetic_metal_mem::api::unmap(p1, 4096), 0);
        assert_eq!(pymergetic_metal_mem::api::unmap(p0, 4096), 0);

        // --- pressure / grow-on-OOM ---
        let mut n_ok = 0usize;
        let mut last = core::ptr::null_mut();
        for _ in 0..64 {
            let p = pymergetic_metal_mem::api::alloc(8 * 1024);
            if p.is_null() {
                break;
            }
            last = p;
            n_ok += 1;
        }
        assert!(n_ok >= 4, "expected several allocs before arena empty, got {n_ok}");
        // Still able to free last and reclaim
        if !last.is_null() {
            pymergetic_metal_mem::api::free(last);
            let again = pymergetic_metal_mem::api::alloc(8 * 1024);
            assert!(!again.is_null());
            pymergetic_metal_mem::api::free(again);
        }

        dealloc(base, layout);
    }
    println!("mem .pm/smoke.rs: PASS");
}
