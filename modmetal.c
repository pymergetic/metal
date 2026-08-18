/*
 * Builtin pymergetic.metal package shell.
 * Heap is pymergetic.util.mem (impl=c) — C/RS include that face; no
 * pymergetic.metal.mem twin. Real Metal leaves (async, net, …) land here
 * with the same one-lang + faces rule.
 */
#include "extmod/metal/modmetal.h"

#include "extmod/metal/boot.h"
#include "ports/micropython/finder.h"
#include "ports/micropython/importhook.h"
#include "py/nlr.h"
#include "py/obj.h"
#include "py/objtuple.h"
#include "py/runtime.h"
#include "pymergetic/metal/async.h"
#include "pymergetic/metal/boot/externals.h"
#include "pymergetic/metal/boot/tree.h"
#include "pymergetic/metal/util/tree.h"
#include "pymergetic/metal/drivers/__types__.h"
#include "pymergetic/wasmmod/boot.h"
#include "pymergetic/wasmmod/net/cdn.h"

#include <string.h>

#ifndef PM_METAL_UPY_GEN_N
#define PM_METAL_UPY_GEN_N 8
#endif

#ifndef PM_METAL_DRV_PY_MAX
#define PM_METAL_DRV_PY_MAX 4
#endif

MP_REGISTER_ROOT_POINTER(mp_obj_t metal_upy_gen[PM_METAL_UPY_GEN_N]);
MP_REGISTER_ROOT_POINTER(mp_obj_t metal_drv_py_attach[PM_METAL_DRV_PY_MAX]);

typedef struct {
    pm_metal_async_coro_t coro;
    uint32_t slot;
} pm_metal_upy_frame_t;

static void metal_ensure(void) {
    mp_wasm_ensure_inited();
    if (!pm_metal_ready() && pm_metal_boot() != 0) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("metal boot failed"));
    }
}

static mp_obj_t metal___init__(void) {
    metal_ensure();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(metal___init___obj, metal___init__);

static mp_obj_t metal_ready(void) {
    metal_ensure();
    return mp_obj_new_bool(pm_metal_ready());
}
static MP_DEFINE_CONST_FUN_OBJ_0(metal_ready_obj, metal_ready);

static mp_obj_t metal_poll(void) {
    metal_ensure();
    pm_metal_async_poll();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(metal_poll_obj, metal_poll);

static pm_metal_async_status_t step_upy(pm_metal_async_coro_t *self) {
    pm_metal_upy_frame_t *f = (pm_metal_upy_frame_t *)self;
    mp_obj_t gen;
    nlr_buf_t nlr;
    if (f->slot >= PM_METAL_UPY_GEN_N) {
        return PM_METAL_ASYNC_ERROR;
    }
    gen = MP_STATE_VM(metal_upy_gen)[f->slot];
    if (gen == MP_OBJ_NULL) {
        return PM_METAL_ASYNC_ERROR;
    }
    if (nlr_push(&nlr) == 0) {
        mp_obj_t v = mp_iternext(gen);
        nlr_pop();
        if (v == MP_OBJ_STOP_ITERATION) {
            MP_STATE_VM(metal_upy_gen)[f->slot] = MP_OBJ_NULL;
            return PM_METAL_ASYNC_DONE;
        }
        return pm_metal_async_yield_park(self);
    }
    MP_STATE_VM(metal_upy_gen)[f->slot] = MP_OBJ_NULL;
    return PM_METAL_ASYNC_ERROR;
}

static mp_obj_t metal_register_upy(mp_obj_t gen) {
    uint32_t i;
    pm_metal_upy_frame_t *frame;
    metal_ensure();
    if (!pm_metal_ready()) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("metal not ready"));
    }
    for (i = 0; i < PM_METAL_UPY_GEN_N; i++) {
        if (MP_STATE_VM(metal_upy_gen)[i] == MP_OBJ_NULL) {
            break;
        }
    }
    if (i >= PM_METAL_UPY_GEN_N) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("upy gen slots full"));
    }
    frame = (pm_metal_upy_frame_t *)pm_metal_async_coro_create(step_upy, sizeof(*frame));
    if (frame == NULL) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("upy coro"));
    }
    /* step_upy re-enters the bytecode VM (nlr_push + mp_iternext on the global
     * metal_upy_gen root-pointer). Only the boot thread owns the VM; mark the
     * coro so SMP/async runners hand it back instead of stepping it. */
    pm_metal_async_coro_set_vm_only(&frame->coro);
    frame->slot = i;
    MP_STATE_VM(metal_upy_gen)[i] = gen;
    if (pm_metal_async_create_task(&frame->coro) == NULL) {
        MP_STATE_VM(metal_upy_gen)[i] = MP_OBJ_NULL;
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("upy task"));
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(metal_register_upy_obj, metal_register_upy);

#define PM_METAL_DRV_PY_DEF(i) \
    static int32_t metal_drv_py_attach_##i(int32_t bus, uint32_t loc0, uint32_t loc1, uint32_t loc2, \
        uint32_t loc3) { \
        mp_obj_t cb = MP_STATE_VM(metal_drv_py_attach)[i]; \
        nlr_buf_t nlr; \
        if (cb == MP_OBJ_NULL) { \
            return -1; \
        } \
        if (nlr_push(&nlr) == 0) { \
            mp_obj_t args[5] = { \
                mp_obj_new_int(bus), \
                mp_obj_new_int_from_uint(loc0), \
                mp_obj_new_int_from_uint(loc1), \
                mp_obj_new_int_from_uint(loc2), \
                mp_obj_new_int_from_uint(loc3), \
            }; \
            mp_obj_t res = mp_call_function_n_kw(cb, 5, 0, args); \
            int32_t out = (int32_t)mp_obj_get_int(res); \
            nlr_pop(); \
            return out; \
        } \
        return -1; \
    }
PM_METAL_DRV_PY_DEF(0)
PM_METAL_DRV_PY_DEF(1)
PM_METAL_DRV_PY_DEF(2)
PM_METAL_DRV_PY_DEF(3)

static pm_metal_drv_attach_fn s_drv_attach[PM_METAL_DRV_PY_MAX] = {
    metal_drv_py_attach_0,
    metal_drv_py_attach_1,
    metal_drv_py_attach_2,
    metal_drv_py_attach_3,
};
static pm_metal_drv_t s_drv_rec[PM_METAL_DRV_PY_MAX];
static char s_drv_mod[PM_METAL_DRV_PY_MAX][80];
static uint32_t s_drv_n;

static void copy_mod(char *dst, size_t cap, const char *src) {
    size_t n = 0;
    if (src == NULL || cap == 0) {
        return;
    }
    while (src[n] != 0 && n + 1u < cap) {
        dst[n] = src[n];
        n++;
    }
    dst[n] = 0;
}

static int32_t drv_bind(const char *mod, uint32_t kind, uint32_t id0, uint32_t id1, mp_obj_t attach) {
    uint32_t i;
    if (!mp_obj_is_callable(attach)) {
        mp_raise_TypeError(MP_ERROR_TEXT("attach"));
    }
    if (s_drv_n >= PM_METAL_DRV_PY_MAX) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("PM_METAL_DRV slots full"));
    }
    i = s_drv_n++;
    copy_mod(s_drv_mod[i], sizeof(s_drv_mod[i]), mod);
    MP_STATE_VM(metal_drv_py_attach)[i] = attach;
    memset(&s_drv_rec[i], 0, sizeof(s_drv_rec[i]));
    s_drv_rec[i].mod = s_drv_mod[i];
    s_drv_rec[i].kind = kind;
    s_drv_rec[i].id0 = id0;
    s_drv_rec[i].id1 = id1;
    s_drv_rec[i].id2 = PM_METAL_DRV_PCI_ANY;
    s_drv_rec[i].id3 = PM_METAL_DRV_PCI_ANY;
    s_drv_rec[i].bar = PM_METAL_DRV_PCI_ANY;
    s_drv_rec[i].attach = s_drv_attach[i];
    if (pm_metal_drv_add(&s_drv_rec[i]) != 0) {
        MP_STATE_VM(metal_drv_py_attach)[i] = MP_OBJ_NULL;
        s_drv_n--;
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("pm_metal_drv_add"));
    }
    return 0;
}

static mp_obj_t metal_PM_METAL_DRV_PCI(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    metal_ensure();
    drv_bind(mp_obj_str_get_str(args[0]), PM_METAL_DRV_KIND_PCI,
        (uint32_t)mp_obj_get_int(args[1]), (uint32_t)mp_obj_get_int(args[2]), args[3]);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(metal_PM_METAL_DRV_PCI_obj, 4, 4, metal_PM_METAL_DRV_PCI);

static mp_obj_t metal_PM_METAL_DRV_ISA(mp_obj_t mod, mp_obj_t port, mp_obj_t attach) {
    metal_ensure();
    drv_bind(mp_obj_str_get_str(mod), PM_METAL_DRV_KIND_ISA, (uint32_t)mp_obj_get_int(port), 0,
        attach);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(metal_PM_METAL_DRV_ISA_obj, metal_PM_METAL_DRV_ISA);

static mp_obj_t metal_PM_METAL_DRV_PLATFORM(mp_obj_t mod, mp_obj_t attach) {
    metal_ensure();
    drv_bind(mp_obj_str_get_str(mod), PM_METAL_DRV_KIND_PLATFORM, 0, 0, attach);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(metal_PM_METAL_DRV_PLATFORM_obj, metal_PM_METAL_DRV_PLATFORM);

static mp_obj_t mp_metal_builtin_quit(size_t n_args, const mp_obj_t *args) {
    /* Process = task with human intent + pid. System REPL has pid 0. */
    uint32_t here = pm_metal_async_process_id();
    uint32_t want = here;
    if (n_args == 1) {
        want = (uint32_t)mp_obj_get_int(args[0]);
    }
    if (here != 0u && (n_args == 0 || want == here)) {
        mp_raise_type(&mp_type_SystemExit);
    }
    if (here == 0u) {
        if (n_args == 0) {
            mp_printf(&mp_plat_print,
                "quit() no-op: not in a process (pid 0); use shutdown() or reboot()\n");
        } else {
            mp_printf(&mp_plat_print,
                "quit(%u) no-op: not in a process (pid 0); use shutdown() or reboot()\n",
                (unsigned)want);
        }
    } else {
        mp_printf(&mp_plat_print,
            "quit(%u) no-op: not in process %u (here pid %u)\n",
            (unsigned)want, (unsigned)want, (unsigned)here);
    }
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mp_metal_builtin_quit_obj, 0, 1, mp_metal_builtin_quit);

/* Case-insensitive substring: does `hay` contain `needle` (ASCII only)? */
static int contains_ci(const char *hay, const char *needle) {
    size_t nl = strlen(needle);
    if (nl == 0) {
        return 1;
    }
    for (; *hay; hay++) {
        size_t i;
        for (i = 0; i < nl; i++) {
            char a = hay[i];
            char b = needle[i];
            if (a >= 'A' && a <= 'Z') {
                a = (char)(a - 'A' + 'a');
            }
            if (b >= 'A' && b <= 'Z') {
                b = (char)(b - 'A' + 'a');
            }
            if (a != b || a == '\0') {
                break;
            }
        }
        if (i == nl && hay[nl - 1] != '\0') {
            return 1;
        }
    }
    return 0;
}

/* Does the package entry (a µPy dict) carry artifacts satisfying kind/arch?
 * Mirrors the RS search card exactly: kind_ok and arch_ok accumulate
 * independently across the whole `artifacts` array (a pack matches when it
 * has some kind-matching artifact AND some arch-matching artifact), then the
 * AND is taken. NULL filters are treated as already-ok. */
static int entry_artifacts_match(mp_obj_t entry, const char *kind, const char *arch) {
    mp_map_elem_t *art;
    mp_obj_t artifacts;
    mp_obj_t it;
    int kind_ok;
    int arch_ok;
    if (kind == NULL && arch == NULL) {
        return 1;
    }
    if (mp_obj_get_type(entry) != &mp_type_dict) {
        return 0;
    }
    art = mp_map_lookup(mp_obj_dict_get_map(entry),
        MP_OBJ_NEW_QSTR(qstr_from_str("artifacts")), MP_MAP_LOOKUP);
    if (art == NULL) {
        return 0;
    }
    artifacts = art->value;
    if (!mp_obj_is_type(artifacts, &mp_type_list)) {
        return 0;
    }
    kind_ok = (kind == NULL) ? 1 : 0;
    arch_ok = (arch == NULL) ? 1 : 0;
    it = mp_getiter(artifacts, NULL);
    while (it) {
        mp_obj_t a = mp_iternext(it);
        if (a == MP_OBJ_STOP_ITERATION) {
            break;
        }
        if (mp_obj_get_type(a) != &mp_type_dict) {
            continue;
        }
        if (!kind_ok) {
            mp_map_elem_t *kv = mp_map_lookup(mp_obj_dict_get_map(a),
                MP_OBJ_NEW_QSTR(qstr_from_str("kind")), MP_MAP_LOOKUP);
            if (kv != NULL && mp_obj_is_str(kv->value)
                && strcmp(mp_obj_str_get_str(kv->value), kind) == 0) {
                kind_ok = 1;
            }
        }
        if (!arch_ok) {
            mp_map_elem_t *av = mp_map_lookup(mp_obj_dict_get_map(a),
                MP_OBJ_NEW_QSTR(qstr_from_str("arch")), MP_MAP_LOOKUP);
            if (av != NULL && mp_obj_is_str(av->value)
                && strcmp(mp_obj_str_get_str(av->value), arch) == 0) {
                arch_ok = 1;
            }
        }
        if (kind_ok && arch_ok) {
            break;
        }
    }
    return kind_ok && arch_ok;
}

/* CDN pack discovery on pymergetic.metal, folded to a µPy-native fill:
 *   packages()            local packs + CDN bases (the REPL tree)
 *   packages_catalog(...) full CDN index
 *   packages_search(q)    pack names containing q
 *   packages_filter(...)  filter by prefix/name/kind/arch
 * Every µPy seat has the json module and cdn.fetch_index, so this fold is a
 * µPy-native fill over the same CDN index — no serde is dragged into any µPy
 * or firmware build. The Rust net.search card stays the C/RS host face. */

/* Fetch the CDN index for `channel` and return the parsed `packages` dict. */
static mp_obj_t packages_load_channel(const char *channel) {
    mp_obj_t pg = mp_import_name(qstr_from_str("pymergetic"), mp_const_none,
        MP_OBJ_NEW_SMALL_INT(0));
    mp_obj_t wm = mp_load_attr(pg, qstr_from_str("wasmmod"));
    mp_obj_t net = mp_load_attr(wm, qstr_from_str("net"));
    mp_obj_t cdnmod = mp_load_attr(net, qstr_from_str("cdn"));
    mp_obj_t fetch = mp_load_attr(cdnmod, qstr_from_str("fetch_index"));
    mp_obj_t bytes = mp_call_function_1(fetch, mp_obj_new_str(channel, strlen(channel)));
    mp_obj_t json_mod = mp_import_name(qstr_from_str("json"), mp_const_none,
        MP_OBJ_NEW_SMALL_INT(0));
    mp_obj_t loads = mp_load_attr(json_mod, qstr_from_str("loads"));
    mp_obj_t doc = mp_call_function_1(loads, bytes);
    return mp_obj_dict_get(doc, MP_OBJ_NEW_QSTR(qstr_from_str("packages")));
}

/* Print every name in the packages dict that survives the given filters.
 * A NULL filter is treated as already-ok (mirrors the RS search card). */
/* Node in the dotted-FQN print tree. Equal names coalesce (a package and the
 * folder that parents deeper packs share their line). */
typedef struct pkg_tree_node {
    struct pkg_tree_node *parent;
    struct pkg_tree_node *next;      /* sibling */
    struct pkg_tree_node *child;     /* first child */
    char label[48];                  /* one dot-segment, NUL-terminated */
    bool has_pack;                   /* a package ends exactly at this node */
    bool is_last;                    /* last sibling (drives "`--" vs "+--") */
} pkg_tree_node_t;

static pkg_tree_node_t *pkg_tree_find_child(pkg_tree_node_t *n, const char *label) {
    for (pkg_tree_node_t *c = n->child; c != NULL; c = c->next) {
        if (strcmp(c->label, label) == 0) {
            return c;
        }
    }
    return NULL;
}

static pkg_tree_node_t *pkg_tree_insert(pkg_tree_node_t *root, const char *fqn, size_t fqn_len) {
    pkg_tree_node_t *cur = root;
    const char *p = fqn;
    const char *end = fqn + fqn_len;
    (void)end;
    while (p[0] != 0) {
        const char *dot = strchr(p, '.');
        size_t plen = (dot == NULL) ? strlen(p) : (size_t)(dot - p);
        char seg[48];
        if (plen >= sizeof(seg)) {
            plen = sizeof(seg) - 1u;
        }
        memcpy(seg, p, plen);
        seg[plen] = 0;
        pkg_tree_node_t *c = pkg_tree_find_child(cur, seg);
        if (c == NULL) {
            c = m_new_obj(pkg_tree_node_t);
            c->parent = cur;
            c->next = NULL;
            c->child = NULL;
            memcpy(c->label, seg, plen + 1u);
            c->has_pack = false;
            c->is_last = false;
            /* Append to the sibling list. */
            pkg_tree_node_t **tail = &cur->child;
            while (*tail != NULL) {
                tail = &(*tail)->next;
            }
            *tail = c;
        }
        p += plen;
        if (p[0] == '.') {
            p++;
        }
        cur = c;
    }
    cur->has_pack = true;
    return cur;
}

static void pkg_tree_mark_last(pkg_tree_node_t *root) {
    size_t count = 0;
    for (pkg_tree_node_t *c = root->child; c != NULL; c = c->next) {
        count++;
    }
    size_t i = 0;
    for (pkg_tree_node_t *c = root->child; c != NULL; c = c->next) {
        c->is_last = (++i == count);
        pkg_tree_mark_last(c);
    }
}

/* Recursively render the tree through the shared boot-tree renderer
 * (pymergetic.metal.util.tree). `stems` is the exact leading bar run for this
 * node (a 4-char cell per ancestor: "|   " when that ancestor still has a
 * sibling to come, "    " when it is last), so non-continuing ancestors draw
 * blank bars exactly like a hand-drawn box tree — something the boot tree's
 * single `parent_cont` (deepest-stem-only) model cannot express below depth 2.
 * `depth` counts from 1 for the children of the reported `cdn` root. */
static void pkg_tree_print(pkg_tree_node_t *root, char *stems, unsigned stems_len) {
    for (pkg_tree_node_t *c = root->child; c != NULL; c = c->next) {
        pm_metal_util_tree_item_at(c->is_last, stems, c->label, NULL);
        /* Descend: extend the stem with this node's own continuation cell. */
        char child_stems[192];
        unsigned cl = stems_len;
        unsigned i;
        for (i = 0; i < stems_len && i < sizeof(child_stems) - 4u; i++) {
            child_stems[i] = stems[i];
        }
        const char *cell = c->is_last ? "    " : "|   ";
        for (i = 0; cell[i] != 0 && cl < sizeof(child_stems) - 1u; i++) {
            child_stems[cl++] = cell[i];
        }
        child_stems[cl] = 0;
        if (c->child != NULL) {
            pkg_tree_print(c, child_stems, cl);
        }
    }
}

/* True when the CDN index entry for a package carries yanked=true. The CDN
 * catalog hides yanked packs (the legacy bare "metal" name, renamed to
 * pymergetic.metal.arch.*, is yanked upstream) — the REPL mirrors that. */
static bool package_is_yanked(mp_obj_t entry) {
    if (mp_obj_get_type(entry) != &mp_type_dict) {
        return false;
    }
    mp_map_elem_t *it = mp_map_lookup(mp_obj_dict_get_map(entry),
        MP_OBJ_NEW_QSTR(qstr_from_str("yanked")), MP_MAP_LOOKUP);
    if (it == NULL) {
        return false;
    }
    return mp_obj_is_true(it->value);
}

/* Print the dotted-FQN index as a natural tree (pymergetic → metal → arch →
 * x86_64), mirroring the CDN's web nav. Filters mimic the RS search card
 * (NULL = already-ok); yanked packs are suppressed like the CDN catalog. */
static void packages_print_matches(mp_obj_t packages,
    const char *prefix, const char *name_contains, const char *q,
    const char *kind, const char *arch) {
    mp_map_t *map = mp_obj_dict_get_map(packages);
    pkg_tree_node_t root;
    memset(&root, 0, sizeof(root));
    for (size_t idx = 0; idx < map->alloc; idx++) {
        if (!mp_map_slot_is_filled(map, idx)) {
            continue;
        }
        const char *name = mp_obj_str_get_str(map->table[idx].key);
        if (prefix != NULL && strncmp(name, prefix, strlen(prefix)) != 0) {
            continue;
        }
        if (name_contains != NULL && !contains_ci(name, name_contains)) {
            continue;
        }
        if (q != NULL && !contains_ci(name, q)) {
            continue;
        }
        if ((kind != NULL || arch != NULL)
            && !entry_artifacts_match(map->table[idx].value, kind, arch)) {
            continue;
        }
        if (package_is_yanked(map->table[idx].value)) {
            continue; /* yanked upstream (e.g. bare "metal"); CDN catalog hides them */
        }
        pkg_tree_insert(&root, name, strlen(name));
    }
    bool any = (root.child != NULL);
    if (!any) {
        pm_metal_util_tree_item(1, 0, 0, "cdn", NULL);
        pm_metal_util_tree_line("-  (no match)");
        return;
    }
    /* Root `cdn` item, then the pack namespace tree starting at column 0. */
    pkg_tree_mark_last(&root);
    pm_metal_util_tree_item(1, 0, 0, "cdn", NULL);
    /* `cdn` is drawn as the last depth-0 root ("`-- cdn"), so its children
     * sit one empty cell in (no vertical bar continues beneath it) at depth 1,
     * exactly matching the boot tree's depth-0/depth-1 indentation. */
    char roots[8];
    memcpy(roots, "    ", 4u);
    roots[4] = 0;
    pkg_tree_print(&root, roots, 4);
}

static mp_obj_t mp_metal_builtin_packages(void) {
    uint32_t n = mp_wasm_local_pack_count();
    uint32_t nbase = pm_wasmmod_net_cdn_base_count();

    metal_ensure();

    mp_printf(&mp_plat_print, "+-- local        %u pack(s)\n", (unsigned)n);
    for (uint32_t i = 0; i < n; i++) {
        const char *name = mp_wasm_local_pack_name(i);
        if (name != NULL && name[0] != 0) {
            mp_printf(&mp_plat_print, "|   %s %s\n", (i + 1u == n) ? "`--" : "+--", name);
        }
    }
    if (nbase == 0u) {
        mp_printf(&mp_plat_print, "`-- cdn          -\n");
    } else {
        mp_printf(&mp_plat_print, "`-- cdn          %u base(s)\n", (unsigned)nbase);
        for (uint32_t i = 0; i < nbase; i++) {
            const char *b = pm_wasmmod_net_cdn_base_at(i);
            if (b != NULL && b[0] != 0) {
                mp_printf(&mp_plat_print, "    %s %s\n", (i + 1u == nbase) ? "`--" : "+--", b);
            }
        }
    }
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_0(mp_metal_builtin_packages_obj, mp_metal_builtin_packages);

static mp_obj_t mp_metal_builtin_packages_catalog(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
static const mp_arg_t allowed_args[] = {
    { MP_QSTR_channel, MP_ARG_OBJ, { .u_rom_obj = MP_ROM_NONE } },
};
mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

metal_ensure();
mp_obj_t channel_obj = (args[0].u_obj == mp_const_none)
    ? mp_obj_new_str("lead", 4) : args[0].u_obj;
const char *channel = mp_obj_str_get_str(channel_obj);
packages_print_matches(packages_load_channel(channel), NULL, NULL, NULL, NULL, NULL);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_KW(mp_metal_builtin_packages_catalog_obj, 0, mp_metal_builtin_packages_catalog);

static mp_obj_t mp_metal_builtin_packages_search(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
static const mp_arg_t allowed_args[] = {
        { MP_QSTR_q, MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_rom_obj = MP_ROM_NONE } },
    { MP_QSTR_channel, MP_ARG_OBJ, { .u_rom_obj = MP_ROM_NONE } },
};
mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

metal_ensure();
if (!mp_obj_is_str(args[0].u_obj)) {
    mp_raise_TypeError(MP_ERROR_TEXT("packages_search(q): q must be str"));
}
const char *q = mp_obj_str_get_str(args[0].u_obj);
mp_obj_t channel_obj = (args[1].u_obj == mp_const_none)
    ? mp_obj_new_str("lead", 4) : args[1].u_obj;
const char *channel = mp_obj_str_get_str(channel_obj);
    packages_print_matches(packages_load_channel(channel), NULL, NULL, q, NULL, NULL);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_KW(mp_metal_builtin_packages_search_obj, 0, mp_metal_builtin_packages_search);

static mp_obj_t mp_metal_builtin_packages_filter(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
static const mp_arg_t allowed_args[] = {
    { MP_QSTR_prefix, MP_ARG_OBJ, { .u_rom_obj = MP_ROM_NONE } },
    { MP_QSTR_name_contains, MP_ARG_OBJ, { .u_rom_obj = MP_ROM_NONE } },
    { MP_QSTR_kind, MP_ARG_OBJ, { .u_rom_obj = MP_ROM_NONE } },
    { MP_QSTR_arch, MP_ARG_OBJ, { .u_rom_obj = MP_ROM_NONE } },
    { MP_QSTR_channel, MP_ARG_OBJ, { .u_rom_obj = MP_ROM_NONE } },
};
mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

metal_ensure();
const char *prefix = (args[0].u_obj == mp_const_none)
    ? NULL : mp_obj_str_get_str(args[0].u_obj);
const char *name_contains = (args[1].u_obj == mp_const_none)
    ? NULL : mp_obj_str_get_str(args[1].u_obj);
const char *kind = (args[2].u_obj == mp_const_none)
    ? NULL : mp_obj_str_get_str(args[2].u_obj);
const char *arch = (args[3].u_obj == mp_const_none)
    ? NULL : mp_obj_str_get_str(args[3].u_obj);
mp_obj_t channel_obj = (args[4].u_obj == mp_const_none)
    ? mp_obj_new_str("lead", 4) : args[4].u_obj;
const char *channel = mp_obj_str_get_str(channel_obj);
    packages_print_matches(packages_load_channel(channel), prefix, name_contains, NULL, kind, arch);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_KW(mp_metal_builtin_packages_filter_obj, 0, mp_metal_builtin_packages_filter);

static mp_obj_t mp_metal_builtin_help(void) {
    mp_printf(&mp_plat_print,
        "MetalPython system REPL (not a process; quit/exit are no-ops here)\n"
        "  packages()     guest packs + CDN bases\n"
        "  packages_catalog(channel=...)     full CDN index (µPy json on cdn.fetch_index)\n"
        "  packages_search(q, channel=...)   pack names containing q\n"
        "  packages_filter(prefix=..., name_contains=..., kind=..., arch=..., channel=...)\n"
        "            filter CDN packs by prefix/name/kind/arch\n"
        "  help()         this text\n"
        "  quit([pid]) / exit([pid])  SystemExit in a process; at pid 0 use shutdown()/reboot()\n"
        "  reboot()       reboot the seat\n"
        "  shutdown()     halt the seat\n"
        "  process()      booted module FQNs\n");
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_0(mp_metal_builtin_help_obj, mp_metal_builtin_help);

static mp_obj_t mp_metal_builtin_reboot(void) {
    pm_metal_boot_shutdown(1);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_0(mp_metal_builtin_reboot_obj, mp_metal_builtin_reboot);

static mp_obj_t mp_metal_builtin_shutdown(void) {
    pm_metal_boot_shutdown(0);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_0(mp_metal_builtin_shutdown_obj, mp_metal_builtin_shutdown);

static mp_obj_t mp_metal_builtin_process(void) {
    uint32_t n;
    uint32_t i;
    mp_obj_t *items;
    metal_ensure();
    n = pm_mod_boot_count();
    if (n == 0) {
        return mp_obj_new_tuple(0, NULL);
    }
    items = m_new(mp_obj_t, n);
    for (i = 0; i < n; i++) {
        const char *fqn = pm_mod_boot_fqn(i);
        if (fqn == NULL) {
            fqn = "";
        }
        items[i] = mp_obj_new_str(fqn, strlen(fqn));
    }
    return mp_obj_new_tuple(n, items);
}
MP_DEFINE_CONST_FUN_OBJ_0(mp_metal_builtin_process_obj, mp_metal_builtin_process);

static const mp_rom_map_elem_t mp_module_pymergetic_metal_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal) },
    { MP_ROM_QSTR(MP_QSTR___init__), MP_ROM_PTR(&metal___init___obj) },
    { MP_ROM_QSTR(MP_QSTR_ready), MP_ROM_PTR(&metal_ready_obj) },
    { MP_ROM_QSTR(MP_QSTR_poll), MP_ROM_PTR(&metal_poll_obj) },
    { MP_ROM_QSTR(MP_QSTR_register_upy), MP_ROM_PTR(&metal_register_upy_obj) },
    { MP_ROM_QSTR(MP_QSTR_PM_METAL_DRV_PCI), MP_ROM_PTR(&metal_PM_METAL_DRV_PCI_obj) },
    { MP_ROM_QSTR(MP_QSTR_PM_METAL_DRV_ISA), MP_ROM_PTR(&metal_PM_METAL_DRV_ISA_obj) },
    { MP_ROM_QSTR(MP_QSTR_PM_METAL_DRV_PLATFORM), MP_ROM_PTR(&metal_PM_METAL_DRV_PLATFORM_obj) },
    { MP_ROM_QSTR(MP_QSTR_packages), MP_ROM_PTR(&mp_metal_builtin_packages_obj) },
    { MP_ROM_QSTR(MP_QSTR_packages_catalog), MP_ROM_PTR(&mp_metal_builtin_packages_catalog_obj) },
    { MP_ROM_QSTR(MP_QSTR_packages_search), MP_ROM_PTR(&mp_metal_builtin_packages_search_obj) },
    { MP_ROM_QSTR(MP_QSTR_packages_filter), MP_ROM_PTR(&mp_metal_builtin_packages_filter_obj) },
    { MP_ROM_QSTR(MP_QSTR_help), MP_ROM_PTR(&mp_metal_builtin_help_obj) },
};
static MP_DEFINE_CONST_DICT(mp_module_pymergetic_metal_globals, mp_module_pymergetic_metal_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_pymergetic_metal_globals,
};

#if MICROPY_MODULE_ATTR_DELEGATION
/* Fixed ROM dict cannot hold child cards; the registry resolves them. */
MP_REGISTER_MODULE_DELEGATION(mp_module_pymergetic_metal, mp_wasm_pymergetic_attr);
#endif
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal, mp_module_pymergetic_metal);

PM_METAL_EXTERNAL_C(micropython, MICROPY_VERSION_STRING);
