//! pymergetic.metal.net.swarm — barrel: not optional, this is what makes
//! `pymergetic::metal::net::swarm` resolve at all (Rust's own `use`/`mod` needs
//! a real item at this path, matching path == module).

#[path = "swarm/discovery.rs"]
pub mod discovery;

#[path = "swarm/membership.rs"]
pub mod membership;

#[path = "swarm/task.rs"]
pub mod task;
