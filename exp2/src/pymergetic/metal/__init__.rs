//! Package marker for `pymergetic.metal` — namespace root, no C ABI.
//! Real modules are nested dirs with their own `.module` (`boot`, `dt`,
//! `mem`, `rt`, …). Do not umbrella-re-export sibling borders here.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
