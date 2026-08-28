/* pymergetic.metal.edit tests — the Phase 12 C editor prove:
 *  - parse_c locates functions and defines with honest spans
 *  - set_define splices only the value bytes (formatting preserved)
 *  - set_fn_body splices only the body span
 *  - typecheck gate: a broken edit errors at edit time
 *  - write_back gates: no note -> refusal; note + valid edit -> fs write
 */
#include "pymergetic/metal/edit/__types__.h"
#include "pymergetic/metal/build/__types__.h"
#include "pymergetic/metal/fs/__exports__.h"
#include "pymergetic/util/mem.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *g_backing;
static pm_util_mem_arena_t *g_arena;

static int32_t setup(void) {
    if (g_arena != NULL) {
        return 0;
    }
    g_backing = malloc(1u << 20);
    if (g_backing == NULL) {
        return 1;
    }
    g_arena = pm_util_mem_arena_create(g_backing, 1u << 20);
    if (g_arena == NULL) {
        free(g_backing);
        g_backing = NULL;
        return 2;
    }
    return 0;
}

static const char *SRC =
    "#include <stdint.h>\n"
    "#define TEST_EDIT_BUF 64\n"
    "#define TEST_EDIT_NAME \"x\"\n"
    "\n"
    "/* a comment with a paren ) inside */\n"
    "int32_t test_edit_add(int32_t a) {\n"
    "    return a + TEST_EDIT_BUF;\n"
    "}\n"
    "\n"
    "static uint32_t test_edit_len(const char *s) {\n"
    "    return 3;\n"
    "}\n";

static int32_t test_parse(void) {
    pm_metal_edit_tree_t t;
    const pm_metal_edit_node_t *n;
    if (pm_metal_edit_parse_c(&t, SRC, strlen(SRC)) != PM_METAL_EDIT_OK) {
        return 1;
    }
    /* two defines, two functions */
    n = pm_metal_edit_locate(&t, PM_METAL_EDIT_DEFINE, "TEST_EDIT_BUF");
    if (n == NULL || n->value_off == 0 || n->value_len != 2
        || strncmp(SRC + n->value_off, "64", 2) != 0) {
        return 2;
    }
    n = pm_metal_edit_locate(&t, PM_METAL_EDIT_DEFINE, "TEST_EDIT_NAME");
    if (n == NULL || n->value_len != 3
        || strncmp(SRC + n->value_off, "\"x\"", 3) != 0) {
        return 3;
    }
    n = pm_metal_edit_locate(&t, PM_METAL_EDIT_FN, "test_edit_add");
    if (n == NULL || SRC[n->span_start] != 'i'
        || SRC[n->span_end - 1] != '}') {
        return 4;
    }
    if (n->line != 6) {
        return 5;
    }
    n = pm_metal_edit_locate(&t, PM_METAL_EDIT_FN, "test_edit_len");
    if (n == NULL) {
        return 6;
    }
    /* the comment's paren must not have unbalanced the parser */
    if (pm_metal_edit_locate(&t, PM_METAL_EDIT_FN, "no_such") != NULL) {
        return 7;
    }
    if (pm_metal_edit_parse_c(NULL, SRC, 10) != PM_METAL_EDIT_ERR_ARGS) {
        return 8;
    }
    return 0;
}

static int32_t test_set_define(void) {
    pm_metal_edit_tree_t t;
    char *out = NULL;
    size_t out_len = 0;
    int32_t rc;
    if (setup()) {
        return 10;
    }
    if (pm_metal_edit_parse_c(&t, SRC, strlen(SRC)) != PM_METAL_EDIT_OK) {
        return 11;
    }
    rc = pm_metal_edit_set_define(g_arena, &t, "TEST_EDIT_BUF", "128",
        &out, &out_len);
    if (rc != PM_METAL_EDIT_OK || out == NULL) {
        return 12;
    }
    /* the splice: only the value bytes change */
    if (out_len != strlen(SRC) + 1) {
        return 13;
    }
    if (strstr(out, "#define TEST_EDIT_BUF 128") == NULL
        || strstr(out, "return a + TEST_EDIT_BUF;") == NULL
        || strstr(out, "TEST_EDIT_NAME \"x\"") == NULL) {
        return 14;
    }
    /* the whole rest is byte-identical */
    if (strstr(out, "static uint32_t test_edit_len") == NULL) {
        return 15;
    }
    /* typecheck the edited source: it must compile */
    if (pm_metal_edit_typecheck_c(out, out_len, NULL, 0)
            != PM_METAL_EDIT_OK) {
        return 16;
    }
    /* not-found is honest */
    rc = pm_metal_edit_set_define(g_arena, &t, "NO_SUCH", "1", &out, &out_len);
    if (rc != PM_METAL_EDIT_ERR_NOT_FOUND) {
        return 17;
    }
    return 0;
}

static int32_t test_set_fn_body(void) {
    pm_metal_edit_tree_t t;
    char *out = NULL;
    size_t out_len = 0;
    if (setup()) {
        return 20;
    }
    if (pm_metal_edit_parse_c(&t, SRC, strlen(SRC)) != PM_METAL_EDIT_OK) {
        return 21;
    }
    if (pm_metal_edit_set_fn_body(g_arena, &t, "test_edit_add",
            " return a * 2; ", &out, &out_len) != PM_METAL_EDIT_OK) {
        return 22;
    }
    if (strstr(out, "int32_t test_edit_add(int32_t a) { return a * 2; }")
        == NULL) {
        return 23;
    }
    /* the sibling is untouched */
    if (strstr(out, "return 3;") == NULL) {
        return 24;
    }
    /* typecheck */
    if (pm_metal_edit_typecheck_c(out, out_len, NULL, 0)
            != PM_METAL_EDIT_OK) {
        return 25;
    }
    if (pm_metal_edit_set_fn_body(g_arena, &t, "no_such", " {} ",
            &out, &out_len) != PM_METAL_EDIT_ERR_NOT_FOUND) {
        return 26;
    }
    return 0;
}

static int32_t test_typecheck_gate(void) {
    static const char *broken =
        "int32_t gate_probe(int32_t x) {\n"
        "    return y + 1;\n"    /* undeclared y */
        "}\n";
    char err[PM_METAL_EDIT_ERR_MAX];
    if (pm_metal_edit_typecheck_c(broken, strlen(broken), err, sizeof(err))
            != PM_METAL_EDIT_ERR_TYPECHECK) {
        return 30;
    }
    if (err[0] == 0) {
        return 31;
    }
    if (pm_metal_edit_typecheck_c(SRC, strlen(SRC), NULL, 0)
            != PM_METAL_EDIT_OK) {
        return 32;
    }
    if (pm_metal_edit_typecheck_c(NULL, 0, NULL, 0)
            != PM_METAL_EDIT_ERR_ARGS) {
        return 33;
    }
    return 0;
}

static int32_t test_write_back_gates(void) {
    pm_metal_edit_tree_t t;
    char *out = NULL;
    size_t out_len = 0;
    char err[PM_METAL_EDIT_ERR_MAX];
    static const char *path = "/src/test.edit.probe.c";
    uint8_t rbuf[512];
    uint32_t rlen = sizeof(rbuf);
    const char *reason = "phase 12 prove: editor write-back probe";
    if (setup()) {
        return 40;
    }
    if (pm_metal_edit_parse_c(&t, SRC, strlen(SRC)) != PM_METAL_EDIT_OK) {
        return 41;
    }
    if (pm_metal_edit_set_define(g_arena, &t, "TEST_EDIT_BUF", "256",
            &out, &out_len) != PM_METAL_EDIT_OK) {
        return 42;
    }

    /* (a) no note -> refusal, and nothing written */
    pm_metal_fs_drop(path);
    err[0] = 0;
    if (pm_metal_edit_write_back("test.edit.probe", path, out, out_len,
            err, sizeof(err)) != PM_METAL_EDIT_ERR_NOTE) {
        return 43;
    }
    if (err[0] == 0 || strstr(err, "ledger note") == NULL) {
        return 44;
    }
    if (pm_metal_fs_stat(path, &rlen) == 0) {
        return 45;    /* nothing was written */
    }

    /* (b) note + valid edit -> write, readable back */
    if (pm_metal_build_note_add("test.edit.probe",
            PM_METAL_BUILD_NOTE_CHANGE, reason, NULL, 0)
            != PM_METAL_BUILD_OK) {
        return 46;
    }
    if (pm_metal_edit_write_back("test.edit.probe", path, out, out_len,
            err, sizeof(err)) != PM_METAL_EDIT_OK) {
        return 47;
    }
    rlen = sizeof(rbuf);
    if (pm_metal_fs_read(path, rbuf, &rlen) != 0
        || rlen != (uint32_t)out_len
        || memcmp(rbuf, out, rlen) != 0) {
        return 48;
    }
    /* the ledger carries the note */
    if (pm_metal_build_note_has("test.edit.probe",
            PM_METAL_BUILD_NOTE_CHANGE) != 1) {
        return 49;
    }

    /* (c) note + broken edit -> typecheck refusal, previous write intact */
    {
        static const char *broken =
            "#define TEST_EDIT_BUF 999\nint f( {\n";
        rlen = sizeof(rbuf);
        if (pm_metal_edit_write_back("test.edit.probe", path, broken,
                strlen(broken), err, sizeof(err))
                != PM_METAL_EDIT_ERR_TYPECHECK) {
            return 50;
        }
        rlen = sizeof(rbuf);
        if (pm_metal_fs_read(path, rbuf, &rlen) != 0
            || rlen != (uint32_t)out_len) {
            return 51;
        }
    }
    pm_metal_fs_drop(path);
    return 0;
}

static int32_t pm_metal_edit_tests(void) {
    int32_t rc;
    rc = test_parse();
    if (rc) return rc;
    rc = test_set_define();
    if (rc) return rc;
    rc = test_set_fn_body();
    if (rc) return rc;
    rc = test_typecheck_gate();
    if (rc) return rc;
    rc = test_write_back_gates();
    if (rc) return rc;
    if (g_arena != NULL) {
        pm_util_mem_arena_destroy(g_arena);
        g_arena = NULL;
        free(g_backing);
        g_backing = NULL;
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.edit, tests, pm_metal_edit_tests);
