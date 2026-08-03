//! reader — memory-only source reader (upstream `py/reader.h`, mem variant).
//!
//! Metal has no POSIX file source: firmware sources live in ROM/heap buffers
//! only, so this mirrors upstream's `mp_reader_new_mem` path and omits the
//! `MICROPY_READER_POSIX` / `MICROPY_READER_VFS` file readers entirely.

use crate::upy::py::malloc;

/// Sentinel returned by `readbyte` at end of stream (upstream `MP_READER_EOF`).
pub const READER_EOF: u32 = u32::MAX;

/// Byte-stream reader over an in-memory buffer.
///
/// Three ownership modes, matching upstream's `free_len` argument:
/// - `new_mem`: borrows a `&'static` buffer that outlives the reader (e.g.
///   a source baked into firmware); never freed.
/// - `borrow`: borrows a buffer of any lifetime the caller vouches for
///   (mirrors upstream's other `free_len == 0` callers, e.g. `repl.rs`
///   feeding a REPL line buffer that is emphatically not `'static`); never
///   freed. `new_mem` is defined in terms of this.
/// - `new_mem_owned`: takes ownership of a Metal-heap buffer, freed on
///   `close`/`Drop` (mirrors `free_len == sizeof(buf)`).
pub struct Reader {
    buf: *const u8,
    pos: usize,
    len: usize,
    owned: bool,
}

impl Reader {
    /// Borrow `len` bytes starting at `buf` for exactly as long as this
    /// `Reader` (and anything built on it, e.g. a `Lexer`/parse tree) is
    /// in use. Never frees `buf`.
    ///
    /// # Safety
    /// `buf` must be valid for reads of `len` bytes for the entire
    /// lifetime of the returned `Reader` (and any value borrowing from
    /// it) -- the caller, not the type system, is vouching for this.
    pub unsafe fn borrow(buf: *const u8, len: usize) -> Self {
        Reader {
            buf,
            pos: 0,
            len,
            owned: false,
        }
    }

    pub fn new_mem(buf: &'static [u8]) -> Self {
        unsafe { Self::borrow(buf.as_ptr(), buf.len()) }
    }

    /// Takes ownership of a Metal-heap buffer (`pm_metal_mem_alloc`); freed
    /// on `close`. Caller must not use `buf` after this call.
    pub unsafe fn new_mem_owned(buf: *mut u8, len: usize) -> Self {
        Reader {
            buf,
            pos: 0,
            len,
            owned: true,
        }
    }

    /// Next byte in the stream, or `READER_EOF`. Safe to call again after EOF.
    pub fn readbyte(&mut self) -> u32 {
        if self.pos < self.len {
            let b = unsafe { *self.buf.add(self.pos) };
            self.pos += 1;
            b as u32
        } else {
            READER_EOF
        }
    }

    pub fn close(&mut self) {
        if self.owned && !self.buf.is_null() {
            unsafe { malloc::m_free(self.buf as *mut u8) };
            self.buf = core::ptr::null();
            self.len = 0;
            self.owned = false;
        }
    }
}

impl Drop for Reader {
    fn drop(&mut self) {
        self.close();
    }
}
