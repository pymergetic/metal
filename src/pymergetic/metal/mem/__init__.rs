//! mem umbrella — pulls nested lock/arena/tlsf faces into one crate name
//! for dependents (`wamr_host`). Host heap remains C `mem/port/mem.c`.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]

use pymergetic_metal_mem_arena as _;
use pymergetic_metal_mem_lock as _;
use pymergetic_metal_mem_tlsf as _;
use pymergetic_metal_rt as _;
