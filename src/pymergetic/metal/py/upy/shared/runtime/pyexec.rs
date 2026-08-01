//! pyexec — run a bytecode buffer on the Metal upy mini-vm (SHARED_OPT).

use crate::upy::py::obj::MpObj;
use crate::upy::py::runtime::{self, VmReturnKind};
use crate::upy::py::vm;

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum ExecResult {
    Ok(MpObj),
    Exception,
    Incomplete,
}

/// Execute raw bytecode (no lexer/compiler yet). Async-first: caller may
/// park around this later; today it runs to completion on the C stack for
/// the short const/return subset.
pub unsafe fn execute_bytecode(code: &[u8]) -> ExecResult {
    if !crate::upy::py::mpstate::ready() {
        runtime::init();
    }
    let mut st = vm::CodeState::new();
    match vm::execute(code, &mut st) {
        VmReturnKind::Normal => ExecResult::Ok(st.result),
        VmReturnKind::Exception => ExecResult::Exception,
        VmReturnKind::Yield => ExecResult::Incomplete,
    }
}

/// True/false immediate from LOAD_CONST_TRUE / FALSE + RETURN.
pub unsafe fn exec_returns_true(code: &[u8]) -> bool {
    match execute_bytecode(code) {
        ExecResult::Ok(o) => crate::upy::py::obj::is_immediate(o) && {
            use crate::upy::py::objects::objbool;
            objbool::value(o) == Some(true)
        },
        _ => false,
    }
}
