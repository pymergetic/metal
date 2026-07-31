//! Tiny content fingerprint (FNV-1a 64). No crates.io deps.
//!
//! Used in generated banners as `Source-sha:` so sync/convert can skip
//! faces whose human source has not changed. Not cryptographic.

use alloc::string::String;

pub fn fnv1a64(data: &[u8]) -> u64 {
    let mut h = 0xcbf29ce484222325u64;
    for &b in data {
        h ^= u64::from(b);
        h = h.wrapping_mul(0x100000001b3);
    }
    h
}

/// Lowercase 16-hex FNV-1a 64 of `data`.
pub fn source_sha_hex(data: &[u8]) -> String {
    alloc::format!("{:016x}", fnv1a64(data))
}
