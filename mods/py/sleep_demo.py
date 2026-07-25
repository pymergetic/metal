import metal.aio as a


async def main():
    t0 = a.mono_us()
    print("sleep demo start", t0)
    await a.sleep_us(100000)
    t1 = a.mono_us()
    print("sleep demo done", t1, "dt", t1 - t0)
