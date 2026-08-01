//! Libc policy for the py edge: Metal libc by default (not upy shared libc).

#[repr(u32)]
#[derive(Clone, Copy, PartialEq, Eq)]
pub enum LibcPolicy {
    /// Use Metal freestanding libc (`src/.../libc`, snprintf, …).
    Metal = 1,
}

pub const DEFAULT: LibcPolicy = LibcPolicy::Metal;

pub fn current() -> LibcPolicy {
    DEFAULT
}
