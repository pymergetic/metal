//! Dumb Linux port: std::fs + stdout/stderr. Always Ready.

use alloc::string::String;
use alloc::vec::Vec;
use std::io::Write as _;
use std::path::Path;

use super::{
    ready_err, ready_ok, ForgeError, ForgePoll, ForgeResult, ForgeSession, ForgeStore,
};

pub struct SoloStore;

impl SoloStore {
    pub fn new() -> Self {
        Self
    }
}

impl ForgeStore for SoloStore {
    fn read_file(&mut self, path: &str) -> ForgePoll<ForgeResult<Vec<u8>>> {
        match std::fs::read(path) {
            Ok(b) => ready_ok(b),
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => ready_err(ForgeError::NotFound),
            Err(_) => ready_err(ForgeError::Io),
        }
    }

    fn write_file(&mut self, path: &str, data: &[u8]) -> ForgePoll<ForgeResult<()>> {
        if let Some(parent) = Path::new(path).parent() {
            if !parent.as_os_str().is_empty() {
                if let Err(_) = std::fs::create_dir_all(parent) {
                    return ready_err(ForgeError::Io);
                }
            }
        }
        match std::fs::write(path, data) {
            Ok(()) => ready_ok(()),
            Err(_) => ready_err(ForgeError::Io),
        }
    }

    fn remove_file(&mut self, path: &str) -> ForgePoll<ForgeResult<()>> {
        match std::fs::remove_file(path) {
            Ok(()) => ready_ok(()),
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => ready_ok(()),
            Err(_) => ready_err(ForgeError::Io),
        }
    }

    fn create_dir_all(&mut self, path: &str) -> ForgePoll<ForgeResult<()>> {
        match std::fs::create_dir_all(path) {
            Ok(()) => ready_ok(()),
            Err(_) => ready_err(ForgeError::Io),
        }
    }

    fn exists(&mut self, path: &str) -> ForgePoll<bool> {
        ForgePoll::Ready(Path::new(path).exists())
    }

    fn is_dir(&mut self, path: &str) -> ForgePoll<bool> {
        ForgePoll::Ready(Path::new(path).is_dir())
    }

    fn is_file(&mut self, path: &str) -> ForgePoll<bool> {
        ForgePoll::Ready(Path::new(path).is_file())
    }

    fn list_dir(&mut self, path: &str) -> ForgePoll<ForgeResult<Vec<String>>> {
        let rd = match std::fs::read_dir(path) {
            Ok(rd) => rd,
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => {
                return ready_err(ForgeError::NotFound);
            }
            Err(_) => return ready_err(ForgeError::Io),
        };
        let mut out = Vec::new();
        for ent in rd {
            match ent {
                Ok(e) => {
                    if let Some(name) = e.file_name().to_str() {
                        out.push(String::from(name));
                    }
                }
                Err(_) => return ready_err(ForgeError::Io),
            }
        }
        ready_ok(out)
    }
}

pub struct SoloSession {
    args: Vec<String>,
    exit: i32,
}

impl SoloSession {
    pub fn from_env() -> Self {
        let args: Vec<String> = std::env::args().skip(1).collect();
        Self { args, exit: 0 }
    }

    pub fn from_args(args: Vec<String>) -> Self {
        Self { args, exit: 0 }
    }
}

impl ForgeSession for SoloSession {
    fn arg_count(&self) -> usize {
        self.args.len()
    }

    fn arg(&self, i: usize) -> Option<&str> {
        self.args.get(i).map(|s| s.as_str())
    }

    fn out_line(&mut self, s: &str) -> ForgePoll<ForgeResult<()>> {
        let mut out = std::io::stdout().lock();
        match writeln!(out, "{}", s) {
            Ok(()) => ready_ok(()),
            Err(_) => ready_err(ForgeError::Io),
        }
    }

    fn err_line(&mut self, s: &str) -> ForgePoll<ForgeResult<()>> {
        let mut err = std::io::stderr().lock();
        match writeln!(err, "{}", s) {
            Ok(()) => ready_ok(()),
            Err(_) => ready_err(ForgeError::Io),
        }
    }

    fn set_exit(&mut self, code: i32) {
        self.exit = code;
    }

    fn exit_code(&self) -> i32 {
        self.exit
    }
}
