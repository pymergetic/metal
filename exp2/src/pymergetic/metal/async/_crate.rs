//! Crate root (private) — wires sibling stems + shared engine. Not a pool face.
#![cfg_attr(target_os = "none", no_std)]
#![allow(dead_code, non_camel_case_types)]

use pymergetic_metal_mem as _;
use pymergetic_metal_rt as _;

#[path = "_engine.rs"]
mod engine;

mod r#await;
mod coro;
mod handle;
mod phase;
mod process;
mod proof;
mod runner;
mod task;
mod time;
