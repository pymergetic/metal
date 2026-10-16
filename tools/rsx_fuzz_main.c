/* rsx_fuzz_main.c — fixed runner for the jit/rs fuzz corpus.
 *
 * Every generated fuzz case (tools/rsx_fuzz.py) emits exactly
 * RSX_FUZZ_N functions with the same signature:
 *
 *     uint32_t fuzz_fK(uint32_t a, uint32_t b, uint32_t c);   K in 0..15
 *
 * The runner calls each one over a fixed input grid and prints
 * "<K> <case>: <value>" lines. Link it against either compiler's
 * object (micro-rustc's emitted C, or mrustc's) — the no_mangle
 * names are the contract — and diff the outputs to get the
 * differential. The expected values come from the generator, which
 * computes the same expression tree in Python as it emits.
 *
 * Not a prove seat: a fuzz harness owned by tools/, never registered. */
#include <stdint.h>
#include <stdio.h>

#define RSX_FUZZ_N 16

uint32_t fuzz_f0(uint32_t a, uint32_t b, uint32_t c);
uint32_t fuzz_f1(uint32_t a, uint32_t b, uint32_t c);
uint32_t fuzz_f2(uint32_t a, uint32_t b, uint32_t c);
uint32_t fuzz_f3(uint32_t a, uint32_t b, uint32_t c);
uint32_t fuzz_f4(uint32_t a, uint32_t b, uint32_t c);
uint32_t fuzz_f5(uint32_t a, uint32_t b, uint32_t c);
uint32_t fuzz_f6(uint32_t a, uint32_t b, uint32_t c);
uint32_t fuzz_f7(uint32_t a, uint32_t b, uint32_t c);
uint32_t fuzz_f8(uint32_t a, uint32_t b, uint32_t c);
uint32_t fuzz_f9(uint32_t a, uint32_t b, uint32_t c);
uint32_t fuzz_f10(uint32_t a, uint32_t b, uint32_t c);
uint32_t fuzz_f11(uint32_t a, uint32_t b, uint32_t c);
uint32_t fuzz_f12(uint32_t a, uint32_t b, uint32_t c);
uint32_t fuzz_f13(uint32_t a, uint32_t b, uint32_t c);
uint32_t fuzz_f14(uint32_t a, uint32_t b, uint32_t c);
uint32_t fuzz_f15(uint32_t a, uint32_t b, uint32_t c);

typedef uint32_t (*rsx_fuzz_fn)(uint32_t, uint32_t, uint32_t);

static const rsx_fuzz_fn FNS[RSX_FUZZ_N] = {
    fuzz_f0,  fuzz_f1,  fuzz_f2,  fuzz_f3,
    fuzz_f4,  fuzz_f5,  fuzz_f6,  fuzz_f7,
    fuzz_f8,  fuzz_f9,  fuzz_f10, fuzz_f11,
    fuzz_f12, fuzz_f13, fuzz_f14, fuzz_f15,
};

/* A fixed grid: small, distinct, and covering 0 so a literal-fold or a
 * dropped operand shows up in at least one column. */
static const uint32_t GRID[4][3] = {
    { 3, 5, 7 },
    { 0, 1, 255 },
    { 17, 200, 42 },
    { 255, 255, 255 },
};

int main(void) {
    for (int k = 0; k < RSX_FUZZ_N; k++) {
        for (int g = 0; g < 4; g++) {
            uint32_t r = FNS[k](GRID[g][0], GRID[g][1], GRID[g][2]);
            printf("%d %d: %u\n", k, g, r);
        }
    }
    return 0;
}
