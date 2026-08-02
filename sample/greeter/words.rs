//! sample.greeter::words — pack export: `() -> i32`.

/// Length of "hello", in letters -- a small, deterministic value a
/// cross-package caller (`sample.announcer`) can assert against.
#[no_mangle]
pub extern "C" fn hello() -> i32 {
    5
}
