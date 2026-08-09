# Process

## Definition

A **process** is a [task](task.md) (hence a [coro](coro.md)) that is
flagged as an **orchestration intent root** and separately watched by a
stable **id**.

In the coro hierarchy it is nothing special: same park/await machinery,
same tree. The flag marks “this node is the public face of a work
bundle”; the id lets the orchestration layer wait, list, kill, or bind
UI without caring what coros/tasks sit behind it.

## Orchestration (not “human user”)

“Intent” here means the **orchestration machinery** — public-facing
Python/C/RS APIs and scripts (e.g. `sshd.listen`, httpd start) — whether
a person typed the call or a script did. That layer crowns the root.

The REPL is **not** a process (core view on the seat). There is no
Linux-style shell command registry. See [`ORCHESTRATION.md`](../../ORCHESTRATION.md).

Internal callers (net life, seed, mod-to-mod, drivers) leave the same
body as a plain [task](task.md).

| Caller kind | Rank |
|-------------|------|
| Orchestration entry (public API / script / service listen) | process |
| Library / internal / behind another face | task |

Example: an SSH accept loop is a task when started from another program;
the same body is a process when started via `net.ssh.listen`.

## Must

- Remain a task/coro in the same hierarchy (promotion, not a second
  scheduler).
- Carry an intent/root flag and a watchable id.
- Hide the subgraph: orchestrators deal with the process id/face, not
  every child coro.

## Must not

- Mean a different async engine or a distinct “process core” lifecycle
  separate from coro steps — policy may attach to the flag (teardown of
  children, UI bind, argv), but progress is still the coro.
- Imply impure module state or a wasm cage. APP/SESSION/PURE is a
  separate axis; a process may or may not own a cage.
- Mean “human user story.” Scripts and shell are the same orchestration
  layer.

## Rank

```text
coro  ⊂  task  ⊂  process
         schedule   intent flag + id watch
```

**Portion added at this rank:** orchestration face (intent stamp +
separate id index). No new await semantics.

## Related

- Metal process table / spawn: `include/pymergetic/metal/process/`.
- Product verbs / unboot: [`ORCHESTRATION.md`](../../ORCHESTRATION.md).
- [`coro.md`](coro.md), [`task.md`](task.md).
