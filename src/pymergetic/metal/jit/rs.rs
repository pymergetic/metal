//! pymergetic.metal.jit.rs — Rust JIT namespace.
//! The existing jit.rs card is __impl__.c (a C face that calls mrustc);
//! the compiler card is the self-hosting Rust→C pipeline in Rust.
#[path = "rs/compiler.rs"]
pub mod compiler;
