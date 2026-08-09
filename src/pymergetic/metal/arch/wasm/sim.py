"""Simulated metal machine backends for arch.wasm (browser experiment seat)."""

# Honest sim inventory — same leaves as live boot tree.

_HEAP_BYTES = 2 * 1024 * 1024
_devices = [
    {"id": "console0", "class": "console", "backend": "js-stdout"},
    {"id": "mem0", "class": "mem", "backend": "emscripten-heap"},
    {"id": "nic0", "class": "net", "backend": "js.fetch"},
    {"id": "clk0", "class": "timer", "backend": "performance.now"},
]



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


def heap_size():
    return _HEAP_BYTES


def alloc_ready():
    return True


def devices():
    return list(_devices)


def console_ready():
    return True


def net_backend():
    return "js.fetch"


def boot_sections():
    return [
        {
            "name": "arch",
            "items": [{"name": "seat", "status": "ok", "detail": "wasm · browser"}],
        },
        {
            "name": "mem",
            "items": [
                {
                    "name": "heap",
                    "status": "sim",
                    "detail": "%d KiB" % (_HEAP_BYTES // 1024),
                },
                {"name": "alloc", "status": "sim", "detail": "browser seat"},
            ],
        },
        {
            "name": "cpu",
            "items": [{"name": "smp", "status": "sim", "detail": "1 runner"}],
        },
        {
            "name": "devices",
            "items": [
                {"name": "catalog", "status": "sim", "detail": "%d nodes" % len(_devices)},
                {"name": "console", "status": "sim", "detail": "panel stdout"},
            ],
        },
        {
            "name": "fs",
            "items": [
                {"name": "mods", "status": "ok", "detail": "/mods/pymergetic.metal*"},
                {"name": "root", "status": "sim", "detail": "memfs"},
            ],
        },
        {
            "name": "net",
            "items": [
                {"name": "nic", "status": "sim", "detail": net_backend()},
                {"name": "cdn", "status": "ok", "detail": "hook"},
            ],
        },
        {
            "name": "async",
            "items": [{"name": "runners", "status": "sim", "detail": "asyncify"}],
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
