//! In-memory module catalog (fns / structs / enums / typedefs / defines).

use alloc::string::String;
use alloc::vec::Vec;

#[derive(Clone, Debug)]
pub struct Field {
    pub name: String,
    pub ty: String,
}

#[derive(Clone, Debug)]
pub struct Struct {
    pub name: String,
    pub fields: Vec<Field>,
    pub is_union: bool,
}

#[derive(Clone, Debug)]
pub struct Typedef {
    pub name: String,
    pub ty: String,
}

/// Object-like `#define NAME value` (C face). `value` is the raw RHS text.
#[derive(Clone, Debug)]
pub struct Define {
    pub name: String,
    pub value: String,
}

#[derive(Clone, Debug)]
pub struct EnumVariant {
    pub name: String,
    pub value: i64,
}

#[derive(Clone, Debug)]
pub struct EnumDef {
    pub name: String,
    pub variants: Vec<EnumVariant>,
}

#[derive(Clone, Debug)]
pub struct Arg {
    pub name: String,
    pub ty: String,
}

#[derive(Clone, Debug)]
pub struct Fn {
    pub name: String,
    pub ret: String,
    pub args: Vec<Arg>,
    pub inline: bool,
}

#[derive(Clone, Debug, Default)]
pub struct Catalog {
    pub structs: Vec<Struct>,
    pub typedefs: Vec<Typedef>,
    pub enums: Vec<EnumDef>,
    pub defines: Vec<Define>,
    /// Sibling C faces to `#include` for types owned elsewhere in the module
    /// (e.g. `async/await.h` needs `async/handle.h` for `pm_metal_async_status_t`).
    /// Paths are package-relative inside angle brackets (`pymergetic/metal/...`).
    pub includes: Vec<String>,
    /// Type names satisfied by [`includes`] (suppresses opaque foreign `struct` decls).
    pub sibling_types: Vec<String>,
    pub fns: Vec<Fn>,
}

impl Catalog {
    pub fn is_empty(&self) -> bool {
        self.structs.is_empty()
            && self.typedefs.is_empty()
            && self.enums.is_empty()
            && self.defines.is_empty()
            && self.fns.is_empty()
    }

    pub fn has_border(&self) -> bool {
        !self.fns.is_empty()
    }
}
