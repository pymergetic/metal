"""Post-ready epilogue for arch.x86_64 (boot tree is C)."""


def run():
    from pymergetic.metal.site import print_net_listeners

    print("")
    print(
        "\033[1;35mMetal Python\033[0m — persistent REPL, shared context, globals stick around."
    )
    print("  - pymergetic.metal.arch.x86_64 seat")
    print("  - packs under /mods/<fqn>/…")
    print_net_listeners()
    print("  - quit()/exit() end a process; shutdown()/reboot() unboot the seat")
    print("  - Ctrl-D returns to >>> (REPL is the seat — nothing to leave)")
    print("  - override: place /etc/autoexec.py to replace this epilogue")
    print("")
    return True
