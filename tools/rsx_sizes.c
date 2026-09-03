/* rsx_sizes.c — layout audit for the rsx compiler's resident state: the
 * exact sizeof of Lower, FnTab, LocalTab and every nested table, plus the
 * stack cost of one by-value Lower temporary in the compile entry. The
 * struct definitions are copied byte-for-byte from __impl__.rs's C face
 * (the layout the self-host gen-1 C declares); the audit fails if the
 * source constants and this copy drift. tools/ posture: not a prove gate. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* __impl__.rs table capacities */
enum {
    SYM_CAP = 512,
    FPC = 64,
    FN_MAXP = 8,
    NT_CAP = 16,
    OPT_CAP = 8,
    ST_CAP = 64,
    TUP_CAP = 8,
    TUP_MAXF = 4,
    TYD_CAP = 96,
    LOCAL_CAP = 256
};

typedef struct {
    const uint8_t *names[SYM_CAP];
    size_t name_lens[SYM_CAP];
    uint32_t field_counts[SYM_CAP];
    const uint8_t *field_names[SYM_CAP * FPC];
    size_t field_name_lens[SYM_CAP * FPC];
    const uint8_t *field_ctypes[SYM_CAP * FPC];
    size_t field_ctype_lens[SYM_CAP * FPC];
    bool used[SYM_CAP];
} SymTab;

typedef struct {
    const uint8_t *names[SYM_CAP];
    size_t name_lens[SYM_CAP];
    const uint8_t *rets[SYM_CAP];
    size_t ret_lens[SYM_CAP];
    uint32_t n_params[SYM_CAP];
    bool used[SYM_CAP];
    const uint8_t *params[SYM_CAP * FN_MAXP];
    size_t param_lens[SYM_CAP * FN_MAXP];
    void *arena; /* pm_util_mem_arena_t * — pointer */
    bool oom;
} FnTab;

typedef struct {
    uint8_t names[8][48];
    size_t name_lens[8];
    uint64_t vals[8];
    size_t n;
} ConstTab;

typedef struct {
    uint8_t names[8][64];
    size_t name_lens[8];
    uint64_t vals[8];
    size_t n;
} EnumTab;

typedef struct {
    void *arena;
    uint8_t *p;
    size_t len;
    size_t cap;
    bool ok;
} Out;

typedef struct {
    void *arena;
    SymTab *syms;
    Out out;
    uint8_t *errbuf;
    size_t errcap;
    uint32_t errline;
    bool ok;
    FnTab *fns;
    ConstTab consts;
    EnumTab enums;
    size_t depth;
    const uint8_t *cur_impl[16];
    size_t cur_impl_lens[16];
    size_t cur_impl_n;
    uint8_t recv_type[64];
    size_t recv_len;
    uint8_t cur_ret[128];
    size_t cur_ret_len;
    uint8_t oom_buf[160];
    uint32_t nerrs;
    uint8_t nt_names[NT_CAP][48];
    size_t nt_lens[NT_CAP];
    size_t nt_n;
    uint8_t cur_opt_elem[96];
    size_t cur_opt_elem_len;
    uint8_t opt_elems[OPT_CAP][64];
    size_t opt_lens[OPT_CAP];
    bool opt_done[OPT_CAP];
    size_t opt_n;
    bool types_done;
    uint8_t tup_elems[TUP_CAP * TUP_MAXF][64];
    size_t tup_lens[TUP_CAP * TUP_MAXF];
    size_t tup_counts[TUP_CAP];
    bool tup_done[TUP_CAP];
    size_t tup_n;
    size_t tup_tmp_n;
    size_t atom_tmp_n;
    uint8_t opq_names[24][48];
    size_t opq_lens[24];
    size_t opq_n;
    size_t opq_flushed;
    uint8_t st_names[ST_CAP][64];
    size_t st_name_lens[ST_CAP];
    uint8_t st_cts[ST_CAP][128];
    size_t st_ct_lens[ST_CAP];
    size_t st_n;
    uint8_t tydone_names[TYD_CAP][48];
    size_t tydone_lens[TYD_CAP];
    size_t tydone_n;
    size_t tyorder_depth;
} Lower;

typedef struct {
    uint8_t names[LOCAL_CAP][48];
    size_t name_lens[LOCAL_CAP];
    const uint8_t *ctypes[LOCAL_CAP];
    size_t ctype_lens[LOCAL_CAP];
    size_t depths[LOCAL_CAP];
    size_t epochs[LOCAL_CAP];
    size_t marks[64];
    size_t nmarks;
    size_t n;
    size_t epoch;
    void *arena;
    bool oom;
} LocalTab;

int main(void) {
    printf("sizeof(Lower)     = %zu (%.1f KiB)\n", sizeof(Lower), (double)sizeof(Lower) / 1024.0);
    printf("sizeof(FnTab)     = %zu\n", sizeof(FnTab));
    printf("sizeof(LocalTab)  = %zu (%.1f KiB)\n", sizeof(LocalTab), (double)sizeof(LocalTab) / 1024.0);
    printf("sizeof(SymTab)    = %zu (%.1f KiB)\n", sizeof(SymTab), (double)sizeof(SymTab) / 1024.0);
    printf("sizeof(Out)       = %zu\n", sizeof(Out));
    printf("sizeof(ConstTab)  = %zu\n", sizeof(ConstTab));
    printf("sizeof(EnumTab)   = %zu\n", sizeof(EnumTab));
    printf("Lower.tup_elems   = %zu\n", sizeof(((Lower *)0)->tup_elems));
    printf("Lower.opt_elems   = %zu\n", sizeof(((Lower *)0)->opt_elems));
    printf("Lower.st_cts      = %zu\n", sizeof(((Lower *)0)->st_cts));
    printf("Lower.nt_names    = %zu\n", sizeof(((Lower *)0)->nt_names));
    printf("LocalTab.names    = %zu\n", sizeof(((LocalTab *)0)->names));
    /* Phase 1 posture: Lower, FnTab, SymTab and every LocalTab are
     * arena-resident blocks behind pointers — none of these sizeof values
     * costs native stack anymore. The numbers stay as the arena budget a
     * compile draws: Lower + FnTab + SymTab + one LocalTab per body. */
    printf("arena draw per compile = Lower %zu + FnTab %zu + SymTab %zu + 1 LocalTab %zu\n",
           sizeof(Lower), sizeof(FnTab), sizeof(SymTab), sizeof(LocalTab));
    return 0;
}
