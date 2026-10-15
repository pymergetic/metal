/* Nested items in fn bodies: a Rust nested item captures nothing — it is
 * a file-scope item with scoped visibility, so the subset hoists it. The
 * walk convention this mirrors (util.mem's arena walk) hands the nested
 * callback's address across a raw slot; the fn name in argument position
 * is its C designator, exactly the cast target's fill. A scope-local
 * `use` is bookkeeping the build face resolves at splice time — its
 * kernel fill is the no-op that lets the imported spelling resolve
 * through the flat namespace. */
pub struct Tally {
    used: u32,
}

type WalkFn = extern "C" fn(*mut u8, usize, i32, *mut u8);

pub fn count_used(t: &mut Tally) -> u32 {
    extern "C" fn count_blocks(_p: *mut u8, _n: usize, used: i32, user: *mut u8) {
        if used != 0 && !user.is_null() {
            let count = unsafe { &mut *(user as *mut u32) };
            *count += 1;
        }
    }
    use core::ptr;
    static WALKS: u32 = 3;
    const SCALE: u32 = 2;
    let mut hits: u32 = 0;
    let mut i: u32 = 0;
    while i < WALKS {
        fake_walk(count_blocks, &mut hits as *mut u32 as *mut u8);
        i += 1;
    }
    fake_walk(count_blocks, ptr::null_mut());
    t.used = hits * SCALE;
    t.used
}

pub fn fake_walk(cb: WalkFn, user: *mut u8) {
    cb(user, 8, 1, user);
}
