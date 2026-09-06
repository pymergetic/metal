//! pymergetic.metal.jit.rs.compiler — the micro-rustc card root (rsx face).
//!
//! One card, one TU, eleven authored faces under parts/. This root is
//! what the kernel's rsx consumes: the build card's splice appends each
//! face's bytes after this file, in declaration order (the sibling-file
//! mod decl, util.gen's own card shape) — the same flat TU
//! tools/rsx_assemble.py and build.rs assemble for cargo (byte-identical,
//! proven by ksweep + the self-host fixed point).
//!
//! cargo never compiles this file (its face is compiler.rs, which
//! includes the build-time assembly); the mod items are splice markers,
//! not modules — the parts are spans of one translation unit and compile
//! only concatenated.

mod head;
mod lexer;
mod parser;
mod tables;
mod lower_core;
mod lower_stmt;
mod lower_expr;
mod lower_item;
mod lower_fn;
mod ast_dump;
mod root;
