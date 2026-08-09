//! bus.virtio — Rust face over the C impl.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use core::ffi::c_void;

use pymergetic_metal_rt as _;

#[repr(C)]
pub struct pm_metal_virtio_dev_t {
    _opaque: [u8; 0],
}

#[repr(C)]
pub struct pm_metal_virtq_t {
    _opaque: [u8; 0],
}

extern "C" {
    fn pm_metal_virtio_pages_alloc(pages: u32) -> *mut c_void;
    fn pm_metal_virtio_pages_free(buf: *mut c_void, pages: u32);
    fn pm_metal_virtio_open(pci_device_id: u16, out: *mut pm_metal_virtio_dev_t) -> i32;
    fn pm_metal_virtio_close(dev: *mut pm_metal_virtio_dev_t);
    fn pm_metal_virtio_get_features(dev: *mut pm_metal_virtio_dev_t) -> u64;
    fn pm_metal_virtio_set_features(dev: *mut pm_metal_virtio_dev_t, features: u64) -> i32;
    fn pm_metal_virtio_set_status(dev: *mut pm_metal_virtio_dev_t, status: u8);
    fn pm_metal_virtio_get_status(dev: *mut pm_metal_virtio_dev_t) -> u8;
    fn pm_metal_virtio_cfg_read(dev: *mut pm_metal_virtio_dev_t, offset: u32, buf: *mut c_void, len: u32) -> i32;
    fn pm_metal_virtio_setup_queue(dev: *mut pm_metal_virtio_dev_t, qidx: u16, want_size: u16) -> i32;
    fn pm_metal_virtio_driver_ok(dev: *mut pm_metal_virtio_dev_t) -> i32;
    fn pm_metal_virtq_add(vq: *mut pm_metal_virtq_t, buf: *mut c_void, len: u32, device_writeable: i32, head_out: *mut u16) -> i32;
    fn pm_metal_virtq_kick(dev: *mut pm_metal_virtio_dev_t, vq: *mut pm_metal_virtq_t);
    fn pm_metal_virtq_get_used(vq: *mut pm_metal_virtq_t, head: *mut u16, len: *mut u32) -> i32;
    fn pm_metal_virtq_free_chain(vq: *mut pm_metal_virtq_t, head: u16);
}

#[inline] pub unsafe fn virtio_pages_alloc(pages: u32) -> *mut c_void { pm_metal_virtio_pages_alloc(pages) }
#[inline] pub unsafe fn virtio_pages_free(buf: *mut c_void, pages: u32) { pm_metal_virtio_pages_free(buf, pages) }
#[inline] pub unsafe fn virtio_open(pci_device_id: u16, out: *mut pm_metal_virtio_dev_t) -> i32 { pm_metal_virtio_open(pci_device_id, out) }
#[inline] pub unsafe fn virtio_close(dev: *mut pm_metal_virtio_dev_t) { pm_metal_virtio_close(dev) }
#[inline] pub unsafe fn virtio_get_features(dev: *mut pm_metal_virtio_dev_t) -> u64 { pm_metal_virtio_get_features(dev) }
#[inline] pub unsafe fn virtio_set_features(dev: *mut pm_metal_virtio_dev_t, features: u64) -> i32 { pm_metal_virtio_set_features(dev, features) }
#[inline] pub unsafe fn virtio_set_status(dev: *mut pm_metal_virtio_dev_t, status: u8) { pm_metal_virtio_set_status(dev, status) }
#[inline] pub unsafe fn virtio_get_status(dev: *mut pm_metal_virtio_dev_t) -> u8 { pm_metal_virtio_get_status(dev) }
#[inline] pub unsafe fn virtio_cfg_read(dev: *mut pm_metal_virtio_dev_t, offset: u32, buf: *mut c_void, len: u32) -> i32 { pm_metal_virtio_cfg_read(dev, offset, buf, len) }
#[inline] pub unsafe fn virtio_setup_queue(dev: *mut pm_metal_virtio_dev_t, qidx: u16, want_size: u16) -> i32 { pm_metal_virtio_setup_queue(dev, qidx, want_size) }
#[inline] pub unsafe fn virtio_driver_ok(dev: *mut pm_metal_virtio_dev_t) -> i32 { pm_metal_virtio_driver_ok(dev) }
#[inline] pub unsafe fn virtq_add(vq: *mut pm_metal_virtq_t, buf: *mut c_void, len: u32, device_writeable: i32, head_out: *mut u16) -> i32 { pm_metal_virtq_add(vq, buf, len, device_writeable, head_out) }
#[inline] pub unsafe fn virtq_kick(dev: *mut pm_metal_virtio_dev_t, vq: *mut pm_metal_virtq_t) { pm_metal_virtq_kick(dev, vq) }
#[inline] pub unsafe fn virtq_get_used(vq: *mut pm_metal_virtq_t, head: *mut u16, len: *mut u32) -> i32 { pm_metal_virtq_get_used(vq, head, len) }
#[inline] pub unsafe fn virtq_free_chain(vq: *mut pm_metal_virtq_t, head: u16) { pm_metal_virtq_free_chain(vq, head) }
