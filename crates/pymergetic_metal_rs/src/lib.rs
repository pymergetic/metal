//! Product freestanding Rust image — one staticlib, no duplicate rt/core.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]

pub use pymergetic_metal_rt as rt;

use pymergetic_metal_async as _;
use pymergetic_metal_dev_blk_ram as _;
use pymergetic_metal_dt as _;
use pymergetic_metal_fs as _;
use pymergetic_metal_fs_embed as _;
use pymergetic_metal_fs_fat as _;
use pymergetic_metal_fs_littlefs as _;
use pymergetic_metal_fs_mtar as _;
use pymergetic_metal_fs_overlay as _;
use pymergetic_metal_fs_tmpfs as _;
use pymergetic_metal_fs_vfs as _;
use pymergetic_metal_fs_wasmmod as _;
use pymergetic_metal_fs_zip as _;
use pymergetic_metal_hwtree as _;
use pymergetic_metal_log as _;
use pymergetic_metal_mem as _;
use pymergetic_metal_mem_arena as _;
use pymergetic_metal_mem_lock as _;
use pymergetic_metal_mem_tlsf as _;
use pymergetic_metal_reg as _;
use pymergetic_metal_util_lz4 as _;
use pymergetic_metal_util_size as _;
use pymergetic_metal_util_tar as _;
use pymergetic_metal_wamr_host as _;

/* net.ip / net.wg RS faces live at path==module:
 *   src/pymergetic/metal/net/{ip,wg}/__init__.rs
 */
