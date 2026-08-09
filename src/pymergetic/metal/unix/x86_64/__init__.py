"""unix x86_64 metal seat (curl-and-run Linux host)."""

NAME = "x86_64"
VERSION = "0.1.0"
CPU = "x86_64"
FIRMWARE = "unix"



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
    return "unix"


def boot_sections():
    return [
        {
            "name": "arch",
            "items": [
                {
                    "name": "seat",
                    "status": "ok",
                    "detail": "unix x86_64 · curl-and-run",
                }
            ],
        },
        {
            "name": "host",
            "items": [
                {"name": "os", "status": "ok", "detail": "linux"},
                {"name": "face", "status": "ok", "detail": "userspace"},
            ],
        },
        {
            "name": "externals",
            "items": _externals_items(),
        },
    ]


def autoexec():
    from pymergetic.metal.unix.x86_64.autoexec import run

    return run()
