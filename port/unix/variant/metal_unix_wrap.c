/* Bare ./micropython → -i -m pymergetic.metal.unix
 * (banner + seat autoexec, then friendly REPL). Linked with -Wl,--wrap=main.
 *
 * Heap: compile default MICROPY_HEAP_SIZE (see mpconfigvariant.h).
 * Runtime: METAL_HEAPSIZE or MICROPY_HEAPSIZE (same grammar as -X heapsize=),
 * or pass -X heapsize=… explicitly (wins over env).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int __real_main(int argc, char **argv);
void pm_metal_unix_boot_tree(void);

static int argv_has_heapsize(int argc, char **argv) {
    int i;
    if (argv == NULL) {
        return 0;
    }
    for (i = 1; i + 1 < argc; i++) {
        if (argv[i] != NULL && strcmp(argv[i], "-X") == 0 && argv[i + 1] != NULL &&
            strncmp(argv[i + 1], "heapsize=", sizeof("heapsize=") - 1) == 0) {
            return 1;
        }
    }
    return 0;
}

static const char *heapsize_env(void) {
    const char *v = getenv("METAL_HEAPSIZE");
    if (v != NULL && v[0] != '\0') {
        return v;
    }
    v = getenv("MICROPY_HEAPSIZE");
    if (v != NULL && v[0] != '\0') {
        return v;
    }
    return NULL;
}

int __wrap_main(int argc, char **argv) {
    const char *env_heap;
    char heap_arg[64];
    char **nargv;
    int nargc;
    int i;
    int inject_heap;
    int inject_mod;

    if (argv == NULL || argc < 1 || argv[0] == NULL) {
        return __real_main(argc, argv);
    }

    env_heap = heapsize_env();
    inject_heap = (env_heap != NULL && !argv_has_heapsize(argc, argv));
    inject_mod = (argc == 1);

    if (!inject_heap && !inject_mod) {
        return __real_main(argc, argv);
    }

    if (inject_heap) {
        /* Cap so a wild env cannot blow the stack-backed buffer. */
        if (strlen(env_heap) > 32) {
            return __real_main(argc, argv);
        }
        (void)snprintf(heap_arg, sizeof(heap_arg), "heapsize=%s", env_heap);
    }

    /* -i keeps the friendly REPL after -m (otherwise the process exits). */
    nargc = argc + (inject_heap ? 2 : 0) + (inject_mod ? 3 : 0);
    nargv = (char **)malloc((size_t)(nargc + 1) * sizeof(char *));
    if (nargv == NULL) {
        return __real_main(argc, argv);
    }

    nargc = 0;
    nargv[nargc++] = argv[0];
    if (inject_heap) {
        nargv[nargc++] = "-X";
        nargv[nargc++] = heap_arg;
    }
    for (i = 1; i < argc; i++) {
        nargv[nargc++] = argv[i];
    }
    if (inject_mod) {
        nargv[nargc++] = "-i";
        nargv[nargc++] = "-m";
        nargv[nargc++] = "pymergetic.metal.unix";
    }
    nargv[nargc] = NULL;

    if (inject_mod) {
        /* Live boot.tree on stdout before µPy (same UX as FW/browser). */
        pm_metal_unix_boot_tree();
    }

    {
        int rc = __real_main(nargc, nargv);
        free(nargv);
        return rc;
    }
}
