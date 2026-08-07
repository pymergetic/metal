#!/bin/sh
# QEMU user-net guestfwd helper: speak a minimal SSH identification string.
printf 'SSH-2.0-qemu\r\n'
exec cat >/dev/null
