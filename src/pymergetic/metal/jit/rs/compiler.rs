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
// its `unsafe fn` bodies used to carry inner `unsafe {}` wrappers around
// every op, nested inside enclosing blocks — all redundant now that the
// crate root allows the ops directly (metal.rs `#![allow(unsafe_op_in_unsafe_fn)]`).
// Those redundant wrappers are unwrapped, so the raw ops sit directly in the
// `unsafe fn` body and `unused_unsafe` is kept live to keep any new redundant
// wrapper from creeping back in.
#![warn(unused_unsafe)]
// Same posture for the subset's C-idiom residue: the card is written in the
// exact Rust subset it compiles (self-host), so it leans on C-style
// initialize-then-fill locals, scratch bindings, and API shims for faces the
// subset does not exercise from inside this TU (their consumers are other
// cards / the C side, invisible to rustc's per-TU dead-code pass).
// Dead stores are physically cleaned: a `let mut x: T;` whose value is
// always overwritten before any read drops its initializer (the subset's
// deferred-init form) and a wholly unread scratch flag drops the variable.
// `unused_assignments` and `unused_unsafe` are kept live so no new dead
// store or redundant wrapper creeps back in. The remaining scoped allows
// are the true C-idiom residue — scratch bindings the subset needs to name
// in one C scope (`unused_variables`), per-fill `mut` on deferred-init
// locals (`unused_mut`), and cross-TU API shims (`dead_code`) — each inert
// to the generated C and to the selfhost cycle. Scoped to this card only —
// the rest of the crate keeps every one of these lints live.
#![warn(dead_code)]
#![allow(unused_variables)]
#![warn(unused_assignments)]
#![allow(unused_mut)]
#![allow(unused_parens)]

include!(concat!(env!("OUT_DIR"), "/rsx_compiler_flat.rs"));
