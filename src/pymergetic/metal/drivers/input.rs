//! pymergetic.metal.drivers.input — barrel: not optional, this is what makes
//! `pymergetic::metal::drivers::input` resolve at all (Rust's own `use`/`mod` needs
//! a real item at this path, matching path == module).
//! C namespace: declares its Rust-reachable children (below).

#[path = "input/ps2.rs"]
pub mod ps2;

#[path = "input/virtio.rs"]
pub mod virtio;
