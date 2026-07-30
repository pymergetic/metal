//! Multi-CPU locks for the mem crate (no OS threads — CAS across CPUs).
//!
//! - [`spin`]: busy-wait exclusive (+ C `pm_metal_mem_lock_spin_*`)
//! - [`mutex`]: SMP waiting mutex (+ C `pm_metal_mem_lock_mutex_*`); [`MutexCell`] owns data

#![allow(dead_code, unused_imports)]

mod mutex;
mod spin;

pub use mutex::{Mutex, MutexCell, MutexGuard};
pub use spin::Spin;
