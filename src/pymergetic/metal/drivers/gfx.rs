//! pymergetic.metal.drivers.gfx — barrel: not optional, this is what makes
//! `pymergetic::metal::drivers::gfx` resolve at all (Rust's own `use`/`mod` needs
//! a real item at this path, matching path == module).
//! C namespace: declares its Rust-reachable children (below).

#[path = "gfx/bochs.rs"]
pub mod bochs;

#[path = "gfx/gop.rs"]
pub mod gop;

#[path = "gfx/i915.rs"]
pub mod i915;

#[path = "gfx/lfb.rs"]
pub mod lfb;

#[path = "gfx/radeon.rs"]
pub mod radeon;

#[path = "gfx/sim.rs"]
pub mod sim;

#[path = "gfx/virtio.rs"]
pub mod virtio;
