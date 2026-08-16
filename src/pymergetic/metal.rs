//! pymergetic.metal — Rust namespace barrel (path == module).
//! C cards stay in metal.mk. RS cards are this crate graph so gen sees Live
//! `PM_MOD_EXPORT_RS!` ctors. Not a second HTTP stack.
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
