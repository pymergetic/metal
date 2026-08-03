//! compile — parse tree -> bytecode (upstream `py/compile.c` mirror
//! slice, generic over an [`Emit`] backend so a future native emitter
//! could reuse this pass unchanged — see `emit.rs` module doc).
//!
//! ## What this compiler slice supports (MicroPython-core bar)
//!
//! - **Module / eval**: statements and expressions as upstream expects;
//!   module bodies end in implicit `return None`; eval returns the expr.
//! - **Control flow**: `if`/`elif`/`else`, `while`/`for` (with `else`),
//!   `break`/`continue`, `try`/`except`/`else`/`finally`, `with` (one item).
//! - **Functions / classes**: top-level and nested `def`, `lambda`, simple
//!   `class C:` / `class C():` (no bases yet; methods + class-level
//!   `name = const` bindings only in the body).
//! - **Assignments**: name, attribute, subscript; augmented (`+=`, …) except
//!   `/=` `@=` `**=`; no chained or tuple-unpacking assignment.
//! - **Expressions**: literals, calls, attrs, subscripts, list/tuple/dict/set
//!   literals, list comprehensions (single `for`, no nested/filter forms),
//!   short-circuit `and`/`or`, ternary `a if c else b`, `in`/`not in`/`is`/
//!   `is not` (single comparison only), unary/binary ops (no `**`).
//! - **Imports**: `import` / `from … import` at module or function scope
//!   (one dotted target per statement, no comma lists).
//!
//! ## Deliberate omissions (honest gaps, not silent miscompiles)
//!
//! `async`/`await`, generators/`yield`, f-strings, `global`/`nonlocal`,
//! closures, keyword/`*args`/`**kwargs` calls, comma-separated imports,
//! chained comparisons, dict/set comprehensions, slice subscripts, float/
//! bytes constants, bare `raise` / `raise from`, `/=` `@=` `**=`/`**`,
//! matrix multiply, true divide, decorated defs, multiple `with` items,
//! class bases (`class C(Base):`), multi-base classes, and tuple-unpacking
//! targets all return `CompileError::Unsupported`.

use crate::upy::py::emit::{Emit, EmitError, JumpHole};
use crate::upy::py::emitbc::Writer;
use crate::upy::py::emitcommon;
use crate::upy::py::emitglue::{self, RawCode};
use crate::upy::py::grammar::RuleId;
use crate::upy::py::lexer::TokenKind;
use crate::upy::py::obj;
use crate::upy::py::parse::{self, ParseNode, ParseTree};
use crate::upy::py::qstrdefs::Qstr;
use crate::upy::py::objects::objtype;
use crate::upy::py::qstr;
use crate::upy::py::scope::{Scope, ScopeKind};

const MAX_LOOP_DEPTH: usize = 8;
const MAX_BREAK_JUMPS: usize = 16;

fn qstr_listcomp_tmp() -> Qstr {
    qstr::from_str("__listcomp")
}

fn qstr_with_tmp() -> Qstr {
    qstr::from_str("__with")
}

struct LoopFrame {
    continue_at: usize,
    break_start: usize,
}

impl Copy for LoopFrame {}
impl Clone for LoopFrame {
    fn clone(&self) -> Self {
        *self
    }
}

struct LoopStack {
    frames: [LoopFrame; MAX_LOOP_DEPTH],
    depth: usize,
    break_holes: [JumpHole; MAX_BREAK_JUMPS],
    n_breaks: usize,
}

impl LoopStack {
    const fn new() -> Self {
        Self {
            frames: [LoopFrame {
                continue_at: 0,
                break_start: 0,
            }; MAX_LOOP_DEPTH],
            depth: 0,
            break_holes: [JumpHole { offset_at: 0 }; MAX_BREAK_JUMPS],
            n_breaks: 0,
        }
    }

    fn push(&mut self, continue_at: usize) {
        self.frames[self.depth] = LoopFrame {
            continue_at,
            break_start: self.n_breaks,
        };
        self.depth += 1;
    }

    fn pop<E: Emit>(&mut self, w: &mut E, end: usize) -> Result<(), CompileError> {
        if self.depth == 0 {
            return Ok(());
        }
        self.depth -= 1;
        let start = self.frames[self.depth].break_start;
        for i in start..self.n_breaks {
            w.patch_jump(self.break_holes[i], end)?;
        }
        self.n_breaks = start;
        Ok(())
    }

    fn emit_break<E: Emit>(
        &mut self,
        w: &mut E,
        pn: ParseNode,
        line: u32,
    ) -> Result<(), CompileError> {
        if self.depth == 0 || self.n_breaks >= MAX_BREAK_JUMPS {
            return Err(unsupported(pn, line));
        }
        self.break_holes[self.n_breaks] = w.jump()?;
        self.n_breaks += 1;
        Ok(())
    }

    fn emit_continue<E: Emit>(
        &mut self,
        w: &mut E,
        pn: ParseNode,
        line: u32,
    ) -> Result<(), CompileError> {
        if self.depth == 0 {
            return Err(unsupported(pn, line));
        }
        let target = self.frames[self.depth - 1].continue_at;
        let hole = w.jump()?;
        w.patch_jump(hole, target)?;
        Ok(())
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CompileError {
    OutOfMemory,
    /// A function body assigns more than `scope::MAX_LOCALS` distinct
    /// names -- a real, documented cap (see `scope.rs`), not silent
    /// truncation.
    TooManyLocals,
    /// A construct outside the supported subset (see module doc).
    /// `line` is the best-effort enclosing source line (0 if the
    /// offending node is a leaf with no line info of its own).
    Unsupported { line: u32 },
    /// `compile_funcdef_body`'s specific preconditions (single top-level
    /// `def` with unsupported parameter shapes) weren't met.
    NotAFuncdef,
}

impl From<EmitError> for CompileError {
    fn from(e: EmitError) -> Self {
        match e {
            EmitError::OutOfMemory => CompileError::OutOfMemory,
        }
    }
}

fn node_line(pn: ParseNode, fallback: u32) -> u32 {
    if parse::is_struct(pn) {
        parse::struct_source_line(pn)
    } else {
        fallback
    }
}

fn unsupported(pn: ParseNode, fallback: u32) -> CompileError {
    CompileError::Unsupported {
        line: node_line(pn, fallback),
    }
}

// -- pass 1: collect function-local names (upstream `scope.c`'s id-info
// collection pass) -----------------------------------------------------

/// Whether `pn` (an `ExprStmt`'s node[1]) is one of the assignment-chain
/// wrapper kinds this compiler doesn't support (augmented/annotated/
/// chained assignment) -- as opposed to a plain value expression.
fn is_assign_machinery(pn: ParseNode) -> bool {
    parse::is_struct(pn)
        && matches!(
            parse::struct_kind(pn),
            RuleId::ExprStmt2
                | RuleId::ExprStmtAugassign
                | RuleId::ExprStmtAssignList
                | RuleId::Annassign
        )
}

fn prescan_assign_target(scope: &mut Scope, pn: ParseNode) -> Result<(), CompileError> {
    if parse::is_id(pn) {
        let qst = parse::leaf_arg(pn) as Qstr;
        if scope.add_local(qst).is_none() {
            return Err(CompileError::TooManyLocals);
        }
        return Ok(());
    }
    if parse::is_struct_kind(pn, RuleId::AtomExprNormal) {
        // attr/subscript targets do not allocate locals here.
        return Ok(());
    }
    Err(unsupported(pn, 0))
}

fn prescan_exprlist_target(scope: &mut Scope, pn: ParseNode) -> Result<(), CompileError> {
    if parse::is_null(pn) {
        return Ok(());
    }
    if parse::is_struct_kind(pn, RuleId::Exprlist) {
        if parse::struct_num_nodes(pn) != 1 {
            return Err(unsupported(pn, 0));
        }
        return prescan_assign_target(scope, unsafe { parse::struct_node(pn, 0) });
    }
    prescan_assign_target(scope, pn)
}

fn prescan_class_body(scope: &mut Scope, pn: ParseNode) -> Result<(), CompileError> {
    if parse::is_null(pn) || !parse::is_struct(pn) {
        return Ok(());
    }
    match parse::struct_kind(pn) {
        RuleId::SuiteBlockStmts => {
            for i in 0..parse::struct_num_nodes(pn) {
                prescan_class_body(scope, unsafe { parse::struct_node(pn, i) })?;
            }
            Ok(())
        }
        RuleId::Funcdef => {
            let mut fn_scope = Scope::new(ScopeKind::Function);
            let params = unsafe { parse::struct_node(pn, 1) };
            let body = unsafe { parse::struct_node(pn, 3) };
            collect_simple_params(&mut fn_scope, params, 0)?;
            prescan_locals(&mut fn_scope, body)?;
            Ok(())
        }
        _ => Ok(()),
    }
}

/// Recursively collect local names for function-scope compilation.
fn prescan_locals(scope: &mut Scope, pn: ParseNode) -> Result<(), CompileError> {
    if parse::is_null(pn) || !parse::is_struct(pn) {
        return Ok(());
    }
    match parse::struct_kind(pn) {
        RuleId::FileInput2 | RuleId::SimpleStmt2 | RuleId::SuiteBlockStmts => {
            for i in 0..parse::struct_num_nodes(pn) {
                prescan_locals(scope, unsafe { parse::struct_node(pn, i) })?;
            }
            Ok(())
        }
        RuleId::ExprStmt => {
            let target = unsafe { parse::struct_node(pn, 0) };
            let value = unsafe { parse::struct_node(pn, 1) };
            if parse::is_null(value) {
                return Ok(());
            }
            if parse::is_struct_kind(value, RuleId::ExprStmtAugassign) {
                return prescan_assign_target(scope, target);
            }
            if !is_assign_machinery(value) && parse::is_id(target) {
                let qst = parse::leaf_arg(target) as Qstr;
                if scope.add_local(qst).is_none() {
                    return Err(CompileError::TooManyLocals);
                }
            }
            Ok(())
        }
        RuleId::ForStmt => {
            prescan_exprlist_target(scope, unsafe { parse::struct_node(pn, 0) })?;
            prescan_locals(scope, unsafe { parse::struct_node(pn, 2) })?;
            if !parse::is_null(unsafe { parse::struct_node(pn, 3) }) {
                let else_stmt = unsafe { parse::struct_node(pn, 3) };
                prescan_locals(scope, unsafe { parse::struct_node(else_stmt, 0) })?;
            }
            Ok(())
        }
        RuleId::WhileStmt | RuleId::IfStmt => {
            prescan_locals(scope, unsafe { parse::struct_node(pn, 1) })?;
            Ok(())
        }
        RuleId::TryStmt => {
            prescan_locals(scope, unsafe { parse::struct_node(pn, 0) })?;
            let rest = unsafe { parse::struct_node(pn, 1) };
            prescan_try_rest(scope, rest)?;
            Ok(())
        }
        RuleId::WithStmt => {
            let items = unsafe { parse::struct_node(pn, 0) };
            if parse::is_struct_kind(items, RuleId::WithStmtList)
                && parse::struct_num_nodes(items) == 1
            {
                let item = unsafe { parse::struct_node(items, 0) };
                if parse::is_struct_kind(item, RuleId::WithItem) {
                    let as_part = unsafe { parse::struct_node(item, 1) };
                    if !parse::is_null(as_part) {
                        let name = unsafe { parse::struct_node(as_part, 0) };
                        if parse::is_id(name) {
                            let qst = parse::leaf_arg(name) as Qstr;
                            if scope.add_local(qst).is_none() {
                                return Err(CompileError::TooManyLocals);
                            }
                        }
                    }
                }
            }
            prescan_locals(scope, unsafe { parse::struct_node(pn, 1) })?;
            Ok(())
        }
        RuleId::Funcdef => {
            let name = unsafe { parse::struct_node(pn, 0) };
            if scope.is_function() && parse::is_id(name) {
                let qst = parse::leaf_arg(name) as Qstr;
                if scope.add_local(qst).is_none() {
                    return Err(CompileError::TooManyLocals);
                }
            }
            let mut inner = Scope::new(ScopeKind::Function);
            let params = unsafe { parse::struct_node(pn, 1) };
            let body = unsafe { parse::struct_node(pn, 3) };
            collect_simple_params(&mut inner, params, 0)?;
            prescan_locals(&mut inner, body)?;
            Ok(())
        }
        RuleId::Classdef => {
            let name = unsafe { parse::struct_node(pn, 0) };
            if scope.is_function() && parse::is_id(name) {
                let qst = parse::leaf_arg(name) as Qstr;
                if scope.add_local(qst).is_none() {
                    return Err(CompileError::TooManyLocals);
                }
            }
            prescan_class_body(scope, unsafe { parse::struct_node(pn, 2) })?;
            Ok(())
        }
        RuleId::ImportFrom => {
            prescan_import_from_names(scope, unsafe { parse::struct_node(pn, 1) })?;
            Ok(())
        }
        RuleId::ImportName => prescan_import_name(scope, pn),
        _ => Ok(()),
    }
}

fn prescan_import_from_names(scope: &mut Scope, pn: ParseNode) -> Result<(), CompileError> {
    let list = if parse::is_struct_kind(pn, RuleId::ImportAsNamesParen) {
        unsafe { parse::struct_node(pn, 0) }
    } else {
        pn
    };
    if parse::is_struct_kind(list, RuleId::ImportAsNames) {
        if parse::struct_num_nodes(list) != 1 {
            return Err(unsupported(list, 0));
        }
        prescan_one_import_as(scope, unsafe { parse::struct_node(list, 0) })
    } else {
        prescan_one_import_as(scope, list)
    }
}

fn prescan_one_import_as(scope: &mut Scope, one: ParseNode) -> Result<(), CompileError> {
    if !parse::is_struct_kind(one, RuleId::ImportAsName) {
        return Ok(());
    }
    let name = unsafe { parse::struct_node(one, 0) };
    let alias = unsafe { parse::struct_node(one, 1) };
    let qst = if parse::is_null(alias) {
        parse::leaf_arg(name) as Qstr
    } else {
        parse::leaf_arg(alias) as Qstr
    };
    if scope.add_local(qst).is_none() {
        return Err(CompileError::TooManyLocals);
    }
    Ok(())
}

fn prescan_import_name(scope: &mut Scope, pn: ParseNode) -> Result<(), CompileError> {
    if !scope.is_function() {
        return Ok(());
    }
    let names = unsafe { parse::struct_node(pn, 0) };
    let one = if parse::is_struct_kind(names, RuleId::DottedAsNames) {
        if parse::struct_num_nodes(names) != 1 {
            return Err(unsupported(pn, 0));
        }
        unsafe { parse::struct_node(names, 0) }
    } else {
        names
    };
    let bind_q = if parse::is_struct_kind(one, RuleId::DottedAsName) {
        let alias = unsafe { parse::struct_node(one, 1) };
        if parse::is_null(alias) {
            let dotted = unsafe { parse::struct_node(one, 0) };
            parse::leaf_arg(unsafe { parse::struct_node(dotted, 0) }) as Qstr
        } else {
            parse::leaf_arg(alias) as Qstr
        }
    } else if parse::is_struct_kind(one, RuleId::DottedName) {
        parse::leaf_arg(unsafe { parse::struct_node(one, 0) }) as Qstr
    } else {
        return Ok(());
    };
    if scope.add_local(bind_q).is_none() {
        return Err(CompileError::TooManyLocals);
    }
    Ok(())
}

fn prescan_try_rest(scope: &mut Scope, pn: ParseNode) -> Result<(), CompileError> {
    if parse::is_null(pn) || !parse::is_struct(pn) {
        return Ok(());
    }
    match parse::struct_kind(pn) {
        RuleId::TryStmtExceptAndMore => {
            let excepts = unsafe { parse::struct_node(pn, 0) };
            if parse::is_struct_kind(excepts, RuleId::TryStmtExceptList) {
                let n = parse::struct_num_nodes(excepts);
                for i in 0..n {
                    let ex = unsafe { parse::struct_node(excepts, i) };
                    prescan_except_clause(scope, ex)?;
                }
            }
            let else_part = unsafe { parse::struct_node(pn, 1) };
            if !parse::is_null(else_part) {
                prescan_locals(scope, unsafe { parse::struct_node(else_part, 0) })?;
            }
            let fin = unsafe { parse::struct_node(pn, 2) };
            if !parse::is_null(fin) {
                prescan_locals(scope, unsafe { parse::struct_node(fin, 0) })?;
            }
            Ok(())
        }
        RuleId::TryStmtFinally => {
            prescan_locals(scope, unsafe { parse::struct_node(pn, 0) })?;
            Ok(())
        }
        _ => Ok(()),
    }
}

fn prescan_except_clause(scope: &mut Scope, pn: ParseNode) -> Result<(), CompileError> {
    if !parse::is_struct_kind(pn, RuleId::TryStmtExcept) {
        return Ok(());
    }
    let as_part = unsafe { parse::struct_node(pn, 0) };
    if !parse::is_null(as_part) && parse::is_struct_kind(as_part, RuleId::TryStmtAsName) {
        let bind = unsafe { parse::struct_node(as_part, 1) };
        if !parse::is_null(bind) && parse::is_struct_kind(bind, RuleId::AsName) {
            let name = unsafe { parse::struct_node(bind, 0) };
            if parse::is_id(name) {
                let qst = parse::leaf_arg(name) as Qstr;
                if scope.add_local(qst).is_none() {
                    return Err(CompileError::TooManyLocals);
                }
            }
        }
    }
    prescan_locals(scope, unsafe { parse::struct_node(pn, 1) })
}

/// Register simple positional parameters as the first local slots (no
/// defaults, `*args`, `**kwargs`, or annotations).
///
/// A single-parameter list often collapses to a bare id (`def f(self):`
/// -> params = `id(self)`), matching upstream parse collapse.
fn collect_simple_params(
    scope: &mut Scope,
    pn: ParseNode,
    line: u32,
) -> Result<(), CompileError> {
    if parse::is_null(pn) {
        return Ok(());
    }
    if parse::is_id(pn) {
        return collect_one_simple_param(scope, pn, line);
    }
    if !parse::is_struct_kind(pn, RuleId::Typedargslist) {
        // One wrapped param that lost the Typedargslist shell.
        return collect_one_simple_param(scope, pn, line);
    }
    let n = parse::struct_num_nodes(pn);
    for i in 0..n {
        let item = unsafe { parse::struct_node(pn, i) };
        collect_one_simple_param(scope, item, line)?;
    }
    Ok(())
}

fn collect_one_simple_param(
    scope: &mut Scope,
    pn: ParseNode,
    line: u32,
) -> Result<(), CompileError> {
    if parse::is_id(pn) {
        let qst = parse::leaf_arg(pn) as Qstr;
        if scope.add_local(qst).is_none() {
            return Err(CompileError::TooManyLocals);
        }
        return Ok(());
    }
    let mut item = pn;
    if parse::is_struct_kind(item, RuleId::TypedargslistItem) {
        if parse::struct_num_nodes(item) != 1 {
            return Err(unsupported(item, line));
        }
        item = unsafe { parse::struct_node(item, 0) };
    }
    if parse::is_struct_kind(item, RuleId::TypedargslistName) {
        if parse::struct_num_nodes(item) == 0 {
            return Err(unsupported(item, line));
        }
        let name = unsafe { parse::struct_node(item, 0) };
        if !parse::is_id(name) {
            return Err(unsupported(name, line));
        }
        for i in 1..parse::struct_num_nodes(item) {
            if !parse::is_null(unsafe { parse::struct_node(item, i) }) {
                // Type hints and/or default values -- not in this slice.
                return Err(unsupported(item, line));
            }
        }
        let qst = parse::leaf_arg(name) as Qstr;
        if scope.add_local(qst).is_none() {
            return Err(CompileError::TooManyLocals);
        }
        Ok(())
    } else {
        // `*args` / `**kwargs` -- not in this slice.
        Err(unsupported(item, line))
    }
}

fn compile_function_body(
    scope: &Scope,
    body: ParseNode,
    line: u32,
) -> Result<RawCode, CompileError> {
    let mut w = Writer::new()?;
    let mut loops = LoopStack::new();
    compile_stmt(&mut w, &mut loops, scope, body, line)?;
    w.load_const_none()?;
    w.return_value()?;
    Ok(w.finish(scope.num_locals()))
}

// -- expressions ----------------------------------------------------------

fn store_name_or_fast<E: Emit>(
    w: &mut E,
    scope: &Scope,
    qst: Qstr,
) -> Result<(), CompileError> {
    if scope.is_function() {
        let slot = scope
            .find_local(qst)
            .expect("prescan_locals must have allocated every assigned local");
        Ok(w.store_fast(slot)?)
    } else {
        Ok(w.store_name(qst)?)
    }
}

fn load_name_or_fast<E: Emit>(
    w: &mut E,
    scope: &Scope,
    qst: Qstr,
    _pn: ParseNode,
    _line: u32,
) -> Result<(), CompileError> {
    if scope.is_function() {
        if let Some(slot) = scope.find_local(qst) {
            Ok(w.load_fast(slot)?)
        } else {
            Ok(w.load_name(qst)?)
        }
    } else {
        Ok(w.load_name(qst)?)
    }
}

fn is_comprehension_node(pn: ParseNode) -> bool {
    if parse::is_null(pn) || !parse::is_struct(pn) {
        return false;
    }
    matches!(
        parse::struct_kind(pn),
        RuleId::CompFor | RuleId::TestlistComp3 | RuleId::TestlistComp3b | RuleId::TestlistComp3c
    )
}

fn compile_testlist_comp<E: Emit>(
    w: &mut E,
    scope: &Scope,
    pn: ParseNode,
    line: u32,
    build: fn(&mut E, u16) -> Result<(), EmitError>,
) -> Result<(), CompileError> {
    if try_compile_list_comp(w, scope, pn, line)? {
        return Ok(());
    }
    if parse::is_null(pn) {
        return Ok(build(w, 0)?);
    }
    if !parse::is_struct_kind(pn, RuleId::TestlistComp) {
        return Err(unsupported(pn, line));
    }
    let n = parse::struct_num_nodes(pn);
    if n >= 2 {
        let second = unsafe { parse::struct_node(pn, 1) };
        if !parse::is_null(second) {
            if is_comprehension_node(second) {
                return Err(unsupported(pn, line));
            }
            if parse::is_struct_kind(second, RuleId::TestlistComp3) {
                let head = unsafe { parse::struct_node(second, 0) };
                if is_comprehension_node(head) {
                    return Err(unsupported(pn, line));
                }
            }
        }
    }
    for i in 0..n {
        let item = unsafe { parse::struct_node(pn, i) };
        if is_comprehension_node(item) {
            return Err(unsupported(pn, line));
        }
        compile_expr(w, scope, item, line)?;
    }
    Ok(build(w, n as u16)?)
}

/// Unwrap `trailer_bracket`'s single index expression.
///
/// Upstream parse collapse often leaves a bare `test` / small-int / id under
/// the bracket (e.g. `{1:2}[1]` -> `TrailerBracket(int 1)`), not a full
/// `Subscript` / `Subscriptlist` wrapper. Slice forms still come through as
/// multi-child `Subscript*` and are rejected here.
fn trailer_bracket_index(pn: ParseNode, line: u32) -> Result<ParseNode, CompileError> {
    if !parse::is_struct_kind(pn, RuleId::TrailerBracket) {
        return Err(unsupported(pn, line));
    }
    if parse::struct_num_nodes(pn) != 1 {
        return Err(unsupported(pn, line));
    }
    let mut cur = unsafe { parse::struct_node(pn, 0) };
    if parse::is_null(cur) {
        return Err(unsupported(pn, line));
    }
    if parse::is_struct_kind(cur, RuleId::Subscriptlist) {
        if parse::struct_num_nodes(cur) != 1 {
            return Err(unsupported(pn, line));
        }
        cur = unsafe { parse::struct_node(cur, 0) };
    }
    if parse::is_struct_kind(cur, RuleId::Subscript) {
        // `subscript: subscript_3 | subscript_2`; simple index is one child.
        if parse::struct_num_nodes(cur) != 1 {
            return Err(unsupported(pn, line));
        }
        cur = unsafe { parse::struct_node(cur, 0) };
    }
    if parse::is_struct_kind(cur, RuleId::Subscript2) {
        // `test [subscript_3]`; reject when the optional slice tail is present.
        let n = parse::struct_num_nodes(cur);
        if n == 0 {
            return Err(unsupported(pn, line));
        }
        if n >= 2 && !parse::is_null(unsafe { parse::struct_node(cur, 1) }) {
            return Err(unsupported(pn, line));
        }
        return Ok(unsafe { parse::struct_node(cur, 0) });
    }
    if parse::is_struct_kind(cur, RuleId::Subscript3)
        || parse::is_struct_kind(cur, RuleId::Subscript3b)
        || parse::is_struct_kind(cur, RuleId::Subscript3c)
        || parse::is_struct_kind(cur, RuleId::Subscript3d)
    {
        return Err(unsupported(pn, line));
    }
    Ok(cur)
}

/// If `pn` is `base[index]` (one subscript, no slice), return `(base, index)`.
fn parse_subscript_store_target(
    pn: ParseNode,
    line: u32,
) -> Result<(ParseNode, ParseNode), CompileError> {
    if !parse::is_struct_kind(pn, RuleId::AtomExprNormal) {
        return Err(unsupported(pn, line));
    }
    if parse::struct_num_nodes(pn) != 2 {
        return Err(unsupported(pn, line));
    }
    let base = unsafe { parse::struct_node(pn, 0) };
    let trailers = unsafe { parse::struct_node(pn, 1) };
    let trailer = if parse::is_struct_kind(trailers, RuleId::AtomExprTrailers) {
        if parse::struct_num_nodes(trailers) != 1 {
            return Err(unsupported(pn, line));
        }
        unsafe { parse::struct_node(trailers, 0) }
    } else {
        trailers
    };
    let index = trailer_bracket_index(trailer, line)?;
    Ok((base, index))
}

fn parse_attr_store_target(
    pn: ParseNode,
    line: u32,
) -> Result<(ParseNode, Qstr), CompileError> {
    if !parse::is_struct_kind(pn, RuleId::AtomExprNormal) {
        return Err(unsupported(pn, line));
    }
    if parse::struct_num_nodes(pn) != 2 {
        return Err(unsupported(pn, line));
    }
    let base = unsafe { parse::struct_node(pn, 0) };
    let trailers = unsafe { parse::struct_node(pn, 1) };
    let trailer = if parse::is_struct_kind(trailers, RuleId::AtomExprTrailers) {
        if parse::struct_num_nodes(trailers) != 1 {
            return Err(unsupported(pn, line));
        }
        unsafe { parse::struct_node(trailers, 0) }
    } else {
        trailers
    };
    if !parse::is_struct_kind(trailer, RuleId::TrailerPeriod) {
        return Err(unsupported(pn, line));
    }
    let name = unsafe { parse::struct_node(trailer, 0) };
    if !parse::is_id(name) {
        return Err(unsupported(pn, line));
    }
    Ok((base, parse::leaf_arg(name) as Qstr))
}

fn compile_store_target<E: Emit>(
    w: &mut E,
    scope: &Scope,
    pn: ParseNode,
    line: u32,
) -> Result<(), CompileError> {
    if parse::is_id(pn) {
        let qst = parse::leaf_arg(pn) as Qstr;
        return store_name_or_fast(w, scope, qst);
    }
    if let Ok((base, index)) = parse_subscript_store_target(pn, line) {
        compile_expr(w, scope, base, line)?;
        compile_expr(w, scope, index, line)?;
        return Ok(w.store_subscr()?);
    }
    if let Ok((base, attr)) = parse_attr_store_target(pn, line) {
        compile_expr(w, scope, base, line)?;
        return Ok(w.store_attr(attr)?);
    }
    Err(unsupported(pn, line))
}

fn compile_exprlist_store<E: Emit>(
    w: &mut E,
    scope: &Scope,
    pn: ParseNode,
    line: u32,
) -> Result<(), CompileError> {
    if parse::is_struct_kind(pn, RuleId::Exprlist) {
        if parse::struct_num_nodes(pn) != 1 {
            return Err(unsupported(pn, line));
        }
        return compile_store_target(w, scope, unsafe { parse::struct_node(pn, 0) }, line);
    }
    compile_store_target(w, scope, pn, line)
}

/// Compile one dict/set element. Accepts a full `DictorsetmakerItem` or a
/// collapsed bare expression (set elements often lose the item wrapper).
fn compile_dictorsetmaker_elem<E: Emit>(
    w: &mut E,
    scope: &Scope,
    pn: ParseNode,
    line: u32,
    is_dict: &mut bool,
) -> Result<(), CompileError> {
    if parse::is_struct_kind(pn, RuleId::DictorsetmakerItem) {
        let key_or_val = unsafe { parse::struct_node(pn, 0) };
        let colon_val = unsafe { parse::struct_node(pn, 1) };
        if !parse::is_null(colon_val) {
            *is_dict = true;
            compile_expr(w, scope, key_or_val, line)?;
            let val_node = if parse::is_struct_kind(colon_val, RuleId::GenericColonTest) {
                unsafe { parse::struct_node(colon_val, 0) }
            } else {
                colon_val
            };
            return compile_expr(w, scope, val_node, line);
        }
        if *is_dict {
            return Err(unsupported(pn, line));
        }
        return compile_expr(w, scope, key_or_val, line);
    }
    // Collapsed set element (`{1}` / `{1, 2}` leave bare ints under braces).
    if *is_dict {
        return Err(unsupported(pn, line));
    }
    compile_expr(w, scope, pn, line)
}

fn compile_dictorsetmaker_list2<E: Emit>(
    w: &mut E,
    scope: &Scope,
    pn: ParseNode,
    line: u32,
    is_dict: &mut bool,
    count: &mut u16,
) -> Result<(), CompileError> {
    if parse::is_null(pn) {
        return Ok(());
    }
    if parse::is_struct_kind(pn, RuleId::DictorsetmakerList2) {
        // `item ,` — one more element (trailing-comma form).
        compile_dictorsetmaker_elem(w, scope, unsafe { parse::struct_node(pn, 0) }, line, is_dict)?;
        *count += 1;
        return Ok(());
    }
    if parse::is_struct_kind(pn, RuleId::DictorsetmakerList) {
        let more = unsafe { parse::struct_node(pn, 0) };
        if parse::is_null(more) {
            return Ok(());
        }
        return compile_dictorsetmaker_list2(w, scope, more, line, is_dict, count);
    }
    if parse::is_struct_kind(pn, RuleId::Dictorsetmaker) {
        compile_dictorsetmaker_elem(w, scope, unsafe { parse::struct_node(pn, 0) }, line, is_dict)?;
        *count += 1;
        let tail = unsafe { parse::struct_node(pn, 1) };
        if parse::is_null(tail) {
            return Ok(());
        }
        if parse::is_struct_kind(tail, RuleId::DictorsetmakerTail) {
            // Comp comprehension / dictcomp — not in this slice.
            return Err(unsupported(tail, line));
        }
        if parse::is_struct_kind(tail, RuleId::DictorsetmakerList) {
            let more = unsafe { parse::struct_node(tail, 0) };
            if parse::is_null(more) {
                return Ok(());
            }
            return compile_dictorsetmaker_list2(w, scope, more, line, is_dict, count);
        }
        return Err(unsupported(tail, line));
    }
    // Bare element (collapsed single set value, or a list2 child that lost its wrapper).
    compile_dictorsetmaker_elem(w, scope, pn, line, is_dict)?;
    *count += 1;
    Ok(())
}

fn compile_dictorsetmaker<E: Emit>(
    w: &mut E,
    scope: &Scope,
    pn: ParseNode,
    line: u32,
) -> Result<(), CompileError> {
    if parse::is_null(pn) {
        // `{}` is an empty dict in Python.
        return Ok(w.build_map(0)?);
    }
    let mut is_dict = false;
    let mut count = 0u16;
    compile_dictorsetmaker_list2(w, scope, pn, line, &mut is_dict, &mut count)?;
    if is_dict {
        w.build_map(count)
    } else {
        w.build_set(count)
    }
    .map_err(CompileError::from)
}

fn compile_atom_brace<E: Emit>(
    w: &mut E,
    scope: &Scope,
    pn: ParseNode,
    line: u32,
) -> Result<(), CompileError> {
    let inner = if parse::struct_num_nodes(pn) == 0 {
        parse::PARSE_NODE_NULL
    } else {
        unsafe { parse::struct_node(pn, 0) }
    };
    compile_dictorsetmaker(w, scope, inner, line)
}

fn compile_list_comp<E: Emit>(
    w: &mut E,
    scope: &Scope,
    expr: ParseNode,
    comp_for: ParseNode,
    line: u32,
) -> Result<(), CompileError> {
    if !parse::is_struct_kind(comp_for, RuleId::CompFor) {
        return Err(unsupported(comp_for, line));
    }
    // Tokens stripped: [target, iter, Opt(CompIter), ...]. Nested `for`/`if`
    // in CompIter is out of this slice.
    let n = parse::struct_num_nodes(comp_for);
    if n < 2 {
        return Err(unsupported(comp_for, line));
    }
    let target = unsafe { parse::struct_node(comp_for, 0) };
    let iter = unsafe { parse::struct_node(comp_for, 1) };
    for i in 2..n {
        if !parse::is_null(unsafe { parse::struct_node(comp_for, i) }) {
            return Err(unsupported(comp_for, line));
        }
    }

    w.build_list(0)?;
    store_name_or_fast(w, scope, qstr_listcomp_tmp())?;
    compile_expr(w, scope, iter, line)?;
    w.get_iter()?;
    let loop_top = w.here();
    let exit = w.for_iter()?;
    compile_exprlist_store(w, scope, target, line)?;
    // LIST_APPEND pops value then list -- stack must be [list, value].
    load_name_or_fast(w, scope, qstr_listcomp_tmp(), parse::PARSE_NODE_NULL, line)?;
    compile_expr(w, scope, expr, line)?;
    w.list_append()?;
    let back = w.jump()?;
    w.patch_jump(back, loop_top)?;
    w.patch_jump(exit, w.here())?;
    load_name_or_fast(w, scope, qstr_listcomp_tmp(), parse::PARSE_NODE_NULL, line)
}

fn try_compile_list_comp<E: Emit>(
    w: &mut E,
    scope: &Scope,
    pn: ParseNode,
    line: u32,
) -> Result<bool, CompileError> {
    if parse::is_null(pn) || !parse::is_struct_kind(pn, RuleId::TestlistComp) {
        return Ok(false);
    }
    let n = parse::struct_num_nodes(pn);
    if n != 2 {
        return Ok(false);
    }
    let expr = unsafe { parse::struct_node(pn, 0) };
    let second = unsafe { parse::struct_node(pn, 1) };
    // `TestlistComp3` often collapses, leaving `CompFor` directly.
    let comp_for = if parse::is_struct_kind(second, RuleId::CompFor) {
        second
    } else if parse::is_struct_kind(second, RuleId::TestlistComp3) {
        let head = unsafe { parse::struct_node(second, 0) };
        if !parse::is_struct_kind(head, RuleId::CompFor) {
            return Ok(false);
        }
        head
    } else {
        return Ok(false);
    };
    compile_list_comp(w, scope, expr, comp_for, line)?;
    Ok(true)
}

fn collect_one_varargs_param(
    scope: &mut Scope,
    pn: ParseNode,
    line: u32,
) -> Result<(), CompileError> {
    if parse::is_id(pn) {
        let qst = parse::leaf_arg(pn) as Qstr;
        if scope.add_local(qst).is_none() {
            return Err(CompileError::TooManyLocals);
        }
        return Ok(());
    }
    if parse::is_struct_kind(pn, RuleId::VarargslistItem) {
        let inner = unsafe { parse::struct_node(pn, 0) };
        return collect_one_varargs_param(scope, inner, line);
    }
    if parse::is_struct_kind(pn, RuleId::VarargslistName) {
        let name = unsafe { parse::struct_node(pn, 0) };
        if !parse::is_id(name) {
            return Err(unsupported(pn, line));
        }
        if parse::struct_num_nodes(pn) > 1
            && !parse::is_null(unsafe { parse::struct_node(pn, 1) })
        {
            return Err(unsupported(pn, line));
        }
        let qst = parse::leaf_arg(name) as Qstr;
        if scope.add_local(qst).is_none() {
            return Err(CompileError::TooManyLocals);
        }
        return Ok(());
    }
    Err(unsupported(pn, line))
}

fn collect_varargslist(
    scope: &mut Scope,
    pn: ParseNode,
    line: u32,
) -> Result<(), CompileError> {
    if parse::is_null(pn) {
        return Ok(());
    }
    // `lambda a: ...` collapses Varargslist to a bare id / single item.
    if !parse::is_struct_kind(pn, RuleId::Varargslist) {
        return collect_one_varargs_param(scope, pn, line);
    }
    let n = parse::struct_num_nodes(pn);
    for i in 0..n {
        collect_one_varargs_param(scope, unsafe { parse::struct_node(pn, i) }, line)?;
    }
    Ok(())
}

fn compile_lambda<E: Emit>(
    w: &mut E,
    _scope: &Scope,
    pn: ParseNode,
    line: u32,
) -> Result<(), CompileError> {
    let params = unsafe { parse::struct_node(pn, 0) };
    let body = unsafe { parse::struct_node(pn, 1) };
    let mut fn_scope = Scope::new(ScopeKind::Function);
    collect_varargslist(&mut fn_scope, params, line)?;
    prescan_locals(&mut fn_scope, body)?;
    let mut inner = Writer::new()?;
    compile_expr(&mut inner, &fn_scope, body, line)?;
    inner.return_value()?;
    let raw = inner.finish(fn_scope.num_locals());
    let fun = unsafe { emitglue::make_function(&raw) };
    if fun == obj::OBJ_NULL {
        return Err(CompileError::OutOfMemory);
    }
    w.load_const_obj_value(fun)
        .map_err(CompileError::from)
}

fn compile_if<E: Emit>(
    w: &mut E,
    loops: &mut LoopStack,
    scope: &Scope,
    pn: ParseNode,
    line: u32,
) -> Result<(), CompileError> {
    const MAX_END_JUMPS: usize = 32;
    let mut end_jumps = [JumpHole { offset_at: 0 }; MAX_END_JUMPS];
    let mut n_end = 0usize;

    let test = unsafe { parse::struct_node(pn, 0) };
    let suite = unsafe { parse::struct_node(pn, 1) };
    let elif_list = unsafe { parse::struct_node(pn, 2) };
    let else_stmt = unsafe { parse::struct_node(pn, 3) };

    compile_expr(w, scope, test, line)?;
    let mut skip = w.pop_jump_if_false()?;
    compile_stmt(w, loops, scope, suite, line)?;
    if n_end >= MAX_END_JUMPS {
        return Err(unsupported(pn, line));
    }
    end_jumps[n_end] = w.jump()?;
    n_end += 1;
    w.patch_jump(skip, w.here())?;

    if !parse::is_null(elif_list) {
        if !parse::is_struct_kind(elif_list, RuleId::IfStmtElifList) {
            return Err(unsupported(elif_list, line));
        }
        let n = parse::struct_num_nodes(elif_list);
        for i in 0..n {
            let elif = unsafe { parse::struct_node(elif_list, i) };
            if !parse::is_struct_kind(elif, RuleId::IfStmtElif) {
                return Err(unsupported(elif, line));
            }
            let elif_test = unsafe { parse::struct_node(elif, 0) };
            let elif_suite = unsafe { parse::struct_node(elif, 1) };
            compile_expr(w, scope, elif_test, line)?;
            skip = w.pop_jump_if_false()?;
            compile_stmt(w, loops, scope, elif_suite, line)?;
            if n_end >= MAX_END_JUMPS {
                return Err(unsupported(pn, line));
            }
            end_jumps[n_end] = w.jump()?;
            n_end += 1;
            w.patch_jump(skip, w.here())?;
        }
    }

    if !parse::is_null(else_stmt) {
        if !parse::is_struct_kind(else_stmt, RuleId::ElseStmt) {
            return Err(unsupported(else_stmt, line));
        }
        let else_suite = unsafe { parse::struct_node(else_stmt, 0) };
        compile_stmt(w, loops, scope, else_suite, line)?;
    }

    let end = w.here();
    for i in 0..n_end {
        w.patch_jump(end_jumps[i], end)?;
    }
    Ok(())
}

fn compile_while<E: Emit>(
    w: &mut E,
    loops: &mut LoopStack,
    scope: &Scope,
    pn: ParseNode,
    line: u32,
) -> Result<(), CompileError> {
    let test = unsafe { parse::struct_node(pn, 0) };
    let suite = unsafe { parse::struct_node(pn, 1) };
    let else_stmt = unsafe { parse::struct_node(pn, 2) };

    let loop_top = w.here();
    loops.push(loop_top);
    compile_expr(w, scope, test, line)?;
    let exit = w.pop_jump_if_false()?;
    compile_stmt(w, loops, scope, suite, line)?;
    let back = w.jump()?;
    w.patch_jump(back, loop_top)?;
    let else_or_end = w.here();
    w.patch_jump(exit, else_or_end)?;
    loops.pop(w, else_or_end)?;
    if !parse::is_null(else_stmt) {
        if !parse::is_struct_kind(else_stmt, RuleId::ElseStmt) {
            return Err(unsupported(else_stmt, line));
        }
        let else_suite = unsafe { parse::struct_node(else_stmt, 0) };
        compile_stmt(w, loops, scope, else_suite, line)?;
    }
    Ok(())
}

fn compile_for<E: Emit>(
    w: &mut E,
    loops: &mut LoopStack,
    scope: &Scope,
    pn: ParseNode,
    line: u32,
) -> Result<(), CompileError> {
    let target = unsafe { parse::struct_node(pn, 0) };
    let iter = unsafe { parse::struct_node(pn, 1) };
    let suite = unsafe { parse::struct_node(pn, 2) };
    let else_stmt = unsafe { parse::struct_node(pn, 3) };

    compile_expr(w, scope, iter, line)?;
    w.get_iter()?;
    let for_iter_pos = w.here();
    loops.push(for_iter_pos);
    let exit = w.for_iter()?;
    compile_exprlist_store(w, scope, target, line)?;
    compile_stmt(w, loops, scope, suite, line)?;
    let back = w.jump()?;
    w.patch_jump(back, for_iter_pos)?;
    let end = w.here();
    w.patch_jump(exit, end)?;
    loops.pop(w, end)?;
    if !parse::is_null(else_stmt) {
        if !parse::is_struct_kind(else_stmt, RuleId::ElseStmt) {
            return Err(unsupported(else_stmt, line));
        }
        let else_suite = unsafe { parse::struct_node(else_stmt, 0) };
        compile_stmt(w, loops, scope, else_suite, line)?;
    }
    Ok(())
}

fn compile_one_import_as_name<E: Emit>(
    w: &mut E,
    scope: &Scope,
    one: ParseNode,
    line: u32,
) -> Result<(), CompileError> {
    if !parse::is_struct_kind(one, RuleId::ImportAsName) {
        return Err(unsupported(one, line));
    }
    if parse::struct_num_nodes(one) != 2 {
        return Err(unsupported(one, line));
    }
    let name = unsafe { parse::struct_node(one, 0) };
    let alias = unsafe { parse::struct_node(one, 1) };
    if !parse::is_id(name) {
        return Err(unsupported(one, line));
    }
    let name_q = parse::leaf_arg(name) as Qstr;
    let bind_q = if parse::is_null(alias) {
        name_q
    } else if parse::is_id(alias) {
        parse::leaf_arg(alias) as Qstr
    } else {
        return Err(unsupported(one, line));
    };
    w.import_from(name_q)?;
    store_name_or_fast(w, scope, bind_q)
}

fn compile_import_as_names<E: Emit>(
    w: &mut E,
    scope: &Scope,
    pn: ParseNode,
    line: u32,
) -> Result<(), CompileError> {
    let list = if parse::is_struct_kind(pn, RuleId::ImportAsNamesParen) {
        if parse::struct_num_nodes(pn) != 1 {
            return Err(unsupported(pn, line));
        }
        let inner = unsafe { parse::struct_node(pn, 0) };
        if parse::is_null(inner) {
            // `from ... import *`
            return Err(unsupported(pn, line));
        }
        inner
    } else {
        pn
    };
    if parse::is_struct_kind(list, RuleId::ImportAsNames) {
        let n = parse::struct_num_nodes(list);
        for i in 0..n {
            compile_one_import_as_name(w, scope, unsafe { parse::struct_node(list, i) }, line)?;
        }
    } else {
        compile_one_import_as_name(w, scope, list, line)?;
    }
    Ok(())
}

fn compile_import_from<E: Emit>(
    w: &mut E,
    scope: &Scope,
    pn: ParseNode,
    line: u32,
) -> Result<(), CompileError> {
    if parse::struct_num_nodes(pn) != 2 {
        return Err(unsupported(pn, line));
    }
    let module_part = unsafe { parse::struct_node(pn, 0) };
    let names_part = unsafe { parse::struct_node(pn, 1) };
    if parse::is_struct_kind(module_part, RuleId::ImportFrom2b) {
        return Err(unsupported(pn, line));
    }
    let mut full_buf = [0u8; 160];
    let (n, _) = dotted_to_buf(module_part, &mut full_buf).ok_or_else(|| unsupported(pn, line))?;
    let full_q = crate::upy::py::qstr::from_strn(&full_buf[..n]);
    if full_q == 0 {
        return Err(unsupported(pn, line));
    }
    // Non-None fromlist -> IMPORT_NAME returns the leaf module (CPython
    // `__import__` rule). Names come from subsequent IMPORT_FROM.
    w.load_const_true()?;
    w.import_name(full_q)?;
    compile_import_as_names(w, scope, names_part, line)?;
    Ok(w.pop_top()?)
}

fn compile_funcdef<E: Emit>(
    w: &mut E,
    scope: &Scope,
    pn: ParseNode,
    line: u32,
) -> Result<(), CompileError> {
    let name = unsafe { parse::struct_node(pn, 0) };
    let params = unsafe { parse::struct_node(pn, 1) };
    let body = unsafe { parse::struct_node(pn, 3) };
    if !parse::is_id(name) {
        return Err(unsupported(pn, line));
    }
    let name_q = parse::leaf_arg(name) as Qstr;

    let mut fn_scope = Scope::new(ScopeKind::Function);
    collect_simple_params(&mut fn_scope, params, line)?;
    prescan_locals(&mut fn_scope, body)?;

    let raw = compile_function_body(&fn_scope, body, line)?;
    let fun = unsafe { emitglue::make_function(&raw) };
    if fun == obj::OBJ_NULL {
        return Err(CompileError::OutOfMemory);
    }
    w.load_const_obj_value(fun)?;
    store_name_or_fast(w, scope, name_q)
}

fn compile_raise<E: Emit>(
    w: &mut E,
    scope: &Scope,
    pn: ParseNode,
    line: u32,
) -> Result<(), CompileError> {
    let arg = unsafe { parse::struct_node(pn, 0) };
    if parse::is_null(arg) {
        return Err(unsupported(pn, line));
    }
    let expr = if parse::is_struct_kind(arg, RuleId::RaiseStmtArg) {
        let from_part = unsafe { parse::struct_node(arg, 1) };
        if !parse::is_null(from_part) {
            return Err(unsupported(pn, line));
        }
        unsafe { parse::struct_node(arg, 0) }
    } else {
        // `raise 1` often collapses to a bare expression under RaiseStmt.
        arg
    };
    compile_expr(w, scope, expr, line)?;
    Ok(w.raise_obj()?)
}

fn compile_except_type_match<E: Emit>(
    w: &mut E,
    scope: &Scope,
    type_test: ParseNode,
    line: u32,
) -> Result<JumpHole, CompileError> {
    w.dup_top()?;
    if parse::is_id(type_test) {
        // Match by exception type name (qstr) — see VM EXCEPTION_MATCH.
        w.load_const_qstr(parse::leaf_arg(type_test) as Qstr)?;
    } else {
        compile_expr(w, scope, type_test, line)?;
    }
    w.binary_op(emitcommon::BINARY_OP_EXCEPTION_MATCH)?;
    w.pop_jump_if_false().map_err(CompileError::from)
}

fn compile_except_handler<E: Emit>(
    w: &mut E,
    loops: &mut LoopStack,
    scope: &Scope,
    pn: ParseNode,
    line: u32,
) -> Result<JumpHole, CompileError> {
    if !parse::is_struct_kind(pn, RuleId::TryStmtExcept) {
        return Err(unsupported(pn, line));
    }
    let as_part = unsafe { parse::struct_node(pn, 0) };
    let suite = unsafe { parse::struct_node(pn, 1) };

    if parse::is_null(as_part) {
        // bare `except:`
        compile_stmt(w, loops, scope, suite, line)?;
        return Ok(w.jump()?);
    }

    let (type_test, bind) = if parse::is_struct_kind(as_part, RuleId::TryStmtAsName) {
        (
            unsafe { parse::struct_node(as_part, 0) },
            unsafe { parse::struct_node(as_part, 1) },
        )
    } else {
        // `except Exception:` collapses TryStmtAsName away, leaving the type id.
        (as_part, parse::PARSE_NODE_NULL)
    };

    if parse::is_null(type_test) {
        compile_stmt(w, loops, scope, suite, line)?;
        return Ok(w.jump()?);
    }

    // On a match the exception value remains on the stack.
    let next = compile_except_type_match(w, scope, type_test, line)?;
    if !parse::is_null(bind) && parse::is_struct_kind(bind, RuleId::AsName) {
        let name = unsafe { parse::struct_node(bind, 0) };
        if parse::is_id(name) {
            store_name_or_fast(w, scope, parse::leaf_arg(name) as Qstr)?;
        } else {
            w.pop_top()?;
        }
    } else {
        w.pop_top()?;
    }
    compile_stmt(w, loops, scope, suite, line)?;
    let to_end = w.jump()?;
    w.patch_jump(next, w.here())?;
    Ok(to_end)
}

fn compile_try<E: Emit>(
    w: &mut E,
    loops: &mut LoopStack,
    scope: &Scope,
    pn: ParseNode,
    line: u32,
) -> Result<(), CompileError> {
    const MAX_HANDLER_EXITS: usize = 8;
    let mut handler_exits = [JumpHole { offset_at: 0 }; MAX_HANDLER_EXITS];
    let mut n_handler_exits = 0usize;

    let body = unsafe { parse::struct_node(pn, 0) };
    let rest = unsafe { parse::struct_node(pn, 1) };
    if parse::is_null(rest) || !parse::is_struct(rest) {
        return Err(unsupported(pn, line));
    }

    let (except_list, else_part, finally_suite) =
        if parse::is_struct_kind(rest, RuleId::TryStmtExceptAndMore) {
            let fin = unsafe { parse::struct_node(rest, 2) };
            let fin_suite = if parse::is_null(fin) {
                None
            } else if parse::is_struct_kind(fin, RuleId::TryStmtFinally) {
                Some(unsafe { parse::struct_node(fin, 0) })
            } else {
                return Err(unsupported(fin, line));
            };
            (
                Some(unsafe { parse::struct_node(rest, 0) }),
                unsafe { parse::struct_node(rest, 1) },
                fin_suite,
            )
        } else if parse::is_struct_kind(rest, RuleId::TryStmtFinally) {
            (None, parse::PARSE_NODE_NULL, Some(unsafe { parse::struct_node(rest, 0) }))
        } else if parse::is_struct_kind(rest, RuleId::TryStmtExceptList)
            || parse::is_struct_kind(rest, RuleId::TryStmtExcept)
        {
            // Single-except `try` collapses TryStmt2 / ExceptAndMore away.
            (Some(rest), parse::PARSE_NODE_NULL, None)
        } else {
            return Err(unsupported(rest, line));
        };

    let handler = w.setup_except()?;
    compile_stmt(w, loops, scope, body, line)?;
    w.pop_except()?;
    let skip_handlers = w.jump()?;

    w.patch_jump(handler, w.here())?;

    if let Some(excepts) = except_list {
        if parse::is_struct_kind(excepts, RuleId::TryStmtExcept) {
            if n_handler_exits >= MAX_HANDLER_EXITS {
                return Err(unsupported(pn, line));
            }
            handler_exits[n_handler_exits] =
                compile_except_handler(w, loops, scope, excepts, line)?;
            n_handler_exits += 1;
        } else if parse::is_struct_kind(excepts, RuleId::TryStmtExceptList) {
            let n = parse::struct_num_nodes(excepts);
            for i in 0..n {
                if n_handler_exits >= MAX_HANDLER_EXITS {
                    return Err(unsupported(pn, line));
                }
                handler_exits[n_handler_exits] = compile_except_handler(
                    w,
                    loops,
                    scope,
                    unsafe { parse::struct_node(excepts, i) },
                    line,
                )?;
                n_handler_exits += 1;
            }
        } else {
            return Err(unsupported(excepts, line));
        }
    }

    if finally_suite.is_some() {
        let to_fin = w.jump()?;
        w.patch_jump(to_fin, w.here())?;
        compile_stmt(w, loops, scope, finally_suite.unwrap(), line)?;
        w.raise_obj()?;
    } else if except_list.is_some() {
        w.raise_obj()?;
    } else {
        return Err(unsupported(pn, line));
    }

    w.patch_jump(skip_handlers, w.here())?;

    if !parse::is_null(else_part) {
        if !parse::is_struct_kind(else_part, RuleId::ElseStmt) {
            return Err(unsupported(else_part, line));
        }
        compile_stmt(w, loops, scope, unsafe { parse::struct_node(else_part, 0) }, line)?;
    }

    let end = w.here();
    for i in 0..n_handler_exits {
        w.patch_jump(handler_exits[i], end)?;
    }

    if let Some(fin) = finally_suite {
        compile_stmt(w, loops, scope, fin, line)?;
    }

    Ok(())
}

fn compile_with<E: Emit>(
    w: &mut E,
    loops: &mut LoopStack,
    scope: &Scope,
    pn: ParseNode,
    line: u32,
) -> Result<(), CompileError> {
    let items = unsafe { parse::struct_node(pn, 0) };
    let suite = unsafe { parse::struct_node(pn, 1) };
    if !parse::is_struct_kind(items, RuleId::WithStmtList) || parse::struct_num_nodes(items) != 1 {
        return Err(unsupported(pn, line));
    }
    let item = unsafe { parse::struct_node(items, 0) };
    if !parse::is_struct_kind(item, RuleId::WithItem) {
        return Err(unsupported(item, line));
    }
    let cm = unsafe { parse::struct_node(item, 0) };
    let as_part = unsafe { parse::struct_node(item, 1) };

    compile_expr(w, scope, cm, line)?;
    store_name_or_fast(w, scope, qstr_with_tmp())?;
    load_name_or_fast(w, scope, qstr_with_tmp(), parse::PARSE_NODE_NULL, line)?;
    w.load_attr(qstr::from_str("__enter__"))?;
    load_name_or_fast(w, scope, qstr_with_tmp(), parse::PARSE_NODE_NULL, line)?;
    w.call_function(0)?;
    if !parse::is_null(as_part) {
        let name = unsafe { parse::struct_node(as_part, 0) };
        if !parse::is_id(name) {
            return Err(unsupported(as_part, line));
        }
        store_name_or_fast(w, scope, parse::leaf_arg(name) as Qstr)?;
    } else {
        w.pop_top()?;
    }

    let handler = w.setup_except()?;
    compile_stmt(w, loops, scope, suite, line)?;
    w.pop_except()?;
    let normal = w.jump()?;

    w.patch_jump(handler, w.here())?;
    load_name_or_fast(w, scope, qstr_with_tmp(), parse::PARSE_NODE_NULL, line)?;
    w.load_attr(qstr::from_str("__exit__"))?;
    load_name_or_fast(w, scope, qstr_with_tmp(), parse::PARSE_NODE_NULL, line)?;
    w.load_const_none()?;
    w.load_const_none()?;
    w.load_const_none()?;
    w.call_function(3)?;
    w.pop_top()?;
    w.raise_obj()?;

    w.patch_jump(normal, w.here())?;
    load_name_or_fast(w, scope, qstr_with_tmp(), parse::PARSE_NODE_NULL, line)?;
    w.load_attr(qstr::from_str("__exit__"))?;
    load_name_or_fast(w, scope, qstr_with_tmp(), parse::PARSE_NODE_NULL, line)?;
    w.load_const_none()?;
    w.load_const_none()?;
    w.load_const_none()?;
    w.call_function(3)?;
    Ok(w.pop_top()?)
}

fn compile_classdef<E: Emit>(
    w: &mut E,
    scope: &Scope,
    pn: ParseNode,
    line: u32,
) -> Result<(), CompileError> {
    // classdef children (tokens stripped): name, Opt(Classdef2), suite [, trailing null].
    let name = unsafe { parse::struct_node(pn, 0) };
    let bases = unsafe { parse::struct_node(pn, 1) };
    let suite = unsafe { parse::struct_node(pn, 2) };
    if !parse::is_id(name) {
        return Err(unsupported(pn, line));
    }
    let name_q = parse::leaf_arg(name) as Qstr;

    // Bases need a runtime BUILD_CLASS path (base MpObj is only known after
    // executing the base expression). This slice ships `class C:` / empty
    // `class C():` only; a non-empty base list is an honest Unsupported.
    if !parse::is_null(bases) {
        if !parse::is_struct_kind(bases, RuleId::Classdef2) {
            return Err(unsupported(bases, line));
        }
        let arglist = unsafe { parse::struct_node(bases, 0) };
        if !parse::is_null(arglist) {
            return Err(unsupported(bases, line));
        }
    }

    let ty_obj = unsafe { objtype::new_user_type(name_q, obj::OBJ_NULL) };
    if ty_obj == obj::OBJ_NULL {
        return Err(CompileError::OutOfMemory);
    }

    compile_class_suite(w, scope, suite, ty_obj, line)?;
    w.load_const_obj_value(ty_obj)?;
    store_name_or_fast(w, scope, name_q)
}

fn compile_class_suite<E: Emit>(
    w: &mut E,
    scope: &Scope,
    pn: ParseNode,
    ty_obj: obj::MpObj,
    line: u32,
) -> Result<(), CompileError> {
    if parse::is_null(pn) || !parse::is_struct(pn) {
        return Ok(());
    }
    match parse::struct_kind(pn) {
        RuleId::SuiteBlockStmts => {
            for i in 0..parse::struct_num_nodes(pn) {
                compile_class_suite(w, scope, unsafe { parse::struct_node(pn, i) }, ty_obj, line)?;
            }
            Ok(())
        }
        RuleId::Funcdef => {
            let meth_name = unsafe { parse::struct_node(pn, 0) };
            if !parse::is_id(meth_name) {
                return Err(unsupported(pn, line));
            }
            let meth_q = parse::leaf_arg(meth_name) as Qstr;
            let params = unsafe { parse::struct_node(pn, 1) };
            let body = unsafe { parse::struct_node(pn, 3) };
            let mut fn_scope = Scope::new(ScopeKind::Function);
            collect_simple_params(&mut fn_scope, params, line)?;
            prescan_locals(&mut fn_scope, body)?;
            let raw = compile_function_body(&fn_scope, body, line)?;
            let fun = unsafe { emitglue::make_function(&raw) };
            if fun == obj::OBJ_NULL {
                return Err(CompileError::OutOfMemory);
            }
            w.load_const_obj_value(fun)?;
            w.load_const_obj_value(ty_obj)?;
            Ok(w.store_attr(meth_q)?)
        }
        RuleId::ExprStmt => {
            let target = unsafe { parse::struct_node(pn, 0) };
            let value = unsafe { parse::struct_node(pn, 1) };
            if parse::is_null(value) || is_assign_machinery(value) || !parse::is_id(target) {
                return Err(unsupported(pn, line));
            }
            compile_expr(w, scope, value, line)?;
            w.load_const_obj_value(ty_obj)?;
            Ok(w.store_attr(parse::leaf_arg(target) as Qstr)?)
        }
        _ => Err(unsupported(pn, line)),
    }
}

fn compile_augassign<E: Emit>(
    w: &mut E,
    scope: &Scope,
    target: ParseNode,
    aug: ParseNode,
    line: u32,
) -> Result<(), CompileError> {
    if !parse::is_struct_kind(aug, RuleId::ExprStmtAugassign) {
        return Err(unsupported(aug, line));
    }
    let op_node = unsafe { parse::struct_node(aug, 0) };
    let rhs = unsafe { parse::struct_node(aug, 1) };
    if !parse::is_token(op_node) {
        return Err(unsupported(aug, line));
    }
    let tok = parse::leaf_arg(op_node);
    if tok == TokenKind::DelSlashEqual as usize
        || tok == TokenKind::DelAtEqual as usize
        || tok == TokenKind::DelDblStarEqual as usize
    {
        return Err(unsupported(aug, line));
    }
    let op = emitcommon::binary_op_from_augassign_token(tok).ok_or_else(|| unsupported(aug, line))?;

    if parse::is_id(target) {
        let qst = parse::leaf_arg(target) as Qstr;
        load_name_or_fast(w, scope, qst, target, line)?;
        compile_expr(w, scope, rhs, line)?;
        w.binary_op(op)?;
        return store_name_or_fast(w, scope, qst);
    }
    if let Ok((base, index)) = parse_subscript_store_target(target, line) {
        compile_expr(w, scope, base, line)?;
        compile_expr(w, scope, index, line)?;
        w.dup_top_two()?;
        w.load_subscr()?;
        compile_expr(w, scope, rhs, line)?;
        w.binary_op(op)?;
        w.store_subscr()?;
        return Ok(());
    }
    if let Ok((base, attr)) = parse_attr_store_target(target, line) {
        compile_expr(w, scope, base, line)?;
        w.dup_top()?;
        w.load_attr(attr)?;
        compile_expr(w, scope, rhs, line)?;
        w.binary_op(op)?;
        w.rot_two()?;
        return Ok(w.store_attr(attr)?);
    }
    Err(unsupported(target, line))
}

fn compile_subscript_index<E: Emit>(
    w: &mut E,
    scope: &Scope,
    pn: ParseNode,
    line: u32,
) -> Result<(), CompileError> {
    let index = trailer_bracket_index(pn, line)?;
    compile_expr(w, scope, index, line)
}

fn compile_term_chain<E: Emit>(
    w: &mut E,
    scope: &Scope,
    pn: ParseNode,
    line: u32,
) -> Result<(), CompileError> {
    let n = parse::struct_num_nodes(pn);
    compile_expr(w, scope, unsafe { parse::struct_node(pn, 0) }, line)?;
    let mut i = 1;
    while i + 1 < n {
        let op_node = unsafe { parse::struct_node(pn, i) };
        let rhs = unsafe { parse::struct_node(pn, i + 1) };
        compile_expr(w, scope, rhs, line)?;
        if !parse::is_token(op_node) {
            return Err(unsupported(pn, line));
        }
        let op = emitcommon::binary_op_from_term_token(parse::leaf_arg(op_node))
            .filter(|&op| {
                matches!(
                    op,
                    emitcommon::BINARY_OP_LSHIFT
                        | emitcommon::BINARY_OP_RSHIFT
                        | emitcommon::BINARY_OP_ADD
                        | emitcommon::BINARY_OP_SUBTRACT
                        | emitcommon::BINARY_OP_MULTIPLY
                        | emitcommon::BINARY_OP_FLOOR_DIVIDE
                        | emitcommon::BINARY_OP_MODULO
                )
            })
            .ok_or_else(|| unsupported(pn, line))?;
        w.binary_op(op)?;
        i += 2;
    }
    Ok(())
}

fn compile_bitwise_chain<E: Emit>(
    w: &mut E,
    scope: &Scope,
    pn: ParseNode,
    kind: RuleId,
    line: u32,
) -> Result<(), CompileError> {
    let op = emitcommon::binary_op_from_rule(kind).ok_or_else(|| unsupported(pn, line))?;
    let n = parse::struct_num_nodes(pn);
    compile_expr(w, scope, unsafe { parse::struct_node(pn, 0) }, line)?;
    for i in 1..n {
        compile_expr(w, scope, unsafe { parse::struct_node(pn, i) }, line)?;
        w.binary_op(op)?;
    }
    Ok(())
}

fn compile_or_test<E: Emit>(
    w: &mut E,
    scope: &Scope,
    pn: ParseNode,
    line: u32,
) -> Result<(), CompileError> {
    let n = parse::struct_num_nodes(pn);
    if n == 0 {
        return Err(unsupported(pn, line));
    }
    compile_expr(w, scope, unsafe { parse::struct_node(pn, 0) }, line)?;
    if n == 1 {
        return Ok(());
    }
    for i in 1..n {
        w.dup_top()?;
        let skip = w.pop_jump_if_true()?;
        w.pop_top()?;
        compile_expr(w, scope, unsafe { parse::struct_node(pn, i) }, line)?;
        w.patch_jump(skip, w.here())?;
    }
    Ok(())
}

fn compile_and_test<E: Emit>(
    w: &mut E,
    scope: &Scope,
    pn: ParseNode,
    line: u32,
) -> Result<(), CompileError> {
    let n = parse::struct_num_nodes(pn);
    if n == 0 {
        return Err(unsupported(pn, line));
    }
    compile_expr(w, scope, unsafe { parse::struct_node(pn, 0) }, line)?;
    if n == 1 {
        return Ok(());
    }
    for i in 1..n {
        w.dup_top()?;
        let skip = w.pop_jump_if_false()?;
        w.pop_top()?;
        compile_expr(w, scope, unsafe { parse::struct_node(pn, i) }, line)?;
        w.patch_jump(skip, w.here())?;
    }
    Ok(())
}

fn compile_test_if_expr<E: Emit>(
    w: &mut E,
    scope: &Scope,
    pn: ParseNode,
    line: u32,
) -> Result<(), CompileError> {
    let then_val = unsafe { parse::struct_node(pn, 0) };
    let ternary = unsafe { parse::struct_node(pn, 1) };
    if parse::is_null(ternary) {
        return compile_expr(w, scope, then_val, line);
    }
    if !parse::is_struct_kind(ternary, RuleId::TestIfElse) {
        return Err(unsupported(pn, line));
    }
    let cond = unsafe { parse::struct_node(ternary, 0) };
    let else_val = unsafe { parse::struct_node(ternary, 1) };
    compile_expr(w, scope, cond, line)?;
    let to_else = w.pop_jump_if_false()?;
    compile_expr(w, scope, then_val, line)?;
    let to_end = w.jump()?;
    let else_pos = w.here();
    w.patch_jump(to_else, else_pos)?;
    compile_expr(w, scope, else_val, line)?;
    w.patch_jump(to_end, w.here())?;
    Ok(())
}

fn compile_comp_op<E: Emit>(
    w: &mut E,
    scope: &Scope,
    op_node: ParseNode,
    lhs: ParseNode,
    rhs: ParseNode,
    line: u32,
) -> Result<(), CompileError> {
    compile_expr(w, scope, lhs, line)?;
    compile_expr(w, scope, rhs, line)?;
    if parse::is_struct_kind(op_node, RuleId::CompOpNotIn) {
        w.binary_op(emitcommon::BINARY_OP_IN)?;
        return Ok(w.unary_op(emitcommon::UNARY_OP_NOT)?);
    }
    if parse::is_struct_kind(op_node, RuleId::CompOpIs) {
        w.binary_op(emitcommon::BINARY_OP_IS)?;
        if parse::struct_num_nodes(op_node) > 0
            && parse::is_struct_kind(unsafe { parse::struct_node(op_node, 0) }, RuleId::CompOpIsNot)
        {
            return Ok(w.unary_op(emitcommon::UNARY_OP_NOT)?);
        }
        return Ok(());
    }
    if !parse::is_token(op_node) {
        return Err(unsupported(op_node, line));
    }
    let op = emitcommon::compare_op_from_token(parse::leaf_arg(op_node))
        .ok_or_else(|| unsupported(op_node, line))?;
    Ok(w.binary_op(op)?)
}

fn compile_comparison<E: Emit>(
    w: &mut E,
    scope: &Scope,
    pn: ParseNode,
    line: u32,
) -> Result<(), CompileError> {
    if parse::struct_num_nodes(pn) != 3 {
        return Err(unsupported(pn, line));
    }
    let lhs = unsafe { parse::struct_node(pn, 0) };
    let op_node = unsafe { parse::struct_node(pn, 1) };
    let rhs = unsafe { parse::struct_node(pn, 2) };
    compile_comp_op(w, scope, op_node, lhs, rhs, line)
}

fn compile_trailer<E: Emit>(
    w: &mut E,
    scope: &Scope,
    pn: ParseNode,
    line: u32,
) -> Result<(), CompileError> {
    if !parse::is_struct(pn) {
        return Err(unsupported(pn, line));
    }
    match parse::struct_kind(pn) {
        RuleId::TrailerPeriod => {
            // Only the Name token is stored (see parse.rs step_and).
            if parse::struct_num_nodes(pn) != 1 {
                return Err(unsupported(pn, line));
            }
            let name = unsafe { parse::struct_node(pn, 0) };
            if !parse::is_id(name) {
                return Err(unsupported(pn, line));
            }
            Ok(w.load_attr(parse::leaf_arg(name) as Qstr)?)
        }
        RuleId::TrailerParen => {
            // Delimiters are not stored; node 0 is Opt(Arglist).
            if parse::struct_num_nodes(pn) != 1 {
                return Err(unsupported(pn, line));
            }
            let args = unsafe { parse::struct_node(pn, 0) };
            let n = compile_arglist(w, scope, args, line)?;
            Ok(w.call_function(n)?)
        }
        RuleId::TrailerBracket => {
            compile_subscript_index(w, scope, pn, line)?;
            Ok(w.load_subscr()?)
        }
        _ => Err(unsupported(pn, line)),
    }
}

/// Emit each positional arg; return how many were pushed. Rejects keyword /
/// star / too-many-arg shapes as `Unsupported`.
fn compile_arglist<E: Emit>(
    w: &mut E,
    scope: &Scope,
    pn: ParseNode,
    line: u32,
) -> Result<u16, CompileError> {
    if parse::is_null(pn) {
        return Ok(0);
    }
    // Single collapsed arg (Argument ALLOW_IDENT -> bare expr).
    if !parse::is_struct(pn) || parse::struct_kind(pn) != RuleId::Arglist {
        if parse::is_struct_kind(pn, RuleId::Argument) {
            compile_expr(w, scope, unsafe { parse::struct_node(pn, 0) }, line)?;
        } else {
            compile_expr(w, scope, pn, line)?;
        }
        return Ok(1);
    }
    let n = parse::struct_num_nodes(pn);
    if n > emitcommon::MAX_CALL_ARGS {
        return Err(unsupported(pn, line));
    }
    for i in 0..n {
        let a = unsafe { parse::struct_node(pn, i) };
        if parse::is_null(a) {
            continue;
        }
        if parse::is_struct_kind(a, RuleId::Argument) {
            let test = unsafe { parse::struct_node(a, 0) };
            let trail = unsafe { parse::struct_node(a, 1) };
            if !parse::is_null(trail) {
                // kw/=/comp_for -- not in this slice.
                return Err(unsupported(pn, line));
            }
            compile_expr(w, scope, test, line)?;
        } else if parse::is_struct_kind(a, RuleId::ArglistStar)
            || parse::is_struct_kind(a, RuleId::ArglistDblStar)
        {
            return Err(unsupported(pn, line));
        } else {
            compile_expr(w, scope, a, line)?;
        }
    }
    Ok(n as u16)
}

fn compile_atom_expr_normal<E: Emit>(
    w: &mut E,
    scope: &Scope,
    pn: ParseNode,
    line: u32,
) -> Result<(), CompileError> {
    let n = parse::struct_num_nodes(pn);
    if n < 1 {
        return Err(unsupported(pn, line));
    }
    compile_expr(w, scope, unsafe { parse::struct_node(pn, 0) }, line)?;
    if n == 1 {
        return Ok(());
    }
    let trailers = unsafe { parse::struct_node(pn, 1) };
    if parse::is_null(trailers) {
        return Ok(());
    }
    if parse::is_struct_kind(trailers, RuleId::AtomExprTrailers) {
        for i in 0..parse::struct_num_nodes(trailers) {
            compile_trailer(w, scope, unsafe { parse::struct_node(trailers, i) }, line)?;
        }
        Ok(())
    } else {
        // Single trailer left on the stack (list rule i==1 collapse).
        compile_trailer(w, scope, trailers, line)
    }
}

fn compile_expr<E: Emit>(
    w: &mut E,
    scope: &Scope,
    pn: ParseNode,
    line: u32,
) -> Result<(), CompileError> {
    if parse::is_small_int(pn) {
        return Ok(w.load_const_small_int(parse::small_int_value(pn))?);
    }
    if parse::is_id(pn) {
        let qst = parse::leaf_arg(pn) as Qstr;
        return load_name_or_fast(w, scope, qst, pn, line);
    }
    if parse::is_string(pn) {
        return Ok(w.load_const_string(parse::leaf_arg(pn) as Qstr)?);
    }
    if parse::is_token_kind(pn, TokenKind::KwNone) {
        return Ok(w.load_const_none()?);
    }
    if parse::is_token_kind(pn, TokenKind::KwTrue) {
        return Ok(w.load_const_true()?);
    }
    if parse::is_token_kind(pn, TokenKind::KwFalse) {
        return Ok(w.load_const_false()?);
    }
    if !parse::is_struct(pn) {
        return Err(unsupported(pn, line));
    }
    let line = parse::struct_source_line(pn);
    match parse::struct_kind(pn) {
        RuleId::ArithExpr | RuleId::Term | RuleId::ShiftExpr => {
            compile_term_chain(w, scope, pn, line)
        }
        RuleId::Expr | RuleId::XorExpr | RuleId::AndExpr => {
            compile_bitwise_chain(w, scope, pn, parse::struct_kind(pn), line)
        }
        RuleId::Testlist => {
            if parse::struct_num_nodes(pn) != 1 {
                return Err(unsupported(pn, line));
            }
            compile_expr(w, scope, unsafe { parse::struct_node(pn, 0) }, line)
        }
        RuleId::Test => compile_expr(w, scope, unsafe { parse::struct_node(pn, 0) }, line),
        RuleId::Comparison => compile_comparison(w, scope, pn, line),
        RuleId::OrTest => compile_or_test(w, scope, pn, line),
        RuleId::AndTest => compile_and_test(w, scope, pn, line),
        RuleId::TestIfExpr => compile_test_if_expr(w, scope, pn, line),
        RuleId::AtomBrace => compile_atom_brace(w, scope, pn, line),
        RuleId::Lambdef | RuleId::LambdefNocond => compile_lambda(w, scope, pn, line),
        RuleId::NotTest => {
            let not_part = unsafe { parse::struct_node(pn, 0) };
            let comp = unsafe { parse::struct_node(pn, 1) };
            compile_expr(w, scope, comp, line)?;
            if !parse::is_null(not_part) {
                Ok(w.unary_op(emitcommon::UNARY_OP_NOT)?)
            } else {
                Ok(())
            }
        }
        RuleId::NotTest2 => {
            compile_expr(w, scope, unsafe { parse::struct_node(pn, 0) }, line)?;
            Ok(w.unary_op(emitcommon::UNARY_OP_NOT)?)
        }
        RuleId::Factor2 => {
            let op_node = unsafe { parse::struct_node(pn, 0) };
            let operand = unsafe { parse::struct_node(pn, 1) };
            if !parse::is_token(op_node) {
                return Err(unsupported(pn, line));
            }
            let op = emitcommon::unary_op_from_factor_token(parse::leaf_arg(op_node))
                .ok_or_else(|| unsupported(pn, line))?;
            compile_expr(w, scope, operand, line)?;
            Ok(w.unary_op(op)?)
        }
        RuleId::Power => {
            let atom = unsafe { parse::struct_node(pn, 0) };
            let dbl = unsafe { parse::struct_node(pn, 1) };
            if !parse::is_null(dbl) {
                return Err(unsupported(pn, line));
            }
            compile_expr(w, scope, atom, line)
        }
        RuleId::Atom => compile_expr(w, scope, unsafe { parse::struct_node(pn, 0) }, line),
        RuleId::AtomExprNormal => compile_atom_expr_normal(w, scope, pn, line),
        RuleId::AtomBracket => {
            let inner = if parse::struct_num_nodes(pn) == 0 {
                parse::PARSE_NODE_NULL
            } else {
                unsafe { parse::struct_node(pn, 0) }
            };
            compile_testlist_comp(w, scope, inner, line, |w, n| w.build_list(n))
        }
        RuleId::AtomParen => {
            if parse::struct_num_nodes(pn) != 1 {
                return Err(unsupported(pn, line));
            }
            let inner = unsafe { parse::struct_node(pn, 0) };
            if !parse::is_struct_kind(inner, RuleId::TestlistComp) {
                return Err(unsupported(pn, line));
            }
            compile_testlist_comp(w, scope, inner, line, |w, n| w.build_tuple(n))
        }
        RuleId::NamedexprTest => compile_expr(w, scope, unsafe { parse::struct_node(pn, 0) }, line),
        RuleId::TestlistStarExpr if parse::struct_num_nodes(pn) == 1 => {
            compile_expr(w, scope, unsafe { parse::struct_node(pn, 0) }, line)
        }
        _ => Err(unsupported(pn, line)),
    }
}

// -- statements -------------------------------------------------------------

fn compile_stmt<E: Emit>(
    w: &mut E,
    loops: &mut LoopStack,
    scope: &Scope,
    pn: ParseNode,
    line: u32,
) -> Result<(), CompileError> {
    if parse::is_null(pn) {
        return Ok(());
    }
    if !parse::is_struct(pn) {
        return Err(unsupported(pn, line));
    }
    let line = parse::struct_source_line(pn);
    match parse::struct_kind(pn) {
        RuleId::FileInput2 | RuleId::SimpleStmt2 | RuleId::SuiteBlockStmts => {
            for i in 0..parse::struct_num_nodes(pn) {
                compile_stmt(w, loops, scope, unsafe { parse::struct_node(pn, i) }, line)?;
            }
            Ok(())
        }
        RuleId::PassStmt => Ok(()),
        RuleId::ReturnStmt => {
            let value = unsafe { parse::struct_node(pn, 0) };
            if parse::is_null(value) {
                w.load_const_none()?;
            } else {
                compile_expr(w, scope, value, line)?;
            }
            Ok(w.return_value()?)
        }
        RuleId::BreakStmt => loops.emit_break(w, pn, line),
        RuleId::ContinueStmt => loops.emit_continue(w, pn, line),
        RuleId::RaiseStmt => compile_raise(w, scope, pn, line),
        RuleId::ExprStmt => {
            let target = unsafe { parse::struct_node(pn, 0) };
            let value = unsafe { parse::struct_node(pn, 1) };
            if parse::is_null(value) {
                compile_expr(w, scope, target, line)?;
                return Ok(w.pop_top()?);
            }
            if parse::is_struct_kind(value, RuleId::ExprStmtAugassign) {
                return compile_augassign(w, scope, target, value, line);
            }
            if is_assign_machinery(value) {
                return Err(unsupported(pn, line));
            }
            if parse::is_id(target) {
                compile_expr(w, scope, value, line)?;
                return store_name_or_fast(w, scope, parse::leaf_arg(target) as Qstr);
            }
            if let Ok((base, index)) = parse_subscript_store_target(target, line) {
                compile_expr(w, scope, value, line)?;
                compile_expr(w, scope, base, line)?;
                compile_expr(w, scope, index, line)?;
                return Ok(w.store_subscr()?);
            }
            if let Ok((base, attr)) = parse_attr_store_target(target, line) {
                compile_expr(w, scope, value, line)?;
                compile_expr(w, scope, base, line)?;
                return Ok(w.store_attr(attr)?);
            }
            Err(unsupported(pn, line))
        }
        RuleId::IfStmt => compile_if(w, loops, scope, pn, line),
        RuleId::WhileStmt => compile_while(w, loops, scope, pn, line),
        RuleId::ForStmt => compile_for(w, loops, scope, pn, line),
        RuleId::TryStmt => compile_try(w, loops, scope, pn, line),
        RuleId::WithStmt => compile_with(w, loops, scope, pn, line),
        RuleId::Funcdef => compile_funcdef(w, scope, pn, line),
        RuleId::Classdef => compile_classdef(w, scope, pn, line),
        RuleId::ImportFrom => compile_import_from(w, scope, pn, line),
        RuleId::ImportName => compile_import(w, scope, pn, line),
        _ => Err(unsupported(pn, line)),
    }
}

/// Build a dotted name into `buf` from a `DottedName` list or a lone id.
/// Returns (bytes_len, first_segment_qstr).
fn dotted_to_buf(pn: ParseNode, buf: &mut [u8]) -> Option<(usize, Qstr)> {
    if parse::is_id(pn) {
        let q = parse::leaf_arg(pn) as Qstr;
        let s = crate::upy::py::qstr::str(q);
        if s.len() >= buf.len() {
            return None;
        }
        buf[..s.len()].copy_from_slice(s);
        return Some((s.len(), q));
    }
    if !parse::is_struct_kind(pn, RuleId::DottedName) {
        return None;
    }
    let n = parse::struct_num_nodes(pn);
    if n == 0 {
        return None;
    }
    let mut o = 0usize;
    let mut first = 0 as Qstr;
    for i in 0..n {
        let id = unsafe { parse::struct_node(pn, i) };
        if !parse::is_id(id) {
            return None;
        }
        let q = parse::leaf_arg(id) as Qstr;
        if i == 0 {
            first = q;
        } else {
            if o >= buf.len() {
                return None;
            }
            buf[o] = b'.';
            o += 1;
        }
        let s = crate::upy::py::qstr::str(q);
        if o + s.len() >= buf.len() {
            return None;
        }
        buf[o..o + s.len()].copy_from_slice(s);
        o += s.len();
    }
    Some((o, first))
}

fn compile_import<E: Emit>(
    w: &mut E,
    scope: &Scope,
    pn: ParseNode,
    line: u32,
) -> Result<(), CompileError> {
    // ImportName stores only DottedAsNames (KwImport is not a Name token).
    if parse::struct_num_nodes(pn) != 1 {
        return Err(unsupported(pn, line));
    }
    let names = unsafe { parse::struct_node(pn, 0) };
    // One target only (comma-separated import is Unsupported).
    let one = if parse::is_struct_kind(names, RuleId::DottedAsNames) {
        if parse::struct_num_nodes(names) != 1 {
            return Err(unsupported(pn, line));
        }
        unsafe { parse::struct_node(names, 0) }
    } else {
        names
    };

    let mut full_buf = [0u8; 160];
    // `import a.b.c` binds the top package (fromlist None).
    // `import a.b.c as m` binds the leaf (fromlist True) -- CPython's
    // `as` rule without needing IMPORT_FROM of the last segment.
    let (full_q, bind_q, want_leaf) = if parse::is_struct_kind(one, RuleId::DottedAsName) {
        let dotted = unsafe { parse::struct_node(one, 0) };
        let alias = unsafe { parse::struct_node(one, 1) };
        let (n, first) = dotted_to_buf(dotted, &mut full_buf).ok_or_else(|| unsupported(pn, line))?;
        let full_q = crate::upy::py::qstr::from_strn(&full_buf[..n]);
        if full_q == 0 {
            return Err(unsupported(pn, line));
        }
        if parse::is_null(alias) {
            (full_q, first, false)
        } else if parse::is_id(alias) {
            (full_q, parse::leaf_arg(alias) as Qstr, true)
        } else {
            return Err(unsupported(pn, line));
        }
    } else {
        let (n, first) = dotted_to_buf(one, &mut full_buf).ok_or_else(|| unsupported(pn, line))?;
        let full_q = crate::upy::py::qstr::from_strn(&full_buf[..n]);
        if full_q == 0 {
            return Err(unsupported(pn, line));
        }
        (full_q, first, false)
    };

    if want_leaf {
        w.load_const_true()?;
    } else {
        w.load_const_none()?;
    }
    w.import_name(full_q)?;
    store_name_or_fast(w, scope, bind_q)
}

// -- public entry points -----------------------------------------------------

/// Compile a `parse::InputKind::Eval` tree (`EvalInput[expr, _]`) to a
/// single expression followed by `RETURN_VALUE`.
pub fn compile_eval(tree: &ParseTree) -> Result<RawCode, CompileError> {
    let root = tree.root;
    if !parse::is_struct_kind(root, RuleId::EvalInput) {
        return Err(unsupported(root, 0));
    }
    let expr = unsafe { parse::struct_node(root, 0) };
    let mut w = Writer::new()?;
    let scope = Scope::new(ScopeKind::Module);
    compile_expr(&mut w, &scope, expr, parse::struct_source_line(root))?;
    w.return_value()?;
    Ok(w.finish(0))
}

/// Compile a `parse::InputKind::File` tree (module top level) ending in
/// an implicit `return None`.
pub fn compile_file(tree: &ParseTree) -> Result<RawCode, CompileError> {
    let mut w = Writer::new()?;
    let scope = Scope::new(ScopeKind::Module);
    let mut loops = LoopStack::new();
    compile_stmt(&mut w, &mut loops, &scope, tree.root, 0)?;
    w.load_const_none()?;
    w.return_value()?;
    Ok(w.finish(0))
}

/// Compile the body of a single top-level `def f(...): ...` (i.e.
/// `tree.root` is a lone `Funcdef`) with real parameter + local slots.
pub fn compile_funcdef_body(tree: &ParseTree) -> Result<RawCode, CompileError> {
    let root = tree.root;
    if !parse::is_struct_kind(root, RuleId::Funcdef) {
        return Err(CompileError::NotAFuncdef);
    }
    let params = unsafe { parse::struct_node(root, 1) };
    let body = unsafe { parse::struct_node(root, 3) };

    let mut scope = Scope::new(ScopeKind::Function);
    let line = parse::struct_source_line(root);
    collect_simple_params(&mut scope, params, line)?;
    prescan_locals(&mut scope, body)?;

    compile_function_body(&scope, body, line)
}
