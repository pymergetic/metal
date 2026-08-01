//! Package marker for boot.tree — public stems: `print`, `api`, `notes`, `ready`.
//! Private helpers: `_line.rs`, `_registry.rs`.

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

pub use api::{
    pm_metal_boot_tree_blank, pm_metal_boot_tree_enter, pm_metal_boot_tree_flush,
    pm_metal_boot_tree_item, pm_metal_boot_tree_leave, pm_metal_boot_tree_reset,
    pm_metal_boot_tree_spacer, pm_metal_boot_tree_status_t,
};
pub use notes::{
    pm_metal_boot_tree_note_await, pm_metal_boot_tree_note_http, pm_metal_boot_tree_note_py,
    pm_metal_boot_tree_note_wasm,
};
pub use print::pm_metal_boot_tree_print;
pub use ready::{pm_metal_boot_tree_notify_ready, pm_metal_boot_tree_ready_mark};
