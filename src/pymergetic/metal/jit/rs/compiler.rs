//! pymergetic.metal.jit.rs.compiler — cargo face of the micro-rustc card.
//!
//! The card's muscle is authored as eleven fragment files under
//! compiler/parts/ — spans of ONE translation unit, not modules (impl
//! blocks straddle part boundaries by design). The flat TU those spans
//! form is assembled at build time (build.rs -> OUT_DIR, from the same
//! ORDER tools/rsx_assemble.py uses) and included here; the committed
//! tree carries no generated monolith.
//!
//! The kernel's own consumers (ksweep, the self-host cycle, the build
//! card's in-kernel rsx compile) never see this file: they splice the
//! card's #[path] shim (`compiler/__impl__.rs`) into the same bytes
//! (proven identical). One card, one TU, two compilers, zero copies of
//! the flat form in the tree.

#![allow(clippy::missing_safety_doc)]
#![allow(non_camel_case_types)]
// The micro-rustc card predates edition 2024's unsafe-op-in-unsafe-fn rule:
// its `unsafe fn` bodies carry inner `unsafe {}` blocks around every op,
// nested inside enclosing blocks — 579 of them, all redundant now that the
// crate root allows the ops directly. Unwrapping them would churn 579 sites
// of a self-hosting card whose flat-TU bytes must stay byte-identical to the
// parts (build.rs --check). The nesting is style, not safety information;
// scoped here so the rest of the crate keeps the lint live.
#![allow(unused_unsafe)]
// Same posture for the subset's C-idiom residue: the card is written in the
// exact Rust subset it compiles (self-host), so it leans on C-style
// initialize-then-fill locals, scratch bindings, and API shims for faces the
// subset does not exercise from inside this TU (their consumers are other
// cards / the C side, invisible to rustc's per-TU dead-code pass). Dead
// stores, unused scratch vars, unused mut, and shim dead_code are inert to
// the generated C and to the selfhost cycle; fixing them piecemeal churns
// the byte-checked parts for no behavior change. Scoped to this card only —
// the rest of the crate keeps every one of these lints live.
#![allow(dead_code)]
#![allow(unused_variables)]
#![allow(unused_assignments)]
#![allow(unused_mut)]
#![allow(unused_parens)]

include!(concat!(env!("OUT_DIR"), "/rsx_compiler_flat.rs"));
