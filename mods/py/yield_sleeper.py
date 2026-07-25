import pymergetic.metal.aio as a


async def main():
    t0 = a.mono_us()
    await a.sleep_us(50000)
    t1 = a.mono_us()
    print("yield_sleeper done", t1 - t0)
