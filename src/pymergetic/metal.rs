//! pymergetic.metal — crate root and Rust namespace barrel (path == module).
//! C cards stay in metal.mk. RS cards are this crate graph so gen sees Live
//! `PM_MOD_EXPORT_RS!` ctors. Not a second HTTP stack.
//!
//! Every namespace/card with an umbrella `metal/<name>.h` has the matching
//! barrel `<name>.rs` here (or in its parent barrel), so the two faces stay
//! in lock-step — a C card's barrel is a face reexport (`#[path] mod export;
//! pub use export::*;`), never a second implementation (`one-defining-lang`).
//!
//! Metal depends on wasmmod, never the reverse. The allocator and panic
//! handler come from `pymergetic_wasmmod`'s `upy-host`, so this crate must
//! stay `no_std` or std would collide with them on `panic_impl`.
#![cfg_attr(not(any(test, feature = "gen")), no_std)]
#[path = "metal/async.rs"]
pub mod r#async;

#[path = "metal/boot.rs"]
pub mod boot;

#[path = "metal/bus.rs"]
pub mod bus;

#[path = "metal/console.rs"]
pub mod console;

#[path = "metal/display.rs"]
pub mod display;

#[path = "metal/drivers.rs"]
pub mod drivers;

#[path = "metal/dt.rs"]
pub mod dt;

#[path = "metal/fs.rs"]
pub mod fs;

#[path = "metal/fw.rs"]
pub mod fw;

#[path = "metal/input.rs"]
pub mod input;

#[path = "metal/inspect.rs"]
pub mod inspect;

#[path = "metal/net.rs"]
pub mod net;

#[path = "metal/jit.rs"]
pub mod jit;

#[path = "metal/process.rs"]
pub mod process;

#[path = "metal/services.rs"]
pub mod services;

#[path = "metal/trust.rs"]
pub mod trust;

#[path = "metal/util.rs"]
pub mod util;
