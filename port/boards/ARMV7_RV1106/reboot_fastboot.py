#!/usr/bin/env python3
"""Rockchip reboot-mode: RAM fastboot. Does not write NAND.

Buildroot libc reboot() is POSIX 1-arg; Linux RESTART2 is syscall 88 on ARM.
"""
import ctypes
import ctypes.util
import os

os.sync()
libc = ctypes.CDLL(ctypes.util.find_library("c"), use_errno=True)
libc.syscall.restype = ctypes.c_int
libc.syscall.argtypes = [
    ctypes.c_long,
    ctypes.c_uint,
    ctypes.c_uint,
    ctypes.c_uint,
    ctypes.c_char_p,
]
# ARM EABI __NR_reboot
r = libc.syscall(88, 0xFEE1DEAD, 0x28121969, 0xA1B2C3D4, b"loader")
raise SystemExit("syscall reboot(loader) returned %s errno=%s" % (r, ctypes.get_errno()))
