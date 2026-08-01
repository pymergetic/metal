//! modos — uname / getenv / listdir (listdir via thin Metal vfs).

use crate::upy::extmod::vfs;
use crate::upy::py::obj::{self, MpObj};
use crate::upy::py::objects::{objstr, objtuple};

pub unsafe fn uname() -> MpObj {
    let sysname = objstr::new(b"Metal");
    let nodename = objstr::new(b"");
    let release = objstr::new(b"0");
    let version = objstr::new(b"upy-metal");
    let machine = objstr::new(b"metal");
    objtuple::new(&[sysname, nodename, release, version, machine])
}

pub unsafe fn getenv(_key: &[u8]) -> Option<MpObj> {
    // No process env on Metal firmware; always None.
    None
}

pub unsafe fn getenv_obj(key: MpObj) -> Option<MpObj> {
    let _ = objstr::as_bytes(key)?;
    getenv(&[])
}

pub unsafe fn listdir(path: &[u8]) -> Option<MpObj> {
    vfs::listdir(path)
}

pub unsafe fn listdir_obj(path: MpObj) -> Option<MpObj> {
    let bytes = objstr::as_bytes(path)?;
    listdir(bytes)
}

pub unsafe fn mkdir(path: &[u8]) -> i32 {
    // Delegate via open CREAT DIRECTORY when driver supports mkdir_async — thin:
    // return -1 until a mount is present (stat fails).
    if vfs::stat(path).is_some() {
        return -1; // exists
    }
    let _ = obj::OBJ_NULL;
    -1
}
