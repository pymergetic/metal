//! pymergetic.metal.util — barrel: not optional, this is what makes
//! `pymergetic::metal::util` resolve at all (Rust's own `use`/`mod` needs
//! a real item at this path, matching path == module).

#[path = "util/ascii.rs"]
pub mod ascii;

#[path = "util/tree.rs"]
pub mod tree;
