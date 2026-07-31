//! Metal port shape: process + console session, Metal FS store (later).
//!
//! v1 stubs return Ready(Err(Unsupported)) / empty. Not linked into EFI yet.
//! When wired: command -> fn_process, Session lines -> attached console;
//! Store ops may return Pending and park on the async runner.

use alloc::string::String;
use alloc::vec::Vec;

use super::{
    ready_err, ForgeError, ForgePoll, ForgeResult, ForgeSession, ForgeStore,
};

/// Placeholder store until Metal FS backs module trees in-binary.
pub struct MetalStore;

impl MetalStore {
    pub fn new() -> Self {
        Self
    }
}

impl ForgeStore for MetalStore {
    fn read_file(&mut self, _path: &str) -> ForgePoll<ForgeResult<Vec<u8>>> {
        ready_err(ForgeError::Unsupported)
    }

    fn write_file(&mut self, _path: &str, _data: &[u8]) -> ForgePoll<ForgeResult<()>> {
        ready_err(ForgeError::Unsupported)
    }

    fn remove_file(&mut self, _path: &str) -> ForgePoll<ForgeResult<()>> {
        ready_err(ForgeError::Unsupported)
    }

    fn create_dir_all(&mut self, _path: &str) -> ForgePoll<ForgeResult<()>> {
        ready_err(ForgeError::Unsupported)
    }

    fn exists(&mut self, _path: &str) -> ForgePoll<bool> {
        ForgePoll::Ready(false)
    }

    fn is_dir(&mut self, _path: &str) -> ForgePoll<bool> {
        ForgePoll::Ready(false)
    }

    fn is_file(&mut self, _path: &str) -> ForgePoll<bool> {
        ForgePoll::Ready(false)
    }

    fn list_dir(&mut self, _path: &str) -> ForgePoll<ForgeResult<Vec<String>>> {
        ready_err(ForgeError::Unsupported)
    }
}

/// Process-intention session: later writes to the process console viewport.
pub struct MetalSession {
    exit: i32,
}

impl MetalSession {
    pub fn new() -> Self {
        Self { exit: 0 }
    }
}

impl ForgeSession for MetalSession {
    fn arg_count(&self) -> usize {
        0
    }

    fn arg(&self, _i: usize) -> Option<&str> {
        None
    }

    fn out_line(&mut self, _s: &str) -> ForgePoll<ForgeResult<()>> {
        // Later: pm_metal_console_write on the process-attached console.
        ready_err(ForgeError::Unsupported)
    }

    fn err_line(&mut self, _s: &str) -> ForgePoll<ForgeResult<()>> {
        ready_err(ForgeError::Unsupported)
    }

    fn set_exit(&mut self, code: i32) {
        self.exit = code;
    }

    fn exit_code(&self) -> i32 {
        self.exit
    }
}
