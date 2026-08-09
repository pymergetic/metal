/*
 * PORT_INIT runs inside mp_init() — too early for frozen imports / banner,
 * and sys is a fixed ROM module so sys.metal_unix cannot be stamped here.
 *
 * Seat selection: platform.machine() in pymergetic.metal.unix.current().
 * Banner / autoexec: micropython -m pymergetic.metal.unix
 *
 * CFG face is still visible to C via PM_METAL_CFG_FW_UNIX / ARCH_* (arch.c).
 */
void metal_unix_port_init(void) {
}
