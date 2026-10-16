macro_rules! add3 {
    ($a:expr, $b:expr, $c:expr) => {
        $a + $b + $c
    };
}

macro_rules! inc {
    ($x:expr) => {
        $x + 1
    };
}

macro_rules! dbl_lit {
    ($n:literal) => {
        $n * 2
    };
}

macro_rules! mk_ident {
    ($i:ident) => {
        $i * 2
    };
}

fn use_idents() -> u32 {
    let q = 5;
    let z = mk_ident!(q);
    z + 1
}

fn deep_chain() -> u32 {
    inc!(inc!(inc!(inc!(10))))
}

fn nested_args() -> u32 {
    add3!(inc!(1), dbl_lit!(3), add3!(1, 2, 3))
}

fn multi_step() -> u32 {
    let a = dbl_lit!(4);
    let b = inc!(a);
    let c = add3!(a, b, inc!(b));
    c
}

fn main() {
    let a = deep_chain();
    let b = nested_args();
    let c = multi_step();
    let d = use_idents();
    let _ = a + b + c + d;
}
