/* let-else + `?` in one fn — the refutable ctor binds (Some/Ok/Err and
 * a tuple of Somes) need the diverging else; `?` rides the same try
 * seam beside them: chained let-elses whose second bind SHADOWS the
 * first (the initializer reads the outer v), `?` in the body, and `?`
 * inside the diverging else-block itself (the else runs the try seam
 * before its return). The tuple-of-Somes form tests both fields'
 * Options in one `if` and runs the else when either is None; the
 * binds are fn-ptr payloads (Option<fn> -> the fn-ptr C type). *
 * Boundary: the else block must return/break/continue; irrefutable
 * destructures (`P(x)`/`S { .. }`) take a plain let — an else on them
 * refuses (`let-else on a non-Some/Ok pattern`). */
fn parse(n: i32) -> Result<i32, i32> {
    if n < 0 {
        Err(-n)
    } else {
        Ok(n + 1)
    }
}

fn find(n: i32) -> Result<i32, i32> {
    if n > 10 {
        Err(10)
    } else {
        Ok(n * 2)
    }
}

fn wrap(n: i32, f: fn(u8) -> u8) -> Option<fn(u8) -> u8> {
    if n < 0 {
        None
    } else {
        Some(f)
    }
}

pub fn chained(n: i32) -> Result<i32, i32> {
    let Ok(v) = parse(n) else {
        return Err(1);
    };
    let w = find(v)?;
    let Ok(x) = find(w) else {
        return Err(2);
    };
    Ok(x + 1)
}

pub fn try_in_else(n: i32) -> Result<i32, i32> {
    /* `?` inside the diverging else block: parse(0)? runs the seam,
     * the else returns Err(1 + 99) */
    let Ok(v) = parse(n) else {
        return Err(parse(0)? + 99);
    };
    Ok(v)
}

pub fn shadow(n: i32) -> Result<i32, i32> {
    let Ok(v) = parse(n) else {
        return Err(1);
    };
    /* the bind shadows the outer v; `find(v)` reads the outer one */
    let Ok(v) = find(v) else {
        return Err(2);
    };
    Ok(v)
}

pub fn tuple_of_somes(a: i32, b: i32) -> u8 {
    let g: fn(u8) -> u8 = |q: u8| q + 1;
    let (Some(x), Some(y)) = (wrap(a, g), wrap(b, g)) else {
        return 0;
    };
    /* both Some -> g(1) + g(2) = 5 */
    x(1) + y(2)
}
