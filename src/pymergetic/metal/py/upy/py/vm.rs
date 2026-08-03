//! vm — bytecode loop for the MetalPython REPL language slice: locals/
//! names, unary/binary ops, strings, attrs, imports, typed native calls,
//! bytecode `FunBc` calls, jumps, list/tuple/dict/set build + subscript,
//! iteration, catchable exceptions, and `LOAD_CONST_OBJ` against a
//! per-code const table.
//!
//! ## Runtime semantics for the opcodes this loop implements
//! - **`LOAD_FAST_MULTI`/`STORE_FAST_MULTI`**: index into
//!   `CodeState::locals`, a fixed `bc0::LOAD_FAST_MULTI_NUM`-slot array --
//!   the only local-slot width `emitbc.rs` emits (see `scope.rs` doc on
//!   `MAX_LOCALS`).
//! - **`LOAD_NAME`/`STORE_NAME`**: qstr (raw id, unsigned varint) against
//!   `CodeState::globals`. Missing name on load raises (see below).
//! - **Binary/unary arithmetic ops on small ints only.** `IS`/`IN`/
//!   `EQUAL`/`NOT_EQUAL`/`EXCEPTION_MATCH` work on any object (see
//!   `binary_op`). Other unsupported type/op combinations raise.
//! - **`LOAD_CONST_STRING`**: allocates a fresh `objstr` from the qstr.
//! - **`LOAD_CONST_OBJ`**: decode uint idx, push `consts[idx]` (passed
//!   into [`execute`]; empty for code with no const table).
//! - **`LOAD_ATTR`/`STORE_ATTR`**: module attributes, or user-class
//!   instance attributes/bound methods (see `load_attr_dispatch`).
//! - **`CALL_FUNCTION`**: positional-only (`n_kw` always 0). Tries
//!   [`objfun_native::call`], a [`objboundmethod`] (injects `self`), a
//!   user [`objtype::UserType`] (constructs an [`objinstance::Instance`]
//!   and calls `__init__` if present), else recursively executes a
//!   [`objfun`] `FunBc` with args in `locals[0..n_pos]` and the parent's
//!   globals. Other callees / failures raise.
//! - **`IMPORT_NAME`**: pops fromlist TOS -- `None` yields the top
//!   package (plain `import a.b.c`); non-`None` yields the leaf (from /
//!   `import ... as`). Resolves via [`builtinimport::import_module`].
//! - **`IMPORT_FROM`**: peek module TOS, load attr by qstr, push attr
//!   (module stays on stack -- upstream `IMPORT_FROM` semantics).
//! - **`JUMP` / `POP_JUMP_IF_*`**: signed relative offset via
//!   [`bc::decode_sint_offset`]; `ip += offset` after the offset bytes
//!   (MicroPython convention). **`FOR_ITER`** uses the same signed
//!   relative offset, taken only on exhaustion (see `objiter` module doc).
//! - **`GET_ITER`**: pop iterable, push a fresh [`objiter::Iter`].
//! - **`BUILD_LIST` / `BUILD_TUPLE` / `BUILD_MAP` / `BUILD_SET`**: pop
//!   `n` (or `2*n` key/value pairs for `BUILD_MAP`), build, push.
//! - **`LOAD_SUBSCR`**: list/tuple (index) or dict/str (arbitrary key --
//!   `dict`) load; **`STORE_SUBSCR`**: pops `(value, index, container)`
//!   in that top-to-bottom order (`compile.rs` pushes container, index,
//!   value so `value` ends up on top) -- list assignment only.
//! - **`DUP_TOP`/`DUP_TOP_TWO`/`ROT_TWO`/`LIST_APPEND`**: see `bc0.rs` docs.
//! - **`SETUP_EXCEPT`/`POP_EXCEPT`/`RAISE_OBJ`**: see
//!   [`try_handle_exception`] doc -- every fault in this loop (VM-
//!   internal or user `raise`) routes through the same catchable path.

use super::bc;
use super::bc0;
use super::builtin::{builtinimport, modsys};
use super::emitcommon::{self, MAX_CALL_ARGS};
use super::obj::{self, MpObj};
use super::objects::{
    self, objbool, objboundmethod, objdict, objfun, objfun_native, objinstance, objiter, objlist,
    objmodule, objnone, objrange, objset, objstr, objtuple, objtype,
};
use super::qstr;
use super::qstrdefs;
use super::runtime::VmReturnKind;

pub const STACK_MAX: usize = 32;

/// Cap on live nested `try`/`with` protected regions per call frame
/// (`compile.rs` rejects deeper nesting as `Unsupported` rather than
/// overflowing this silently).
pub const MAX_EXC_HANDLERS: usize = 8;

#[derive(Clone, Copy)]
pub struct ExcHandler {
    /// Absolute `ip` of the `except`-chain dispatch point (or the
    /// `finally`-then-reraise point for a finally-only wrapper).
    pub target_ip: usize,
    /// `sp` to restore before pushing the raised exception object.
    pub stack_depth: usize,
}

impl ExcHandler {
    const fn new() -> Self {
        Self {
            target_ip: 0,
            stack_depth: 0,
        }
    }
}

pub struct CodeState {
    pub ip: usize,
    /// Next free stack slot (0 = empty).
    pub sp: usize,
    pub stack: [MpObj; STACK_MAX],
    pub result: MpObj,
    /// Function-local slots (`LOAD_FAST_MULTI`/`STORE_FAST_MULTI`).
    /// Unused (all `OBJ_NULL`) for code that never reads/writes a local.
    pub locals: [MpObj; bc0::LOAD_FAST_MULTI_NUM],
    /// Module globals dict (`LOAD_NAME`/`STORE_NAME`). `obj::OBJ_NULL` for
    /// code that never uses a name (e.g. a bare `eval` expression).
    pub globals: MpObj,
    /// Active `SETUP_EXCEPT` handler frames (top = last pushed).
    pub exc_handlers: [ExcHandler; MAX_EXC_HANDLERS],
    pub n_exc_handlers: usize,
}

impl CodeState {
    pub const fn new() -> Self {
        Self {
            ip: 0,
            sp: 0,
            stack: [obj::OBJ_NULL; STACK_MAX],
            result: obj::OBJ_NULL,
            locals: [obj::OBJ_NULL; bc0::LOAD_FAST_MULTI_NUM],
            globals: obj::OBJ_NULL,
            exc_handlers: [ExcHandler::new(); MAX_EXC_HANDLERS],
            n_exc_handlers: 0,
        }
    }

    fn push(&mut self, v: MpObj) -> bool {
        if self.sp >= STACK_MAX {
            return false;
        }
        self.stack[self.sp] = v;
        self.sp += 1;
        true
    }

    fn pop(&mut self) -> Option<MpObj> {
        if self.sp == 0 {
            return None;
        }
        self.sp -= 1;
        Some(self.stack[self.sp])
    }

    fn peek(&self) -> Option<MpObj> {
        if self.sp == 0 {
            return None;
        }
        Some(self.stack[self.sp - 1])
    }

    fn push_handler(&mut self, target_ip: usize) -> bool {
        if self.n_exc_handlers >= MAX_EXC_HANDLERS {
            return false;
        }
        self.exc_handlers[self.n_exc_handlers] = ExcHandler {
            target_ip,
            stack_depth: self.sp,
        };
        self.n_exc_handlers += 1;
        true
    }
}

/// Route a fault to the innermost active exception handler if one
/// exists (pop it, rewind `sp` to what it was when the handler was set
/// up, push `exc` for the `except`-chain dispatch code to inspect/bind,
/// jump to its target); this is the single place every VM-internal
/// fault (stack underflow, bad opcode args, arithmetic fault, ...) and
/// every user `RAISE_OBJ` funnels through, so `try`/`except` catches
/// both uniformly, not just user-raised exceptions. `true` means "a
/// handler took it, resume the loop"; `false` means "uncaught".
fn try_handle_exception(st: &mut CodeState, exc: MpObj) -> bool {
    if st.n_exc_handlers == 0 {
        return false;
    }
    st.n_exc_handlers -= 1;
    let h = st.exc_handlers[st.n_exc_handlers];
    st.sp = h.stack_depth;
    if !st.push(exc) {
        return false;
    }
    st.ip = h.target_ip;
    true
}

/// Raise `exc` (or, with one argument, a generic internal-fault
/// `runtime_exc()`) through [`try_handle_exception`]; on a caught
/// exception this `continue`s the enclosing `loop` in [`execute`] --
/// relies on macro expansion (not hygiene-renamed) placing that
/// `continue` lexically inside that loop at every call site.
macro_rules! raise {
    ($st:expr, $exc:expr) => {{
        let exc = $exc;
        if try_handle_exception($st, exc) {
            continue;
        }
        $st.result = exc;
        return VmReturnKind::Exception;
    }};
    ($st:expr) => {
        raise!($st, runtime_exc())
    };
}

/// Run `code` until return / yield / exception. `consts` is the
/// `LOAD_CONST_OBJ` table for this code object (`&[]` if none).
pub fn execute(code: &[u8], consts: &[MpObj], st: &mut CodeState) -> VmReturnKind {
    loop {
        if st.ip >= code.len() {
            raise!(st);
        }
        let op = code[st.ip];
        st.ip += 1;

        match op {
            x if x == bc0::LOAD_CONST_FALSE => {
                if !st.push(bool_obj(false)) {
                    raise!(st);
                }
            }
            x if x == bc0::LOAD_CONST_TRUE => {
                if !st.push(bool_obj(true)) {
                    raise!(st);
                }
            }
            x if x == bc0::LOAD_CONST_NONE => {
                if !st.push(none_obj()) {
                    raise!(st);
                }
            }
            x if x == bc0::POP_TOP => {
                if st.pop().is_none() {
                    raise!(st);
                }
            }
            x if x == bc0::DUP_TOP => {
                let Some(v) = st.peek() else {
                    raise!(st);
                };
                if !st.push(v) {
                    raise!(st);
                }
            }
            x if x == bc0::DUP_TOP_TWO => {
                if st.sp < 2 {
                    raise!(st);
                }
                let a = st.stack[st.sp - 2];
                let b = st.stack[st.sp - 1];
                if !st.push(a) || !st.push(b) {
                    raise!(st);
                }
            }
            x if x == bc0::ROT_TWO => {
                if st.sp < 2 {
                    raise!(st);
                }
                st.stack.swap(st.sp - 1, st.sp - 2);
            }
            x if x == bc0::RETURN_VALUE => {
                return match st.pop() {
                    Some(v) => {
                        st.result = v;
                        VmReturnKind::Normal
                    }
                    None => {
                        st.result = runtime_exc();
                        VmReturnKind::Exception
                    }
                };
            }
            x if x == bc0::YIELD_VALUE => {
                return match st.pop() {
                    Some(v) => {
                        st.result = v;
                        VmReturnKind::Yield
                    }
                    None => {
                        st.result = runtime_exc();
                        VmReturnKind::Exception
                    }
                };
            }
            x if x == bc0::RAISE_OBJ => {
                let Some(exc) = st.pop() else {
                    raise!(st);
                };
                raise!(st, exc);
            }
            x if x == bc0::SETUP_EXCEPT => {
                let Some(off) = bc::decode_sint_offset(code, &mut st.ip) else {
                    raise!(st);
                };
                let Some(target) = compute_jump_target(st.ip, off, code.len()) else {
                    raise!(st);
                };
                if !st.push_handler(target) {
                    raise!(st);
                }
            }
            x if x == bc0::POP_EXCEPT => {
                if st.n_exc_handlers == 0 {
                    raise!(st);
                }
                st.n_exc_handlers -= 1;
            }
            x if (bc0::LOAD_CONST_SMALL_INT_MULTI
                ..bc0::LOAD_CONST_SMALL_INT_MULTI + bc0::LOAD_CONST_SMALL_INT_MULTI_NUM as u8)
                .contains(&x) =>
            {
                let excess = bc0::LOAD_CONST_SMALL_INT_MULTI_EXCESS;
                let v = (x - bc0::LOAD_CONST_SMALL_INT_MULTI) as isize - excess;
                if !st.push(obj::new_small_int(v)) {
                    raise!(st);
                }
            }
            x if x == bc0::LOAD_CONST_SMALL_INT => {
                let Some(u) = bc::decode_uint(code, &mut st.ip) else {
                    raise!(st);
                };
                if !st.push(obj::new_small_int(bc::zigzag_decode(u))) {
                    raise!(st);
                }
            }
            x if x == bc0::LOAD_CONST_OBJ => {
                let Some(idx) = bc::decode_uint(code, &mut st.ip) else {
                    raise!(st);
                };
                if idx >= consts.len() {
                    raise!(st);
                }
                if !st.push(consts[idx]) {
                    raise!(st);
                }
            }
            x if x == bc0::LOAD_CONST_QSTR => {
                let Some(qst) = bc::decode_uint(code, &mut st.ip) else {
                    raise!(st);
                };
                if !st.push(obj::new_qstr(qst)) {
                    raise!(st);
                }
            }
            x if (bc0::LOAD_FAST_MULTI..bc0::LOAD_FAST_MULTI + bc0::LOAD_FAST_MULTI_NUM as u8)
                .contains(&x) =>
            {
                let slot = (x - bc0::LOAD_FAST_MULTI) as usize;
                if !st.push(st.locals[slot]) {
                    raise!(st);
                }
            }
            x if (bc0::STORE_FAST_MULTI..bc0::STORE_FAST_MULTI + bc0::STORE_FAST_MULTI_NUM as u8)
                .contains(&x) =>
            {
                let slot = (x - bc0::STORE_FAST_MULTI) as usize;
                match st.pop() {
                    Some(v) => st.locals[slot] = v,
                    None => raise!(st),
                }
            }
            x if x == bc0::LOAD_NAME => {
                let Some(qst) = bc::decode_uint(code, &mut st.ip) else {
                    raise!(st);
                };
                let key = obj::new_qstr(qst);
                let v = if st.globals != obj::OBJ_NULL {
                    unsafe { objdict::load(st.globals, key) }
                } else {
                    None
                };
                match v {
                    Some(v) => {
                        if !st.push(v) {
                            raise!(st);
                        }
                    }
                    None => raise!(st),
                }
            }
            x if x == bc0::STORE_NAME => {
                let Some(qst) = bc::decode_uint(code, &mut st.ip) else {
                    raise!(st);
                };
                let key = obj::new_qstr(qst);
                let ok = match st.pop() {
                    Some(v) if st.globals != obj::OBJ_NULL => unsafe {
                        objdict::store(st.globals, key, v)
                    },
                    _ => false,
                };
                if !ok {
                    raise!(st);
                }
            }
            x if x == bc0::LOAD_CONST_STRING => {
                let Some(qst) = bc::decode_uint(code, &mut st.ip) else {
                    raise!(st);
                };
                let s = unsafe { objstr::new(qstr::str(qst)) };
                if s == obj::OBJ_NULL || !st.push(s) {
                    raise!(st);
                }
            }
            x if x == bc0::LOAD_ATTR => {
                let Some(qst) = bc::decode_uint(code, &mut st.ip) else {
                    raise!(st);
                };
                let Some(obj_v) = st.pop() else {
                    raise!(st);
                };
                let key = obj::new_qstr(qst);
                match unsafe { load_attr_dispatch(obj_v, key) } {
                    Some(v) => {
                        if !st.push(v) {
                            raise!(st);
                        }
                    }
                    None => raise!(st),
                }
            }
            x if x == bc0::STORE_ATTR => {
                let Some(qst) = bc::decode_uint(code, &mut st.ip) else {
                    raise!(st);
                };
                let (Some(obj_v), Some(val)) = (st.pop(), st.pop()) else {
                    raise!(st);
                };
                let key = obj::new_qstr(qst);
                let ok = unsafe {
                    if objtype::is_user_type(obj_v) {
                        match objtype::user_methods(obj_v) {
                            Some(methods) => objdict::store(methods, key, val),
                            None => false,
                        }
                    } else {
                        objmodule::store_attr(obj_v, key, val)
                            || objinstance::store_attr(obj_v, key, val)
                    }
                };
                if !ok {
                    raise!(st);
                }
            }
            x if x == bc0::CALL_FUNCTION => {
                // `n_kw` is always 0 in this compiler slice (see
                // `emitbc::Writer::call_function`), so the whole varint
                // is `n_pos`.
                let Some(n_pos) = bc::decode_uint(code, &mut st.ip) else {
                    raise!(st);
                };
                if n_pos > MAX_CALL_ARGS {
                    raise!(st);
                }
                let mut args = [obj::OBJ_NULL; MAX_CALL_ARGS];
                for i in (0..n_pos).rev() {
                    match st.pop() {
                        Some(v) => args[i] = v,
                        None => raise!(st),
                    }
                }
                let Some(callee) = st.pop() else {
                    raise!(st);
                };
                let result = unsafe { call_function(callee, &args[..n_pos], st.globals) };
                match result {
                    Some(v) => {
                        if !st.push(v) {
                            raise!(st);
                        }
                    }
                    None => raise!(st),
                }
            }
            x if x == bc0::IMPORT_NAME => {
                let Some(qst) = bc::decode_uint(code, &mut st.ip) else {
                    raise!(st);
                };
                // fromlist TOS: None -> top package (plain `import a.b.c`);
                // non-None -> leaf module (`from a.b import x`,
                // `import a.b.c as m`). Names for `from` still come from
                // subsequent IMPORT_FROM.
                let Some(fromlist) = st.pop() else {
                    raise!(st);
                };
                let want_leaf = !objnone::is_none(fromlist);
                let module = match core::str::from_utf8(qstr::str(qst)) {
                    Ok(s) => unsafe {
                        let top = builtinimport::import_module(s);
                        if want_leaf {
                            modsys::modules_get_str(s).or(top)
                        } else {
                            top
                        }
                    },
                    Err(_) => None,
                };
                match module {
                    Some(m) => {
                        if !st.push(m) {
                            raise!(st);
                        }
                    }
                    None => raise!(st),
                }
            }
            x if x == bc0::IMPORT_FROM => {
                let Some(qst) = bc::decode_uint(code, &mut st.ip) else {
                    raise!(st);
                };
                let Some(module) = st.peek() else {
                    raise!(st);
                };
                let key = obj::new_qstr(qst);
                match unsafe { objmodule::load_attr(module, key) } {
                    Some(v) => {
                        if !st.push(v) {
                            raise!(st);
                        }
                    }
                    None => raise!(st),
                }
            }
            x if x == bc0::JUMP => {
                let Some(off) = bc::decode_sint_offset(code, &mut st.ip) else {
                    raise!(st);
                };
                if !apply_jump(st, off, code.len()) {
                    raise!(st);
                }
            }
            x if x == bc0::POP_JUMP_IF_FALSE => {
                let Some(off) = bc::decode_sint_offset(code, &mut st.ip) else {
                    raise!(st);
                };
                let Some(v) = st.pop() else {
                    raise!(st);
                };
                if !is_true(v) && !apply_jump(st, off, code.len()) {
                    raise!(st);
                }
            }
            x if x == bc0::POP_JUMP_IF_TRUE => {
                let Some(off) = bc::decode_sint_offset(code, &mut st.ip) else {
                    raise!(st);
                };
                let Some(v) = st.pop() else {
                    raise!(st);
                };
                if is_true(v) && !apply_jump(st, off, code.len()) {
                    raise!(st);
                }
            }
            x if x == bc0::GET_ITER => {
                let Some(v) = st.pop() else {
                    raise!(st);
                };
                let it = unsafe { objiter::new(v) };
                if it == obj::OBJ_NULL || !st.push(it) {
                    raise!(st);
                }
            }
            x if x == bc0::FOR_ITER => {
                let Some(off) = bc::decode_sint_offset(code, &mut st.ip) else {
                    raise!(st);
                };
                let Some(it) = st.peek() else {
                    raise!(st);
                };
                match unsafe { objiter::next(it) } {
                    Some(Some(item)) => {
                        if !st.push(item) {
                            raise!(st);
                        }
                    }
                    Some(None) => {
                        // Exhausted: pop the iterator, then jump past the loop.
                        st.pop();
                        if !apply_jump(st, off, code.len()) {
                            raise!(st);
                        }
                    }
                    None => raise!(st),
                }
            }
            x if x == bc0::BUILD_LIST => {
                let Some(n) = bc::decode_uint(code, &mut st.ip) else {
                    raise!(st);
                };
                match unsafe { build_list(st, n) } {
                    Some(lst) => {
                        if !st.push(lst) {
                            raise!(st);
                        }
                    }
                    None => raise!(st),
                }
            }
            x if x == bc0::BUILD_TUPLE => {
                let Some(n) = bc::decode_uint(code, &mut st.ip) else {
                    raise!(st);
                };
                match unsafe { build_tuple(st, n) } {
                    Some(t) => {
                        if !st.push(t) {
                            raise!(st);
                        }
                    }
                    None => raise!(st),
                }
            }
            x if x == bc0::BUILD_MAP => {
                let Some(n) = bc::decode_uint(code, &mut st.ip) else {
                    raise!(st);
                };
                match unsafe { build_map(st, n) } {
                    Some(d) => {
                        if !st.push(d) {
                            raise!(st);
                        }
                    }
                    None => raise!(st),
                }
            }
            x if x == bc0::BUILD_SET => {
                let Some(n) = bc::decode_uint(code, &mut st.ip) else {
                    raise!(st);
                };
                match unsafe { build_set(st, n) } {
                    Some(s) => {
                        if !st.push(s) {
                            raise!(st);
                        }
                    }
                    None => raise!(st),
                }
            }
            x if x == bc0::LOAD_SUBSCR => {
                let (Some(idx), Some(container)) = (st.pop(), st.pop()) else {
                    raise!(st);
                };
                match unsafe { load_subscr(container, idx) } {
                    Some(v) => {
                        if !st.push(v) {
                            raise!(st);
                        }
                    }
                    None => raise!(st),
                }
            }
            x if x == bc0::STORE_SUBSCR => {
                // Stack (bottom-to-top, i.e. push order): container,
                // index, value -- `compile.rs` pushes the target's own
                // sub-expressions first, the assigned value last, so
                // `value` ends up on TOS (matches an augmented
                // subscript assignment's natural evaluation order too;
                // see `bc0::DUP_TOP_TWO` doc).
                let (Some(value), Some(idx), Some(container)) = (st.pop(), st.pop(), st.pop())
                else {
                    raise!(st);
                };
                if !unsafe { store_subscr(container, idx, value) } {
                    raise!(st);
                }
            }
            x if x == bc0::LIST_APPEND => {
                let (Some(value), Some(list)) = (st.pop(), st.pop()) else {
                    raise!(st);
                };
                if !unsafe { objlist::append(list, value) } {
                    raise!(st);
                }
            }
            x if (bc0::UNARY_OP_MULTI..bc0::BINARY_OP_MULTI).contains(&x) => {
                let op = x - bc0::UNARY_OP_MULTI;
                let Some(v) = st.pop() else {
                    raise!(st);
                };
                match unary_op(op, v) {
                    Some(r) => {
                        if !st.push(r) {
                            raise!(st);
                        }
                    }
                    None => raise!(st),
                }
            }
            x if x >= bc0::BINARY_OP_MULTI => {
                let op = x - bc0::BINARY_OP_MULTI;
                let (Some(rhs), Some(lhs)) = (st.pop(), st.pop()) else {
                    raise!(st);
                };
                match unsafe { binary_op(op, lhs, rhs) } {
                    Some(r) => {
                        if !st.push(r) {
                            raise!(st);
                        }
                    }
                    None => raise!(st),
                }
            }
            _ => raise!(st),
        }
    }
}

/// Module attribute, or user-class instance attribute/bound-method
/// dispatch (upstream `mp_load_attr`'s instance-attr + bound-method
/// slice): the instance's own attribute dict wins over a class method
/// (matches Python's instance-dict-then-class-dict precedence); a
/// method found only on the class comes back wrapped as a
/// [`objboundmethod::BoundMethod`] so `self` is injected on call.
unsafe fn load_attr_dispatch(o: MpObj, key: MpObj) -> Option<MpObj> {
    if let Some(v) = objmodule::load_attr(o, key) {
        return Some(v);
    }
    if objinstance::is_instance(o) {
        if let Some(v) = objinstance::load_attr(o, key) {
            return Some(v);
        }
        let class = objinstance::class_of(o)?;
        let m = objtype::find_method(class, key)?;
        return Some(objboundmethod::new(o, m));
    }
    // Class-object attribute (methods dict) -- not wrapped as a bound
    // method; calling `C.meth(inst, ...)` is the caller's job.
    if objtype::is_user_type(o) {
        return objtype::find_method(o, key);
    }
    None
}

/// Native first; then a bound method (inject `self`); then a user
/// class (construct an instance, run `__init__` if present); else a
/// `FunBc` with a fresh `CodeState` sharing `globals`. `None` on
/// unknown callee / arity / nested failure.
unsafe fn call_function(callee: MpObj, args: &[MpObj], globals: MpObj) -> Option<MpObj> {
    if objfun_native::is_fun_native(callee) {
        return objfun_native::call(callee, args);
    }
    if objboundmethod::is_bound_method(callee) {
        let (self_obj, func) = objboundmethod::parts(callee)?;
        if args.len() + 1 > MAX_CALL_ARGS {
            return None;
        }
        let mut buf = [obj::OBJ_NULL; MAX_CALL_ARGS];
        buf[0] = self_obj;
        buf[1..1 + args.len()].copy_from_slice(args);
        return call_function(func, &buf[..1 + args.len()], globals);
    }
    if objtype::is_user_type(callee) {
        let inst = objinstance::new(callee);
        if inst == obj::OBJ_NULL {
            return None;
        }
        let init_key = obj::new_qstr(qstrdefs::QSTR_INIT);
        if let Some(init_fn) = objtype::find_method(callee, init_key) {
            if args.len() + 1 > MAX_CALL_ARGS {
                return None;
            }
            let mut buf = [obj::OBJ_NULL; MAX_CALL_ARGS];
            buf[0] = inst;
            buf[1..1 + args.len()].copy_from_slice(args);
            call_function(init_fn, &buf[..1 + args.len()], globals)?;
        }
        return Some(inst);
    }
    if !objfun::is_fun_bc(callee) {
        return None;
    }
    let code = objfun::code(callee)?;
    let n_state = objfun::n_state(callee)?;
    let fun_consts = objfun::consts(callee)?;
    if args.len() > n_state || args.len() > bc0::LOAD_FAST_MULTI_NUM {
        return None;
    }
    let mut child = CodeState::new();
    child.globals = globals;
    for (i, a) in args.iter().enumerate() {
        child.locals[i] = *a;
    }
    match execute(code, fun_consts, &mut child) {
        VmReturnKind::Normal => Some(child.result),
        _ => None,
    }
}

/// After `decode_sint_offset`, `st.ip` is past the offset bytes; apply
/// MicroPython's `ip += offset`.
fn apply_jump(st: &mut CodeState, off: isize, code_len: usize) -> bool {
    match compute_jump_target(st.ip, off, code_len) {
        Some(t) => {
            st.ip = t;
            true
        }
        None => false,
    }
}

fn compute_jump_target(ip: usize, off: isize, code_len: usize) -> Option<usize> {
    let nip = (ip as isize).checked_add(off)?;
    if nip < 0 || (nip as usize) > code_len {
        return None;
    }
    Some(nip as usize)
}

fn is_true(o: MpObj) -> bool {
    if objnone::is_none(o) {
        return false;
    }
    if let Some(b) = objbool::value(o) {
        return b;
    }
    if let Some(v) = obj::small_int_value_checked(o) {
        return v != 0;
    }
    unsafe {
        if let Some(n) = objlist::len(o) {
            return n != 0;
        }
        if let Some(n) = objtuple::len(o) {
            return n != 0;
        }
        if let Some(n) = objdict::len(o) {
            return n != 0;
        }
        if let Some(n) = objset::len(o) {
            return n != 0;
        }
        if let Some(b) = objstr::as_bytes(o) {
            return !b.is_empty();
        }
    }
    true
}

unsafe fn build_list(st: &mut CodeState, n: usize) -> Option<MpObj> {
    if n > STACK_MAX {
        return None;
    }
    let mut items = [obj::OBJ_NULL; STACK_MAX];
    for i in (0..n).rev() {
        items[i] = st.pop()?;
    }
    let lst = objlist::new(n);
    if lst == obj::OBJ_NULL {
        return None;
    }
    for i in 0..n {
        if !objlist::append(lst, items[i]) {
            return None;
        }
    }
    Some(lst)
}

unsafe fn build_tuple(st: &mut CodeState, n: usize) -> Option<MpObj> {
    if n > STACK_MAX {
        return None;
    }
    let mut items = [obj::OBJ_NULL; STACK_MAX];
    for i in (0..n).rev() {
        items[i] = st.pop()?;
    }
    let t = objtuple::new(&items[..n]);
    if t == obj::OBJ_NULL {
        None
    } else {
        Some(t)
    }
}

unsafe fn build_map(st: &mut CodeState, n: usize) -> Option<MpObj> {
    if n > STACK_MAX / 2 {
        return None;
    }
    let mut items = [obj::OBJ_NULL; STACK_MAX];
    for i in (0..2 * n).rev() {
        items[i] = st.pop()?;
    }
    let d = objdict::new(if n < 4 { 4 } else { n });
    if d == obj::OBJ_NULL {
        return None;
    }
    for i in 0..n {
        if !objdict::store(d, items[2 * i], items[2 * i + 1]) {
            return None;
        }
    }
    Some(d)
}

unsafe fn build_set(st: &mut CodeState, n: usize) -> Option<MpObj> {
    if n > STACK_MAX {
        return None;
    }
    let mut items = [obj::OBJ_NULL; STACK_MAX];
    for i in (0..n).rev() {
        items[i] = st.pop()?;
    }
    let s = objset::new(if n < 4 { 4 } else { n });
    if s == obj::OBJ_NULL {
        return None;
    }
    for i in 0..n {
        if !objset::add(s, items[i]) {
            return None;
        }
    }
    Some(s)
}

unsafe fn load_subscr(container: MpObj, idx: MpObj) -> Option<MpObj> {
    if let Some(v) = objdict::load(container, idx) {
        return Some(v);
    }
    if objdict::len(container).is_some() {
        // A real dict, but the key was missing -- a `KeyError`-shaped
        // fault, not "fall through and try list/tuple indexing".
        return None;
    }
    let i = obj::small_int_value_checked(idx)?;
    if i < 0 {
        return None;
    }
    let i = i as usize;
    if let Some(v) = objlist::get(container, i) {
        return Some(v);
    }
    objtuple::get(container, i)
}

unsafe fn store_subscr(container: MpObj, idx: MpObj, value: MpObj) -> bool {
    if objdict::len(container).is_some() {
        return objdict::store(container, idx, value);
    }
    let Some(i) = obj::small_int_value_checked(idx) else {
        return false;
    };
    if i < 0 {
        return false;
    }
    objlist::set(container, i as usize, value)
}

/// `v in container` membership (upstream `mp_binary_op`'s
/// `MP_BINARY_OP_IN` handler): substring for `str`, element scan for
/// `list`/`tuple`, key presence for `dict`, membership for `set`,
/// arithmetic membership for `range`.
unsafe fn contains_op(item: MpObj, container: MpObj) -> Option<bool> {
    if let Some(cbytes) = objstr::as_bytes(container) {
        let ibytes = objstr::as_bytes(item)?;
        return Some(bytes_contains(cbytes, ibytes));
    }
    if let Some(n) = objlist::len(container) {
        for i in 0..n {
            if objects::obj_eq(objlist::get(container, i)?, item) {
                return Some(true);
            }
        }
        return Some(false);
    }
    if let Some(n) = objtuple::len(container) {
        for i in 0..n {
            if objects::obj_eq(objtuple::get(container, i)?, item) {
                return Some(true);
            }
        }
        return Some(false);
    }
    if objdict::len(container).is_some() {
        return Some(objdict::load(container, item).is_some());
    }
    if objset::len(container).is_some() {
        return Some(objset::contains(container, item));
    }
    if objrange::len(container).is_some() {
        let v = obj::small_int_value_checked(item)?;
        return objrange::contains(container, v);
    }
    None
}

fn bytes_contains(hay: &[u8], needle: &[u8]) -> bool {
    if needle.is_empty() {
        return true;
    }
    if needle.len() > hay.len() {
        return false;
    }
    hay.windows(needle.len()).any(|w| w == needle)
}

/// `except <TypeName>:` dispatch (upstream `MP_BINARY_OP_EXCEPTION_MATCH`
/// slice): `marker_qstr == "Exception"` is a catch-all (there's no real
/// exception-class hierarchy in this VM, so the one universal base name
/// stands in for it); otherwise compares `exc`'s own type name (an
/// `objexcept::Except`'s `type_name`, or a user-class `objinstance`'s
/// class name) against the marker by exact qstr equality.
unsafe fn exception_match(exc: MpObj, marker: super::qstrdefs::Qstr) -> bool {
    if marker == qstrdefs::QSTR_EXCEPTION {
        return true;
    }
    if let Some(tn) = super::objects::objexcept::type_name(exc) {
        return tn == marker;
    }
    if objinstance::is_instance(exc) {
        if let Some(class) = objinstance::class_of(exc) {
            if let Some(n) = objtype::name(class) {
                return n == marker;
            }
        }
    }
    false
}

/// `op` is one of `emitcommon::UNARY_OP_*`. `None` on a type/op mismatch
/// (caller turns that into a raised exception).
fn unary_op(op: u8, v: MpObj) -> Option<MpObj> {
    if op == emitcommon::UNARY_OP_NOT {
        // Truthiness, not small-int-only -- `not True` / `not (x in y)`
        // both produce bool immediates, not small ints.
        return Some(bool_obj(!is_true(v)));
    }
    let a = obj::small_int_value_checked(v)?;
    match op {
        emitcommon::UNARY_OP_POSITIVE => Some(obj::new_small_int(a)),
        emitcommon::UNARY_OP_NEGATIVE => a.checked_neg().map(obj::new_small_int),
        emitcommon::UNARY_OP_INVERT => Some(obj::new_small_int(!a)),
        _ => None,
    }
}

/// `op` is one of `emitcommon::BINARY_OP_*`. `IS`/`IN`/`EQUAL`/
/// `NOT_EQUAL`/`EXCEPTION_MATCH` work on any object pair; every other op
/// is small-ints-only (no bigint promotion, no float coercion -- see
/// module doc). `None` on a type/op mismatch or an arithmetic fault
/// (overflow, division/modulo by zero, negative shift amount) --
/// caller turns that into a raised exception.
unsafe fn binary_op(op: u8, lhs: MpObj, rhs: MpObj) -> Option<MpObj> {
    match op {
        emitcommon::BINARY_OP_IS => return Some(bool_obj(lhs == rhs)),
        emitcommon::BINARY_OP_EQUAL => return Some(bool_obj(objects::obj_eq(lhs, rhs))),
        emitcommon::BINARY_OP_NOT_EQUAL => return Some(bool_obj(!objects::obj_eq(lhs, rhs))),
        emitcommon::BINARY_OP_IN => return contains_op(lhs, rhs).map(bool_obj),
        emitcommon::BINARY_OP_EXCEPTION_MATCH => {
            let marker = obj::qstr_value(rhs);
            return Some(bool_obj(exception_match(lhs, marker)));
        }
        _ => {}
    }
    let a = obj::small_int_value_checked(lhs)?;
    let b = obj::small_int_value_checked(rhs)?;
    match op {
        emitcommon::BINARY_OP_LESS => Some(bool_obj(a < b)),
        emitcommon::BINARY_OP_MORE => Some(bool_obj(a > b)),
        emitcommon::BINARY_OP_LESS_EQUAL => Some(bool_obj(a <= b)),
        emitcommon::BINARY_OP_MORE_EQUAL => Some(bool_obj(a >= b)),
        emitcommon::BINARY_OP_OR => Some(obj::new_small_int(a | b)),
        emitcommon::BINARY_OP_XOR => Some(obj::new_small_int(a ^ b)),
        emitcommon::BINARY_OP_AND => Some(obj::new_small_int(a & b)),
        emitcommon::BINARY_OP_LSHIFT => shl_checked(a, b).map(obj::new_small_int),
        emitcommon::BINARY_OP_RSHIFT => shr_checked(a, b).map(obj::new_small_int),
        emitcommon::BINARY_OP_ADD => a.checked_add(b).map(obj::new_small_int),
        emitcommon::BINARY_OP_SUBTRACT => a.checked_sub(b).map(obj::new_small_int),
        emitcommon::BINARY_OP_MULTIPLY => a.checked_mul(b).map(obj::new_small_int),
        emitcommon::BINARY_OP_FLOOR_DIVIDE => py_floordiv(a, b).map(obj::new_small_int),
        emitcommon::BINARY_OP_MODULO => py_mod(a, b).map(obj::new_small_int),
        _ => None,
    }
}

fn shl_checked(a: isize, b: isize) -> Option<isize> {
    if b < 0 || b >= isize::BITS as isize {
        return None;
    }
    let r = (a as i128) << b;
    if r > isize::MAX as i128 || r < isize::MIN as i128 {
        None
    } else {
        Some(r as isize)
    }
}

fn shr_checked(a: isize, b: isize) -> Option<isize> {
    if b < 0 {
        return None;
    }
    if b >= isize::BITS as isize {
        Some(if a < 0 { -1 } else { 0 })
    } else {
        Some(a >> b)
    }
}

/// Python floor-division semantics (rounds toward negative infinity,
/// unlike Rust's truncating `/`). `None` on divide-by-zero.
fn py_floordiv(a: isize, b: isize) -> Option<isize> {
    if b == 0 {
        return None;
    }
    let q = a.checked_div(b)?;
    let r = a - q * b;
    if r != 0 && (r < 0) != (b < 0) {
        q.checked_sub(1)
    } else {
        Some(q)
    }
}

/// Python modulo semantics (result has the same sign as `b`). `None` on
/// divide-by-zero.
fn py_mod(a: isize, b: isize) -> Option<isize> {
    if b == 0 {
        return None;
    }
    let r = a.checked_rem(b)?;
    if r != 0 && (r < 0) != (b < 0) {
        r.checked_add(b)
    } else {
        Some(r)
    }
}

/// A generic, catchable internal-fault exception (`except Exception:`
/// matches it -- see `exception_match`). Falls back to the immediate
/// sentinel only if the allocation itself fails (OOM on the error path).
fn runtime_exc() -> MpObj {
    let e = unsafe { super::objects::objexcept::new(qstrdefs::QSTR_EXCEPTION, obj::OBJ_NULL) };
    if e == obj::OBJ_NULL {
        obj::OBJ_SENTINEL
    } else {
        e
    }
}

fn bool_obj(v: bool) -> MpObj {
    super::objects::objbool::get(v)
}

fn none_obj() -> MpObj {
    super::objects::objnone::get()
}
