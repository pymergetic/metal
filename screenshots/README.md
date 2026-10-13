# Screenshots

Stills used in the package README.

**Unix seat** (`METAL_SERVE=1`, httpd on `:8090`) — all from a single run:
`BUILD ALL` typed at the terminal, the pages shot while that walk ran.

| File | Scene |
|------|--------|
| `factory-walk.png` | Factory pane — walk #1 done, 85 ok / 0 failed / 0 skipped, per-unit objects |
| `factory-console.png` | Factory with the console pane open tall — the command that drove the walk |
| `console-build.png` | The seat's terminal — boot tree (`limits ok 75 knob(s)`), `POST /build?all=1`, the knobs |
| `registry.png` | Registry pane — health / capabilities / self, `methods=89 gaps=0 modules=89` |
| `packages.png` | Browse pane — the seat's own card catalog, 66 packages |

**Two UEFI seats on one wire** (`make multi-run` keeps both dashboards up) and
the self-host cycle (`make selfhost`):

| File | Scene |
|------|--------|
| `p2p-discovery.png` | Node A (`metal-11`) P2P pane — neighbor `metal-12` seen over the shared L2, `zenoh, services` in sync |
| `p2p-discovery-peer.png` | Node B (`metal-12`) — neighbor `metal-11`, distributed state 2/2, RPC handlers/calls |
| `services-remote.png` | Node A browsing node B's advertised `httpd` + `ssh` — Ownership remote |
| `selfhost-fixpoint.png` | `make selfhost` on the terminal: gen-2 equals gen-1 byte for byte, then the linked compiler's output matches again |

Refresh after UI changes: bring a seat up
(`METAL_SERVE=1 ../../ports/unix/build-metal/micropython`), drive it with
`i.handle("POST", "/build?all=1")`, and replace these files.

The multi-seat stills come from the live pair (`make multi-run`, or the same
launch as `port/multi/run_two_uefi.sh`) once both `/p2p/self` answers show the
peer; shoot the pages at 1500x950 @2x. The firmware serves HTTP/1.0 from a small
connection table, so load pages with `Connection: close` and wait until the
stylesheets actually parse before shooting — otherwise the still catches
half-painted badges in user-agent defaults.
