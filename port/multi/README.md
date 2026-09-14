# Real two-seat UEFI proof

`make multi-e2e` builds one immutable UEFI image, boots two independent QEMU
address spaces, and joins their second virtio NICs on a shared QEMU Ethernet
segment. Explicit MAC tails give the seats `10.0.0.11` (`metal-11`) and
`10.0.0.12` (`metal-12`); separate SLIRP NICs expose dashboards on ports 18091
and 18092. The gate requires both guests to exchange UDP traffic over the
shared L2, complete a cross-guest Zenoh OPEN, expose distinct hostnames/ZIDs,
and exchange their HTTP/SSH rosters. Headless Chromium executes the shipped
P2P and Services panel scripts against both live firmware APIs and verifies the
rendered remote host and service DOM.

Prerequisites: QEMU x86_64, OVMF, mtools, Python 3, curl, Python Playwright, and
its matching Chromium. Set `PLAYWRIGHT_PYTHON=/path/to/python` when Playwright
is installed outside the active virtual environment; the harness also checks
`/usr/bin/python3` automatically. No root, TAP, host bridge, or DHCP daemon is
required. QEMU's socket backend is the shared L2 bus.
Artifacts are isolated under `port/build/multi-uefi` and guests are terminated
on every exit. `make multi-run` leaves both dashboards running for inspection.
