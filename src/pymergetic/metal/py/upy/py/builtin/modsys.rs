//! modsys — sys.modules / path / version (single process, no isolation).

use core::cell::UnsafeCell;
use core::sync::atomic::{AtomicBool, Ordering};

use crate::upy::py::obj::{self, MpObj};
use crate::upy::py::objects::objdict;
use crate::upy::py::objects::objlist;
use crate::upy::py::objects::objmodule;
use crate::upy::py::objects::objstr;
use crate::upy::py::qstr;
use crate::upy::py::qstrdefs;

struct SysState {
    ready: AtomicBool,
    module: UnsafeCell<MpObj>,
    modules: UnsafeCell<MpObj>,
    path: UnsafeCell<MpObj>,
}

// Safety: init once under ready flag; later single-threaded host smoke / Metal runners use sync elsewhere.
unsafe impl Sync for SysState {}

static SYS: SysState = SysState {
    ready: AtomicBool::new(false),
    module: UnsafeCell::new(obj::OBJ_NULL),
    modules: UnsafeCell::new(obj::OBJ_NULL),
    path: UnsafeCell::new(obj::OBJ_NULL),
};

/// Initialize sys module + empty modules dict + path list.
pub unsafe fn init() {
    if SYS.ready.load(Ordering::Acquire) {
        return;
    }
    let modules = objdict::new(16);
    let path = objlist::new(0);
    let m = objmodule::new(qstr::from_str("sys"));
    if modules == obj::OBJ_NULL || path == obj::OBJ_NULL || m == obj::OBJ_NULL {
        return;
    }
    let _ = objlist::append(path, objstr::new(b""));
    let ver = objstr::new(b"Metal upy 0.1");
    let _ = objmodule::store_attr(m, obj::new_qstr(qstr::from_str("modules")), modules);
    let _ = objmodule::store_attr(m, obj::new_qstr(qstr::from_str("path")), path);
    let _ = objmodule::store_attr(m, obj::new_qstr(qstr::from_str("version")), ver);
    let _ = objmodule::store_attr(
        m,
        obj::new_qstr(qstrdefs::QSTR_NAME),
        obj::new_qstr(qstr::from_str("sys")),
    );
    *SYS.modules.get() = modules;
    *SYS.path.get() = path;
    *SYS.module.get() = m;
    // sys in sys.modules
    let _ = objdict::store(modules, obj::new_qstr(qstr::from_str("sys")), m);
    SYS.ready.store(true, Ordering::Release);
}

pub fn ready() -> bool {
    SYS.ready.load(Ordering::Acquire)
}

pub unsafe fn module() -> MpObj {
    init();
    *SYS.module.get()
}

pub unsafe fn modules_dict() -> MpObj {
    init();
    *SYS.modules.get()
}

pub unsafe fn path_list() -> MpObj {
    init();
    *SYS.path.get()
}

pub unsafe fn modules_get(name_q: crate::upy::py::qstrdefs::Qstr) -> Option<MpObj> {
    objdict::load(modules_dict(), obj::new_qstr(name_q))
}

pub unsafe fn modules_set(name_q: crate::upy::py::qstrdefs::Qstr, mod_obj: MpObj) -> bool {
    objdict::store(modules_dict(), obj::new_qstr(name_q), mod_obj)
}

pub unsafe fn modules_get_str(name: &str) -> Option<MpObj> {
    modules_get(qstr::from_str(name))
}

pub unsafe fn modules_set_str(name: &str, mod_obj: MpObj) -> bool {
    modules_set(qstr::from_str(name), mod_obj)
}
