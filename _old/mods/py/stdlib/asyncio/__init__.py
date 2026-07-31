# Metal asyncio — name-compatible shim over pymergetic.metal.aio.
#
# Metal owns the scheduler (cooperative runners / awaitables). This module
# exists so stock libraries (microdot, ...) can `import asyncio` without
# bringing a second event loop. See docs/MICROPYTHON.md / COOP_MEMORY.md:
#   asyncio.sleep(s)  -> metal.aio.sleep_us / yield_
#   asyncio.sleep(0)  -> metal.aio.yield_
#   create_task       -> returns the coro (already a Metal-driven awaitable)
#   gather            -> sequential await (true parallel = metal.aio later)
#   Event             -> poll via yield_
#   get_running_loop().run_in_executor -> run sync inline (no thread pool)
#
# Do NOT vendor CircuitPython/uasyncio — that would fight Metal.

import pymergetic.metal.aio as _aio


class CancelledError(Exception):
  pass


class InvalidStateError(Exception):
  pass


TimeoutError = CancelledError


def _seconds_to_us(seconds):
  if seconds is None:
    return 0
  try:
    us = int(seconds * 1000000.0)
  except TypeError:
    us = int(seconds) * 1000000
  if us < 0:
    return 0
  return us


async def sleep(seconds=0):
  us = _seconds_to_us(seconds)
  if us == 0:
    await _aio.yield_()
  else:
    await _aio.sleep_us(us)


async def sleep_ms(ms=0):
  if not ms:
    await _aio.yield_()
  else:
    await _aio.sleep_us(int(ms) * 1000)


class Event:
  def __init__(self):
    self._set = False

  def is_set(self):
    return self._set

  def set(self):
    self._set = True

  def clear(self):
    self._set = False

  async def wait(self):
    while not self._set:
      await _aio.yield_()


def create_task(coro):
  """Return coro as a 'task'. Metal already pumps the calling Python job;
  a dedicated multi-runner spawn is pm_metal_async_create_task (expose on
  metal.aio when a caller needs true overlap)."""
  return coro


def ensure_future(aw):
  return aw


async def gather(*aws, return_exceptions=False):
  out = []
  for aw in aws:
    try:
      out.append(await aw)
    except Exception as e:
      if return_exceptions:
        out.append(e)
      else:
        raise
  return out


async def wait_for(aw, timeout):
  # Full deadline cancel needs C pm_metal_wait_for; for now await directly.
  # timeout kept for API compatibility with microdot/helpers.
  _ = timeout
  return await aw


class _Loop:
  def run_in_executor(self, executor, func, *args):
    async def _run():
      _ = executor
      return func(*args)

    return _run()

  def create_task(self, coro):
    return create_task(coro)


_loop = _Loop()


def get_running_loop():
  return _loop


def get_event_loop():
  return _loop


def new_event_loop():
  return _Loop()


async def start_server(*args, **kwargs):
  _ = args
  _ = kwargs
  raise NotImplementedError(
      "asyncio.start_server: Metal C ASGI owns sockets; use ASGI mounts"
  )


def run(main):
  _ = main
  raise NotImplementedError(
      "asyncio.run: Metal tasks own CPUs; use async def + py runner"
  )
