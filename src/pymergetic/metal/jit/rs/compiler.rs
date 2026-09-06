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

include!(concat!(env!("OUT_DIR"), "/rsx_compiler_flat.rs"));
