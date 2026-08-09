/*
 * Register nested metal faces as dotted builtins so
 * `import pymergetic.metal.<path>` works (path == module).
 *
 * String literals seed the qstr pool with real dots (not `_dot_` text).
 * Firmware seats only — browser root is owned by wasmmod.
 */
#include "py/obj.h"

#include "modules.h"

#if !defined(PM_METAL_CFG_FW_BROWSER) || !PM_METAL_CFG_FW_BROWSER

/* Keep these literals: makeqstr turns "a.b.c" into MP_QSTR_a_dot_b_dot_c. */
static const char *const pm_metal_modname_seeds[] = {
    "pymergetic.metal",
    "pymergetic.metal.async",
    "pymergetic.metal.auth",
    "pymergetic.metal.boot",
    "pymergetic.metal.boot.tree",
    "pymergetic.metal.bus",
    "pymergetic.metal.bus.pci",
    "pymergetic.metal.bus.virtio",
    "pymergetic.metal.console",
    "pymergetic.metal.dev",
    "pymergetic.metal.dev.acpi",
    "pymergetic.metal.dev.blk",
    "pymergetic.metal.dev.gfx",
    "pymergetic.metal.dev.gfx.compositor",
    "pymergetic.metal.dev.gfx.scanout",
    "pymergetic.metal.dev.gfx.text",
    "pymergetic.metal.dev.input",
    "pymergetic.metal.dev.input.kbd",
    "pymergetic.metal.dev.net",
    "pymergetic.metal.dev.net.bge",
    "pymergetic.metal.dev.net.virtio_net",
    "pymergetic.metal.dev.serial",
    "pymergetic.metal.dev.stream",
    "pymergetic.metal.draw",
    "pymergetic.metal.externals",
    "pymergetic.metal.fs",
    "pymergetic.metal.fs.embed",
    "pymergetic.metal.fs.fat",
    "pymergetic.metal.fs.littlefs",
    "pymergetic.metal.fs.mtar",
    "pymergetic.metal.fs.overlay",
    "pymergetic.metal.fs.tmpfs",
    "pymergetic.metal.fs.vfs",
    "pymergetic.metal.fs.wasmmod",
    "pymergetic.metal.fs.zip",
    "pymergetic.metal.hwtree",
    "pymergetic.metal.mem",
    "pymergetic.metal.mem.arena",
    "pymergetic.metal.mem.lock",
    "pymergetic.metal.mem.port",
    "pymergetic.metal.mem.tlsf",
    "pymergetic.metal.net",
    "pymergetic.metal.net.asgi",
    "pymergetic.metal.net.dhcp",
    "pymergetic.metal.net.dns",
    "pymergetic.metal.net.faces",
    "pymergetic.metal.net.http",
    "pymergetic.metal.net.ip",
    "pymergetic.metal.net.nic",
    "pymergetic.metal.net.ntp",
    "pymergetic.metal.net.pump",
    "pymergetic.metal.net.ssh",
    "pymergetic.metal.net.tftp",
    "pymergetic.metal.net.tls",
    "pymergetic.metal.net.wg",
    "pymergetic.metal.pack",
    "pymergetic.metal.process",
    "pymergetic.metal.reg",
    "pymergetic.metal.rt",
    "pymergetic.metal.shell",
    "pymergetic.metal.shell.tui",
    "pymergetic.metal.shell.ui",
    "pymergetic.metal.shell.vt",
    "pymergetic.metal.trust",
    "pymergetic.metal.util",
    "pymergetic.metal.util.ascii",
    "pymergetic.metal.util.eightcc",
    "pymergetic.metal.util.endian",
    "pymergetic.metal.util.fourcc",
    "pymergetic.metal.util.lz4",
    "pymergetic.metal.util.size",
    "pymergetic.metal.util.tar",
    "pymergetic.metal.wamr_host",
};

void pm_metal_modname_seeds_keep(void)
{
    (void)pm_metal_modname_seeds;
}

MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal, mp_module_pymergetic_metal);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_async, mp_module_pymergetic_metal_async);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_auth, mp_module_pymergetic_metal_auth);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_process, mp_module_pymergetic_metal_process);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_boot, mp_module_pymergetic_metal_boot);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_boot_dot_tree, mp_module_pymergetic_metal_boot_tree);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_bus, mp_module_pymergetic_metal_bus);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_bus_dot_pci, mp_module_pymergetic_metal_bus_pci);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_bus_dot_virtio, mp_module_pymergetic_metal_bus_virtio);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_console, mp_module_pymergetic_metal_console);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_dev, mp_module_pymergetic_metal_dev);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_dev_dot_acpi, mp_module_pymergetic_metal_dev_acpi);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_dev_dot_blk, mp_module_pymergetic_metal_dev_blk);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_dev_dot_gfx, mp_module_pymergetic_metal_dev_gfx);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_dev_dot_gfx_dot_compositor, mp_module_pymergetic_metal_dev_gfx_compositor);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_dev_dot_gfx_dot_scanout, mp_module_pymergetic_metal_dev_gfx_scanout);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_dev_dot_gfx_dot_text, mp_module_pymergetic_metal_dev_gfx_text);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_dev_dot_input, mp_module_pymergetic_metal_dev_input);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_dev_dot_input_dot_kbd, mp_module_pymergetic_metal_dev_input_kbd);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_dev_dot_net, mp_module_pymergetic_metal_dev_net);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_dev_dot_net_dot_bge, mp_module_pymergetic_metal_dev_net_bge);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_dev_dot_net_dot_virtio_net, mp_module_pymergetic_metal_dev_net_virtio_net);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_dev_dot_serial, mp_module_pymergetic_metal_dev_serial);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_dev_dot_stream, mp_module_pymergetic_metal_dev_stream);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_draw, mp_module_pymergetic_metal_draw);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_externals, mp_module_pymergetic_metal_externals);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_fs, mp_module_pymergetic_metal_fs);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_fs_dot_embed, mp_module_pymergetic_metal_fs_embed);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_fs_dot_fat, mp_module_pymergetic_metal_fs_fat);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_fs_dot_littlefs, mp_module_pymergetic_metal_fs_littlefs);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_fs_dot_mtar, mp_module_pymergetic_metal_fs_mtar);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_fs_dot_overlay, mp_module_pymergetic_metal_fs_overlay);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_fs_dot_tmpfs, mp_module_pymergetic_metal_fs_tmpfs);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_fs_dot_vfs, mp_module_pymergetic_metal_fs_vfs);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_fs_dot_wasmmod, mp_module_pymergetic_metal_fs_wasmmod);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_fs_dot_zip, mp_module_pymergetic_metal_fs_zip);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_hwtree, mp_module_pymergetic_metal_hwtree);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_mem, mp_module_pymergetic_metal_mem);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_mem_dot_arena, mp_module_pymergetic_metal_mem_arena);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_mem_dot_lock, mp_module_pymergetic_metal_mem_lock);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_mem_dot_port, mp_module_pymergetic_metal_mem_port);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_mem_dot_tlsf, mp_module_pymergetic_metal_mem_tlsf);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_net, mp_module_pymergetic_metal_net);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_net_dot_asgi, mp_module_pymergetic_metal_net_asgi);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_net_dot_dhcp, mp_module_pymergetic_metal_net_dhcp);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_net_dot_dns, mp_module_pymergetic_metal_net_dns);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_net_dot_faces, mp_module_pymergetic_metal_net_faces);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_net_dot_http, mp_module_pymergetic_metal_net_http);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_net_dot_ip, mp_module_pymergetic_metal_net_ip);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_net_dot_nic, mp_module_pymergetic_metal_net_nic);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_net_dot_ntp, mp_module_pymergetic_metal_net_ntp);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_net_dot_pump, mp_module_pymergetic_metal_net_pump);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_net_dot_ssh, mp_module_pymergetic_metal_net_ssh);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_net_dot_tftp, mp_module_pymergetic_metal_net_tftp);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_net_dot_tls, mp_module_pymergetic_metal_net_tls);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_net_dot_wg, mp_module_pymergetic_metal_net_wg);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_pack, mp_module_pymergetic_metal_pack);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_reg, mp_module_pymergetic_metal_reg);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_rt, mp_module_pymergetic_metal_rt);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_shell, mp_module_pymergetic_metal_shell);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_shell_dot_tui, mp_module_pymergetic_metal_shell_tui);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_shell_dot_ui, mp_module_pymergetic_metal_shell_ui);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_shell_dot_vt, mp_module_pymergetic_metal_shell_vt);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_trust, mp_module_pymergetic_metal_trust);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_util, mp_module_pymergetic_metal_util);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_util_dot_ascii, mp_module_pymergetic_metal_util_ascii);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_util_dot_eightcc, mp_module_pymergetic_metal_util_eightcc);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_util_dot_endian, mp_module_pymergetic_metal_util_endian);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_util_dot_fourcc, mp_module_pymergetic_metal_util_fourcc);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_util_dot_lz4, mp_module_pymergetic_metal_util_lz4);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_util_dot_size, mp_module_pymergetic_metal_util_size);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_util_dot_tar, mp_module_pymergetic_metal_util_tar);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_wamr_host, mp_module_pymergetic_metal_wamr_host);

#endif
