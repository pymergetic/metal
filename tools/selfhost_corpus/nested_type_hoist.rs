/* Nested type/impl items in fn bodies: a Rust nested `struct`, `enum`,
 * `type`, or `impl` captures nothing from the enclosing scope — it is a
 * file-scope item with scoped visibility, exactly like the nested fn/static
 * case (see nested_callback_item.rs). The subset hoists these to file scope
 * so a local helper type is a plain translation unit item. This mirrors the
 * registry's inline Seen/Counter table rows and the loader's marker-trait
 * `unsafe impl` guards living next to the code that owns them. */
pub struct Frame {
    base: u32,
}

pub fn tally_frames(n: u32) -> u32 {
    struct Row {
        hits: u32,
        tag: u8,
    }
    type RowAlias = Row;
    static BUMP: u32 = 3;
    unsafe impl Sync for Row {}
    let mut r = Row { hits: 0, tag: 0 };
    let mut i: u32 = 0;
    while i < n {
        r.hits += BUMP;
        r.tag = 1;
        i += 1;
    }
    let f = Frame { base: r.hits };
    f.base
}
