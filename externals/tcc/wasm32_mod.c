/*
 * WASM32 module serializer — standalone compilation (not ONE_SOURCE).
 * Links with libtcc.o to access code_buf and func tracking arrays.
 */
#include "tcc.h"
#include <string.h>
#include <stdlib.h>

/* Access wasm32-gen.c static data. When compiled with -DTCC_TARGET_WASM32
   this gets the reg_classes etc. We reach into the codegen buffer through
   extern references that wasm32-gen.c's #else-block exposes. */

extern uint8_t code_buf[];       /* defined in wasm32-gen.c */
extern int func_start_offs[];    /* defined in wasm32-gen.c */
extern int func_body_len[];      /* defined in wasm32-gen.c */
extern int func_count;           /* defined in wasm32-gen.c */

int wasm_build_module(uint8_t **out_buf, int *out_len)
{
    int total = 8 + 7 + (2 + func_count) + (2 + 5) + (2 + 18) + (2 + 256*1024) + 256;
    *out_buf = (uint8_t *)malloc((size_t)total);
    if (!*out_buf) return -1;
    uint8_t *p = *out_buf;

    memcpy(p, "\0asm\x01\0\0\0", 8); p += 8;

    /* Type section (id=1): one functype () -> i32 */
    *p++ = 1; *p++ = 6; *p++ = 1; *p++ = 0x60; *p++ = 0; *p++ = 1; *p++ = 0x7f;

    /* Function section (id=3) */
    *p++ = 3; *p++ = (uint8_t)(func_count + 1); *p++ = (uint8_t)func_count;
    for (int i = 0; i < func_count; i++) *p++ = 0;

    /* Memory section (id=5) */
    *p++ = 5; *p++ = 5; *p++ = 1; *p++ = 0x01; *p++ = 0x01; *p++ = 0x80; *p++ = 0x02;

    /* Export section (id=7) */
    { *p++ = 7; uint8_t *len_ptr = p++; *p++ = 2;
      *p++ = 6; memcpy(p, "memory", 6); p += 6; *p++ = 0x02; *p++ = 0x00;
      *p++ = 4; memcpy(p, "main", 4); p += 4; *p++ = 0x00; *p++ = 0x00;
      *len_ptr = (uint8_t)(p - len_ptr - 1); }

    /* Code section (id=10) */
    *p++ = 10; uint8_t *cs_len_ptr = p++; *p++ = (uint8_t)func_count;
    for (int i = 0; i < func_count; i++) {
        int len = func_body_len[i];
        memcpy(p, code_buf + func_start_offs[i], (size_t)len); p += len;
    }
    *cs_len_ptr = (uint8_t)(p - cs_len_ptr - 1);

    *out_len = (int)(p - *out_buf);
    return 0;
}