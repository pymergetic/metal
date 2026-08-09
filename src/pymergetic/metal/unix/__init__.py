"""Linux userspace metal seats (manylinux / curl-and-run)."""
_NAMES = ("x86", "x86_64")

def names():
    return _NAMES

def current():
    try:
        import sys
        forced = getattr(sys, "metal_unix", None)
        if forced == "x86":
            from pymergetic.metal.unix import x86
            return x86
        if forced == "x86_64":
            from pymergetic.metal.unix import x86_64
            return x86_64
    except Exception:
        pass
    try:
        import platform
        m = (platform.machine() or "").lower()
        if m in ("i386", "i686", "x86"):
            from pymergetic.metal.unix import x86
            return x86
    except Exception:
        pass
    from pymergetic.metal.unix import x86_64
    return x86_64

def name():
    return current().NAME

def boot():
    """Banner + seat autoexec — used by -m pymergetic.metal.unix."""
    try:
        import sys

        import metal_unix_orch
        import pymergetic.metal as metal

        metal_unix_orch.bind()
        # Frozen metal rejects C store_attr; re-export from sys.modules.
        metal.process = sys.modules["pymergetic.metal.process"]
        metal.boot = sys.modules["pymergetic.metal.boot"]
    except Exception:
        pass
    try:
        from pymergetic.metal.site import install as _install_quitters

        _install_quitters()
    except ImportError:
        pass
    return current().autoexec()
