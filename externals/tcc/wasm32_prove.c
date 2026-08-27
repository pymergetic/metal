#include "tcc.h"
#include "libtcc.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
extern int wasm_build_module(uint8_t **out_buf, int *out_len);
int main(void) {
    TCCState *s = tcc_new();
    assert(s);
    const char *src = "int main(void) { return 42; }";
    tcc_set_output_type(s, TCC_OUTPUT_MEMORY);
    if (tcc_compile_string(s, src) != 0) { fprintf(stderr, "FAIL: compile\n"); return 1; }
    uint8_t *wasm = NULL; int len = 0;
    if (wasm_build_module(&wasm, &len) != 0 || !wasm) { fprintf(stderr, "FAIL: build module\n"); return 1; }
    assert(len >= 8 && wasm[0]==0x00 && wasm[1]==0x61 && wasm[2]==0x73 && wasm[3]==0x6d);
    printf("WASM hex (%d bytes): ", len);
    for (int i = 0; i < len && i < 200; i++) printf("%02x ", wasm[i]);
    printf("\n");
    printf("PASS: WASM32 backend proves, %d bytes\n", len);
    tcc_free(wasm); tcc_delete(s);
    return 0;
}
