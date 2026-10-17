/* dyn Trait dispatch + first-class trait-object VALUES. A trait decl
 * lowers a C trait-object plane — `typedef struct { RET (*m)(void
 * *_self, ..); .. } Trait_vt;` plus the fat pair `typedef struct {
 * const void *_self; const Trait_vt *vt; } Trait_ref;`. `&dyn T` /
 * `&mut dyn T` are the pair VALUE `Trait_ref`, carried exactly like
 * Rust's `&dyn T` fat pointer: it RETURNS from fns, binds to locals,
 * copies, shadows and re-assigns, and every method call dispatches
 * `p.vt->m(p._self, ..)` through the per-(type, trait) static vtable
 * `rsx_vt_<S>_<T>` (emitted once in the declare pass — every coercion
 * site just takes its address, use-before-impl included). A coercion
 * `&mut s` -> `&mut dyn T` materializes `(Trait_ref){ &s,
 * &rsx_vt_S_T }` — a 2-word value, never a block-lifetime folded
 * struct behind `&(...)`. The method name `add` shadows the
 * integer-convenience fill (`x.add(k)` -> `x + k`): a trait method
 * dispatches through the vtable, never the name-keyed fill. */
pub struct Counter {
    v: u8,
}

pub trait Adder {
    fn add(&mut self, x: u8) -> u8;
    fn get(&self) -> u8;
}

impl Adder for Counter {
    fn add(&mut self, x: u8) -> u8 {
        self.v = self.v + x;
        self.v
    }
    fn get(&self) -> u8 {
        self.v
    }
}

pub fn bump(p: &mut dyn Adder) -> u8 {
    let a: u8 = p.add(1);
    p.add(a)
}

pub fn peek(p: &dyn Adder) -> u8 {
    p.get()
}

/* first-class value: the pair is RETURNED from a fn (the concrete
 * `&mut Counter` coerces at the return). */
pub fn pick(c: &mut Counter) -> &mut dyn Adder {
    c
}

pub fn smoke() -> u8 {
    let mut c: Counter = Counter { v: 10 };
    let t: u8 = bump(&mut c);
    let s: u8 = peek(&c);
    t + s
}

/* value semantics: bind the returned pair, COPY it (a `&mut dyn` to
 * `&dyn` rebind), shadow it, and dispatch through every copy. The
 * borrowed `&mut c` outlives all of them (c is a local of this fn),
 * so the pair's data pointer stays live — Rust's borrow model. */
pub fn smoke_values() -> u8 {
    let mut c: Counter = Counter { v: 10 };
    let p: &mut dyn Adder = pick(&mut c);
    let q: &dyn Adder = p;
    let u: u8 = q.get();
    let r: &dyn Adder = q;
    let w: u8 = bump(p);
    u + w + peek(r)
}
