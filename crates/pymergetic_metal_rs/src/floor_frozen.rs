//! Frozen Py packages as inventory-only RegMods (+ seat rows for import/tests).

use pymergetic_metal_reg::{find_mod, pm_metal_reg_mod_load, RegMod};

const SEAT_FROZEN: i32 = 1;

type SeatTestFn = Option<unsafe extern "C" fn() -> i32>;

extern "C" {
    fn pm_metal_reg_seat_register(
        path: *const u8,
        kind: i32,
        fw: u8,
        browser: u8,
        test: SeatTestFn,
    ) -> i32;
    fn pm_metal_inspect_seat_test() -> i32;
    fn pm_metal_net_microdot_seat_test() -> i32;
}

fn load_inv(m: &'static RegMod) -> i32 {
    if find_mod(m.name).is_some() {
        return 0;
    }
    unsafe { pm_metal_reg_mod_load(m) }
}

fn seat(path: &'static [u8], test: SeatTestFn) -> i32 {
    debug_assert!(path.last() == Some(&0));
    unsafe { pm_metal_reg_seat_register(path.as_ptr(), SEAT_FROZEN, 1, 1, test) }
}

static ARCH: RegMod = RegMod::py_inventory("pymergetic.metal.arch");
static ARCH_WASM: RegMod = RegMod::py_inventory("pymergetic.metal.arch.wasm");
static ARCH_X86: RegMod = RegMod::py_inventory("pymergetic.metal.arch.x86");
static ARCH_X86_64: RegMod = RegMod::py_inventory("pymergetic.metal.arch.x86_64");
static INSPECT: RegMod = RegMod::py_inventory("pymergetic.metal.inspect");
static MICRODOT: RegMod = RegMod::py_inventory("pymergetic.metal.net.microdot");
static UNIX_X86: RegMod = RegMod::py_inventory("pymergetic.metal.unix.x86");
static UNIX_X86_64: RegMod = RegMod::py_inventory("pymergetic.metal.unix.x86_64");

/// Load frozen inventory RegMods and keep seat rows (import / seat_test).
pub fn load_all() -> i32 {
    let mut rc = 0i32;
    rc |= load_inv(&ARCH);
    rc |= load_inv(&ARCH_WASM);
    rc |= load_inv(&ARCH_X86);
    rc |= load_inv(&ARCH_X86_64);
    rc |= load_inv(&INSPECT);
    rc |= load_inv(&MICRODOT);
    rc |= load_inv(&UNIX_X86);
    rc |= load_inv(&UNIX_X86_64);

    /* Seat ring still drives import/smoke tests until kill-parallel. */
    rc |= seat(b"pymergetic.metal.arch\0", None);
    rc |= seat(b"pymergetic.metal.arch.wasm\0", None);
    rc |= seat(b"pymergetic.metal.arch.x86\0", None);
    rc |= seat(b"pymergetic.metal.arch.x86_64\0", None);
    rc |= seat(
        b"pymergetic.metal.inspect\0",
        Some(pm_metal_inspect_seat_test),
    );
    rc |= seat(
        b"pymergetic.metal.net.microdot\0",
        Some(pm_metal_net_microdot_seat_test),
    );
    rc |= seat(b"pymergetic.metal.unix.x86\0", None);
    rc |= seat(b"pymergetic.metal.unix.x86_64\0", None);
    rc
}
