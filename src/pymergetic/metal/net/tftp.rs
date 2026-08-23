//! pymergetic.metal.net.tftp — barrel: not optional, this is what makes
//! `pymergetic::metal::net::tftp` resolve at all (Rust's own `use`/`mod` needs
//! a real item at this path, matching path == module). Hollow RS path:
//! the muscle is C (`tftp/__impl__.c`); the generated `__exports__.rs` mirror is not
//! a crate face and stays unlinked here.
