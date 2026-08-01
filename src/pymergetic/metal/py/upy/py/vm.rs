//! vm — minimal bytecode loop for a finished B1 skeleton.
//!
//! Handles a small opcode set so the path is real (not a hollow stub).
//! Full vm.c lands across later bands.

use super::bc0;
use super::obj::{self, MpObj};
use super::runtime::VmReturnKind;

pub const STACK_MAX: usize = 32;

pub struct CodeState {
    pub ip: usize,
    /// Next free stack slot (0 = empty).
    pub sp: usize,
    pub stack: [MpObj; STACK_MAX],
    pub result: MpObj,
}

impl CodeState {
    pub const fn new() -> Self {
        Self {
            ip: 0,
            sp: 0,
            stack: [obj::OBJ_NULL; STACK_MAX],
            result: obj::OBJ_NULL,
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
}

/// Run `code` until return / yield / exception.
pub fn execute(code: &[u8], st: &mut CodeState) -> VmReturnKind {
    loop {
        if st.ip >= code.len() {
            st.result = runtime_exc();
            return VmReturnKind::Exception;
        }
        let op = code[st.ip];
        st.ip += 1;

        match op {
            x if x == bc0::LOAD_CONST_FALSE => {
                if !st.push(bool_obj(false)) {
                    st.result = runtime_exc();
                    return VmReturnKind::Exception;
                }
            }
            x if x == bc0::LOAD_CONST_TRUE => {
                if !st.push(bool_obj(true)) {
                    st.result = runtime_exc();
                    return VmReturnKind::Exception;
                }
            }
            x if x == bc0::LOAD_CONST_NONE => {
                if !st.push(none_obj()) {
                    st.result = runtime_exc();
                    return VmReturnKind::Exception;
                }
            }
            x if x == bc0::POP_TOP => {
                if st.pop().is_none() {
                    st.result = runtime_exc();
                    return VmReturnKind::Exception;
                }
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
            x if (bc0::LOAD_CONST_SMALL_INT_MULTI
                ..bc0::LOAD_CONST_SMALL_INT_MULTI + bc0::LOAD_CONST_SMALL_INT_MULTI_NUM as u8)
                .contains(&x) =>
            {
                let excess = bc0::LOAD_CONST_SMALL_INT_MULTI_EXCESS;
                let v = (x - bc0::LOAD_CONST_SMALL_INT_MULTI) as isize - excess;
                if !st.push(obj::new_small_int(v)) {
                    st.result = runtime_exc();
                    return VmReturnKind::Exception;
                }
            }
            _ => {
                st.result = runtime_exc();
                return VmReturnKind::Exception;
            }
        }
    }
}

fn runtime_exc() -> MpObj {
    obj::OBJ_SENTINEL
}

fn bool_obj(v: bool) -> MpObj {
    super::objects::objbool::get(v)
}

fn none_obj() -> MpObj {
    super::objects::objnone::get()
}
