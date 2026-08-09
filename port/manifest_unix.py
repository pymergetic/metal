# Frozen modules for Linux userspace metal seat (curl-and-run / manylinux).
# Built via extmod/metal/port/unix.

include("$(MPY_DIR)/extmod/asyncio")

# Parents: nest builtins with metal.__path__ — no frozen_ns markers.

package(
    "pymergetic",
    (
        "metal/site.py",
        "metal/unix/__init__.py",
        "metal/unix/__main__.py",
        "metal/unix/x86/__init__.py",
        "metal/unix/x86/autoexec.py",
        "metal/unix/x86_64/__init__.py",
        "metal/unix/x86_64/autoexec.py",
    ),
    base_path="$(METAL)/src",
    opt=3,
)
