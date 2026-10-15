/* assert! / assert_eq! / assert_ne! — the test-face assertion macros.
 * The check lowers to the subset's fail path (abort(), the container
 * ops' allocation-failure call), the operands evaluate exactly once,
 * and a trailing message's format literal is carried as a comment in
 * the generated C (its lazy args drop with the abort-on-fail path).
 * Regressed 2026-10-15 as `unsupported: expression macro`, which kept
 * every card's test face out of the in-kernel chain. */
pub fn g() -> i32 {
    7
}

pub fn check() -> i32 {
    assert!(g() == 7);
    assert_eq!(g(), 7);
    assert_ne!(g(), 6);
    0
}

pub fn checked(x: i32) -> i32 {
    assert!(x > 0, "x={x} should be positive");
    assert_eq!(x, 1, "got {}", x);
    x
}
