//! Host smoke — ``metal mod test pymergetic/metal/fs/fat``.
//! Public ABI only (lives under `.pm/`, not main source).

const ATTR_LFN: u8 = 0x0F;

fn main() {
    let mut buf = vec![0u8; 256 * 1024];
    unsafe {
        assert_eq!(
            pymergetic_metal_fs_fat::pm_metal_fs_fat_format_buf(buf.as_mut_ptr(), buf.len()),
            0
        );
        let n1 = b"hello_world.py\0";
        let d1 = b"py\n";
        let n2 = b"long_subdir_name/my_module.py\0";
        let d2 = b"nested\n";
        let names = [n1.as_ptr(), n2.as_ptr()];
        let datas = [d1.as_ptr(), d2.as_ptr()];
        let lens = [d1.len() as u32, d2.len() as u32];
        assert_eq!(
            pymergetic_metal_fs_fat::pm_metal_fs_fat_seed_simple(
                buf.as_mut_ptr(),
                buf.len(),
                names.as_ptr(),
                datas.as_ptr(),
                lens.as_ptr(),
                2,
            ),
            0
        );
        assert!(buf.windows(8).any(|w| w == b"HELL~1  "));
        assert!(buf.iter().any(|&b| b == ATTR_LFN));
    }
    println!("fat .pm/smoke.rs: PASS");
}
