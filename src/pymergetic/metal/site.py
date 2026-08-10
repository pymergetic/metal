"""CPython-like ``quit`` / ``exit`` / ``shutdown`` / ``reboot`` for Metal seats.

See docs/ORCHESTRATION.md:

- quit/exit end the **current process** (noop + hint if none)
- shutdown/reboot are boot shims over ``unboot``
- On every Metal seat the REPL *is* the face: Ctrl-D returns to ``>>>``;
  only shutdown()/reboot() leave (browser: reload to revive).

Note: avoid ``%`` string formatting here — freestanding µPy raises
``TypeError: unsupported type for operator`` on several ``%`` forms.
"""


def _browser_seat():
    try:
        import sys

        plat = str(getattr(sys, "platform", "") or "")
        return plat in ("webassembly", "emscripten") or "wasm" in plat
    except Exception:
        return False


def _process_quit(pid=0, code=None):
    try:
        from pymergetic.metal import process
    except ImportError:
        return False
    try:
        process.quit(pid, 0 if code is None else code)
        return True
    except Exception:
        return False


def _boot_shutdown():
    try:
        from pymergetic.metal import boot

        boot.shutdown()
        return True
    except ImportError:
        return False
    except Exception:
        return False


def _boot_reboot():
    try:
        from pymergetic.metal import boot

        boot.reboot()
        return True
    except ImportError:
        return False
    except Exception:
        return False


class Quitter:
    def __init__(self, name):
        self.name = name

    def __repr__(self):
        return (
            "Use "
            + self.name
            + "() to end a process; shutdown()/reboot() to leave the seat"
        )

    def __call__(self, *args):
        code = None
        pid = 0
        if len(args) == 1:
            a0 = args[0]
            # quit(pid) outside, or quit(code) when current — prefer pid if process exists
            try:
                from pymergetic.metal import process

                cur = process.current()
            except Exception:
                cur = 0
            if cur:
                # inside a process: quit(code)
                code = a0
            else:
                # outside: quit(pid)
                pid = int(a0)
        elif len(args) >= 2:
            pid = int(args[0])
            code = args[1]

        try:
            from pymergetic.metal import process

            cur = process.current()
        except Exception:
            cur = 0

        if pid != 0:
            if _process_quit(pid, code):
                return
            print(self.name + ": process nest unavailable")
            return

        if cur:
            _process_quit(cur, code)
            return

        print(
            self.name
            + ": not in a process — use shutdown()/reboot() to leave the seat"
        )


def _shutdown_builtin(*_args):
    if _boot_shutdown():
        if _browser_seat():
            print("shutdown: seat dead — use Reset to boot again")
        return
    print("shutdown: boot.shutdown unavailable")


def _reboot_builtin(*_args):
    if _boot_reboot():
        if _browser_seat():
            print("reboot: revive / Reset")
        return
    print("reboot: boot.reboot unavailable")


def print_net_listeners():
    """One job: are httpd/sshd listening? If not, how to start.

    Same shape as boot.tree detail: ``:80/:443`` / ``:22`` — or start recipe.
    """
    try:
        import pymergetic.metal.net.asgi as asgi

        if asgi.ready():
            print("  - httpd: :80/:443")
        else:
            print("  - httpd: down — asgi.init(80); asgi.init_tls(443)")
    except Exception:
        print("  - httpd: down — asgi.init(80); asgi.init_tls(443)")

    try:
        import pymergetic.metal.net.ssh as ssh

        port = ssh.listen_port()
        if port != 0:
            print("  - sshd: :" + str(port))
        else:
            print("  - sshd: down — ssh.autoload(); ssh.listen(22)")
    except Exception:
        print(
            "  - sshd: down — import pymergetic.metal.net.ssh as ssh; ssh.listen(22)"
        )


def install():
    """Idempotent: bind quit/exit/shutdown/reboot on builtins.

    Always overwrite quit/exit — seats must not keep a SystemExit seat-leave
    quitter (REPL is not a process).
    """
    try:
        import builtins
    except ImportError:
        return False
    builtins.quit = Quitter("quit")
    builtins.exit = Quitter("exit")
    builtins.shutdown = _shutdown_builtin
    builtins.reboot = _reboot_builtin
    return True
