//! pymergetic.metal.net.dhcp — barrel: not optional, this is what makes
//! `pymergetic::metal::net::dhcp` resolve at all (Rust's own `use`/`mod` needs
//! a real item at this path, matching path == module). Hollow RS path:
//! the muscle is C (`dhcp/__impl__.c`); the generated `__exports__.rs` mirror is not
//! a crate face and stays unlinked here.
