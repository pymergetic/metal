//! Embed a package's declared cross-package imports (`.pm/module`
//! `imports`) directly into its own packed `.wasm` as a custom
//! section, instead of generating a kernel-compiled Rust table (the
//! old `pkg_imports.gen.rs`/`_pkgimports.rs` -- retired). See
//! docs/definitions/module.md "Cross-package imports": the host loader
//! (`runtime_host.c`'s `pm_metal_wasm_port_load`) reads this section
//! straight back out of whatever bytes it is handed at load time, so a
//! package's import list travels with its own binary and needs no
//! prior, build-time knowledge from forge about the rest of the tree --
//! works identically for a package baked in at kernel-build time and
//! one loaded from disk/network after boot.
//!
//! Section layout: a standard wasm custom section (id `0`, ULEB128
//! size, ULEB128 name length + name bytes -- the only parts the wasm
//! spec dictates), holding a private payload only this writer and
//! `runtime_host.c`'s reader agree on: a `u32` little-endian count,
//! then that many `"module\0func\0"` NUL-terminated string pairs.
//! Custom sections are legal anywhere in a module, including after
//! every other section, so this is a pure byte-append -- no need to
//! parse or re-emit the rest of the file.

use alloc::string::String;
use alloc::vec::Vec;
use std::fs;
use std::path::Path;

use crate::_meta::PkgImport;

pub const SECTION_NAME: &str = "pm_metal_imports";

fn write_uleb128(out: &mut Vec<u8>, mut v: u64) {
    loop {
        let byte = (v & 0x7f) as u8;
        v >>= 7;
        if v != 0 {
            out.push(byte | 0x80);
        } else {
            out.push(byte);
            break;
        }
    }
}

fn section_payload(imports: &[PkgImport]) -> Vec<u8> {
    let mut payload = Vec::new();
    write_uleb128(&mut payload, SECTION_NAME.len() as u64);
    payload.extend_from_slice(SECTION_NAME.as_bytes());
    payload.extend_from_slice(&(imports.len() as u32).to_le_bytes());
    for imp in imports {
        payload.extend_from_slice(imp.module.as_bytes());
        payload.push(0);
        payload.extend_from_slice(imp.func.as_bytes());
        payload.push(0);
    }
    payload
}

/// Append the imports section onto `wasm_path` in place. No-op (file
/// untouched) when `imports` is empty -- a package that imports
/// nothing gets no section, not an empty one.
pub fn append(wasm_path: &Path, imports: &[PkgImport]) -> Result<(), String> {
    if imports.is_empty() {
        return Ok(());
    }
    let mut bytes =
        fs::read(wasm_path).map_err(|e| alloc::format!("read {}: {e}", wasm_path.display()))?;
    let payload = section_payload(imports);
    bytes.push(0); // custom section id
    write_uleb128(&mut bytes, payload.len() as u64);
    bytes.extend_from_slice(&payload);
    fs::write(wasm_path, bytes).map_err(|e| alloc::format!("write {}: {e}", wasm_path.display()))
}

#[cfg(test)]
mod tests {
    use super::*;
    use alloc::string::ToString;

    fn imp(module: &str, func: &str) -> PkgImport {
        PkgImport {
            module: module.to_string(),
            func: func.to_string(),
        }
    }

    #[test]
    fn uleb128_encodes_small_and_multi_byte_values() {
        let mut out = Vec::new();
        write_uleb128(&mut out, 5);
        assert_eq!(out, alloc::vec![5]);

        let mut out = Vec::new();
        write_uleb128(&mut out, 300); // 0b1_0010_1100
        assert_eq!(out, alloc::vec![0xac, 0x02]);
    }

    #[test]
    fn payload_carries_name_count_and_nul_terminated_pairs() {
        let imports = alloc::vec![imp("sample.greeter", "hello"), imp("sample.greeter", "lucky")];
        let payload = section_payload(&imports);

        // Name length (17, single ULEB128 byte) + name.
        assert_eq!(payload[0], SECTION_NAME.len() as u8);
        assert_eq!(&payload[1..1 + SECTION_NAME.len()], SECTION_NAME.as_bytes());
        let mut i = 1 + SECTION_NAME.len();

        // u32 LE count.
        assert_eq!(&payload[i..i + 4], &2u32.to_le_bytes());
        i += 4;

        assert_eq!(&payload[i..], b"sample.greeter\0hello\0sample.greeter\0lucky\0");
    }

    #[test]
    fn empty_imports_leave_file_untouched() {
        let dir = std::env::temp_dir().join("pm_metal_wasm_import_section_test");
        let _ = fs::create_dir_all(&dir);
        let path = dir.join("empty.wasm");
        fs::write(&path, b"\0asm\x01\0\0\0").unwrap();
        let before = fs::read(&path).unwrap();
        append(&path, &[]).unwrap();
        let after = fs::read(&path).unwrap();
        assert_eq!(before, after);
        let _ = fs::remove_file(&path);
    }
}
