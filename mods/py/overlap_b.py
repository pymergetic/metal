import metal.aio as a


async def main():
    t0 = a.mono_us()
    print("overlap_b start", t0)
    await a.sleep_us(150000)
    t1 = a.mono_us()
    print("overlap_b done", t1, "dt", t1 - t0)
