//! modrandom — getrandbits / randint (xorshift seeded from Metal mono_us).

use crate::upy::py::obj::MpObj;
use crate::upy::py::objects::objint;

extern "C" {
    fn pm_metal_async_mono_us() -> u64;
}

static mut STATE: u64 = 0;

unsafe fn seed_if_needed() {
    if STATE == 0 {
        let t = pm_metal_async_mono_us();
        STATE = t ^ 0x9e37_79b9_7f4a_7c15;
        if STATE == 0 {
            STATE = 0x1234_5678_9abc_def0;
        }
    }
}

unsafe fn next_u64() -> u64 {
    seed_if_needed();
    // xorshift64*
    let mut x = STATE;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    STATE = x;
    x
}

pub unsafe fn getrandbits(n: u32) -> Option<MpObj> {
    if n == 0 || n > 30 {
        return None;
    }
    let mask = (1u32 << n) - 1;
    let v = (next_u64() as u32) & mask;
    Some(objint::from_isize(v as isize))
}

pub unsafe fn randint(a: isize, b: isize) -> Option<MpObj> {
    if a > b {
        return None;
    }
    let span = (b - a) as u64 + 1;
    let v = a + (next_u64() % span) as isize;
    Some(objint::from_isize(v))
}

pub unsafe fn seed(n: u64) {
    STATE = if n == 0 { 1 } else { n };
}
