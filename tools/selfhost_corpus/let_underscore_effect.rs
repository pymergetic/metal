/* `let _ = expr;` must still RUN the initializer for side effects — the
 * registration call here is the same shape as Lower_collect's enum
 * registration, which silently vanished when `_`-bindings dropped the
 * whole expression. */
pub struct Tab {
    used: [bool; 8],
    n: usize,
}

impl Tab {
    unsafe fn reg(&mut self, slot: usize) -> usize {
        if slot < 8 {
            self.used[slot] = true;
            self.n += 1;
        }
        slot
    }
}

pub fn register_two() -> usize {
    let mut t = Tab {
        used: [false; 8],
        n: 0,
    };
    let _ = unsafe { t.reg(1) };
    let _ = unsafe { t.reg(2) };
    t.n
}