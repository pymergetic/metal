"""Post-ready epilogue for unix x86 seat (curl-and-run host)."""


def run():
    print("")
    print(
        "\033[1;35mMetal Python\033[0m — unix x86 seat (curl-and-run host)."
    )
    print("  - pymergetic.metal.unix.x86")
    print("  - Linux userspace MicroPython (i686)")
    print("  - httpd/sshd do not auto-listen here; start explicitly, e.g.:")
    print("      import ssh; ssh.listen(22)")
    print("      # ASGI/httpd: metalnet.services_start() when bound on this seat")
    print("")
    return True
