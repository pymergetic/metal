# Screenshots

Stills from one unix seat (`METAL_SERVE=1`, httpd on `:8090`), used in the
package README. All five come from a single run: `BUILD ALL` typed at the
terminal, the pages shot while that walk ran.

| File | Scene |
|------|--------|
| `factory-walk.png` | Factory pane — walk #1 done, 85 ok / 0 failed / 0 skipped, per-unit objects |
| `factory-console.png` | Factory with the console pane open tall — the command that drove the walk |
| `console-build.png` | The seat's terminal — boot tree (`limits ok 75 knob(s)`), `POST /build?all=1`, the knobs |
| `registry.png` | Registry pane — health / capabilities / self, `methods=89 gaps=0 modules=89` |
| `packages.png` | Browse pane — the seat's own card catalog, 66 packages |

Refresh after UI changes: bring a seat up
(`METAL_SERVE=1 ../../ports/unix/build-metal/micropython`), drive it with
`i.handle("POST", "/build?all=1")`, and replace these files.
