/* Capturing closures, limited form: a let-bound closure that references
 * outer locals synthesizes a static fn carrying one leading
 * `T *rsx_cap_<i>` per capture (T is the captured local's own body-view
 * type). The let emits no value at all — the binding is only the name —
 * and a DIRECT call rewrites to the designator with `&place` prepended
 * per capture, in capture order. What stays refused: a capturing closure
 * in value position (a fn pointer has no env slot), the self-binding
 * reference, more than 8 captures, and fn-pointer/array capture types. */
pub fn cap_one() -> u8 {
    let n: u8 = 40;
    let inc = |x: u8| n + x;
    inc(2)
}

pub fn cap_two() -> u8 {
    let a: u8 = 1;
    let b: u8 = 2;
    let sum = |x: u8| a + b + x;
    let t: u8 = sum(3);
    sum(t)
}

pub fn cap_nested() -> u8 {
    let k: u8 = 10;
    let outer = |x: u8| {
        let inner = |y: u8| x + y + k;
        inner(x)
    };
    outer(1)
}

pub fn cap_twice() -> u8 {
    let v: u8 = 5;
    let f = |a: u8| v + a;
    let w: u8 = f(1);
    f(w)
}
