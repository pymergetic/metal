/* dyn Trait dispatch: a trait decl lowers a C trait-object plane — the
 * vtable is inlined as fields (`typedef struct { void *_self; RET (*m)
 * (void *_self, ..); .. } Trait;`), `&mut dyn T` is `T *`, and a `Trait *`
 * method call dispatches `p->m(p->_self, ..)`. A coercion `&mut s` ->
 * `&mut dyn T` materializes `&(Trait){ ._self = &s, .m = Type_Trait_m, .. }`
 * at the call. The method name `add` shadows the integer-convenience fill
 * (`x.add(k)` -> `x + k`): a trait method dispatches through the vtable,
 * never the name-keyed fill. */
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

pub fn smoke() -> u8 {
    let mut c: Counter = Counter { v: 10 };
    let t: u8 = bump(&mut c);
    let s: u8 = peek(&c);
    t + s
}
