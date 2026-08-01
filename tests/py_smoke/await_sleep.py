# tests/py_smoke — W4.2 await/park proof (app .py OK here).
# Firmware runs the Metal-async path via pm_metal_py_proof_await().
import asyncio

async def main():
    await asyncio.sleep_ms(1)
    print("await ok")

asyncio.run(main())
