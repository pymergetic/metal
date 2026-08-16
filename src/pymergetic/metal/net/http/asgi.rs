//! pymergetic.metal.net.http.asgi — thin reexport (path == module).
#[path = "asgi/__impl__.rs"]
mod r#impl;
pub use r#impl::*;
