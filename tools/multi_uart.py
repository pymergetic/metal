#!/usr/bin/env python3
"""Start two firmware Zenoh peers concurrently over their actual UART REPLs."""
import socket, sys, threading, time
paths = sys.argv[1:3]
ss = []
for path in paths:
    deadline = time.time() + 90
    while True:
        try:
            s = socket.socket(socket.AF_UNIX); s.connect(path); ss.append(s); break
        except OSError:
            if time.time() >= deadline: raise
            time.sleep(.1)
for s in ss: s.settimeout(.2)
# Wait until each independent guest has actually printed its REPL prompt. A
# fixed sleep races slow software-emulated boots and can inject Ctrl-C into the
# boot script, rebooting one guest and turning a protocol failure into noise.
for s in ss:
    banner = bytearray()
    deadline = time.time() + 120
    while b">>>" not in banner:
        if time.time() >= deadline:
            raise TimeoutError("guest REPL did not become ready")
        try:
            banner.extend(s.recv(65536))
        except OSError:
            pass
# The firmware idle hook owns mesh startup and keeps both network stacks
# progressing while the guests wait at their REPLs. Do not inject z.up() here:
# a UART command can monopolize one guest while the other side needs CPU, and
# Ctrl-C resets this firmware port instead of merely interrupting that call.
time.sleep(30)
for s in ss: s.close()
