//! pymergetic.metal.bus — transports (pci, virtio ids). Not a NIC.
#[path = "bus/pci.rs"]
pub mod pci;

#[path = "bus/virtio.rs"]
pub mod virtio;
