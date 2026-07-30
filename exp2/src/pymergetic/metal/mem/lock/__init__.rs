//! Package marker for mem.lock — C ABI is sibling stems `spin` / `mutex`
//! (no umbrella re-export). Rust `pub use` below is crate-internal wiring only.
//!
//! - [`spin`]: busy-wait exclusive (+ C `pm_metal_mem_lock_spin_*`)
//! - [`mutex`]: SMP waiting mutex (+ C `pm_metal_mem_lock_mutex_*`); [`MutexCell`] owns data

#![allow(dead_code, unused_imports)]

mod mutex;
mod spin;

pub use mutex::{Mutex, MutexCell, MutexGuard};
pub use spin::Spin;
