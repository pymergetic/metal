/* rsx_dump.c — one-file rsx diagnostic: reads a .rs source, runs it through
 * pm_metal_jit_rsx_compile (the same one-shot the build card's Rust units
 * take), prints the generated C to stdout or the refusal to stderr. tools/
 * posture: not a prove gate, same link shape as rsx_probe.
 *
 * usage: rsx_dump <file.rs> <fqn>
 *   The fqn (e.g. pymergetic.wasmmod.registry) runs the build card's
 *   #[path] splice pass first, so the dumped C matches what unit_compile
 *   feeds rsx (faces ride in, cfg(test)-guarded items skip). */
#include "pymergetic/metal/build/__types__.h"
#include "pymergetic/metal/jit/rs/compiler/__types__.h"
#include "pymergetic/util/mem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    enum { SPAN = 256u * 1024u * 1024u };
    const char *path;
    FILE *f;
    long sz;
    char *src;
    char *c = NULL;
    size_t c_len = 0;
    char err[PM_METAL_BUILD_ERR_MAX];
    void *backing;
    pm_util_mem_arena_t *arena;
    const char *feed;
    size_t feed_len;

    if (argc < 2) {
        fprintf(stderr, "usage: rsx_dump <file.rs> [fqn]\n");
        return 2;
    }
    path = argv[1];
    f = fopen(path, "rb");
    if (f == NULL) {
        perror(path);
        return 2;
    }
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    src = (char *)malloc((size_t)sz + 1u);
    if (src == NULL || fread(src, 1u, (size_t)sz, f) != (size_t)sz) {
        fprintf(stderr, "rsx_dump: read failed\n");
        fclose(f);
        return 2;
    }
    src[sz] = '\0';
    fclose(f);

    backing = malloc(SPAN);
    if (backing == NULL) {
        return 2;
    }
    arena = pm_util_mem_arena_create(backing, SPAN);
    if (arena == NULL) {
        return 2;
    }

    feed = src;
    feed_len = (size_t)sz;
    if (argc >= 3) {
        /* the build card's splice appends each #[path] face after the unit
         * source; for the dump the same job is a manual concat of the
         * sibling face files named on the command line */
        size_t cap = (size_t)sz + 2u; /* src + '\n' + final '\0' */
        char *buf;
        size_t len = (size_t)sz;
        int a;
        for (a = 3; a < argc; a++) {
            FILE *g = fopen(argv[a], "rb");
            long gz;
            if (g != NULL) {
                fseek(g, 0, SEEK_END);
                gz = ftell(g);
                fseek(g, 0, SEEK_SET);
                cap += (size_t)gz + 1u; /* face bytes + '\n' */
                fclose(g);
            }
        }        buf = (char *)malloc(cap);
        if (buf == NULL) {
            return 2;
        }
        memcpy(buf, src, (size_t)sz);
        buf[sz] = '\n';
        len = (size_t)sz + 1u;
        for (a = 3; a < argc; a++) {
            FILE *g = fopen(argv[a], "rb");
            long gz;
            if (g == NULL) {
                continue;
            }
            fseek(g, 0, SEEK_END);
            gz = ftell(g);
            fseek(g, 0, SEEK_SET);
            if (fread(buf + len, 1u, (size_t)gz, g) == (size_t)gz) {
                len += (size_t)gz;
                buf[len++] = '\n';
            }
            fclose(g);
        }
        buf[len] = '\0';
        free(src);
        src = buf;
        feed = src;
        feed_len = len;
    }

    memset(err, 0, sizeof(err));
    if (pm_metal_jit_rsx_compile(arena, feed, feed_len,
            &c, &c_len, err, sizeof(err)) != 0) {
        fprintf(stderr, "rsx_dump: refused: %s\n", err);
        return 1;
    }
    fwrite(c, 1u, c_len, stdout);
    return 0;
}

