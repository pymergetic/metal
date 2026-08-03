//! Metal mpconfig — fixed policy for the Rust upy mirror (x86_64, GC off).
//!
//! Upstream `mpconfig.h` is a huge option matrix; Metal pins the subset the
//! mirror needs. Port options live in `py/port/mpconfigport.h` for any C edge.

/// Object representation A: tagged `usize` (small-int / qstr / ptr).
pub const OBJ_REPR_A: u8 = 1;
pub const OBJ_REPR: u8 = OBJ_REPR_A;

/// GC disabled — Metal heap only (Locked #3/#5).
pub const ENABLE_GC: bool = false;
pub const ENABLE_FINALISER: bool = false;

/// Qstr hash / length field sizes (match common upy embed defaults).
pub const QSTR_BYTES_IN_HASH: usize = 2;
pub const QSTR_BYTES_IN_LEN: usize = 1;

/// No upy threads / GIL / percpu state (Locked #4).
pub const PY_THREAD: bool = false;

/// Source -> bytecode compiling is real as of W11.1 (`compile.rs` +
/// `emit.rs`/`emitbc.rs`/`emitglue.rs`/`emitcommon.rs`): a genuine, if
/// partial, subset (see `compile.rs` module doc for exactly what
/// `compile_eval`/`compile_file`/`compile_funcdef_body` support and what
/// they honestly reject as `CompileError::Unsupported`).
pub const DYNAMIC_COMPILER: bool = true;

/// Architecture tag for later native emit (x86_64 only).
pub const NATIVE_ARCH_X64: u8 = 1;
pub const NATIVE_ARCH: u8 = NATIVE_ARCH_X64;
