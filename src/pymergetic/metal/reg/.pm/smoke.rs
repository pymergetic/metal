//! Host smoke — register / lookup / bind / call0.
use std::os::raw::c_void;

use pymergetic_metal_reg::{
    pm_metal_reg_bind, pm_metal_reg_call0, pm_metal_reg_count, pm_metal_reg_lookup,
    pm_metal_reg_publish_kernel, pm_metal_reg_register, register_rows_bytes,
};

extern "C" fn smoke_forty_two() -> i32 {
    42
}

extern "C" fn smoke_seven() -> i32 {
    7
}

fn main() {
    let mod_name = b"pymergetic.metal.reg.smoke\0";
    let f_a = b"forty_two\0";
    let f_b = b"seven\0";
    let missing = b"missing\0";

    unsafe {
        assert_eq!(
            pm_metal_reg_register(
                mod_name.as_ptr(),
                f_a.as_ptr(),
                smoke_forty_two as *const c_void,
            ),
            0
        );
        assert_eq!(
            pm_metal_reg_register(mod_name.as_ptr(), f_b.as_ptr(), smoke_seven as *const c_void,),
            0
        );
        assert_eq!(pm_metal_reg_count(), 2);

        let mut out: *const c_void = std::ptr::null();
        assert_eq!(
            pm_metal_reg_lookup(mod_name.as_ptr(), f_a.as_ptr(), &mut out),
            0
        );
        assert!(!out.is_null());
        assert_eq!(out, smoke_forty_two as *const c_void);

        let bound = pm_metal_reg_bind(mod_name.as_ptr(), f_b.as_ptr());
        assert_eq!(bound, smoke_seven as *const c_void);

        assert_eq!(pm_metal_reg_call0(mod_name.as_ptr(), f_a.as_ptr()), 42);
        assert_eq!(pm_metal_reg_call0(mod_name.as_ptr(), f_b.as_ptr()), 7);
        assert_eq!(pm_metal_reg_call0(mod_name.as_ptr(), missing.as_ptr()), -1);

        // Replace existing
        assert_eq!(
            pm_metal_reg_register(mod_name.as_ptr(), f_a.as_ptr(), smoke_seven as *const c_void,),
            0
        );
        assert_eq!(pm_metal_reg_call0(mod_name.as_ptr(), f_a.as_ptr()), 7);
        assert_eq!(pm_metal_reg_count(), 2);

        // Reject empty / null
        assert_eq!(
            pm_metal_reg_register(mod_name.as_ptr(), f_a.as_ptr(), std::ptr::null()),
            -1
        );
        assert_eq!(
            pm_metal_reg_register(std::ptr::null(), f_a.as_ptr(), smoke_seven as *const c_void),
            -1
        );

        // W7.1 helpers: bulk register under one module
        let bulk_mod = b"pymergetic.metal.reg.bulk\0";
        assert_eq!(
            register_rows_bytes(
                bulk_mod,
                &[
                    (b"a\0", smoke_forty_two as *const c_void),
                    (b"b\0", smoke_seven as *const c_void),
                ],
            ),
            0
        );
        assert_eq!(pm_metal_reg_call0(bulk_mod.as_ptr(), b"a\0".as_ptr()), 42);
        assert_eq!(pm_metal_reg_call0(bulk_mod.as_ptr(), b"b\0".as_ptr()), 7);
        // Host: kernel modules not linked into this crate
        assert_eq!(pm_metal_reg_publish_kernel(), -1);
    }

    eprintln!("reg smoke ok");
}
