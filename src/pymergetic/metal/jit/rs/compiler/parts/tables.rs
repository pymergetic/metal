/* ================= lowering (AST -> C) =================
 *
 * Two passes over the file's items:
 *   pass 1 collects type information (struct fields, fn signatures,
 *        enum variants, extern decls, statics) into tables;
 *   pass 2 emits C in dependency order: aggregates/typedefs, extern
 *        prototypes, statics, fn prototypes, fn bodies.
 * Type tables are fixed-size open addressing hashes — this file is its own
 * subset input (no generics, no closures), so sizes are compile-time
 * constants. */

const SYM_CAP: usize = 512;
/* fields per struct — the Lower struct itself has 43 (grew with the tuple
 * support's six state fields); the cap must carry the compiler's own
 * shape or the self-host prove fails field inference past slot 40. */
/* max fields per struct — Lower itself is the ceiling case (self-host
 * proves the cap every compile: 97 fields as of the Default plane), so
 * the cap must clear the compiler's own shape with headroom to spare. */
const FPC: usize = 128;
/* params per fn whose C types the FnTab records (for `None` args). */
const FN_MAXP: usize = 8;

/* Transparent newtype registry cap (single-field generic tuple structs). */
const NT_CAP: usize = 16;
const OPT_CAP: usize = 24;
const ST_CAP: usize = 64;
/* Tuple signature cap: distinct (A, B, ..) spellings per unit. Tuples
 * render as named structs rsx_tuple_<sig>; the table mirrors the Option
 * payload table (register idempotent, emit once in the preamble). 48:
 * pymergetic.util.gen's own face names 30+ distinct signatures (the
 * collect pass renders every struct field and fn sig tuple before the
 * bodies), and the self-host spine stays under 40. */
const TUP_CAP: usize = 48;
const TUP_MAXF: usize = 4;
/* Result payload-pair cap: distinct (T, E) spellings per unit. Results
 * render as named structs rsx_res_<T>_<E>; the table mirrors the Option
 * payload table (register idempotent, emit once per unit). */
const RES_CAP: usize = 24;
/* Emitted-type set cap (dependency-ordered struct pass). One entry per
 * struct/union/alias emitted this unit — 96 covers a card's types plus
 * the appended face's. */
const TYD_CAP: usize = 96;
/* Trait-object plane: declared traits per unit and methods per trait.
 * A trait decl lowers ONE C typedef — `typedef struct { ret (*m)(..);
 * .. } Name;` — the vtable inlined as fields; `&mut dyn Name` in a
 * signature renders as `Name *` and a method call through it renders
 * `p->m(p, args..)`. Small caps: the units this compiler carries declare
 * a handful of traits with a handful of methods each. */
const TRAIT_CAP: usize = 8;
const TRAIT_MCAP: usize = 12;
const TRAIT_SIG: usize = 192;
/* trait impl pairs per unit (Type, Trait) — enough for the unit's impls. */
const TI_CAP: usize = 16;

/* A struct record: name, fields, C types of fields. Field names and C
 * types are arena spans (NUL-terminated copies), so the table stores
 * pointers, not inline arrays — this file's own structs carry up to 15
 * fields and the inline 8-slot table could not hold them. */
struct SymTab {
    names: [*const u8; SYM_CAP],
    name_lens: [usize; SYM_CAP],
    field_counts: [u32; SYM_CAP],
    field_names: [*const u8; SYM_CAP * FPC],
    field_name_lens: [usize; SYM_CAP * FPC],
    field_ctypes: [*const u8; SYM_CAP * FPC],
    field_ctype_lens: [usize; SYM_CAP * FPC],
    used: [bool; SYM_CAP],
}

impl SymTab {
    unsafe fn new(arena: *mut pm_util_mem_arena_t) -> *mut SymTab {
        let p = unsafe { pm_util_mem_alloc(arena, core::mem::size_of::<SymTab>()) } as *mut SymTab;
        if p.is_null() {
            return p;
        }
        /* tlsf does not zero — clear the block so every table starts
         * empty (used=false, counts 0, pointers NULL). */
        unsafe {
            let zb = p as *mut u8;
            let n = core::mem::size_of::<SymTab>();
            let mut i = 0usize;
            while i < n {
                *zb.add(i) = 0;
                i += 1;
            }
        }
        p
    }

    /* Copy len bytes into a fresh arena block (NUL-terminated). */
    unsafe fn span(&self, arena: *mut pm_util_mem_arena_t, src: *const u8, len: usize) -> *const u8 {
        let p = unsafe { pm_util_mem_alloc(arena, len + 1) };
        if p.is_null() {
            return b"\0".as_ptr();
        }
        unsafe {
            core::ptr::copy_nonoverlapping(src, p, len);
            *p.add(len) = 0;
        }
        p
    }

    /* Length-aware compare — stored names point into the source and are
     * not NUL-terminated, so z_eq (which needs a terminator) cannot be
     * used here. */
    unsafe fn zprefix_eq(&self, s: usize, len: usize, name: *const u8) -> bool {
        if self.name_lens[s] != len {
            return false;
        }
        let z = self.names[s];
        let mut i = 0usize;
        while i < len {
            if unsafe { *z.add(i) } != unsafe { *name.add(i) } {
                return false;
            }
            i += 1;
        }
        true
    }

    unsafe fn slot(&self, name: *const u8, len: usize) -> usize {
        let mut h: usize = 5381;
        let mut i = 0usize;
        let mut probe = 0usize;
        while i < len && i < 64 {
            h = h.wrapping_mul(33).wrapping_add(unsafe { *name.add(i) } as usize);
            i += 1;
        }
        probe = h % SYM_CAP;
        i = 0;
        while i < SYM_CAP {
            let s = (probe + i) % SYM_CAP;
            if !self.used[s] {
                return s;
            }
            if unsafe { self.zprefix_eq(s, len, name) } {
                return s;
            }
            i += 1;
        }
        SYM_CAP
    }

    unsafe fn add(&mut self, name: *const u8, len: usize) -> usize {
        let s = unsafe { self.slot(name, len) };
        if s >= SYM_CAP {
            return s;
        }
        self.names[s] = name;
        self.name_lens[s] = len;
        self.used[s] = true;
        self.field_counts[s] = 0;
        s
    }

    unsafe fn add_field(&mut self, arena: *mut pm_util_mem_arena_t, s: usize, fname: *const u8, flen: usize, ct: *const u8, ctlen: usize) {
        let n = self.field_counts[s] as usize;
        if s >= SYM_CAP || n >= FPC || flen == 0 || ctlen == 0 {
            return;
        }
        let dst = s * FPC + n;
        self.field_names[dst] = unsafe { self.span(arena, fname, flen) };
        self.field_name_lens[dst] = flen;
        self.field_ctypes[dst] = unsafe { self.span(arena, ct, ctlen) };
        self.field_ctype_lens[dst] = ctlen;
        self.field_counts[s] += 1;
    }

    unsafe fn find(&self, name: *const u8, len: usize) -> usize {
        let s = unsafe { self.slot(name, len) };
        if s < SYM_CAP && self.used[s] {
            return s;
        }
        SYM_CAP
    }

    /* Field C type into out (NUL-terminated); 0 = not found. */
    unsafe fn field_ctype(&self, s: usize, fname: *const u8, flen: usize, out: *mut u8) -> usize {
        let n = self.field_counts[s] as usize;
        let mut i = 0usize;
        while i < n {
            let d = s * FPC + i;
            if self.field_name_lens[d] == flen && unsafe { z_eq(self.field_names[d], flen, fname) } {
                let cl = self.field_ctype_lens[d];
                let src = self.field_ctypes[d];
                let mut j = 0usize;
                while j < cl {
                    unsafe {
                        *out.add(j) = *src.add(j);
                    }
                    j += 1;
                }
                unsafe {
                    *out.add(cl) = 0;
                }
                return cl;
            }
            i += 1;
        }
        0
    }
}

/* A fn record: name, return C type, n params. */
struct FnTab {
    names: [*const u8; SYM_CAP],
    name_lens: [usize; SYM_CAP],
    /* Return and param C types are arena spans (NUL-terminated copies),
     * not inline arrays: tuple/Option typedef names are unbounded by any
     * fixed slot — a span grows to the name, exactly like SymTab's field
     * ctypes. NULL/0 = unknown. */
    rets: [*const u8; SYM_CAP],
    ret_lens: [usize; SYM_CAP],
    n_params: [u32; SYM_CAP],
    used: [bool; SYM_CAP],
    /* param C types (rendered), up to FN_MAXP per fn: a `None` argument
     * needs the param's Option shape — pointer-Option renders NULL,
     * struct-shaped renders the rsx_opt_<elem> zero literal — and the
     * only source of that shape is the callee's signature. */
    params: [*const u8; SYM_CAP * FN_MAXP],
    param_lens: [usize; SYM_CAP * FN_MAXP],
    /* arena for the span copies (FnTab lives inside the arena-resident
     * Lower — the spans must outlive it, so they live in the arena). */
    arena: *mut pm_util_mem_arena_t,
    /* set when a span allocation refuses — the owning Lower turns it into
     * the immediate "arena exhausted" refusal instead of compiling on with
     * unknown param/return types. */
    oom: bool,
}

impl FnTab {
    /* Arena-resident like its owning Lower: an ~84 KiB table must never be
     * built by-value (that is a native-stack temporary of the same size).
     * Zeroed block, armed arena; NULL = arena refused. */
    unsafe fn new(arena: *mut pm_util_mem_arena_t) -> *mut FnTab {
        let p = unsafe { pm_util_mem_alloc(arena, core::mem::size_of::<FnTab>()) } as *mut FnTab;
        if p.is_null() {
            return p;
        }
        unsafe {
            let zb = p as *mut u8;
            let n = core::mem::size_of::<FnTab>();
            let mut i = 0usize;
            while i < n {
                *zb.add(i) = 0;
                i += 1;
            }
            (*p).arena = arena;
        }
        p
    }

    /* Length-aware compare — stored names point into the source and are
     * not NUL-terminated, so z_eq (which needs a terminator) cannot be
     * used here. */
    unsafe fn zprefix_eq(&self, s: usize, len: usize, name: *const u8) -> bool {
        if self.name_lens[s] != len {
            return false;
        }
        let z = self.names[s];
        let mut i = 0usize;
        while i < len {
            if unsafe { *z.add(i) } != unsafe { *name.add(i) } {
                return false;
            }
            i += 1;
        }
        true
    }

    unsafe fn slot(&self, name: *const u8, len: usize) -> usize {
        let mut h: usize = 5381;
        let mut i = 0usize;
        let mut probe = 0usize;
        while i < len && i < 64 {
            h = h.wrapping_mul(33).wrapping_add(unsafe { *name.add(i) } as usize);
            i += 1;
        }
        probe = h % SYM_CAP;
        i = 0;
        while i < SYM_CAP {
            let s = (probe + i) % SYM_CAP;
            if !self.used[s] {
                return s;
            }
            if unsafe { self.zprefix_eq(s, len, name) } {
                return s;
            }
            i += 1;
        }
        SYM_CAP
    }

    unsafe fn add(&mut self, name: *const u8, len: usize, ret: *const u8, retlen: usize) -> usize {
        let s = unsafe { self.slot(name, len) };
        if s >= SYM_CAP {
            return s;
        }
        self.names[s] = name;
        self.name_lens[s] = len;
        /* an arena span copy of the return C type: NUL-terminated, the
         * caller's tmp buffer is reused after this returns. NULL span =
         * unknown (ret_ctype reports 0) — the fn still compiles, only
         * call-site return-type inference loses it. */
        self.rets[s] = b"\0".as_ptr();
        self.ret_lens[s] = 0;
        if !ret.is_null() && retlen > 0 {
            let sp = unsafe { self.span(ret, retlen) };
            if unsafe { *sp } != 0 {
                self.rets[s] = sp;
                self.ret_lens[s] = retlen;
            }
        }
        self.used[s] = true;
        self.n_params[s] = 0;
        let mut p = 0usize;
        while p < FN_MAXP {
            self.param_lens[s * FN_MAXP + p] = 0;
            p += 1;
        }
        s
    }

    unsafe fn set_n_params(&mut self, s: usize, n: u32) {
        if s < SYM_CAP {
            self.n_params[s] = n;
        }
    }

    /* Copy len bytes into a fresh arena block (NUL-terminated). NULL-safe:
     * an OOM hands back the empty string AND flags the tab — the compile
     * refuses with the specific arena error rather than degrading. */
    unsafe fn span(&mut self, src: *const u8, len: usize) -> *const u8 {
        let p = unsafe { pm_util_mem_alloc(self.arena, len + 1) };
        if p.is_null() {
            self.oom = true;
            return b"\0".as_ptr();
        }
        unsafe {
            core::ptr::copy_nonoverlapping(src, p, len);
            *p.add(len) = 0;
        }
        p
    }

    /* Record param slot p's rendered C type (collect-time; the params
     * table is what a `None` argument reads at the call site). */
    unsafe fn add_param(&mut self, s: usize, p: usize, ct: *const u8, ctlen: usize) {
        if s >= SYM_CAP || p >= FN_MAXP || ct.is_null() || ctlen == 0 {
            return;
        }
        let at = s * FN_MAXP + p;
        let sp = unsafe { self.span(ct, ctlen) };
        if unsafe { *sp } != 0 {
            self.params[at] = sp;
            self.param_lens[at] = ctlen;
        }
    }

    /* Rendered C type of the callee's param slot p (0 when unknown —
     * extern fns and methods beyond the table). */
    unsafe fn param_ctype(&self, name: *const u8, len: usize, p: usize, out: *mut u8) -> usize {
        if p >= FN_MAXP {
            return 0;
        }
        let s = unsafe { self.slot(name, len) };
        if s < SYM_CAP && self.used[s] && self.name_lens[s] == len {
            let at = s * FN_MAXP + p;
            let pl = self.param_lens[at];
            if pl == 0 {
                return 0;
            }
            let src = self.params[at];
            let mut i = 0usize;
            while i < pl {
                unsafe {
                    *out.add(i) = *src.add(i);
                }
                i += 1;
            }
            unsafe {
                *out.add(pl) = 0;
            }
            return pl;
        }
        0
    }

    unsafe fn ret_ctype(&self, name: *const u8, len: usize, out: *mut u8) -> usize {
        let s = unsafe { self.slot(name, len) };
        if s < SYM_CAP && self.used[s] && self.name_lens[s] == len {
            let rl = self.ret_lens[s];
            if rl == 0 {
                return 0;
            }
            let src = self.rets[s];
            let mut i = 0usize;
            while i < rl {
                unsafe {
                    *out.add(i) = *src.add(i);
                }
                i += 1;
            }
            unsafe {
                *out.add(rl) = 0;
            }
            return rl;
        }
        0
    }
}

/* Integer constants (const items with literal values): repeat counts and
 * enum discriminants may name them; emission needs the numeric value. */
const CONST_CAP: usize = 128;

struct ConstTab {
    names: [[u8; 48]; CONST_CAP],
    name_lens: [usize; CONST_CAP],
    vals: [u64; CONST_CAP],
    n: usize,
}

impl ConstTab {
    unsafe fn new() -> ConstTab {
        ConstTab {
            names: [[0; 48]; CONST_CAP],
            name_lens: [0; CONST_CAP],
            vals: [0; CONST_CAP],
            n: 0,
        }
    }

    unsafe fn add(&mut self, name: *const u8, nlen: usize, val: u64) {
        if self.n >= CONST_CAP || nlen > 48 {
            return;
        }
        let mut i = 0usize;
        while i < nlen {
            self.names[self.n][i] = unsafe { *name.add(i) };
            i += 1;
        }
        self.name_lens[self.n] = nlen;
        self.vals[self.n] = val;
        self.n += 1;
    }

    /* Parses a plain decimal/hex literal or a named constant; returns 0 with
     * *found=false when the text names nothing known. */
    unsafe fn lookup(&self, s: *const u8, n: usize, found: *mut bool) -> u64 {
        unsafe {
            *found = false;
        }
        if n == 0 || s.is_null() {
            return 0;
        }
        let mut i = 0usize;
        while i < self.n {
            if self.name_lens[i] == n {
                let mut j = 0usize;
                let mut eq = true;
                while j < n {
                    if self.names[i][j] != unsafe { *s.add(j) } {
                        eq = false;
                        break;
                    }
                    j += 1;
                }
                if eq {
                    unsafe {
                        *found = true;
                    }
                    return self.vals[i];
                }
            }
            i += 1;
        }
        0
    }
}

/* Enum variant discriminants: the all-zero static-initializer elision
 * needs to prove `E::V` (and bare variant names) carry value 0, the same
 * numbering the C `enum` lowering assigns (explicit `= N` wins, else the
 * previous variant + 1, first starts at 0). Names are stored as the C
 * member spelling (`Enum_Variant`) so both the 2-segment path and the
 * joined emission resolve through one lookup. */
const ENUM_CAP: usize = 256;

struct EnumTab {
    names: [[u8; 64]; ENUM_CAP],
    name_lens: [usize; ENUM_CAP],
    vals: [u64; ENUM_CAP],
    n: usize,
}

impl EnumTab {
    unsafe fn new() -> EnumTab {
        EnumTab {
            names: [[0; 64]; ENUM_CAP],
            name_lens: [0; ENUM_CAP],
            vals: [0; ENUM_CAP],
            n: 0,
        }
    }

    /* Splice one `Enum_Variant = value` row; a duplicate name is a
     * re-registration of the same variant (never occurs in one unit) and
     * keeps the first value. */
    unsafe fn add(&mut self, name: *const u8, nlen: usize, val: u64) {
        if self.n >= ENUM_CAP || nlen > 64 {
            return;
        }
        let mut i = 0usize;
        while i < nlen {
            self.names[self.n][i] = unsafe { *name.add(i) };
            i += 1;
        }
        self.name_lens[self.n] = nlen;
        self.vals[self.n] = val;
        self.n += 1;
    }

    unsafe fn lookup(&self, s: *const u8, n: usize, found: *mut bool) -> u64 {
        unsafe {
            *found = false;
        }
        if n == 0 || s.is_null() {
            return 0;
        }
        let mut i = 0usize;
        while i < self.n {
            if self.name_lens[i] == n {
                let mut j = 0usize;
                let mut eq = true;
                while j < n {
                    if self.names[i][j] != unsafe { *s.add(j) } {
                        eq = false;
                        break;
                    }
                    j += 1;
                }
                if eq {
                    unsafe {
                        *found = true;
                    }
                    return self.vals[i];
                }
            }
            i += 1;
        }
        0
    }
}


/* Tagged enums: names of payload-bearing (tagged-union) enums. Every
 * variant test on such a name compares `._tag`, even fieldless variants
 * (the C value is a struct, never a bare int). */
struct EnumTagTab {
    names: [[u8; 64]; ENUM_CAP],
    name_lens: [usize; ENUM_CAP],
    n: usize,
}

impl EnumTagTab {
    unsafe fn new() -> EnumTagTab {
        EnumTagTab {
            names: [[0; 64]; ENUM_CAP],
            name_lens: [0; ENUM_CAP],
            n: 0,
        }
    }

    unsafe fn add(&mut self, name: *const u8, nlen: usize) {
        if self.n >= ENUM_CAP || nlen > 64 {
            return;
        }
        let mut i = 0usize;
        while i < nlen {
            self.names[self.n][i] = unsafe { *name.add(i) };
            i += 1;
        }
        self.name_lens[self.n] = nlen;
        self.n += 1;
    }

    unsafe fn has(&self, s: *const u8, n: usize) -> bool {
        if n == 0 || s.is_null() {
            return false;
        }
        let mut i = 0usize;
        while i < self.n {
            if self.name_lens[i] == n {
                let mut j = 0usize;
                let mut eq = true;
                while j < n {
                    if self.names[i][j] != unsafe { *s.add(j) } {
                        eq = false;
                        break;
                    }
                    j += 1;
                }
                if eq {
                    return true;
                }
            }
            i += 1;
        }
        false
    }
}

/* Data-carrying enum payloads: one row per `Enum_Variant` that carries a
 * single payload — the variant's joined name plus its payload's C type.
 * The match/ctor planes consult this to emit the tagged-union arm
 * (fieldless variants have no row; their tag test is the enum table's). */
struct EnumPayTab {
    names: [[u8; 64]; ENUM_CAP],
    name_lens: [usize; ENUM_CAP],
    pays: [[u8; 96]; ENUM_CAP],
    pay_lens: [usize; ENUM_CAP],
    n: usize,
}

impl EnumPayTab {
    unsafe fn new() -> EnumPayTab {
        EnumPayTab {
            names: [[0; 64]; ENUM_CAP],
            name_lens: [0; ENUM_CAP],
            pays: [[0; 96]; ENUM_CAP],
            pay_lens: [0; ENUM_CAP],
            n: 0,
        }
    }

    unsafe fn add(&mut self, name: *const u8, nlen: usize, pay: *const u8, plen: usize) {
        if self.n >= ENUM_CAP || nlen > 64 || plen >= 96 {
            return;
        }
        let mut i = 0usize;
        while i < nlen {
            self.names[self.n][i] = unsafe { *name.add(i) };
            i += 1;
        }
        let mut p = 0usize;
        while p < plen {
            self.pays[self.n][p] = unsafe { *pay.add(p) };
            p += 1;
        }
        self.name_lens[self.n] = nlen;
        self.pay_lens[self.n] = plen;
        self.n += 1;
    }

    /* the payload's C type for a joined `Enum_Variant` name; 0 = no row */
    unsafe fn lookup(&self, s: *const u8, n: usize, out: *mut u8, cap: usize) -> usize {
        if n == 0 || s.is_null() || cap == 0 {
            return 0;
        }
        let mut i = 0usize;
        while i < self.n {
            if self.name_lens[i] == n {
                let mut j = 0usize;
                let mut eq = true;
                while j < n {
                    if self.names[i][j] != unsafe { *s.add(j) } {
                        eq = false;
                        break;
                    }
                    j += 1;
                }
                if eq {
                    let pl = self.pay_lens[i];
                    if pl >= cap {
                        return 0;
                    }
                    let mut w = 0usize;
                    while w < pl {
                        unsafe {
                            *out.add(w) = self.pays[i][w];
                        }
                        w += 1;
                    }
                    unsafe {
                        *out.add(pl) = 0;
                    }
                    return pl;
                }
            }
            i += 1;
        }
        0
    }
}


/* A trait record: the object name plus its methods' vtable slots. The
 * fn-ptr signature of each method is rendered ONCE at collect (the sig
 * AST is discarded after the collect pass) into a fixed inline string:
 * `<ret> (*name)(Name *self, T p, ..)` — the body of a vtable field. The
 * typedef emit reads the rendered sigs; the callsite dispatch only needs
 * `p->name`; the impl-side vtable initializer reads method names. */
struct TraitTab {
    names: [[u8; 64]; TRAIT_CAP],
    name_lens: [usize; TRAIT_CAP],
    n: usize,
    /* method rows: trait t, method m lives at row t * TRAIT_MCAP + m */
    m_names: [[u8; 64]; TRAIT_CAP * TRAIT_MCAP],
    m_name_lens: [usize; TRAIT_CAP * TRAIT_MCAP],
    /* receiver by ref? `&self`/`&mut self` — the fn-ptr takes Name*;
     * by-value self takes Name (rare; the dyn plane is ref-carried). */
    m_ref_recv: [bool; TRAIT_CAP * TRAIT_MCAP],
    /* rendered fn-ptr sig bodies, NUL-terminated */
    m_sigs: [[u8; TRAIT_SIG]; TRAIT_CAP * TRAIT_MCAP],
    m_sig_lens: [usize; TRAIT_CAP * TRAIT_MCAP],
    m_counts: [usize; TRAIT_CAP],
}

impl TraitTab {
    unsafe fn new() -> TraitTab {
        TraitTab {
            names: [[0; 64]; TRAIT_CAP],
            name_lens: [0; TRAIT_CAP],
            n: 0,
            m_names: [[0; 64]; TRAIT_CAP * TRAIT_MCAP],
            m_name_lens: [0; TRAIT_CAP * TRAIT_MCAP],
            m_ref_recv: [false; TRAIT_CAP * TRAIT_MCAP],
            m_sigs: [[0; TRAIT_SIG]; TRAIT_CAP * TRAIT_MCAP],
            m_sig_lens: [0; TRAIT_CAP * TRAIT_MCAP],
            m_counts: [0; TRAIT_CAP],
        }
    }

    /* Find or create the trait's row; returns the slot, or TRAIT_CAP on
     * overflow (the caller refuses loudly). */
    unsafe fn intern(&mut self, name: *const u8, nlen: usize) -> usize {
        let mut i = 0usize;
        while i < self.n {
            if self.name_lens[i] == nlen {
                let mut j = 0usize;
                let mut eq = true;
                while j < nlen {
                    if self.names[i][j] != unsafe { *name.add(j) } {
                        eq = false;
                        break;
                    }
                    j += 1;
                }
                if eq {
                    return i;
                }
            }
            i += 1;
        }
        if self.n >= TRAIT_CAP || nlen > 64 {
            return TRAIT_CAP;
        }
        let mut k = 0usize;
        while k < nlen {
            self.names[self.n][k] = unsafe { *name.add(k) };
            k += 1;
        }
        self.name_lens[self.n] = nlen;
        self.n += 1;
        self.n - 1
    }

    unsafe fn find(&self, name: *const u8, nlen: usize) -> usize {
        let mut i = 0usize;
        while i < self.n {
            if self.name_lens[i] == nlen {
                let mut j = 0usize;
                let mut eq = true;
                while j < nlen {
                    if self.names[i][j] != unsafe { *name.add(j) } {
                        eq = false;
                        break;
                    }
                    j += 1;
                }
                if eq {
                    return i;
                }
            }
            i += 1;
        }
        TRAIT_CAP
    }

    /* Append a method row to trait slot t. Returns false on overflow
     * (trait cap, method cap, name or sig too long) — the row index is
     * t * TRAIT_MCAP + m, derivable by any caller that needs it. */
    unsafe fn add_method(
        &mut self,
        t: usize,
        mname: *const u8,
        mlen: usize,
        ref_recv: bool,
        sig: *const u8,
        sig_len: usize,
    ) -> bool {
        if t >= TRAIT_CAP || self.m_counts[t] >= TRAIT_MCAP || mlen > 64 || sig_len >= TRAIT_SIG {
            return false;
        }
        let m = self.m_counts[t];
        let row = t * TRAIT_MCAP + m;
        let mut k = 0usize;
        while k < mlen {
            self.m_names[row][k] = unsafe { *mname.add(k) };
            k += 1;
        }
        self.m_name_lens[row] = mlen;
        self.m_ref_recv[row] = ref_recv;
        let mut s = 0usize;
        while s < sig_len {
            self.m_sigs[row][s] = unsafe { *sig.add(s) };
            s += 1;
        }
        self.m_sig_lens[row] = sig_len;
        self.m_sigs[row][sig_len] = 0;
        self.m_counts[t] = m + 1;
        true
    }

    /* Find a trait's method row by name; false = absent. */
    unsafe fn find_method(&self, t: usize, mname: *const u8, mlen: usize) -> bool {
        if t >= TRAIT_CAP {
            return false;
        }
        let mut m = 0usize;
        while m < self.m_counts[t] {
            let row = t * TRAIT_MCAP + m;
            if self.m_name_lens[row] == mlen {
                let mut j = 0usize;
                let mut eq = true;
                while j < mlen {
                    if self.m_names[row][j] != unsafe { *mname.add(j) } {
                        eq = false;
                        break;
                    }
                    j += 1;
                }
                if eq {
                    return true;
                }
            }
            m += 1;
        }
        false
    }
}

/* Container plane (Vec/String/BTreeMap): distinct element-type
 * spellings intern into named C types, emitted once per unit in the
 * preamble — the same register/emit-once discipline as the Option and
 * tuple tables. Vec<T> renders as
 *   typedef struct { T *p; size_t n, cap; } rsx_vec_<elem>;
 * with push/len/is_empty/free static ops in the preamble. */
const VEC_CAP: usize = 96;
const CT_SIG: usize = 160;

/* Map plane: one row per distinct BTreeMap VALUE C-type (the key plane is
 * rsx_str_t only). Registry/tree maps number in the tens — the same
 * scale the lock/arr planes sized against. */
const MAP_CAP: usize = 32;

/* Fn-pointer plane: full `RET (*)(params)` spellings are 50-70 bytes with
 * spaces and parens — as an Option payload the hex name doubles past the
 * ctype buffers. Intern each distinct signature once:
 *   typedef RET (*)(params) rsx_fnp_<row>;
 * The short alnum row name then rides the Option's raw (R) form. */
const FNP_CAP: usize = 24;
const FNP_SIG: usize = 192;

struct FnPtrTab {
    sigs: [[u8; FNP_SIG]; FNP_CAP],
    sig_lens: [usize; FNP_CAP],
    n: usize,
    done: [bool; FNP_CAP],
}

impl FnPtrTab {
    unsafe fn new() -> FnPtrTab {
        FnPtrTab {
            sigs: [[0; FNP_SIG]; FNP_CAP],
            sig_lens: [0; FNP_CAP],
            n: 0,
            done: [false; FNP_CAP],
        }
    }

    /* rsx_fnp_<row> — numbered like the Vec rows (deterministic under the
     * same collection order contract). */
    unsafe fn name_for(row: usize, out: *mut u8, cap: usize) -> usize {
        let at = unsafe { bput(out, cap, 0, b"rsx_fnp_\0".as_ptr(), 8) };
        if at != 8 || cap <= 10 {
            return 0;
        }
        if row >= FNP_CAP {
            return 0;
        }
        let d0 = b'0' + (row % 10) as u8;
        let d1 = b'0' + (row / 10) as u8;
        let at2 = if row >= 10 {
            let digs: [u8; 2] = [d1, d0];
            unsafe { bput(out, cap, at, digs.as_ptr(), 2) }
        } else {
            let digs: [u8; 1] = [d0];
            unsafe { bput(out, cap, at, digs.as_ptr(), 1) }
        };
        if at2 >= cap {
            return 0;
        }
        unsafe {
            *out.add(at2) = 0;
        }
        at2
    }

    /* find-or-create; FNP_CAP = full (caller refuses). */
    unsafe fn intern(&mut self, sig: *const u8, sig_len: usize) -> usize {
        if sig_len >= FNP_SIG {
            return FNP_CAP;
        }
        let mut s = 0usize;
        while s < self.n {
            if self.sig_lens[s] == sig_len {
                let mut same = true;
                let mut i = 0usize;
                while i < sig_len {
                    if self.sigs[s][i] != unsafe { *sig.add(i) } {
                        same = false;
                        break;
                    }
                    i += 1;
                }
                if same {
                    return s;
                }
            }
            s += 1;
        }
        if self.n >= FNP_CAP {
            return FNP_CAP;
        }
        let slot = self.n;
        let mut i = 0usize;
        while i < sig_len {
            self.sigs[slot][i] = unsafe { *sig.add(i) };
            i += 1;
        }
        self.sig_lens[slot] = sig_len;
        self.n += 1;
        slot
    }
}

struct VecTab {
    /* canonical element C-type text (the same bytes the name mangles) */
    elems: [[u8; CT_SIG]; VEC_CAP],
    elem_lens: [usize; VEC_CAP],
    n: usize,
    done: [bool; VEC_CAP],
}

impl VecTab {
    unsafe fn new() -> VecTab {
        VecTab {
            elems: [[0; CT_SIG]; VEC_CAP],
            elem_lens: [0; VEC_CAP],
            n: 0,
            done: [false; VEC_CAP],
        }
    }

    /* typedef name: rsx_vec_<row> — the row index of the interned element
     * spelling. Numbered, not content-hexed: a content-hex name doubles at
     * every composition level (a Vec of tuples of Vecs blows past any
     * fixed Option/tuple payload cap), while numbering adds a constant
     * ~10 bytes per level. Deterministic because interning order follows
     * the unit's own deterministic collection order — the self-host
     * fixed point (gen1 == gen2, byte for byte) gates exactly that. */
    unsafe fn name_for(row: usize, out: *mut u8, cap: usize) -> usize {
        let at = unsafe { bput(out, cap, 0, b"rsx_vec_\0".as_ptr(), 8) };
        if at != 8 || cap <= 10 {
            return 0;
        }
        /* decimal row, no leading zeros; row < VEC_CAP so <= 2 digits */
        if row >= VEC_CAP {
            return 0;
        }
        let d0 = b'0' + (row % 10) as u8;
        let d1 = b'0' + (row / 10) as u8;
        let at2 = if row >= 10 {
            let digs: [u8; 2] = [d1, d0];
            unsafe { bput(out, cap, at, digs.as_ptr(), 2) }
        } else {
            let digs: [u8; 1] = [d0];
            unsafe { bput(out, cap, at, digs.as_ptr(), 1) }
        };
        if at2 >= cap {
            return 0;
        }
        /* NUL-terminate: callers hand this buffer to zput/out.put, both
         * NUL-scanning — an unterminated name leaks arena residue into
         * the generated C whenever the arena block is recycled (the
         * nondeterministic garbage-byte refusals). */
        unsafe {
            *out.add(at2) = 0;
        }
        at2
    }

    /* find-or-create the row; VEC_CAP = full (caller refuses). */
    unsafe fn intern(&mut self, elem: *const u8, elen: usize) -> usize {
        let mut s = 0usize;
        while s < self.n {
            if self.elem_lens[s] == elen {
                let mut same = true;
                let mut i = 0usize;
                while i < elen {
                    if self.elems[s][i] != unsafe { *elem.add(i) } {
                        same = false;
                        break;
                    }
                    i += 1;
                }
                if same {
                    return s;
                }
            }
            s += 1;
        }
        if self.n >= VEC_CAP || elen >= CT_SIG {
            return VEC_CAP;
        }
        let mut i = 0usize;
        while i < elen {
            self.elems[self.n][i] = unsafe { *elem.add(i) };
            i += 1;
        }
        self.elem_lens[self.n] = elen;
        self.n += 1;
        self.n - 1
    }

    unsafe fn find(&self, elem: *const u8, elen: usize) -> usize {
        let mut s = 0usize;
        while s < self.n {
            if self.elem_lens[s] == elen {
                let mut same = true;
                let mut i = 0usize;
                while i < elen {
                    if self.elems[s][i] != unsafe { *elem.add(i) } {
                        same = false;
                        break;
                    }
                    i += 1;
                }
                if same {
                    return s;
                }
            }
            s += 1;
        }
        VEC_CAP
    }

    /* reverse lookup — typedef name rsx_vec_<digits> -> row: the INDEX
     * arm of expr_ctype maps `v[i]` on a Vec back to the element's C
     * type from the rendered typedef name alone. */
    unsafe fn find_by_name(&self, name: *const u8, nlen: usize) -> usize {
        if nlen < 9 || nlen > 10 {
            return VEC_CAP;
        }
        if !unsafe { z_eq(name, 8, b"rsx_vec_\0".as_ptr()) } {
            return VEC_CAP;
        }
        let mut row: usize = 0;
        let mut i = 8usize;
        while i < nlen {
            let ch = unsafe { *name.add(i) };
            if ch < b'0' || ch > b'9' {
                return VEC_CAP;
            }
            row = row * 10 + (ch - b'0') as usize;
            i += 1;
        }
        if row >= self.n {
            return VEC_CAP;
        }
        row
    }
}

/* ---- map rows (the BTreeMap<K, V> plane) ----
 *
 * `alloc::collections::BTreeMap<K, V>` / `std::collections::BTreeMap<K, V>`
 * lowers to a C struct row: { rsx_map_kv_<row> *rows; size_t n, cap; } with
 * the kv row { K k; V v; } — the growable-spine discipline the Vec plane
 * rides, applied to an ordered kv store. Keys in the kernel discipline are
 * Strings (rsx_str_t); linear scan is the map (registry/tree maps are
 * dozens of rows, not millions — the C stays readable and the semantics
 * match: get/insert/entry-or-insert on the exact key bytes).
 * Numbered rows mirror VecTab: rsx_map_<row>, deterministic by collection
 * order, the self-host fixed point gates that. */
struct MapTab {
    /* canonical value C-type text (the key plane is rsx_str_t only) */
    vals: [[u8; CT_SIG]; MAP_CAP],
    val_lens: [usize; MAP_CAP],
    n: usize,
    done: [bool; MAP_CAP],
}

impl MapTab {
    unsafe fn new() -> MapTab {
        MapTab {
            vals: [[0; CT_SIG]; MAP_CAP],
            val_lens: [0; MAP_CAP],
            n: 0,
            done: [false; MAP_CAP],
        }
    }

    /* typedef name: rsx_map_<row> (row < MAP_CAP, <= 2 digits). */
    unsafe fn name_for(row: usize, out: *mut u8, cap: usize) -> usize {
        let at = unsafe { bput(out, cap, 0, b"rsx_map_\0".as_ptr(), 8) };
        if at != 8 || cap <= 10 {
            return 0;
        }
        if row >= MAP_CAP {
            return 0;
        }
        let d0 = b'0' + (row % 10) as u8;
        let d1 = b'0' + (row / 10) as u8;
        let at2 = if row >= 10 {
            let digs: [u8; 2] = [d1, d0];
            unsafe { bput(out, cap, at, digs.as_ptr(), 2) }
        } else {
            let digs: [u8; 1] = [d0];
            unsafe { bput(out, cap, at, digs.as_ptr(), 1) }
        };
        if at2 >= cap {
            return 0;
        }
        unsafe {
            *out.add(at2) = 0;
        }
        at2
    }

    /* find-or-create the row by the VALUE's C type (the key plane is
     * fixed); MAP_CAP = full (caller refuses). */
    unsafe fn intern(&mut self, val: *const u8, vlen: usize) -> usize {
        let mut s = 0usize;
        while s < self.n {
            if self.val_lens[s] == vlen {
                let mut same = true;
                let mut i = 0usize;
                while i < vlen {
                    if self.vals[s][i] != unsafe { *val.add(i) } {
                        same = false;
                        break;
                    }
                    i += 1;
                }
                if same {
                    return s;
                }
            }
            s += 1;
        }
        if self.n >= MAP_CAP || vlen >= CT_SIG {
            return MAP_CAP;
        }
        let mut i = 0usize;
        while i < vlen {
            self.vals[self.n][i] = unsafe { *val.add(i) };
            i += 1;
        }
        self.val_lens[self.n] = vlen;
        self.n += 1;
        self.n - 1
    }

    unsafe fn find(&self, val: *const u8, vlen: usize) -> usize {
        let mut s = 0usize;
        while s < self.n {
            if self.val_lens[s] == vlen {
                let mut same = true;
                let mut i = 0usize;
                while i < vlen {
                    if self.vals[s][i] != unsafe { *val.add(i) } {
                        same = false;
                        break;
                    }
                    i += 1;
                }
                if same {
                    return s;
                }
            }
            s += 1;
        }
        MAP_CAP
    }

    /* reverse lookup — rsx_map_<digits> -> row (the INDEX/value arm of
     * expr_ctype maps a map back to its value's C type by name). */
    unsafe fn find_by_name(&self, name: *const u8, nlen: usize) -> usize {
        if nlen < 9 || nlen > 10 {
            return MAP_CAP;
        }
        if !unsafe { z_eq(name, 8, b"rsx_map_\0".as_ptr()) } {
            return MAP_CAP;
        }
        let mut row: usize = 0;
        let mut i = 8usize;
        while i < nlen {
            let ch = unsafe { *name.add(i) };
            if ch < b'0' || ch > b'9' {
                return MAP_CAP;
            }
            row = row * 10 + (ch - b'0') as usize;
            i += 1;
        }
        if row >= self.n {
            return MAP_CAP;
        }
        row
    }
}

/* ---- lock rows (the Mutex<T>/SpinLock<T> plane) ----
 *
 * `crate::util::lock::Mutex<T>` / `SpinLock<T>` (any qualification depth)
 * lowers to a C struct row: { pm_util_lock_t raw; T value; } — the exact
 * layout the glue alias (util/lock.rs SpinLock<T>) defines and the lock
 * card's rs muscle exports the primitive for. acquire/release are the
 * card's own C faces (pm_util_lock_acquire/pm_util_lock_release, link-
 * time resolved): one mechanism, not two. Numbered rows mirror VecTab
 * — deterministic interning keeps the self-host fixed point honest. */
const LOCK_CAP: usize = 8;
const LOCK_SIG: usize = 64;

/* ---- &[T] slice-reference rows ----
 *
 * A `&[T]` param/local lowers to a fat pointer { const T *p; size_t n; }
 * interned per element C type (rsx_arr_<row>) — same discipline as the
 * str plane's rsx_str_ref_t and the container plane's rsx_vec_<row>:
 * the slice carries its length, so .iter()/.len()/closure builtins/
 * for-in all walk .p[0..n) exactly like the Vec rows, and &Vec<T> ->
 * &[T] coerces at call args with a compound literal {v.p, v.n}.
 * Numbered rows keep names bounded and the interning order
 * deterministic — the self-host fixed point gates exactly that. */
const ARR_CAP: usize = 16;
const ARR_SIG: usize = 64;

struct ArrTab {
    elems: [[u8; ARR_SIG]; ARR_CAP],
    elem_lens: [usize; ARR_CAP],
    n: usize,
    done: [bool; ARR_CAP],
}

impl ArrTab {
    unsafe fn new() -> ArrTab {
        ArrTab {
            elems: [[0; ARR_SIG]; ARR_CAP],
            elem_lens: [0; ARR_CAP],
            n: 0,
            done: [false; ARR_CAP],
        }
    }

    /* typedef name: rsx_arr_<row> */
    unsafe fn name_for(row: usize, out: *mut u8, cap: usize) -> usize {
        let at = unsafe { bput(out, cap, 0, b"rsx_arr_\0".as_ptr(), 8) };
        if at != 8 || cap <= 10 || row >= ARR_CAP {
            return 0;
        }
        let d0 = b'0' + (row % 10) as u8;
        let d1 = b'0' + (row / 10) as u8;
        let at2 = if row >= 10 {
            let digs: [u8; 2] = [d1, d0];
            unsafe { bput(out, cap, at, digs.as_ptr(), 2) }
        } else {
            let digs: [u8; 1] = [d0];
            unsafe { bput(out, cap, at, digs.as_ptr(), 1) }
        };
        if at2 >= cap {
            return 0;
        }
        /* NUL-terminate — see VecTab::name_for; zput/out.put scan to NUL
         * and an unterminated row name leaks arena residue. */
        unsafe {
            *out.add(at2) = 0;
        }
        at2
    }

    unsafe fn intern(&mut self, elem: *const u8, elen: usize) -> usize {
        let mut s = 0usize;
        while s < self.n {
            if self.elem_lens[s] == elen {
                let mut same = true;
                let mut i = 0usize;
                while i < elen {
                    if self.elems[s][i] != unsafe { *elem.add(i) } {
                        same = false;
                        break;
                    }
                    i += 1;
                }
                if same {
                    return s;
                }
            }
            s += 1;
        }
        if self.n >= ARR_CAP || elen >= ARR_SIG {
            return ARR_CAP;
        }
        let mut i = 0usize;
        while i < elen {
            self.elems[self.n][i] = unsafe { *elem.add(i) };
            i += 1;
        }
        self.elem_lens[self.n] = elen;
        self.n += 1;
        self.n - 1
    }

    unsafe fn find(&self, elem: *const u8, elen: usize) -> usize {
        let mut s = 0usize;
        while s < self.n {
            if self.elem_lens[s] == elen {
                let mut same = true;
                let mut i = 0usize;
                while i < elen {
                    if self.elems[s][i] != unsafe { *elem.add(i) } {
                        same = false;
                        break;
                    }
                    i += 1;
                }
                if same {
                    return s;
                }
            }
            s += 1;
        }
        ARR_CAP
    }

    /* rsx_arr_<digits> -> row */
    unsafe fn find_by_name(&self, name: *const u8, nlen: usize) -> usize {
        if nlen < 9 || nlen > 10 {
            return ARR_CAP;
        }
        if !unsafe { z_eq(name, 8, b"rsx_arr_\0".as_ptr()) } {
            return ARR_CAP;
        }
        let mut row: usize = 0;
        let mut i = 8usize;
        while i < nlen {
            let ch = unsafe { *name.add(i) };
            if ch < b'0' || ch > b'9' {
                return ARR_CAP;
            }
            row = row * 10 + (ch - b'0') as usize;
            i += 1;
        }
        if row >= self.n {
            return ARR_CAP;
        }
        row
    }
}

struct LockTab {
    elems: [[u8; LOCK_SIG]; LOCK_CAP],
    elem_lens: [usize; LOCK_CAP],
    n: usize,
    done: [bool; LOCK_CAP],
}

impl LockTab {
    unsafe fn new() -> LockTab {
        LockTab {
            elems: [[0; LOCK_SIG]; LOCK_CAP],
            elem_lens: [0; LOCK_CAP],
            n: 0,
            done: [false; LOCK_CAP],
        }
    }

    /* typedef name: rsx_lock_<row> */
    unsafe fn name_for(row: usize, out: *mut u8, cap: usize) -> usize {
        let at = unsafe { bput(out, cap, 0, b"rsx_lock_\0".as_ptr(), 9) };
        if at != 9 || cap <= 11 {
            return 0;
        }
        if row >= LOCK_CAP {
            return 0;
        }
        let d0 = b'0' + (row % 10) as u8;
        let digs: [u8; 1] = [d0];
        let at2 = unsafe { bput(out, cap, at, digs.as_ptr(), 1) };
        if at2 >= cap {
            return 0;
        }
        /* NUL-terminate — see VecTab::name_for. */
        unsafe {
            *out.add(at2) = 0;
        }
        at2
    }

    unsafe fn intern(&mut self, elem: *const u8, elen: usize) -> usize {
        let mut s = 0usize;
        while s < self.n {
            if self.elem_lens[s] == elen {
                let mut same = true;
                let mut i = 0usize;
                while i < elen {
                    if self.elems[s][i] != unsafe { *elem.add(i) } {
                        same = false;
                        break;
                    }
                    i += 1;
                }
                if same {
                    return s;
                }
            }
            s += 1;
        }
        if self.n >= LOCK_CAP || elen >= LOCK_SIG {
            return LOCK_CAP;
        }
        let mut i = 0usize;
        while i < elen {
            self.elems[self.n][i] = unsafe { *elem.add(i) };
            i += 1;
        }
        self.elem_lens[self.n] = elen;
        self.n += 1;
        self.n - 1
    }

    unsafe fn find(&self, elem: *const u8, elen: usize) -> usize {
        let mut s = 0usize;
        while s < self.n {
            if self.elem_lens[s] == elen {
                let mut same = true;
                let mut i = 0usize;
                while i < elen {
                    if self.elems[s][i] != unsafe { *elem.add(i) } {
                        same = false;
                        break;
                    }
                    i += 1;
                }
                if same {
                    return s;
                }
            }
            s += 1;
        }
        LOCK_CAP
    }

    /* rsx_lock_<digit> -> row */
    unsafe fn find_by_name(&self, name: *const u8, nlen: usize) -> usize {
        if nlen != 10 {
            return LOCK_CAP;
        }
        if !unsafe { z_eq(name, 9, b"rsx_lock_\0".as_ptr()) } {
            return LOCK_CAP;
        }
        let ch = unsafe { *name.add(9) };
        if ch < b'0' || ch > b'9' {
            return LOCK_CAP;
        }
        let row = (ch - b'0') as usize;
        if row >= self.n {
            return LOCK_CAP;
        }
        row
    }
}

/* BTreeMap plane: (K, V) C-type pairs intern into rows rendering as
 *   typedef struct NODE { K key; V val; struct NODE *l, *r; } rsx_btmn_<row>;
 *   typedef struct { rsx_btmn_<row> *root; size_t n; } rsx_btm_<row>;
 * A sorted-insert BST: get/insert walk by key order, the pairs snapshot
 * walks in-order (BTreeMap's ordered iteration). The ops are unit-static
 * C emitted once per row, same contract as the Vec rows. */
const BTM_CAP: usize = 8;
const BTM_SIG: usize = 64;

struct BtmTab {
    keys: [[u8; BTM_SIG]; BTM_CAP],
    key_lens: [usize; BTM_CAP],
    vals: [[u8; BTM_SIG]; BTM_CAP],
    val_lens: [usize; BTM_CAP],
    n: usize,
    done: [bool; BTM_CAP],
    /* forward part emitted (map typedef + node fwd decl): the map row
     * names only `node *root`, so the typedef may precede the val type's
     * BODY — a recursive TreeNode { kids: BTreeMap<String, TreeNode> }
     * closes through it (the node body, which names the val by value,
     * waits in btm_emit_rest's gated half). */
    fwd_done: [bool; BTM_CAP],
}

impl BtmTab {
    unsafe fn new() -> BtmTab {
        BtmTab {
            keys: [[0; BTM_SIG]; BTM_CAP],
            key_lens: [0; BTM_CAP],
            vals: [[0; BTM_SIG]; BTM_CAP],
            val_lens: [0; BTM_CAP],
            n: 0,
            done: [false; BTM_CAP],
            fwd_done: [false; BTM_CAP],
        }
    }

    /* rsx_btm_<row> — numbered like the Vec rows. */
    unsafe fn name_for(row: usize, out: *mut u8, cap: usize) -> usize {
        let at = unsafe { bput(out, cap, 0, b"rsx_btm_\0".as_ptr(), 8) };
        if at != 8 || cap <= 10 {
            return 0;
        }
        if row >= BTM_CAP {
            return 0;
        }
        let d0 = b'0' + (row % 10) as u8;
        let digs: [u8; 1] = [d0];
        let at2 = unsafe { bput(out, cap, at, digs.as_ptr(), 1) };
        if at2 >= cap {
            return 0;
        }
        unsafe {
            *out.add(at2) = 0;
        }
        at2
    }

    /* the node struct name rsx_btmn_<row> */
    unsafe fn node_name_for(row: usize, out: *mut u8, cap: usize) -> usize {
        let at = unsafe { bput(out, cap, 0, b"rsx_btmn_\0".as_ptr(), 9) };
        if at != 9 || cap <= 11 {
            return 0;
        }
        if row >= BTM_CAP {
            return 0;
        }
        let d0 = b'0' + (row % 10) as u8;
        let digs: [u8; 1] = [d0];
        let at2 = unsafe { bput(out, cap, at, digs.as_ptr(), 1) };
        if at2 >= cap {
            return 0;
        }
        unsafe {
            *out.add(at2) = 0;
        }
        at2
    }

    /* find-or-create; BTM_CAP = full (caller refuses). */
    unsafe fn intern(&mut self, key: *const u8, klen: usize, val: *const u8, vlen: usize) -> usize {
        if klen >= BTM_SIG || vlen >= BTM_SIG {
            return BTM_CAP;
        }
        let mut s = 0usize;
        while s < self.n {
            if self.key_lens[s] == klen && self.val_lens[s] == vlen {
                let mut same = true;
                let mut i = 0usize;
                while i < klen {
                    if self.keys[s][i] != unsafe { *key.add(i) } {
                        same = false;
                        break;
                    }
                    i += 1;
                }
                if same {
                    let mut i2 = 0usize;
                    while i2 < vlen {
                        if self.vals[s][i2] != unsafe { *val.add(i2) } {
                            same = false;
                            break;
                        }
                        i2 += 1;
                    }
                }
                if same {
                    return s;
                }
            }
            s += 1;
        }
        if self.n >= BTM_CAP {
            return BTM_CAP;
        }
        let slot = self.n;
        let mut i = 0usize;
        while i < klen {
            self.keys[slot][i] = unsafe { *key.add(i) };
            i += 1;
        }
        let mut i2 = 0usize;
        while i2 < vlen {
            self.vals[slot][i2] = unsafe { *val.add(i2) };
            i2 += 1;
        }
        self.key_lens[slot] = klen;
        self.val_lens[slot] = vlen;
        self.n += 1;
        slot
    }

    unsafe fn find_by_name(&self, name: *const u8, nlen: usize) -> usize {
        if nlen != 9 {
            return BTM_CAP;
        }
        if !unsafe { z_eq(name, 8, b"rsx_btm_\0".as_ptr()) } {
            return BTM_CAP;
        }
        let ch = unsafe { *name.add(8) };
        if ch < b'0' || ch > b'9' {
            return BTM_CAP;
        }
        let row = (ch - b'0') as usize;
        if row >= self.n {
            return BTM_CAP;
        }
        row
    }
}
