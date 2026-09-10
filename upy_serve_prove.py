"""m.serve() brings the whole UI stack up in one call.

The unix seat's UI reachability is net.fwd's host mirror: the metal TCP
stack's listeners exist only inside the process, so the inspect console
(and every pane in it, the factory floor included) is dead from the
browser until something mirrors the port onto a real host socket. m.serve() starts every registered service
AND the mirror, so this prove pins that contract: serve, then fetch /health
through the loopback guest client — the same path the mirrored bytes ride.
"""

import pymergetic.metal as m

if not m.ready():
    raise SystemExit("metal not ready")

m.serve()

# the host mirror: one fwd listener per service port. Zero means the seat
# can never be reached from a browser — the exact gap this prove guards.
import pymergetic.metal.net.fwd as fwd

n = fwd.count()
if n < 1:
    raise SystemExit("fwd mirror count %d" % (n,))
print("upy serve fwd mirror %d" % (n,))

# the packs renderer route: the deferred /packs page must be registered (the
# renderer is what answers HTML; without it every catalog link times out).
import pymergetic.metal.inspect as inspect

st, body = inspect.handle("GET", "/inspect/reg"), inspect.body()
if st != 200 or '"module"' not in body:
    raise SystemExit("serve reg %s %s" % (st, body))
print("upy serve inspect faces")

print("upy serve prove")