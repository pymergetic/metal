//! Port surface: Store (module tree bytes) + Session (how a run is hosted).
//!
//! Ops return [`ForgePoll`] so metal can park later; solo is always Ready.

#![allow(dead_code)]

use alloc::string::String;
use alloc::vec::Vec;

#[cfg(feature = "metal")]
pub mod metal;
#[cfg(feature = "solo")]
pub mod solo;

/// Ready or must park / retry (solo never returns Pending).
#[derive(Clone, Debug)]
pub enum ForgePoll<T> {
    Ready(T),
    Pending,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ForgeError {
    NotFound,
    Io,
    Permission,
    BadMeta,
    Unsupported,
    PendingBug,
    Other,
}

pub type ForgeResult<T> = Result<T, ForgeError>;

/// Module-tree byte store (checkout FS outside; Metal FS inside later).
pub trait ForgeStore {
    fn read_file(&mut self, path: &str) -> ForgePoll<ForgeResult<Vec<u8>>>;
    fn write_file(&mut self, path: &str, data: &[u8]) -> ForgePoll<ForgeResult<()>>;
    fn remove_file(&mut self, path: &str) -> ForgePoll<ForgeResult<()>>;
    fn create_dir_all(&mut self, path: &str) -> ForgePoll<ForgeResult<()>>;
    fn exists(&mut self, path: &str) -> ForgePoll<bool>;
    fn is_dir(&mut self, path: &str) -> ForgePoll<bool>;
    fn is_file(&mut self, path: &str) -> ForgePoll<bool>;
    /// Immediate children names (files + dirs), unsorted ok (caller sorts).
    fn list_dir(&mut self, path: &str) -> ForgePoll<ForgeResult<Vec<String>>>;
}

/// How a forge run is hosted (stdio outside; process+console inside later).
pub trait ForgeSession {
    fn arg_count(&self) -> usize;
    fn arg(&self, i: usize) -> Option<&str>;
    fn out_line(&mut self, s: &str) -> ForgePoll<ForgeResult<()>>;
    fn err_line(&mut self, s: &str) -> ForgePoll<ForgeResult<()>>;
    fn set_exit(&mut self, code: i32);
    fn exit_code(&self) -> i32;
}

/// Run a Ready-or-Pending op to completion. Solo: Pending is a hard bug.
pub fn block_on<T, F>(mut f: F) -> T
where
    F: FnMut() -> ForgePoll<T>,
{
    loop {
        match f() {
            ForgePoll::Ready(v) => return v,
            ForgePoll::Pending => {
                #[cfg(feature = "solo")]
                panic!("forge solo: ForgePoll::Pending (port must be always-Ready)");
                #[cfg(not(feature = "solo"))]
                {
                    // Metal later: caller should park; block_on is host-only.
                    continue;
                }
            }
        }
    }
}

pub fn ready_ok<T>(v: T) -> ForgePoll<ForgeResult<T>> {
    ForgePoll::Ready(Ok(v))
}

pub fn ready_err<T>(e: ForgeError) -> ForgePoll<ForgeResult<T>> {
    ForgePoll::Ready(Err(e))
}
