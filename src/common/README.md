# Common host stack — not on this branch

`app/`, `memory/`, `mount/`, `net/`, `port/`, `runtime/`, and `util/` live on
`archive/multi-host-linux-zephyr-nuttx`.

This branch is EFI-first. Bring those modules back from the archive when the
EDK2 + WAMR owned phase needs them (see [docs/EFI.md](../../docs/EFI.md)).
