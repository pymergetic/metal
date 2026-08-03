//! Crate root (private) — wires sibling stems + shared engine. Not a pool face.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(dead_code, non_camel_case_types)]

use pymergetic_metal_mem as _;
use pymergetic_metal_rt as _;

#[path = "_engine.rs"]
mod engine;

mod handle; /* first — C ABI handle typedef for sibling stems */
mod r#await;
mod coro;
mod metric;
mod phase;
mod process;
mod quiesce;
mod runner;
mod task;
mod time;
