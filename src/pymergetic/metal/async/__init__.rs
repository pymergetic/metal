//! pymergetic.metal.async — Rust marker over the C N-runner impl.
//!
//! Product LIVE links the C callee. RS modules use `extern "C"` for verbs;
//! this crate exists so FS/other RS crates can `use pymergetic_metal_async as _`
//! without pulling a second async engine or `reg`.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
