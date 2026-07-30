//! `pymergetic.metal` — kernel module namespace (exp2).
//! Real modules are nested dirs with their own `.module` (`boot`, `dt`,
//! `mem`, `rt`, …). This entry is the package root marker.
#![cfg_attr(target_os = "none", no_std)]
