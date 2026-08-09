"""Post-ready epilogue for arch.x86 (boot tree is C)."""


def run():
    print("")
    print(
        "\033[1;35mMetal Python\033[0m — persistent REPL, shared context, globals stick around."
    )
    print("  - pymergetic.metal.arch.x86 seat")
    print("  - packs under /mods/<fqn>/…")
    print("  - default: httpd :80/:443 + sshd :22 (C live / services_start)")
    print("  - quit()/exit() end a process; shutdown()/reboot() unboot the seat")
    print("  - Ctrl-D leaves the REPL (REPL is not a process)")
    print("  - override: place /etc/autoexec.py to replace this epilogue")
    print("")
    return True
