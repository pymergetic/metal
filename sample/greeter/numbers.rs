//! sample.greeter::numbers — pack export: `() -> i32`.

/// The lucky number -- a small, deterministic value a cross-package
/// caller (`sample.announcer`) can assert against.
#[no_mangle]
pub extern "C" fn lucky() -> i32 {
    7
}
