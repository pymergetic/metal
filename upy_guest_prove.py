# Prove Python PM_MOD_BOOT* run (dep order) and late PM_METAL_DRV_* attach.
from pymergetic.wasmmod.guest import PM_MOD_BOOT, PM_MOD_BOOTDEP, PM_MOD_BOOT_CHILD

order = []


def a_init():
    order.append("a")
    return 0


def b_init():
    if order != ["a"]:
        raise RuntimeError("BOOTDEP order")
    order.append("b")
    return 0


def c_init():
    order.append("c")
    return 0


def nop():
    pass


PM_MOD_BOOT("pymergetic.test.a", a_init, nop)
PM_MOD_BOOT("pymergetic.test.b", b_init, nop)
PM_MOD_BOOT("pymergetic.test.c", c_init, nop)
PM_MOD_BOOTDEP("pymergetic.test.b", "pymergetic.test.a")
PM_MOD_BOOT_CHILD("pymergetic.test.a", "pymergetic.test.c")

import pymergetic.metal as m

if not m.ready():
    raise SystemExit("metal not ready")
if order != ["a", "b", "c"] and order != ["a", "c", "b"]:
    raise SystemExit("boot order %s" % (order,))
if "a" not in order or order.index("a") > order.index("b"):
    raise SystemExit("b before a: %s" % (order,))
if order.index("a") > order.index("c"):
    raise SystemExit("c before a: %s" % (order,))

g = (x for x in (1, 2))
m.register_upy(g)
m.poll()
m.poll()

life = []


def i2():
    life.append("init")
    return 0


def d2():
    life.append("deinit")


def r2():
    return -1


try:
    PM_MOD_BOOT("pymergetic.test.failready", i2, d2, r2)
    raise SystemExit("ready-fail should raise")
except RuntimeError:
    pass
if life != ["init", "deinit"]:
    raise SystemExit("deinit %s" % (life,))

hits = []


def attach(bus, loc0, loc1, loc2, loc3):
    hits.append(bus)
    return 0


m.PM_METAL_DRV_PLATFORM("pymergetic.test.plat", attach)
m.PM_METAL_DRV_ISA("pymergetic.test.isa", 0x300, attach)
if len(hits) < 2:
    raise SystemExit("late drv attach %s" % (hits,))

# Cards resolve from the registry at any depth, with no name list to maintain.
import pymergetic.metal.net.ip as ip

if ip.socket is None:
    raise SystemExit("native card import")
if m.net.ip is not ip:
    raise SystemExit("namespace walk")
if "ip" not in m.net.__dict__:
    raise SystemExit("namespace listing")
print("upy native card import")
print("guest prove ok")
