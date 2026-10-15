macro_rules! sq {
    ( $x:expr ) => { $x * $x }
}
macro_rules! pick2 {
    ( $x:expr, $y:expr ) => { $x * 10 + $y }
}
macro_rules! one {
    () => { 1 }
}

fn main() -> u8 {
    let y: u8 = sq!(3);
    let z: u8 = sq!(y + 1);
    let a: u8 = pick2!(3, 4);
    let b: u8 = one!();
    y + z + a + b
}
