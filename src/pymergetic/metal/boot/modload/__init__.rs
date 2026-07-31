//! Wasm / pack mod loader — mount and umount `/mods/<id>` + `/src/<id>`.

extern "C" {
    fn pm_metal_boot_rootfs_mount_module(
        id: *const u8,
        mods: *const u8,
        mods_len: usize,
        src: *const u8,
        src_len: usize,
    ) -> i32;
    fn pm_metal_vfs_umount(target: *const u8) -> i32;
}

fn module_mount_path(out: &mut [u8], kind: &[u8], id: &[u8]) -> usize {
    if kind.len() + id.len() + 1 >= out.len() {
        return 0;
    }
    out[..kind.len()].copy_from_slice(kind);
    out[kind.len()..kind.len() + id.len()].copy_from_slice(id);
    let n = kind.len() + id.len();
    out[n] = 0;
    n
}

fn id_bytes(id: *const u8) -> ([u8; 96], usize) {
    let mut id_buf = [0u8; 96];
    let mut i = 0usize;
    if id.is_null() {
        return (id_buf, 0);
    }
    unsafe {
        while i + 1 < id_buf.len() {
            let c = *id.add(i);
            if c == 0 {
                break;
            }
            id_buf[i] = c;
            i += 1;
        }
    }
    (id_buf, i)
}

/// Mount module packs at `/mods/<id>` and optional `/src/<id>`.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_boot_mod_load(
    id: *const u8,
    mods_blob: *const u8,
    mods_len: usize,
    src_blob: *const u8,
    src_len: usize,
) -> i32 {
    pm_metal_boot_rootfs_mount_module(id, mods_blob, mods_len, src_blob, src_len)
}

/// Umount `/mods/<id>` and `/src/<id>` (missing mounts ignored).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_boot_mod_unload(id: *const u8) -> i32 {
    let (id_buf, id_len) = id_bytes(id);
    if id_len == 0 {
        return -1;
    }
    let id_slice = &id_buf[..id_len];
    let mut path = [0u8; 128];

    let plen = module_mount_path(&mut path, b"/mods/", id_slice);
    if plen == 0 {
        return -1;
    }
    let _ = pm_metal_vfs_umount(path.as_ptr());
    let plen = module_mount_path(&mut path, b"/src/", id_slice);
    if plen == 0 {
        return -1;
    }
    let _ = pm_metal_vfs_umount(path.as_ptr());
    0
}
