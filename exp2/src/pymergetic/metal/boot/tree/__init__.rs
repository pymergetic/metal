//! Package marker for boot.tree — C ABI is sibling stem `print` (no umbrella re-export).
//! Private helpers: `_line.rs`, `_registry.rs`.

#[path = "_line.rs"]
mod line;
#[path = "print.rs"]
mod print;
#[path = "_registry.rs"]
mod registry;

pub use print::pm_metal_boot_tree_print;
