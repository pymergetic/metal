//! pymergetic.metal.jit — JIT compiler cards (C, C++, Python, Rust).
//! The C and C++ cards live in __impl__.c; the Rust compiler lives in
//! jit/rs/compiler/__impl__.rs (one defining lang = rs).
#[path = "jit/rs.rs"]
pub mod rs;
