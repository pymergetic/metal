"""shutdown() must unwind the boot graph it printed.

The teardown used to print nine subsystem lines with "ok" behind each and call
no deinit at all, which nothing noticed because nothing ran it. The Makefile
greps the stop line for a card count, so a teardown that stops tearing down
fails here.
"""

import pymergetic.metal as m

if not m.ready():
    raise SystemExit("metal not ready")
print("upy shutdown prove")
shutdown()  # noqa: F821 - seat builtin (mpconfig_unix.h), not a module attr
raise SystemExit("shutdown() returned instead of halting the seat")
