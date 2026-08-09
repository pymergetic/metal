#!/bin/sh
# QEMU user-net guestfwd helper: SSH identification + minimal SSH_MSG_KEXINIT.
# Packet: length=8, padlen=6, type=20 (KEXINIT), 6 zero padding bytes.
printf 'SSH-2.0-qemu\r\n'
printf '\000\000\000\010\006\024\000\000\000\000\000\000'
exec cat >/dev/null
