"""Post-ready epilogue for unix x86_64 (boot tree is C: pm_metal_unix_boot_tree)."""


def run():
    from pymergetic.metal.site import print_net_listeners

    print("")
    print(
        "\033[1;35mMetal Python\033[0m — persistent REPL, shared context, globals stick around."
    )
    print("  - pymergetic.metal.unix.x86_64")
    print_net_listeners()
    print("  - quit()/exit() end a process (noop+hint in bare REPL)")
    print("  - Ctrl-D returns to >>> (REPL is the seat — nothing to leave)")
    print("  - shutdown()/reboot() unboot then leave the seat")
    print("")
    return True
