/* rsx_span.c — temp diagnostic: compile a .rs with an argv-sized arena,
 * print used/high-water. Same link shape as rsx_dump. Not a prove gate. */
#include "pymergetic/metal/build/__types__.h"
#include "pymergetic/metal/jit/rs/compiler/__types__.h"
#include "pymergetic/util/mem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern size_t pm_util_mem_arena_heap_used(const pm_util_mem_arena_t *);

int main(int argc, char **argv) {
    const char *path;
    size_t span_mb;
    FILE *f;
    long sz;
    char *src;
    char *c = NULL;
    size_t c_len = 0;
    char err[PM_METAL_BUILD_ERR_MAX];
    void *backing;
    pm_util_mem_arena_t *arena;

    if (argc < 3) {
        fprintf(stderr, "usage: rsx_span <file.rs> <span_mb>\n");
        return 2;
    }
    path = argv[1];
    span_mb = (size_t)strtoul(argv[2], NULL, 10);
    f = fopen(path, "rb");
    if (!f) { perror("open"); return 2; }
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    src = malloc((size_t)sz);
    if (fread(src, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); return 2; }
    fclose(f);

    backing = malloc(span_mb * 1024u * 1024u);
    if (!backing) { perror("malloc"); return 2; }
    arena = pm_util_mem_arena_create(backing, span_mb * 1024u * 1024u);
    if (!arena) return 2;
    memset(err, 0, sizeof(err));
    if (pm_metal_jit_rsx_compile(arena, src, (size_t)sz, &c, &c_len,
                                 err, sizeof(err)) != 0) {
        fprintf(stderr, "refused: %s\n", err);
        return 1;
    }
    {
        /* optional C dump for post-compile byte scans (garbage-byte
         * hunts): SPAN_OUT=<path> writes the generated unit whole. */
        const char *out_path = getenv("SPAN_OUT");
        if (out_path != NULL) {
            FILE *o = fopen(out_path, "wb");
            if (o != NULL) {
                fwrite(c, 1u, c_len, o);
                fclose(o);
            }
        }
    }
    printf("ok: c_len=%zu heap_used=%zu\n", c_len,
           pm_util_mem_arena_heap_used(arena));
    return 0;
}
