//! Host smoke: the real (non-synthetic) kernel module actually loads and
//! becomes module #0 in `pymergetic_metal_reg`'s kernel table. The
//! generic lifecycle mechanics (register/connect/refcount/cascading
//! unload) already have a full synthetic-module smoke in
//! `reg/.pm/smoke.rs`; this only proves `pymergetic.metal`'s own
//! `pm_metal_kernel_load` wires into that mechanism for real.
use pymergetic_metal::pm_metal_kernel_load;
use pymergetic_metal_reg::{pm_metal_reg_mod_count, pm_metal_reg_mod_unload};

fn main() {
    unsafe {
        assert_eq!(pm_metal_reg_mod_count(), 0);
        assert_eq!(pm_metal_kernel_load(), 0);
        assert_eq!(pm_metal_reg_mod_count(), 1);

        // Loading twice is a caller bug (kernel is architecturally first
        // and loaded exactly once) and must be refused, not silently
        // accepted as a no-op.
        assert_eq!(pm_metal_kernel_load(), -1);

        // Permanently `unloadable = false`: even though nothing else is
        // holding a reference, unload must still refuse (kernel never
        // actually runs `deregister_symbols`/`on_unload` in practice).
        let name = b"pymergetic.metal\0";
        assert_eq!(pm_metal_reg_mod_unload(name.as_ptr()), -1);
        assert_eq!(pm_metal_reg_mod_count(), 1);
    }
    eprintln!("kernel module smoke ok");
}
