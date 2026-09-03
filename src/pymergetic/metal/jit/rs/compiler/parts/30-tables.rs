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
const FPC: usize = 64;
/* params per fn whose C types the FnTab records (for `None` args). */
const FN_MAXP: usize = 8;

/* Transparent newtype registry cap (single-field generic tuple structs). */
const NT_CAP: usize = 16;
const OPT_CAP: usize = 8;
const ST_CAP: usize = 64;
/* Tuple signature cap: distinct (A, B, ..) spellings per unit. Tuples
 * render as named structs rsx_tuple_<sig>; the table mirrors the Option
 * payload table (register idempotent, emit once in the preamble). */
const TUP_CAP: usize = 8;
const TUP_MAXF: usize = 4;
/* Emitted-type set cap (dependency-ordered struct pass). One entry per
 * struct/union/alias emitted this unit — 96 covers a card's types plus
 * the appended face's. */
const TYD_CAP: usize = 96;

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

