//! Package marker for boot.tree — public stems: `print`, `api`, `notes`, `ready`.
//! Private helpers: `_line.rs`, `_registry.rs`.
//!
//! C ABI is `#[no_mangle]` on the stems themselves; this hub only re-exports
//! what the parent `boot` crate references in Rust.

#[path = "api.rs"]
mod api;
#[path = "_line.rs"]
mod line;
#[path = "notes.rs"]
mod notes;
#[path = "print.rs"]
mod print;
#[path = "ready.rs"]
mod ready;
#[path = "_registry.rs"]
mod registry;

pub use print::pm_metal_boot_tree_print;
