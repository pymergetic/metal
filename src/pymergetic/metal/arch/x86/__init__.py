"""x86 (i686) metal seat (BIOS / UEFI firmware faces)."""

NAME = "x86"
VERSION = "0.1.0"
CPU = "i686"



def _externals_items():
    """Live registry when C face is present; else empty."""
    try:
        from pymergetic.metal import externals as ext
        rows = ext.list()
        return [
            {"name": r.get("id", "?"), "status": "ok", "detail": r.get("version", "")}
            for r in rows
        ]
    except Exception:
        return []


def firmware():
    try:
        import sys

        # Board may set this; default bios for freestanding product.
        return getattr(sys, "metal_firmware", "bios")
    except Exception:
        return "bios"


FIRMWARE = "bios"  # updated at runtime via firmware()


def boot_sections():
    fw = firmware()
    return [
        {
            "name": "arch",
            "items": [{"name": "seat", "status": "ok", "detail": "i686 · %s" % fw}],
        },
        {
            "name": "mem",
            "items": [
                {"name": "heap", "status": "ok", "detail": "tlsf"},
                {"name": "alloc", "status": "ok", "detail": "ready"},
            ],
        },
        {
            "name": "cpu",
            "items": [{"name": "smp", "status": "ok", "detail": "online"}],
        },
        {
            "name": "devices",
            "items": [
                {"name": "acpi", "status": "ok", "detail": "ready"},
                {"name": "console", "status": "ok", "detail": "uart"},
            ],
        },
        {
            "name": "fs",
            "items": [
                {"name": "mods", "status": "ok", "detail": "/mods/pymergetic.metal*"},
                {"name": "vfs", "status": "ok", "detail": "ready"},
            ],
        },
        {
            "name": "net",
            "items": [
                {"name": "virtio", "status": "ok", "detail": "ready"},
                {"name": "ip", "status": "ok", "detail": "ready"},
            ],
        },
        {
            "name": "async",
            "items": [{"name": "runners", "status": "ok", "detail": "ready"}],
        },
        {
            "name": "wasm",
            "items": [{"name": "wasmmod", "status": "ok", "detail": "host"}],
        },
        {
            "name": "externals",
            "items": _externals_items(),
        },
    ]


def autoexec():
    from pymergetic.metal.arch.x86.autoexec import run

    return run()
