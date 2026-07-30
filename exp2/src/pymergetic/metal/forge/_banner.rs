//! Ownership banner + write-gate helpers (policy on top of [`crate::_comment`]).
//!
//! The ownership phrase is split so this source file is never mistaken for a
//! generated face by the banner scan.
//!
//! `Source-sha:` is an FNV-1a 64 hex of the human source bytes (see `_hash`).
//! Sync/convert skip a face when that fingerprint still matches.

use alloc::string::String;
use alloc::vec::Vec;

use crate::_comment::comment_block_slot;

/// Must match Python `metal mod sync` write gate.
pub fn generated_hint() -> &'static str {
    // Split so `_banner.rs` itself does not contain the contiguous phrase.
    concat!("DO NOT HAND-EDIT", " THIS FILE.")
}

/// Prefix of the source-fingerprint banner line (value is 16 hex chars).
pub const SOURCE_SHA_KEY: &str = "Source-sha:";

/// Standard generated-face ownership banner for pool slot `style` (`c`/`rs`/`py`).
pub fn generated_banner(
    style: &str,
    human: &str,
    this_file: &str,
    source_sha: &str,
) -> Vec<String> {
    let hint = generated_hint();
    let this_line = alloc::format!("This file is:  {}", this_file);
    let edit_line = alloc::format!("Edit instead:  {}", human);
    let sha_line = alloc::format!("{} {}", SOURCE_SHA_KEY, source_sha);
    let body: [&str; 7] = [
        "GENERATED",
        hint,
        this_line.as_str(),
        edit_line.as_str(),
        sha_line.as_str(),
        "Regenerate:    metal mod sync",
        "Owned by:      metal mod sync (banner = write gate)",
    ];
    comment_block_slot(style, &body)
}

pub fn content_has_banner(text: &str) -> bool {
    let hint = generated_hint();
    text.get(..4096.min(text.len()))
        .map(|h| h.contains(hint))
        .unwrap_or(false)
}

/// Parse `Source-sha: <hex>` from the ownership header (first 4 KiB).
pub fn parse_source_sha(text: &str) -> Option<String> {
    let head = text.get(..4096.min(text.len()))?;
    for line in head.lines() {
        let t = line
            .trim_start_matches(|c: char| {
                c == '/' || c == '*' || c == '#' || c == '!' || c.is_whitespace()
            })
            .trim();
        if let Some(rest) = t.strip_prefix(SOURCE_SHA_KEY) {
            let v = rest.trim();
            if !v.is_empty() {
                return Some(String::from(v));
            }
        }
    }
    None
}

/// True when face text is banner-owned and its Source-sha matches `want`.
pub fn face_matches_source_sha(text: &str, want: &str) -> bool {
    if !content_has_banner(text) {
        return false;
    }
    match parse_source_sha(text) {
        Some(got) => got == want,
        None => false,
    }
}
