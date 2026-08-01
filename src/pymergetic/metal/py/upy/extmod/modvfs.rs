//! modvfs — Python-facing re-exports of thin Metal vfs.

pub use crate::upy::extmod::vfs::{
    close, listdir, open, open_obj, read, stat, write, INVALID, O_CREAT, O_DIRECTORY, O_RDONLY,
    O_RDWR, O_TRUNC, O_WRONLY,
};
