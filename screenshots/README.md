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

**Two UEFI seats on one wire** and the self-host cycle (`make selfhost`):

| File | Scene |
|------|--------|
| `p2p-discovery.png` | Node A (`metal-11`) P2P pane — neighbor `metal-12` seen over the shared L2, `zenoh, services` in sync |
| `p2p-discovery-peer.png` | Node B (`metal-12`) — neighbor `metal-11`, distributed state 2/2, RPC handlers/calls |
| `services-remote.png` | Node A browsing node B's advertised `httpd` + `ssh` — Ownership remote |
| `services-remote-peer.png` | Node B browsing node A's advertised `httpd` + `ssh` — Ownership remote |
| `selfhost-fixpoint.png` | `make selfhost` on the terminal: gen-2 equals gen-1 byte for byte, then the linked compiler's output matches again |

Refresh after UI changes: bring a seat up
(`METAL_SERVE=1 ../../ports/unix/build-metal/micropython`), drive it with
`i.handle("POST", "/build?all=1")`, and replace these files.

The multi-seat stills come from a live pair (`make multi-run`, which keeps both
dashboards up) via the capture tool:

```bash
tools/multi_shot.py screenshots http://127.0.0.1:18091 http://127.0.0.1:18092
```

It shoots at 1500x950 @2x and only after the DOM holds what
`tools/multi_browser_e2e.py` asserts. It also steps over the two traps a naive
capture hits on this firmware: pages must load with `Connection: close` (the
seat serves HTTP/1.0 from a small connection table, so Chromium's keep-alive
burst wedges it), and `document.styleSheets` lists a stylesheet link before its
bytes arrive (wait for the rules to parse, or the still catches half-painted
badges in user-agent defaults).
