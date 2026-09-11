//! pymergetic.metal.jit.c — barrel: not optional, this is what makes
//! `pymergetic::metal::jit::c` resolve at all (Rust's own `use`/`mod` needs
//! a real item at this path, matching path == module).
//! C namespace: declares its Rust-reachable children (below).

#[path = "c/tccwasm.rs"]
pub mod tccwasm;
