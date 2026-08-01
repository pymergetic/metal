//! GC is ripped out (Locked #5). No upy collector, no private GC heap.

/// Always false — there is no garbage collector.
pub fn enabled() -> bool {
    false
}

/// Collect is a no-op success (nothing to collect).
pub fn collect() -> i32 {
    0
}
