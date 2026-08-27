// Standalone prove: link the mrustc embed shim against mrustc.a and run
// Rust -> C in-process on a #![no_std] library input, then assert the C
// output looks sane. Uses rlib crate type + Trans_Enumerate_Public path
// so mrustc emits every public #[no_mangle] / extern "C" function without
// needing a main entrypoint or panic lang items.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "mrustc_embed.h"

static const char *NO_STD_RS =
    "#![no_std]\n"
    "\n"
    "#[no_mangle]\n"
    "pub extern \"C\" fn jit_add_four() -> i32 {\n"
    "    2 + 2\n"
    "}\n";

int main(void) {
    static char cbuf[1024 * 1024];
    size_t clen = 0;

    if (!getenv("MRUSTC_LIBDIR")) {
        setenv("MRUSTC_LIBDIR",
               "/home/ladmin/Devel/os-sdk/packages/metalpython/extmod/metal/externals/mrustc/output-1.90.0",
               0);
    }

    int rc = pm_metal_jit_rs_mrustc_compile(
        NO_STD_RS, strlen(NO_STD_RS), cbuf, sizeof(cbuf), &clen);
    if (rc != 0) {
        fprintf(stderr, "FAIL: mrustc in-process compile returned %d\n", rc);
        return 1;
    }
    if (clen == 0) {
        fprintf(stderr, "FAIL: empty C output\n");
        return 1;
    }
    if (!strstr(cbuf, "jit_add_four")) {
        fprintf(stderr, "FAIL: generated C lacks 'jit_add_four'\n");
        return 1;
    }
    printf("PASS: Rust->C in-process, %zu bytes of C emitted\n", clen);
    return 0;
}
