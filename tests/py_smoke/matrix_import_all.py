# Import every MODULE_MATRIX Browser=yes seat (69).
# Host ledger test asserts SEATS matches docs/MODULE_MATRIX.md.
# Run via: make -C tests/matrix browser
SEATS = (
    "arch",
    "arch.wasm",
    "arch.x86",
    "arch.x86_64",
    "async",
    "auth",
    "boot",
    "boot.tree",
    "bus.pci",
    "bus.virtio",
    "console",
    "dev.acpi",
    "dev.blk",
    "dev.gfx.compositor",
    "dev.gfx.scanout",
    "dev.gfx.text",
    "dev.input.kbd",
    "dev.net.bge",
    "dev.net.virtio_net",
    "dev.serial",
    "dev.stream",
    "draw",
    "externals",
    "fs",
    "fs.embed",
    "fs.fat",
    "fs.littlefs",
    "fs.mtar",
    "fs.overlay",
    "fs.tmpfs",
    "fs.vfs",
    "fs.wasmmod",
    "fs.zip",
    "hwtree",
    "inspect",
    "mem.arena",
    "mem.lock",
    "mem.port",
    "mem.tlsf",
    "net.asgi",
    "net.dhcp",
    "net.dns",
    "net.faces",
    "net.http",
    "net.ip",
    "net.microdot",
    "net.nic",
    "net.ntp",
    "net.pump",
    "net.ssh",
    "net.tftp",
    "net.tls",
    "net.wg",
    "pack",
    "rt",
    "shell.tui",
    "shell.ui",
    "shell.vt",
    "trust",
    "unix.x86",
    "unix.x86_64",
    "util.ascii",
    "util.eightcc",
    "util.endian",
    "util.fourcc",
    "util.lz4",
    "util.size",
    "util.tar",
    "wamr_host",
)
def _imp(s):
    # Progressive import: builtin leaf, else parent nest attr (boot.tree etc.).
    # Also avoids SyntaxError on keyword seat name `async`.
    parts = ("pymergetic", "metal") + tuple(s.split("."))
    cur = None
    for i in range(len(parts)):
        dotted = ".".join(parts[: i + 1])
        leaf = parts[i]
        try:
            cur = __import__(dotted, None, None, (leaf,))
        except ImportError:
            cur = getattr(cur, leaf)
    return cur


for s in SEATS:
    _ = _imp(s)
print("MATRIX_BROWSER_OK", len(SEATS))
