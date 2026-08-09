//! Glue seats for floor RegMods — import/smoke view (not a parallel SoT).

const SEAT_GLUE: i32 = 0;

type SeatTestFn = Option<unsafe extern "C" fn() -> i32>;

extern "C" {
    fn pm_metal_reg_seat_register(
        path: *const u8,
        kind: i32,
        fw: u8,
        browser: u8,
        test: SeatTestFn,
    ) -> i32;
    fn pm_metal_net_ssh_seat_test() -> i32;
    fn pm_metal_process_seat_test() -> i32;
}

fn seat(path: &'static [u8], test: SeatTestFn) -> i32 {
    debug_assert!(path.last() == Some(&0));
    unsafe { pm_metal_reg_seat_register(path.as_ptr(), SEAT_GLUE, 1, 1, test) }
}

/// Register µPy glue seats for permanently-linked RegMods.
pub fn load_all() -> i32 {
    let mut rc = 0i32;
    rc |= seat(b"pymergetic.metal.async\0", None);
    rc |= seat(b"pymergetic.metal.auth\0", None);
    rc |= seat(b"pymergetic.metal.boot\0", None);
    rc |= seat(b"pymergetic.metal.boot.tree\0", None);
    rc |= seat(b"pymergetic.metal.bus.pci\0", None);
    rc |= seat(b"pymergetic.metal.bus.virtio\0", None);
    rc |= seat(b"pymergetic.metal.console\0", None);
    rc |= seat(b"pymergetic.metal.draw\0", None);
    rc |= seat(b"pymergetic.metal.fs\0", None);
    rc |= seat(b"pymergetic.metal.fs.embed\0", None);
    rc |= seat(b"pymergetic.metal.fs.fat\0", None);
    rc |= seat(b"pymergetic.metal.fs.littlefs\0", None);
    rc |= seat(b"pymergetic.metal.fs.mtar\0", None);
    rc |= seat(b"pymergetic.metal.fs.overlay\0", None);
    rc |= seat(b"pymergetic.metal.fs.tmpfs\0", None);
    rc |= seat(b"pymergetic.metal.fs.vfs\0", None);
    rc |= seat(b"pymergetic.metal.fs.wasmmod\0", None);
    rc |= seat(b"pymergetic.metal.fs.zip\0", None);
    rc |= seat(b"pymergetic.metal.hwtree\0", None);
    rc |= seat(b"pymergetic.metal.mem.arena\0", None);
    rc |= seat(b"pymergetic.metal.mem.lock\0", None);
    rc |= seat(b"pymergetic.metal.mem.port\0", None);
    rc |= seat(b"pymergetic.metal.mem.tlsf\0", None);
    rc |= seat(b"pymergetic.metal.net.asgi\0", None);
    rc |= seat(b"pymergetic.metal.net.dhcp\0", None);
    rc |= seat(b"pymergetic.metal.net.dns\0", None);
    rc |= seat(b"pymergetic.metal.net.faces\0", None);
    rc |= seat(b"pymergetic.metal.net.http\0", None);
    rc |= seat(b"pymergetic.metal.net.ip\0", None);
    rc |= seat(b"pymergetic.metal.net.nic\0", None);
    rc |= seat(b"pymergetic.metal.net.ntp\0", None);
    rc |= seat(b"pymergetic.metal.net.pump\0", None);
    rc |= seat(b"pymergetic.metal.net.ssh\0", Some(pm_metal_net_ssh_seat_test));
    rc |= seat(b"pymergetic.metal.net.tftp\0", None);
    rc |= seat(b"pymergetic.metal.net.tls\0", None);
    rc |= seat(b"pymergetic.metal.net.wg\0", None);
    rc |= seat(b"pymergetic.metal.pack\0", None);
    rc |= seat(b"pymergetic.metal.process\0", Some(pm_metal_process_seat_test));
    rc |= seat(b"pymergetic.metal.rt\0", None);
    rc |= seat(b"pymergetic.metal.shell.tui\0", None);
    rc |= seat(b"pymergetic.metal.shell.ui\0", None);
    rc |= seat(b"pymergetic.metal.shell.vt\0", None);
    rc |= seat(b"pymergetic.metal.trust\0", None);
    rc |= seat(b"pymergetic.metal.util.ascii\0", None);
    rc |= seat(b"pymergetic.metal.util.eightcc\0", None);
    rc |= seat(b"pymergetic.metal.util.endian\0", None);
    rc |= seat(b"pymergetic.metal.util.fourcc\0", None);
    rc |= seat(b"pymergetic.metal.util.lz4\0", None);
    rc |= seat(b"pymergetic.metal.util.size\0", None);
    rc |= seat(b"pymergetic.metal.util.tar\0", None);
    rc |= seat(b"pymergetic.metal.wamr_host\0", None);
    rc |= seat(b"pymergetic.metal.dev.acpi\0", None);
    rc |= seat(b"pymergetic.metal.dev.blk\0", None);
    rc |= seat(b"pymergetic.metal.dev.gfx.compositor\0", None);
    rc |= seat(b"pymergetic.metal.dev.gfx.scanout\0", None);
    rc |= seat(b"pymergetic.metal.dev.gfx.text\0", None);
    rc |= seat(b"pymergetic.metal.dev.input.kbd\0", None);
    rc |= seat(b"pymergetic.metal.dev.net.bge\0", None);
    rc |= seat(b"pymergetic.metal.dev.net.virtio_net\0", None);
    rc |= seat(b"pymergetic.metal.dev.serial\0", None);
    rc |= seat(b"pymergetic.metal.dev.stream\0", None);
    rc |= seat(b"pymergetic.metal.externals\0", None);
    rc
}
