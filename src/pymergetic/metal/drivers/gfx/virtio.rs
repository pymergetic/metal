//! pymergetic.metal.drivers.gfx.virtio — barrel: not optional, this is what makes
//! `pymergetic::metal::drivers::gfx::virtio` resolve at all (Rust's own `use`/`mod` needs
//! a real item at this path, matching path == module). Hollow RS path:
//! the muscle is C (`virtio/__impl__.c`); the generated `__exports__.rs` mirror is not
//! a crate face and stays unlinked here.
