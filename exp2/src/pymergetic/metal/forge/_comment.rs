//! Generic comment-block formatting (C / Rust doc / Python / plain).
//!
//! Reusable for banners, file headers, or any multi-line note that must
//! render in a language's comment syntax. No forge/ownership policy here.

use alloc::string::String;
use alloc::vec::Vec;

/// How to wrap body lines as comments.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CommentStyle {
    /// `/*` … ` * line` … ` */`
    CBlock,
    /// `//! line` (Rust inner-doc / file header style)
    RsDoc,
    /// `# line`
    Hash,
    /// No prefix; body lines as-is
    Plain,
}

impl CommentStyle {
    /// Map pool / emit slot names (`"c"`, `"rs"`, `"py"`, …).
    pub fn from_slot(slot: &str) -> Self {
        match slot {
            "c" | "h" | "cpp" | "hpp" => CommentStyle::CBlock,
            "rs" => CommentStyle::RsDoc,
            "py" | "pyi" | "toml" | "sh" => CommentStyle::Hash,
            _ => CommentStyle::Plain,
        }
    }
}

/// Wrap `body` lines in `style`. Body lines should be plain text (no
/// comment markers). Empty body still emits open/close for block styles.
pub fn comment_block(style: CommentStyle, body: &[&str]) -> Vec<String> {
    let mut out = Vec::new();
    match style {
        CommentStyle::CBlock => {
            out.push(String::from("/*"));
            for line in body {
                if line.is_empty() {
                    out.push(String::from(" *"));
                } else {
                    out.push(alloc::format!(" * {}", line));
                }
            }
            out.push(String::from(" */"));
        }
        CommentStyle::RsDoc => {
            for line in body {
                if line.is_empty() {
                    out.push(String::from("//!"));
                } else {
                    out.push(alloc::format!("//! {}", line));
                }
            }
        }
        CommentStyle::Hash => {
            for line in body {
                if line.is_empty() {
                    out.push(String::from("#"));
                } else {
                    out.push(alloc::format!("# {}", line));
                }
            }
        }
        CommentStyle::Plain => {
            for line in body {
                out.push(String::from(*line));
            }
        }
    }
    out
}

/// Convenience: `comment_block(CommentStyle::from_slot(slot), body)`.
pub fn comment_block_slot(slot: &str, body: &[&str]) -> Vec<String> {
    comment_block(CommentStyle::from_slot(slot), body)
}
