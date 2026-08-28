/* WASM32 backend prove: a wasm module builds, and its export section names
 * every defined function (main + helpers) — the property the registry-linked
 * build path on wasm seats addresses faces by. */
#include "tcc.h"
#include "libtcc.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
extern int wasm_build_module(uint8_t **out_buf, int *out_len);

/* find "name" in the export section (kind 0x00 func); returns 1 when found */
static int has_export(const uint8_t *w, int len, const char *name) {
    int i = 8;
    size_t nl = strlen(name);
    while (i < len) {
        int id = w[i];
        int j = i + 1;
        size_t size = 0;
        int shift = 0;
        while (j < len && (w[j] & 0x80)) { /* multi-byte LEB128 size */
            size |= (size_t)(w[j] & 0x7f) << shift;
            shift += 7;
            j++;
        }
        if (j < len) {
            size |= (size_t)(w[j] & 0x7f) << shift;
            j++;
        }
        int body = j;
        if (id == 7) {
            int k2 = body;
            int count = w[k2++];
            for (int k = 0; k < count; k++) {
                int nlen = w[k2++];
                if ((size_t)nlen == nl && memcmp(w + k2, name, nl) == 0) {
                    return 1;
                }
                k2 += nlen;
                k2 += 2; /* kind + index (index < 128 in these proves) */
            }
            return 0;
        }
        i = body + size;
    }
    return 0;
}

int main(void) {
    TCCState *s = tcc_new();
    assert(s);
    const char *src =
        "int main(void) { return 42; }\n"
        "int add_one(int x) { return x + 1; }\n"
        "int scale3(int x) { return x * 3; }\n";
    tcc_set_output_type(s, TCC_OUTPUT_MEMORY);
    if (tcc_compile_string(s, src) != 0) { fprintf(stderr, "FAIL: compile\n"); return 1; }
    uint8_t *wasm = NULL; int len = 0;
    if (wasm_build_module(&wasm, &len) != 0 || !wasm) { fprintf(stderr, "FAIL: build module\n"); return 1; }
    assert(len >= 8 && wasm[0]==0x00 && wasm[1]==0x61 && wasm[2]==0x73 && wasm[3]==0x6d);
    if (!has_export(wasm, len, "main")) { fprintf(stderr, "FAIL: no main export\n"); return 1; }
    if (!has_export(wasm, len, "add_one")) { fprintf(stderr, "FAIL: no add_one export\n"); return 1; }
    if (!has_export(wasm, len, "scale3")) { fprintf(stderr, "FAIL: no scale3 export\n"); return 1; }
    printf("PASS: WASM32 backend proves named exports, %d bytes\n", len);
    tcc_free(wasm); tcc_delete(s);

    /* second compile: the serializer's name table must reset, no stale
     * exports from the previous module */
    {
        TCCState *s2 = tcc_new();
        assert(s2);
        uint8_t *w2 = NULL; int l2 = 0;
        tcc_set_output_type(s2, TCC_OUTPUT_MEMORY);
        if (tcc_compile_string(s2, "int main(void) { return 7; }") != 0) {
            fprintf(stderr, "FAIL: compile 2\n"); return 1;
        }
        if (wasm_build_module(&w2, &l2) != 0 || !w2) { fprintf(stderr, "FAIL: build 2\n"); return 1; }
        if (has_export(w2, l2, "add_one")) { fprintf(stderr, "FAIL: stale export\n"); return 1; }
        if (!has_export(w2, l2, "main")) { fprintf(stderr, "FAIL: no main 2\n"); return 1; }
        printf("PASS: WASM32 serializer resets between modules, %d bytes\n", l2);
        tcc_free(w2); tcc_delete(s2);
    }
    return 0;
}
