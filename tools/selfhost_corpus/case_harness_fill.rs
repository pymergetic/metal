/* The module test/bench registration convention at its kernel fill —
 * the RUST twins of guest.h's PM_MOD_TEST_C / PM_MOD_BENCH_C. The guest
 * branch of those macros is a keep-ref (the packer scans the call site;
 * the guest TU just keeps the case fn referenced), and that is exactly
 * what PM_MOD_TEST_RS! / PM_MOD_BENCH_RS! lower to here. The harness
 * calls: case(|| BODY) runs the body and passes (the host's
 * catch_unwind + TEST_LOCK have no kernel symbol — asserts abort
 * in-kernel, so run-then-pass is the kernel meaning), case_rc(NAME)
 * calls the closure-free trampoline and returns its own rc. */
pub fn body_passes() {
    assert_eq!(1 + 1, 2);
}

pub fn body_also_passes() {
    let n: u32 = 4;
    assert_eq!(n, 4);
}

unsafe extern "C" fn case_body_passes() -> i32 {
    case(|| body_passes())
}

unsafe extern "C" fn case_body_also_passes() -> i32 {
    case(|| body_also_passes())
}

unsafe extern "C" fn case_trampoline_body() -> i32 {
    body_passes();
    0
}

crate::PM_MOD_TEST_RS!("pymergetic.util.demo", "body_passes", case_body_passes);
crate::PM_MOD_TEST_RS!(
    "pymergetic.util.demo",
    "body_also_passes",
    case_body_also_passes
);

unsafe extern "C" fn bench_tick(_iterations: u64) -> i32 {
    0
}
crate::PM_MOD_BENCH_RS!("pymergetic.util.demo", "tick", bench_tick);

pub fn run_through_case_rc() -> i32 {
    unsafe { case_rc(case_trampoline_body) }
}

pub fn run_through_case() -> i32 {
    unsafe { case(|| body_passes()) }
}
