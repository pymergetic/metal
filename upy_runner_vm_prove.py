"""Every async-runner core may consume the interpreter.

The old model bounced every vm_only coro to one "VM owner" slot. Now a vm_only
coro can be stepped by any runner pthread/AP core, serialized by the VM lock
(MicroPython GIL + the async VM-entry lock). This proves it from Python: we
register a vm_only generator, then *do not* poll from the boot thread, so only
the background async runners consume it. If the generator completes, a real
runner steppped it through the bytecode VM on a non-boot core.

Lines ending in "runner ate it" means the prove passed.
"""

import gc
import time

import pymergetic.metal as m

if not m.ready():
    raise SystemExit("metal not ready")

ran = []


def gen():
    ran.append("enter")
    yield 1
    ran.append("mid")
    yield 2
    ran.append("stop")


m.register_upy(gen())

# Do NOT call m.poll() here. The 3 background runner pthreads (boot thread is
# slot 0, not a runner) must pick the vm_only task up on their own and step it
# under the VM lock. Give them a short window, then let mp_iternext finish.
deadline = time.time() + 2.0
while time.time() < deadline:
    if ran == ["enter", "mid", "stop"]:
        break
    time.sleep(0.005)

if ran != ["enter", "mid", "stop"]:
    raise SystemExit("vm_only was never stepped by a runner: %s" % (ran,))

gc.collect()
print("runner ate it")
