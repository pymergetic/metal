//! pymergetic.metal — crate root and Rust namespace barrel (path == module).
//! C cards stay in metal.mk. RS cards are this crate graph so gen sees Live
//! `PM_MOD_EXPORT_RS!` ctors. Not a second HTTP stack.
//!
//! Metal depends on wasmmod, never the reverse. The allocator and panic
//! handler come from `pymergetic_wasmmod`'s `upy-host`, so this crate must
//! stay `no_std` or std would collide with them on `panic_impl`.
#![cfg_attr(not(any(test, feature = "gen")), no_std)]
#[path = "metal/dt.rs"]
pub mod dt;

#[path = "metal/bus.rs"]
pub mod bus;

#[path = "metal/drivers.rs"]
pub mod drivers;

#[path = "metal/fw.rs"]
pub mod fw;

#[path = "metal/net.rs"]
pub mod net;
