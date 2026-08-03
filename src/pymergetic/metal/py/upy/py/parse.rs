//! parse — pushdown parse-tree builder (upstream `py/parse.c` mirror).
//!
//! Builds a `ParseTree` from lexer tokens using the generated grammar tables
//! (`grammar.rs`). Faithful port of upstream's non-recursive table-driven
//! algorithm (same `RULE_ACT_*`/`RuleArg` shapes, same 3-argument
//! `push_result_rule` node-shaping special cases, same `rule_stack`/
//! `result_stack` bookkeeping), with these deliberate, documented
//! differences from `parse.c`:
//!
//! - **Result, not `nlr_raise`.** `parse()` returns `Result<ParseTree,
//!   ParseError>` (Metal style, no exception unwinding). On error the
//!   partially-built tree's chunk memory is freed before returning (see
//!   `Drop for ChunkAllocator`), matching upstream's "free the lexer /
//!   tree on error" intent without an NLR jump callback.
//! - **Node allocator never moves live nodes.** Upstream's `parser_alloc`
//!   tries to *grow* the current chunk in place via `m_renew_maybe(...,
//!   allow_move=false)`, which either extends without moving or fails
//!   (freezing the chunk). Metal's `pm_metal_mem_realloc` has no
//!   no-move-guaranteed variant, so `ChunkAllocator` never grows a chunk
//!   that might already hold pointers taken by other nodes: a full chunk
//!   is simply frozen (linked into the finished list) and a fresh chunk is
//!   allocated for what follows. Same net effect (bump-pointer batches,
//!   struct-node addresses never move), no move-safety assumption needed.
//! - **No compiler-only optimisation passes.** `MICROPY_COMP_CONST_FOLDING`
//!   / `MICROPY_COMP_CONST_TUPLE` / `MICROPY_COMP_CONST` (arithmetic
//!   constant folding, literal-tuple pre-building, `id = const(...)`
//!   dynamic constants) are compiler-side memory/size optimisations, not
//!   grammar correctness -- there is no compiler here yet, so they're
//!   omitted; the tree this module builds is the same shape as upstream's
//!   *unoptimised* tree. The unconditional, non-flag-gated tree-shaping in
//!   `push_result_rule` (`atom_paren` unwrap, `testlist_comp` merge, the
//!   "lone expression statement becomes `pass`" `MICROPY_ENABLE_DOC_STRING`
//!   default-off behaviour) *is* ported, since that's grammar shape, not
//!   an optimisation.
//! - **No bigint / complex.** See `parsenum.rs` module doc.
//! - **`RuleId::ConstObject` holds only a float bit-pattern.** Upstream's
//!   `RULE_const_object` node wraps an arbitrary heap `mp_obj_t` (used for
//!   floats, bignums, and non-interned strings/bytes). Metal's version
//!   never needs the general case: integer overflow is a `ParseError`
//!   (no bigint), and every STRING/BYTES token here is qstr-interned (see
//!   below) -- so `ConstObject` only ever carries a `f64` (`to_bits()`),
//!   inline in the node, no heap object required.
//! - **`STRING`/`BYTES` both qstr-intern (Metal extension leaf tag).**
//!   Upstream distinguishes them by the *type* of the heap object it
//!   builds (`mp_obj_new_str_copy` vs `mp_obj_new_bytes`), never by parse
//!   node shape. Metal's `objects` layer has no heap bytes/arbitrary-length
//!   string type at all yet (`objstr.rs` is `TYPE_STR` only), so re-using
//!   it for bytes would mislabel the object's type. Instead this module
//!   adds a genuine `MP_PARSE_NODE_BYTES` leaf kind using the one spare
//!   4-bit leaf-tag slot `parse.h`'s encoding leaves free (`ID`=0b0010,
//!   `STRING`=0b0110, `TOKEN`=0b1010, `0b1110` unused) -- a real
//!   distinction a future compiler can switch on, not a mislabelled
//!   str. A literal that doesn't fit the qstr pool (`qstr::from_strn`
//!   returns the empty-string id for a non-empty input) is a real
//!   `ParseError::StringTooLong`, not a silent truncation.

use crate::upy::py::lexer::{Lexer, TokenKind};
use crate::upy::py::malloc;
use crate::upy::py::qstr;
use crate::upy::py::qstrdefs::QSTR_NULL;

use super::grammar::{self, RuleArg};
pub use super::grammar::RuleId;

// -- ParseNode: tagged uintptr leaf/struct encoding (upstream `parse.h`) ---

/// A parse node: either a small immediate value (leaf) or a pointer to a
/// `ParseNodeStruct` living in a `ChunkAllocator` chunk. Same tag scheme as
/// upstream's `mp_parse_node_t` (`0` = no node; `...1` = small int; `...10`
/// with a 2-bit sub-tag = id/string/token/[Metal: bytes]; `...00` non-zero
/// = struct pointer).
pub type ParseNode = usize;

pub const PARSE_NODE_NULL: ParseNode = 0;

const LEAF_SMALL_INT: usize = 0x1;
const LEAF_ID: usize = 0x02;
const LEAF_STRING: usize = 0x06;
const LEAF_TOKEN: usize = 0x0a;
/// Metal extension: the one leaf sub-tag `parse.h`'s scheme leaves spare
/// (see module doc). Never emitted/consumed by upstream code.
const LEAF_BYTES: usize = 0x0e;

#[inline]
pub const fn is_null(pn: ParseNode) -> bool {
    pn == PARSE_NODE_NULL
}
#[inline]
pub const fn is_leaf(pn: ParseNode) -> bool {
    pn & 3 != 0
}
#[inline]
pub const fn is_struct(pn: ParseNode) -> bool {
    pn != 0 && (pn & 3) == 0
}
#[inline]
pub const fn is_small_int(pn: ParseNode) -> bool {
    pn & 1 == LEAF_SMALL_INT
}
#[inline]
pub const fn is_id(pn: ParseNode) -> bool {
    pn & 0xf == LEAF_ID
}
#[inline]
pub const fn is_string(pn: ParseNode) -> bool {
    pn & 0xf == LEAF_STRING
}
#[inline]
pub const fn is_bytes(pn: ParseNode) -> bool {
    pn & 0xf == LEAF_BYTES
}
#[inline]
pub const fn is_token(pn: ParseNode) -> bool {
    pn & 0xf == LEAF_TOKEN
}
#[inline]
pub fn is_token_kind(pn: ParseNode, k: TokenKind) -> bool {
    pn == new_leaf(LEAF_TOKEN, k as usize)
}

#[inline]
pub const fn leaf_kind(pn: ParseNode) -> usize {
    pn & 0xf
}
#[inline]
pub const fn leaf_arg(pn: ParseNode) -> usize {
    pn >> 4
}
#[inline]
pub const fn small_int_value(pn: ParseNode) -> isize {
    (pn as isize) >> 1
}

#[inline]
const fn new_small_int(v: isize) -> ParseNode {
    LEAF_SMALL_INT | ((v as usize) << 1)
}
#[inline]
const fn new_leaf(kind: usize, arg: usize) -> ParseNode {
    kind | (arg << 4)
}

/// Whether `v` round-trips through the small-int tag (1 bit of `v` is used
/// as the tag, so the usable range is platform-pointer-width minus 1 bit).
fn small_int_fits(v: i128) -> bool {
    if v > isize::MAX as i128 || v < isize::MIN as i128 {
        return false;
    }
    let iv = v as isize;
    let encoded = (iv as usize) << 1;
    ((encoded as isize) >> 1) == iv
}

#[repr(C)]
struct StructHeader {
    source_line: u32,
    kind_num_nodes: u32, // low 8 bits = RuleId; remaining bits = num_nodes
}

#[inline]
pub fn is_struct_kind(pn: ParseNode, k: RuleId) -> bool {
    is_struct(pn) && struct_kind(pn) == k
}

#[inline]
pub fn struct_kind(pn: ParseNode) -> RuleId {
    let hdr = pn as *const StructHeader;
    grammar::rule_id_from_u8(unsafe { ((*hdr).kind_num_nodes & 0xff) as u8 })
}

#[inline]
pub fn struct_num_nodes(pn: ParseNode) -> usize {
    let hdr = pn as *const StructHeader;
    unsafe { ((*hdr).kind_num_nodes >> 8) as usize }
}

#[inline]
pub fn struct_source_line(pn: ParseNode) -> u32 {
    let hdr = pn as *const StructHeader;
    unsafe { (*hdr).source_line }
}

/// # Safety
/// `pn` must be a struct node (`is_struct(pn)`) and `i < struct_num_nodes(pn)`.
#[inline]
pub unsafe fn struct_node(pn: ParseNode, i: usize) -> ParseNode {
    let base = (pn as *mut u8).add(core::mem::size_of::<StructHeader>()) as *mut ParseNode;
    *base.add(i)
}

#[inline]
unsafe fn struct_node_mut(pn: ParseNode, i: usize) -> *mut ParseNode {
    let base = (pn as *mut u8).add(core::mem::size_of::<StructHeader>()) as *mut ParseNode;
    base.add(i)
}

/// Extract the `f64` held by a `RuleId::ConstObject` leaf (see module doc);
/// `None` for any other node.
pub fn extract_float(pn: ParseNode) -> Option<f64> {
    if is_struct_kind(pn, RuleId::ConstObject) && struct_num_nodes(pn) == 1 {
        let bits = unsafe { struct_node(pn, 0) } as u64;
        Some(f64::from_bits(bits))
    } else {
        None
    }
}

// -- Chunk allocator (batches struct-node storage; see module doc) --------

struct Chunk {
    buf: *mut u8,
    alloc: usize,
    used: usize,
    next: *mut Chunk,
}

const ALLOC_PARSE_CHUNK_INIT: usize = 128;

struct ChunkAllocator {
    cur: *mut Chunk,
    frozen: *mut Chunk,
}

impl ChunkAllocator {
    fn new() -> Self {
        Self {
            cur: core::ptr::null_mut(),
            frozen: core::ptr::null_mut(),
        }
    }

    unsafe fn freeze_current(&mut self) {
        if !self.cur.is_null() {
            (*self.cur).next = self.frozen;
            self.frozen = self.cur;
            self.cur = core::ptr::null_mut();
        }
    }

    unsafe fn alloc(&mut self, num_bytes: usize) -> *mut u8 {
        if !self.cur.is_null() {
            let c = &mut *self.cur;
            if c.used + num_bytes <= c.alloc {
                let p = c.buf.add(c.used);
                c.used += num_bytes;
                return p;
            }
            self.freeze_current();
        }

        let cap = if num_bytes > ALLOC_PARSE_CHUNK_INIT {
            num_bytes
        } else {
            ALLOC_PARSE_CHUNK_INIT
        };
        let hdr = malloc::m_malloc(core::mem::size_of::<Chunk>()) as *mut Chunk;
        if hdr.is_null() {
            return core::ptr::null_mut();
        }
        let buf = malloc::m_malloc(cap);
        if buf.is_null() {
            malloc::m_free(hdr as *mut u8);
            return core::ptr::null_mut();
        }
        *hdr = Chunk {
            buf,
            alloc: cap,
            used: num_bytes,
            next: core::ptr::null_mut(),
        };
        self.cur = hdr;
        buf
    }

    /// Freeze the current chunk and hand the whole finished list to the
    /// caller (a `ParseTree`), leaving this allocator empty.
    unsafe fn take_all(&mut self) -> *mut Chunk {
        self.freeze_current();
        let head = self.frozen;
        self.frozen = core::ptr::null_mut();
        head
    }

    unsafe fn free_list(mut c: *mut Chunk) {
        while !c.is_null() {
            let next = (*c).next;
            malloc::m_free((*c).buf);
            malloc::m_free(c as *mut u8);
            c = next;
        }
    }
}

impl Drop for ChunkAllocator {
    fn drop(&mut self) {
        unsafe {
            self.freeze_current();
            Self::free_list(self.frozen);
        }
    }
}

// -- ParseTree --------------------------------------------------------------

/// A completed parse tree: `root` plus the chunk memory backing every
/// struct node reachable from it. `clear()`/`Drop` free that memory
/// (upstream `mp_parse_tree_clear`).
pub struct ParseTree {
    pub root: ParseNode,
    chunks: *mut Chunk,
}

impl ParseTree {
    pub fn clear(&mut self) {
        unsafe { ChunkAllocator::free_list(self.chunks) };
        self.chunks = core::ptr::null_mut();
        self.root = PARSE_NODE_NULL;
    }
}

impl Drop for ParseTree {
    fn drop(&mut self) {
        self.clear();
    }
}

// -- growable rule_stack / result_stack (malloc-backed, no `alloc` crate) --

#[derive(Clone, Copy)]
struct RuleFrame {
    src_line: u32,
    rule_id: RuleId,
    arg_i: usize,
}

const ALLOC_PARSE_RULE_INIT: usize = 64;
const ALLOC_PARSE_RULE_INC: usize = 16;
const ALLOC_PARSE_RESULT_INIT: usize = 32;
const ALLOC_PARSE_RESULT_INC: usize = 16;

struct RuleStack {
    ptr: *mut RuleFrame,
    cap: usize,
    len: usize,
}

impl RuleStack {
    fn new() -> Self {
        let ptr = unsafe {
            malloc::m_malloc(ALLOC_PARSE_RULE_INIT * core::mem::size_of::<RuleFrame>())
        } as *mut RuleFrame;
        Self {
            ptr,
            cap: if ptr.is_null() { 0 } else { ALLOC_PARSE_RULE_INIT },
            len: 0,
        }
    }

    fn push(&mut self, f: RuleFrame) -> bool {
        if self.ptr.is_null() {
            return false;
        }
        if self.len >= self.cap {
            let ncap = self.cap + ALLOC_PARSE_RULE_INC;
            let np = unsafe {
                malloc::m_realloc(self.ptr as *mut u8, ncap * core::mem::size_of::<RuleFrame>())
            } as *mut RuleFrame;
            if np.is_null() {
                return false;
            }
            self.ptr = np;
            self.cap = ncap;
        }
        unsafe { *self.ptr.add(self.len) = f };
        self.len += 1;
        true
    }

    fn pop(&mut self) -> RuleFrame {
        self.len -= 1;
        unsafe { *self.ptr.add(self.len) }
    }

    fn is_empty(&self) -> bool {
        self.len == 0
    }
}

impl Drop for RuleStack {
    fn drop(&mut self) {
        if !self.ptr.is_null() {
            unsafe { malloc::m_free(self.ptr as *mut u8) };
        }
    }
}

struct ResultStack {
    ptr: *mut ParseNode,
    cap: usize,
    len: usize,
}

impl ResultStack {
    fn new() -> Self {
        let ptr = unsafe {
            malloc::m_malloc(ALLOC_PARSE_RESULT_INIT * core::mem::size_of::<ParseNode>())
        } as *mut ParseNode;
        Self {
            ptr,
            cap: if ptr.is_null() { 0 } else { ALLOC_PARSE_RESULT_INIT },
            len: 0,
        }
    }

    fn push(&mut self, pn: ParseNode) -> bool {
        if self.ptr.is_null() {
            return false;
        }
        if self.len >= self.cap {
            let ncap = self.cap + ALLOC_PARSE_RESULT_INC;
            let np = unsafe {
                malloc::m_realloc(self.ptr as *mut u8, ncap * core::mem::size_of::<ParseNode>())
            } as *mut ParseNode;
            if np.is_null() {
                return false;
            }
            self.ptr = np;
            self.cap = ncap;
        }
        unsafe { *self.ptr.add(self.len) = pn };
        self.len += 1;
        true
    }

    fn pop(&mut self) -> ParseNode {
        self.len -= 1;
        unsafe { *self.ptr.add(self.len) }
    }

    fn peek(&self, pos: usize) -> ParseNode {
        unsafe { *self.ptr.add(self.len - 1 - pos) }
    }
}

impl Drop for ResultStack {
    fn drop(&mut self) {
        if !self.ptr.is_null() {
            unsafe { malloc::m_free(self.ptr as *mut u8) };
        }
    }
}

// -- public entry points -----------------------------------------------------

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum InputKind {
    Single,
    File,
    Eval,
}

/// Metal-style `Result` error (see module doc: no `nlr_raise`). Numeric
/// literal errors surface as `Number`/`StringTooLong` rather than a
/// separate `ValueError`, mirroring upstream's own choice to convert
/// `parsenum`'s `ValueError` into a `SyntaxError` when parsing (not
/// calling `int()`/`float()` directly).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ParseError {
    Syntax { line: usize },
    UnexpectedIndent { line: usize },
    UnindentMismatch { line: usize },
    Number { line: usize },
    StringTooLong { line: usize },
    OutOfMemory,
}

struct Parser<'a> {
    rules: RuleStack,
    results: ResultStack,
    lex: &'a mut Lexer,
    chunks: ChunkAllocator,
    input_kind: InputKind,
    backtrack: bool,
}

impl<'a> Parser<'a> {
    fn new(lex: &'a mut Lexer, input_kind: InputKind) -> Result<Self, ParseError> {
        let rules = RuleStack::new();
        let results = ResultStack::new();
        if rules.ptr.is_null() || results.ptr.is_null() {
            return Err(ParseError::OutOfMemory);
        }
        Ok(Self {
            rules,
            results,
            lex,
            chunks: ChunkAllocator::new(),
            input_kind,
            backtrack: false,
        })
    }

    fn syntax_err(&self) -> ParseError {
        let line = self.lex.tok_line;
        match self.lex.tok_kind {
            TokenKind::Indent => ParseError::UnexpectedIndent { line },
            TokenKind::DedentMismatch => ParseError::UnindentMismatch { line },
            _ => ParseError::Syntax { line },
        }
    }

    fn push_rule(&mut self, src_line: usize, rule_id: RuleId, arg_i: usize) -> Result<(), ParseError> {
        if self.rules.push(RuleFrame {
            src_line: src_line as u32,
            rule_id,
            arg_i,
        }) {
            Ok(())
        } else {
            Err(ParseError::OutOfMemory)
        }
    }

    fn pop_rule(&mut self) -> RuleFrame {
        self.rules.pop()
    }

    fn push_result_node(&mut self, pn: ParseNode) -> Result<(), ParseError> {
        if self.results.push(pn) {
            Ok(())
        } else {
            Err(ParseError::OutOfMemory)
        }
    }

    fn pop_result(&mut self) -> ParseNode {
        self.results.pop()
    }

    fn peek_result(&self, pos: usize) -> ParseNode {
        self.results.peek(pos)
    }

    fn alloc_struct(
        &mut self,
        src_line: usize,
        rule_id: RuleId,
        num_args: usize,
    ) -> Result<ParseNode, ParseError> {
        let bytes = core::mem::size_of::<StructHeader>() + num_args * core::mem::size_of::<ParseNode>();
        let p = unsafe { self.chunks.alloc(bytes) };
        if p.is_null() {
            return Err(ParseError::OutOfMemory);
        }
        unsafe {
            let hdr = p as *mut StructHeader;
            (*hdr).source_line = src_line as u32;
            (*hdr).kind_num_nodes = (rule_id as u32) | ((num_args as u32) << 8);
        }
        Ok(p as ParseNode)
    }

    fn make_const_float(&mut self, src_line: usize, v: f64) -> Result<ParseNode, ParseError> {
        let pn = self.alloc_struct(src_line, RuleId::ConstObject, 1)?;
        unsafe { *struct_node_mut(pn, 0) = v.to_bits() as usize };
        Ok(pn)
    }

    /// Convert the lexer's current token into a leaf/const `ParseNode` and
    /// push it (upstream `push_result_token`).
    fn push_result_token(&mut self) -> Result<(), ParseError> {
        let tok_kind = self.lex.tok_kind;
        let tok_line = self.lex.tok_line;
        let pn = match tok_kind {
            TokenKind::Name => {
                let id = qstr::from_strn(self.lex.tok_text());
                new_leaf(LEAF_ID, id)
            }
            TokenKind::Integer => {
                let v = super::parsenum::parse_int(self.lex.tok_text(), 0)
                    .map_err(|_| ParseError::Number { line: tok_line })?;
                if !small_int_fits(v) {
                    // No bigint fallback (see parsenum.rs doc) -- honest error.
                    return Err(ParseError::Number { line: tok_line });
                }
                new_small_int(v as isize)
            }
            TokenKind::FloatOrImag => {
                let v = super::parsenum::parse_float(self.lex.tok_text())
                    .map_err(|_| ParseError::Number { line: tok_line })?;
                self.make_const_float(tok_line, v)?
            }
            TokenKind::String => {
                let text = self.lex.tok_text();
                let id = qstr::from_strn(text);
                if id == QSTR_NULL && !text.is_empty() {
                    return Err(ParseError::StringTooLong { line: tok_line });
                }
                new_leaf(LEAF_STRING, id)
            }
            TokenKind::Bytes => {
                let text = self.lex.tok_text();
                let id = qstr::from_strn(text);
                if id == QSTR_NULL && !text.is_empty() {
                    return Err(ParseError::StringTooLong { line: tok_line });
                }
                new_leaf(LEAF_BYTES, id)
            }
            _ => new_leaf(LEAF_TOKEN, tok_kind as usize),
        };
        self.push_result_node(pn)
    }

    /// Finalise a matched rule into a result node (upstream
    /// `push_result_rule`, minus the `MICROPY_COMP_CONST*` optimisation
    /// passes -- see module doc). The 3 unconditional tree-shaping special
    /// cases (`atom_paren`, `testlist_comp`, `testlist_comp_3c`) are grammar
    /// shape, not optimisation, and are ported faithfully.
    fn push_result_rule(
        &mut self,
        src_line: usize,
        rule_id: RuleId,
        mut num_args: usize,
    ) -> Result<(), ParseError> {
        if rule_id == RuleId::AtomParen {
            let pn = self.peek_result(0);
            if is_null(pn) {
                // keep parens for `()`
            } else if is_struct_kind(pn, RuleId::TestlistComp) {
                // keep parens for `(a, b, ...)`
            } else {
                // parens around a single expression: unwrap (leave as-is)
                return Ok(());
            }
        } else if rule_id == RuleId::TestlistComp {
            debug_assert_eq!(num_args, 2);
            let pn = self.peek_result(0);
            if is_struct(pn) {
                let kind = struct_kind(pn);
                if kind == RuleId::TestlistComp3b {
                    // tuple of one item, with trailing comma
                    self.pop_result();
                    num_args -= 1;
                } else if kind == RuleId::TestlistComp3c {
                    // tuple of many items: relabel testlist_comp_3c in place
                    self.pop_result();
                    debug_assert_eq!(pn, self.peek_result(0));
                    unsafe {
                        let hdr = pn as *mut StructHeader;
                        let nn = (*hdr).kind_num_nodes >> 8;
                        (*hdr).kind_num_nodes = (RuleId::TestlistComp as u32) | (nn << 8);
                    }
                    return Ok(());
                }
                // else: comp_for (generator expr) or a plain 2-item tuple -- fall through
            }
        } else if rule_id == RuleId::TestlistComp3c {
            // steal the first arg of the outer testlist_comp rule (see module
            // doc on push_result_rule / grammar.h comment above testlist_comp)
            num_args += 1;
        }

        let pn = self.alloc_struct(src_line, rule_id, num_args)?;
        for i in (0..num_args).rev() {
            let child = self.pop_result();
            unsafe { *struct_node_mut(pn, i) = child };
        }
        if rule_id == RuleId::TestlistComp3c {
            // pushed twice: the enclosing and_ident wrapper (testlist_comp_3b)
            // consumes one copy as its own result, leaving the other as the
            // outer testlist_comp's stolen first arg (see module doc).
            self.push_result_node(pn)?;
        }
        self.push_result_node(pn)
    }

    fn step_or(&mut self, frame: RuleFrame) -> Result<(), ParseError> {
        let rule_id = frame.rule_id;
        let act = grammar::RULE_ACT_TABLE[rule_id as usize];
        let n = (act & grammar::RULE_ACT_ARG_MASK) as usize;
        let args = grammar::RULE_ARGS[rule_id as usize];
        let mut i = frame.arg_i;
        let src_line = frame.src_line as usize;

        if i > 0 && !self.backtrack {
            return Ok(());
        }
        self.backtrack = false;

        while i < n {
            match args[i] {
                RuleArg::Tok(tk) => {
                    if self.lex.tok_kind == tk {
                        self.push_result_token()?;
                        self.lex.to_next();
                        return Ok(());
                    }
                }
                RuleArg::Rule(r) | RuleArg::OptRule(r) => {
                    if i + 1 < n {
                        self.push_rule(src_line, rule_id, i + 1)?;
                    }
                    self.push_rule(self.lex.tok_line, r, 0)?;
                    return Ok(());
                }
            }
            i += 1;
        }
        self.backtrack = true;
        Ok(())
    }

    fn step_and(&mut self, frame: RuleFrame) -> Result<(), ParseError> {
        let rule_id = frame.rule_id;
        let act = grammar::RULE_ACT_TABLE[rule_id as usize];
        let n = (act & grammar::RULE_ACT_ARG_MASK) as usize;
        let args = grammar::RULE_ARGS[rule_id as usize];
        let mut i = frame.arg_i;
        let src_line = frame.src_line as usize;

        if self.backtrack {
            debug_assert!(i > 0);
            if matches!(args[i - 1], RuleArg::OptRule(_)) {
                self.push_result_node(PARSE_NODE_NULL)?;
                self.backtrack = false;
            } else if i > 1 {
                return Err(self.syntax_err());
            } else {
                return Ok(());
            }
        }

        while i < n {
            match args[i] {
                RuleArg::Tok(tk) => {
                    if self.lex.tok_kind == tk {
                        if tk == TokenKind::Name {
                            self.push_result_token()?;
                        }
                        self.lex.to_next();
                    } else if i > 0 {
                        return Err(self.syntax_err());
                    } else {
                        self.backtrack = true;
                        return Ok(());
                    }
                }
                RuleArg::Rule(r) | RuleArg::OptRule(r) => {
                    self.push_rule(src_line, rule_id, i + 1)?;
                    self.push_rule(self.lex.tok_line, r, 0)?;
                    return Ok(());
                }
            }
            i += 1;
        }
        debug_assert_eq!(i, n);

        // Lone expression-statement becomes `pass` (upstream's default
        // `!MICROPY_ENABLE_DOC_STRING` behaviour -- discards e.g. a bare
        // docstring/number/bytes/`...`/`None`/`True`/`False` statement).
        if self.input_kind != InputKind::Single
            && rule_id == RuleId::ExprStmt
            && is_null(self.peek_result(0))
        {
            let p = self.peek_result(1);
            if (is_leaf(p) && !is_id(p)) || is_struct_kind(p, RuleId::ConstObject) {
                self.pop_result();
                self.pop_result();
                self.push_result_rule(src_line, RuleId::PassStmt, 0)?;
                return Ok(());
            }
        }

        // count number of arguments for the parse node
        let mut cnt = 0usize;
        let mut num_not_nil = 0usize;
        for x in (0..n).rev() {
            match args[x] {
                RuleArg::Tok(tk) => {
                    if tk == TokenKind::Name {
                        cnt += 1;
                        num_not_nil += 1;
                    }
                }
                RuleArg::Rule(_) | RuleArg::OptRule(_) => {
                    if !is_null(self.peek_result(cnt)) {
                        num_not_nil += 1;
                    }
                    cnt += 1;
                }
            }
        }

        if num_not_nil == 1 && (act & grammar::RULE_ACT_ALLOW_IDENT) != 0 {
            // this rule has only 1 argument and should not be emitted
            let mut pn = PARSE_NODE_NULL;
            for _ in 0..cnt {
                let pn2 = self.pop_result();
                if !is_null(pn2) {
                    pn = pn2;
                }
            }
            self.push_result_node(pn)?;
        } else {
            if (act & grammar::RULE_ACT_ADD_BLANK) != 0 {
                self.push_result_node(PARSE_NODE_NULL)?;
                cnt += 1;
            }
            self.push_result_rule(src_line, rule_id, cnt)?;
        }
        Ok(())
    }

    fn step_list(&mut self, frame: RuleFrame) -> Result<(), ParseError> {
        let rule_id = frame.rule_id;
        let act = grammar::RULE_ACT_TABLE[rule_id as usize];
        let n = (act & grammar::RULE_ACT_ARG_MASK) as usize;
        let args = grammar::RULE_ARGS[rule_id as usize];
        let mut i = frame.arg_i;
        let src_line = frame.src_line as usize;

        let had_trailing_sep = if self.backtrack {
            match list_backtrack(n, i) {
                ListBt::Propagate => return Ok(()),
                ListBt::Finish(sep) => {
                    self.backtrack = false;
                    sep
                }
                ListBt::SyntaxErr => return Err(self.syntax_err()),
            }
        } else {
            loop {
                let idx = i & 1 & n;
                match args[idx] {
                    RuleArg::Tok(tk) => {
                        if self.lex.tok_kind == tk {
                            if idx == 0 {
                                self.push_result_token()?;
                            }
                            self.lex.to_next();
                            i += 1;
                        } else {
                            i += 1;
                            match list_backtrack(n, i) {
                                ListBt::Propagate => {
                                    self.backtrack = true;
                                    return Ok(());
                                }
                                ListBt::Finish(sep) => break sep,
                                ListBt::SyntaxErr => return Err(self.syntax_err()),
                            }
                        }
                    }
                    RuleArg::Rule(r) | RuleArg::OptRule(r) => {
                        self.push_rule(src_line, rule_id, i + 1)?;
                        self.push_rule(self.lex.tok_line, r, 0)?;
                        return Ok(());
                    }
                }
            }
        };

        debug_assert!(i >= 1);
        i -= 1;
        if (n & 1) != 0 && matches!(args[1], RuleArg::Tok(_)) {
            // don't count separators when they are tokens
            i = (i + 1) / 2;
        }

        if i == 1 {
            if had_trailing_sep {
                self.push_result_rule(src_line, rule_id, i)?;
            }
            // else: just leave the single item on the stack
        } else {
            self.push_result_rule(src_line, rule_id, i)?;
        }
        Ok(())
    }

    fn run(&mut self) -> Result<(), ParseError> {
        while !self.rules.is_empty() {
            let frame = self.pop_rule();
            let act = grammar::RULE_ACT_TABLE[frame.rule_id as usize];
            match act & grammar::RULE_ACT_KIND_MASK {
                grammar::RULE_ACT_OR => self.step_or(frame)?,
                grammar::RULE_ACT_AND => self.step_and(frame)?,
                _ => self.step_list(frame)?,
            }
        }
        Ok(())
    }
}

enum ListBt {
    /// This list rule failed on its very first item: propagate the failure
    /// (`backtrack` stays true) to whatever pushed this list rule.
    Propagate,
    /// The list is done; `bool` is whether it ended on a trailing separator.
    Finish(bool),
    /// Already consumed tokens for this list and it doesn't allow a
    /// trailing separator: a hard syntax error, no backtrack possible.
    SyntaxErr,
}

fn list_backtrack(n: usize, i: usize) -> ListBt {
    if n == 2 {
        if i == 1 {
            ListBt::Propagate
        } else {
            ListBt::Finish(false)
        }
    } else if i == 1 {
        ListBt::Propagate
    } else if (i & 1) == 1 {
        if n == 3 {
            ListBt::Finish(true)
        } else {
            ListBt::SyntaxErr
        }
    } else {
        ListBt::Finish(false)
    }
}

/// Parse `lex`'s token stream into a `ParseTree` (upstream `mp_parse`).
///
/// Does **not** take ownership of `lex` or free it (Metal keeps the lexer
/// borrowed so a caller/smoke test can still inspect its final `tok_kind`);
/// this differs from upstream, which frees its lexer unconditionally --
/// documented in the module/task brief as the intended Metal shape.
pub fn parse(lex: &mut Lexer, kind: InputKind) -> Result<ParseTree, ParseError> {
    let mut p = Parser::new(lex, kind)?;

    let top_rule = match kind {
        InputKind::Single => grammar::RULE_SINGLE_INPUT,
        InputKind::Eval => grammar::RULE_EVAL_INPUT,
        InputKind::File => grammar::RULE_FILE_INPUT,
    };
    let start_line = p.lex.tok_line;
    p.push_rule(start_line, top_rule, 0)?;
    p.run()?;

    if p.lex.tok_kind != TokenKind::End || p.results.len == 0 {
        return Err(p.syntax_err());
    }
    debug_assert_eq!(p.results.len, 1);
    let root = p.pop_result();
    let chunks = unsafe { p.chunks.take_all() };
    Ok(ParseTree { root, chunks })
}
