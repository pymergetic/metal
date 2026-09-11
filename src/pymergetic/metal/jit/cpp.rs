//! pymergetic.metal.jit.cpp — barrel: not optional, this is what makes
//! `pymergetic::metal::jit::cpp` resolve at all (Rust's own `use`/`mod` needs
//! a real item at this path, matching path == module).
//! C namespace: declares its Rust-reachable children (below).

#[path = "cpp/fixtures.rs"]
pub mod fixtures;
