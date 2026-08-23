//! pymergetic.metal.drivers.net — netdev class + C NIC leaves (`tap`, `virtio`, `bge`, `sim`, `gmac`).
//! Same L2 ops face as `metal.net.wg` (tunnel, not a NIC). No RS cards yet.
#[path = "net/bge.rs"]
pub mod bge;

#[path = "net/gmac.rs"]
pub mod gmac;

#[path = "net/sim.rs"]
pub mod sim;

#[path = "net/tap.rs"]
pub mod tap;

#[path = "net/virtio.rs"]
pub mod virtio;
