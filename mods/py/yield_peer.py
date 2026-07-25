import metal.aio as a


async def main():
    # Busy-cooperative peer: yield often so a sleeper can finish.
    n = 0
    while n < 4000:
        await a.yield_()
        n += 1
    print("yield_peer done", n)
