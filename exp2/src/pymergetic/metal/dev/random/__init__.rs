//! Random probe — RDRAND -> DT RANDOM compat "rdrand".
#![cfg_attr(target_os = "none", no_std)]

use pymergetic_metal_dt::{
    pm_metal_dt_add, pm_metal_dt_bus_t, pm_metal_dt_cap_t, pm_metal_dt_class_t, pm_metal_dt_count,
    pm_metal_dt_get, DtNode,
};
use pymergetic_metal_rt as _;

static COMPAT_RDRAND: &[u8] = b"rdrand\0";

unsafe fn already_rdrand() -> bool {
    let n = pm_metal_dt_count();
    for i in 0..n {
        let p = pm_metal_dt_get(i);
        if p.is_null() {
            continue;
        }
        let node = &*p;
        if node.class != pm_metal_dt_class_t::PM_METAL_DT_CLASS_RANDOM {
            continue;
        }
        if (node.caps & (pm_metal_dt_cap_t::PM_METAL_DT_CAP_BOUND as u32)) != 0 {
            return true;
        }
    }
    false
}

#[cfg(target_arch = "x86_64")]
unsafe fn rdrand_ok() -> bool {
    /* CPUID.01H:ECX.RDRAND[bit 30] — preserve rbx for LLVM. */
    let mut ecx: u32;
    core::arch::asm!(
        "push rbx",
        "mov eax, 1",
        "cpuid",
        "mov {0:e}, ecx",
        "pop rbx",
        out(reg) ecx,
        out("eax") _,
        out("ecx") _,
        out("edx") _,
        options(preserves_flags),
    );
    if (ecx & (1u32 << 30)) == 0 {
        return false;
    }
    let mut val: u32 = 0;
    let mut ok: u8;
    core::arch::asm!(
        "rdrand {0:e}",
        "setc {1}",
        out(reg) val,
        out(reg_byte) ok,
        options(nostack, nomem),
    );
    let _ = val;
    ok != 0
}

/// Probe RDRAND; add DT RANDOM if present. Returns 0.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_random_detect() -> i32 {
    if already_rdrand() {
        return 0;
    }
    #[cfg(target_arch = "x86_64")]
    {
        if !rdrand_ok() {
            return 0;
        }
        let node = DtNode {
            class: pm_metal_dt_class_t::PM_METAL_DT_CLASS_RANDOM,
            compat: COMPAT_RDRAND.as_ptr(),
            unit: 0,
            caps: 0,
            bus: pm_metal_dt_bus_t::PM_METAL_DT_BUS_PLATFORM,
            loc: [0, 0, 0, 0],
        };
        let _ = pm_metal_dt_add(&node);
    }
    0
}
