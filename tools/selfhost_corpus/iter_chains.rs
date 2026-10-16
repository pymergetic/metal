/* Iterator adapter chains: a run of lazy adapters (filter / map, in any
 * order, any count) fused with ONE terminal (collect / count / sum / fold /
 * for_each / all / any / position) lowers to a single C statement-expr with
 * one `for` walk over the container — every stage renames its closure bind
 * onto the previous stage's stream spelling (`__rsx_c<k>`), so stages do not
 * collide and no materialized intermediate exists. The chain's `.iter()` may
 * stand alone (`kept.iter().sum()` — a terminal with zero stages). Chain
 * closure binds take a `: T` ascription freely (the stream types the bind);
 * the legacy lone-adapter emitters below want a PLAIN bind. `map(|r| r.0)`
 * is the tuple-field view stage. A LONE adapter with no terminal stays on
 * its historical emitter — the legacy `.map().collect()` and `.all()`
 * shapes at the bottom must keep their old golden byte for byte; a lone
 * filter is refused (nothing to fuse it WITH). Regressed as `unsupported:
 * method call ... 'filter'`. */
pub fn fill(dst: &mut Vec<i32>, src: &[i32]) {
    let mut v: Vec<i32> = Vec::new();
    v.extend_from_slice(src);
    let doubled: Vec<i32> = v.iter().filter(|x: i32| x > 0).map(|x: i32| x * 2).collect();
    dst.extend_from_slice(&doubled);
}

pub fn reorder(src: &[i32]) -> i32 {
    /* map BEFORE filter: the stream after map is the mapped type */
    let kept: Vec<i32> = src.iter().map(|x: i32| x + 1).filter(|y: i32| y > 2).collect();
    /* the collected Vec reads back through its own chain */
    kept.iter().sum()
}

pub fn tallies(src: &[i32]) -> i32 {
    let n = src.iter().filter(|x: i32| x > 0).count();
    let s = src.iter().filter(|x: i32| x > 0).sum();
    let big = src.iter().map(|x: i32| x * 2).all(|y: i32| y > -1000);
    let any = src.iter().filter(|x: i32| x > 0).any(|y: i32| y > 100);
    let pos = src.iter().map(|x: i32| x - 1).position(|y: i32| y == 0);
    let mut acc = 0;
    if big && any {
        acc += 1;
    }
    (n as i32) + s + acc + (pos as i32)
}

pub fn accumulate(dst: &mut Vec<i32>, src: &[i32]) -> i32 {
    let total = src.iter().filter(|x: i32| x > 0).fold(0, |acc: i32, x: i32| acc + x);
    src.iter().map(|x: i32| x * 3).for_each(|y: i32| dst.push(y));
    total
}

pub fn pick(pairs: &[(i32, i32)]) -> i32 {
    /* the tuple-field view stage: `r.0` reads the first slot */
    let firsts: Vec<i32> = pairs.iter().map(|r| r.0).filter(|x: i32| x > 0).collect();
    firsts.iter().map(|x: i32| x).sum()
}

pub fn legacy_map() -> i32 {
    /* LONE adapter + terminal: the historical emitter's golden — the
     * chain gate must leave this shape byte-identical */
    let mut v: Vec<i32> = Vec::new();
    v.push(1);
    v.push(2);
    let out: Vec<i32> = v.iter().map(|x| x * 10).collect();
    out.iter().sum()
}

pub fn legacy_pred() -> bool {
    let mut v: Vec<i32> = Vec::new();
    v.push(2);
    v.iter().all(|x| x > 0)
}
