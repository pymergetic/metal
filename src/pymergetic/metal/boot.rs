//! pymergetic.metal.boot — barrel: not optional, this is what makes
//! `pymergetic::metal::boot` resolve at all (Rust's own `use`/`mod` needs
//! a real item at this path, matching path == module).
//! C namespace: declares its Rust-reachable children (below).

#[path = "boot/externals.rs"]
pub mod externals;

#[path = "boot/tree.rs"]
pub mod tree;
