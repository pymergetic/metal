/* Result combinators over the rsx_res_<T>_<E> row — the Option-
 * combinator twin for both payloads: is_ok/is_err read the tag,
 * unwrap_or/unwrap_or_else yield the T on Ok (the fallback runs over
 * the unpacked E on Err), map rewrites the T into the (U, E) row and
 * map_err the E into (T, F) — each out row registers through res_add
 * like a fn return row. The receiver rides a `__rsx_r` temp so a
 * closure bind shadowing the receiver's own name cannot eat it; the
 * bind is the CLOSURE's PARAM kid (a `: T` ascription lands a TYPE
 * kid beside it). `?` rides the try seam: statement level, nested
 * inside a call argument, chained on a call's result, and early
 * return. Regressed as `unsupported: method call ... 'is_ok'`. */
pub fn parse(n: i32) -> Result<i32, i32> {
    if n < 0 {
        Err(-n)
    } else {
        Ok(n + 1)
    }
}

pub fn tag(n: i32) -> i32 {
    let r = parse(n);
    if r.is_ok() {
        1
    } else if r.is_err() {
        -1
    } else {
        0
    }
}

pub fn use_combinators(n: i32) -> i32 {
    let r = parse(n);
    if r.is_ok() {
        let c = r.unwrap_or(0);
        let e = r.map(|v: i32| v * 2);
        let ev = e.unwrap_or(0);
        c + ev
    } else {
        let d = r.unwrap_or_else(|e: i32| e * 10);
        let f = r.map_err(|e: i32| e + 1);
        let fv = f.unwrap_or_else(|e: i32| e);
        d + fv
    }
}

pub fn twice(n: i32) -> Result<i32, i32> {
    let v = parse(n)?;
    Ok(v * 2)
}

pub fn nested(n: i32) -> Result<i32, i32> {
    /* `?` inside a call argument, then `?` on the call's own result */
    let v = twice(parse(n)?)?;
    if v > 100 {
        return Err(9);
    }
    Ok(v + 1)
}
