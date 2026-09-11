/* pymergetic.metal.boot.externals — walk PM_METAL_EXTERNAL_C records. */
#include "pymergetic/metal/boot/externals/__exports__.h"

#include "pymergetic/metal/boot/externals/__types__.h"
#include "pymergetic/util/mem/__types__.h"

#include <stdint.h>
#include <string.h>

extern const pm_metal_external_t __start_pm_metal_externals[] __attribute__((weak));
extern const pm_metal_external_t __stop_pm_metal_externals[] __attribute__((weak));

/* Registered from constructors, so a list through the callers' own nodes and
 * never a table of our own: there is no allocator yet when these arrive, and
 * a fixed table would be both a reservation and a ceiling. */
static pm_metal_external_node_t *s_head;

static int name_eq(const char *a, const char *b) {
    if (a == NULL || b == NULL) {
        return 0;
    }
    return strcmp(a, b) == 0;
}

static int ver_ok(const char *ver) {
    if (ver == NULL || ver[0] == 0) {
        return 0;
    }
    /* "ok" is a status token in the tree, not a version. */
    return !(ver[0] == 'o' && ver[1] == 'k' && ver[2] == 0);
}

/* Every record this seat has, section first and then the attached nodes, as
 * one indexable run. Nothing is copied and nothing is held: the list is read
 * where it lies, which is why this card needs no memory of its own to say
 * what the seat was built out of. */
static uint32_t cand_section(void) {
#if defined(__wasm__)
    return 0u;
#else
    if ((uintptr_t)(const void *)__start_pm_metal_externals == 0
        || (uintptr_t)(const void *)__stop_pm_metal_externals == 0) {
        return 0u;
    }
    return (uint32_t)(__stop_pm_metal_externals - __start_pm_metal_externals);
#endif
}

static const pm_metal_external_t *cand(uint32_t i) {
    uint32_t nsec = cand_section();
    pm_metal_external_node_t *n;
#if !defined(__wasm__)
    if (i < nsec) {
        return &__start_pm_metal_externals[i];
    }
#endif
    i -= nsec;
    for (n = s_head; n != NULL; n = n->next) {
        if (i == 0u) {
            return n->rec;
        }
        i--;
    }
    return NULL;
}

static int cand_ok(const pm_metal_external_t *r) {
    return r != NULL && r->name != NULL && r->name[0] != 0 && ver_ok(r->version);
}

int32_t pm_metal_external_attach(pm_metal_external_node_t *node) {
    pm_metal_external_node_t *n;
    if (node == NULL || !cand_ok(node->rec)) {
        return -1;
    }
    for (n = s_head; n != NULL; n = n->next) {
        if (n == node) {
            return 0; /* already on, registered twice */
        }
        if (name_eq(n->rec->name, node->rec->name)) {
            /* One name, one version: two versions of the same library in one
             * seat is a build to fix, not a row to print. */
            return name_eq(n->rec->version, node->rec->version) ? 0 : -1;
        }
    }
    node->next = s_head;
    s_head = node;
    return 0;
}

/* Off the list again. A linked library never leaves, but something loaded at
 * runtime can, and a prove that adds a row has to be able to take it back. */
int32_t pm_metal_external_detach(pm_metal_external_node_t *node) {
    pm_metal_external_node_t **pp;
    if (node == NULL) {
        return -1;
    }
    for (pp = &s_head; *pp != NULL; pp = &(*pp)->next) {
        if (*pp == node) {
            *pp = node->next;
            node->next = NULL;
            return 0;
        }
    }
    return -1;
}

/* Distinct names. A record whose name an earlier one already carries is the
 * same external twice (the section copy and the node beside it, on a seat
 * that has both), counted once. */
uint32_t pm_metal_external_count(void) {
    uint32_t n = 0;
    uint32_t i;
    for (i = 0; cand(i) != NULL; i++) {
        const pm_metal_external_t *r = cand(i);
        uint32_t j;
        int seen = 0;
        if (!cand_ok(r)) {
            continue;
        }
        for (j = 0; j < i; j++) {
            const pm_metal_external_t *e = cand(j);
            if (cand_ok(e) && name_eq(e->name, r->name)) {
                seen = 1;
                break;
            }
        }
        if (!seen) {
            n++;
        }
    }
    return n;
}

/* The i-th external by name, chosen by rank rather than sorted into a buffer:
 * the seat has a handful of these and the tree asks for them one at a time,
 * so the walk costs less than a table would. Asking for a name already taken
 * is how a duplicate drops out. */
static const pm_metal_external_t *nth(uint32_t want) {
    const char *prev = NULL;
    const pm_metal_external_t *best = NULL;
    uint32_t k;
    for (k = 0; k <= want; k++) {
        uint32_t i;
        best = NULL;
        for (i = 0; cand(i) != NULL; i++) {
            const pm_metal_external_t *r = cand(i);
            if (!cand_ok(r)) {
                continue;
            }
            if (prev != NULL && strcmp(r->name, prev) <= 0) {
                continue;
            }
            if (best == NULL || strcmp(r->name, best->name) < 0) {
                best = r;
            }
        }
        if (best == NULL) {
            return NULL;
        }
        prev = best->name;
    }
    return best;
}

const char *pm_metal_external_name(uint32_t i) {
    const pm_metal_external_t *r = nth(i);
    return r != NULL ? r->name : NULL;
}

const char *pm_metal_external_version(uint32_t i) {
    const pm_metal_external_t *r = nth(i);
    return r != NULL ? r->version : NULL;
}

static int32_t pm_metal_boot_externals_init(pm_util_mem_arena_t *arena) {
    (void)arena;
    return 0;
}

static void pm_metal_boot_externals_deinit(void) {}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.boot.externals, pm_metal_external_count, pm_metal_external_count, uint32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.boot.externals, pm_metal_external_name, pm_metal_external_name, const char *(uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.boot.externals, pm_metal_external_version, pm_metal_external_version, const char *(uint32_t));

PM_MOD_BOOT_C(pymergetic.metal.boot.externals, pm_metal_boot_externals_init, pm_metal_boot_externals_deinit);
