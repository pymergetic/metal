"""Post-ready epilogue for unix x86_64 (boot tree is C: pm_metal_unix_boot_tree)."""


def run():
    print("")
    print(
        "\033[1;35mMetal Python\033[0m — persistent REPL, shared context, globals stick around."
    )
    print("  - pymergetic.metal.unix.x86_64")
    print("  - httpd/sshd do not auto-listen; start explicitly, e.g.:")
    print("      import pymergetic.metal.net.ssh as sshd")
    print("      sshd.listen(22)")
    print("      # httpd: Microdot/ASGI app.listen(:80/:443)")
    print("  - quit()/exit() end a process (noop+hint in bare REPL)")
    print("  - shutdown()/reboot() unboot then leave host; Ctrl-D leaves REPL")
    print("")
    return True
