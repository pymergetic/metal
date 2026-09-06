//! Build-time assembly of the micro-rustc card's flat TU.
//!
//! The rsx compiler card is authored as eleven fragment files under
//! src/pymergetic/metal/jit/rs/compiler/parts/ — one TU's worth of
//! spans, not eleven modules (impl blocks straddle part boundaries by
//! design; cargo's real modules cannot compile them). Every consumer
//! of the flat form gets the same bytes:
//!
//!   - the kernel's rsx + build-card splice assembles it in-kernel from
//!     the card's #[path] shim (proven byte-identical; ksweep green);
//!   - cargo compiles the flat TU produced HERE, into OUT_DIR, never
//!     committed to the tree;
//!   - tools/selfhost_cycle.sh runs tools/rsx_assemble.py for the same
//!     bytes at prove time.
//!
//! The committed tree carries no generated monolith: the parts are the
//! card, this script is the only assembler, and rsx_assemble --check
//! guards that the OUT_DIR bytes match the parts on every build.

use std::env;
use std::fs;
use std::path::PathBuf;
use std::process::Command;

fn main() {
    let manifest = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let out_dir = PathBuf::from(env::var("OUT_DIR").unwrap());
    let card = manifest.join("src/pymergetic/metal/jit/rs/compiler");
    let parts = card.join("parts");
    let flat = out_dir.join("rsx_compiler_flat.rs");

    // Deterministic: fixed order, byte-for-byte, no reformatting. Same
    // contract as tools/rsx_assemble.py — keep the two spellings in
    // lockstep (the tool is the reference; this is the cargo port).
    let order = [
        "head.rs",
        "lexer.rs",
        "parser.rs",
        "tables.rs",
        "lower_core.rs",
        "lower_stmt.rs",
        "lower_expr.rs",
        "lower_item.rs",
        "lower_fn.rs",
        "ast_dump.rs",
        "root.rs",
    ];

    let mut buf: Vec<u8> = Vec::with_capacity(1 << 21);
    for name in order {
        let data = fs::read(parts.join(name)).unwrap_or_else(|e| {
            panic!("rsx card part {name} unreadable: {e}");
        });
        if data.contains(&0u8) {
            panic!("rsx card part {name} contains a NUL byte");
        }
        if data.last() != Some(&b'\n') {
            panic!("rsx card part {name} does not end with a newline");
        }
        buf.extend_from_slice(&data);
    }
    fs::write(&flat, &buf).unwrap();

    // Re-run when any part changes (cargo can't see through our write).
    println!("cargo:rerun-if-changed={}", parts.display());
    println!("cargo:rerun-if-changed={}", card.join("__impl__.rs").display());

    // Cross-check against the reference assembler, best-effort: in a
    // checkout without python3 the flat bytes still stand alone.
    let status = Command::new("python3")
        .arg(manifest.join("tools/rsx_assemble.py"))
        .arg("--check-flat")
        .arg(&flat)
        .status();
    if let Ok(s) = status {
        if !s.success() {
            panic!("rsx parts and the reference assembly diverge");
        }
    }}
