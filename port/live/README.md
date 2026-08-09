# port/live/ — firmware LIVE proofs (+ host QEMU helpers)

Linked into the image when `LIVE=1` / smoke battery runs (`*_smoke.c`,
`live_http`, `live_ssh`). Not host `tests/` packs.

Host-only helpers used by board `run` / QEMU:

- `tftp-root/` — SLIRP TFTP payload
- `qemu-ssh-banner.sh` — guestfwd SSH ident helper
