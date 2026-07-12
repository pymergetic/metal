#!/usr/bin/env bash
# --console mode, on linux: kernel command console + per-handle panes —
# load/run/focus/escape/unload/quit, through the real console.h/viewport.h
# path (a real tty session would type these same bytes at a real terminal;
# here we just pipe them in). See docs/CONSOLE.md.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

"${ROOT}/scripts/build-mod.sh"
"${ROOT}/scripts/build-linux.sh"

RUNTIME="${ROOT}/build/linux/runtime/pm-linux-runtime"
if [ ! -x "${RUNTIME}" ]; then
	echo "missing ${RUNTIME}" >&2
	exit 1
fi

VFS_ROOT="$(mktemp -d)"
trap 'rm -rf "${VFS_ROOT}"' EXIT

printf 'hello from vfs root\n' > "${VFS_ROOT}/README"
mkdir -p "${VFS_ROOT}/mods"
cp "${ROOT}/build/mods/t0_hello.wasm" "${ROOT}/build/mods/t1_read.wasm" "${VFS_ROOT}/mods/"

# Escape byte (Ctrl-A, 0x01) between "focus 1" and "ps" below proves the
# viewport can always get back to the kernel pane even while a handle is
# focused — see console/viewport.h "PM_METAL_VIEWPORT_ESCAPE_BYTE".
OUT="$( { \
	printf 'load /mods/t0_hello.wasm\n'; sleep 0.2; \
	printf 'load /mods/t1_read.wasm\n'; sleep 0.2; \
	printf 'run 1\n'; sleep 0.3; \
	printf 'focus 1\n'; sleep 0.2; \
	printf '\x01'; sleep 0.1; printf 'ps\n'; sleep 0.2; \
	printf 'run 2\n'; sleep 0.3; \
	printf 'focus 2\n'; sleep 0.2; \
	printf '\x01'; sleep 0.1; printf 'ps\n'; sleep 0.2; \
	printf 'unload 1\nunload 2\nquit\n'; sleep 0.2; \
} | timeout 10 "${RUNTIME}" --memory=16777216 --bytecode-memory=1048576 \
	--vfs-root="${VFS_ROOT}" --console )"

echo "${OUT}"

echo "${OUT}" | grep -q "t0_hello" \
	|| { echo "FAIL: missing t0_hello output" >&2; exit 1; }
echo "${OUT}" | grep -q "hello from vfs root" \
	|| { echo "FAIL: missing t1_read output" >&2; exit 1; }
echo "${OUT}" | grep -q "loaded /mods/t0_hello.wasm -> handle=1" \
	|| { echo "FAIL: missing load confirmation for handle=1" >&2; exit 1; }
echo "${OUT}" | grep -q "loaded /mods/t1_read.wasm -> handle=2" \
	|| { echo "FAIL: missing load confirmation for handle=2" >&2; exit 1; }
echo "${OUT}" | grep -q "focus: handle=1" \
	|| { echo "FAIL: missing focus confirmation for handle=1" >&2; exit 1; }
echo "${OUT}" | grep -q "focus: handle=2" \
	|| { echo "FAIL: missing focus confirmation for handle=2" >&2; exit 1; }
# After the escape byte, "ps" output must land on the kernel pane again —
# i.e. these lines must actually be present, proving focus really moved
# back to slot 0 rather than getting swallowed by the still-focused handle.
echo "${OUT}" | grep -q "kernel (sink 0)" \
	|| { echo "FAIL: escape byte did not return focus to kernel (no 'ps' output)" >&2; exit 1; }
# `ps` lists both handles it was called with a process attached to
# (handle=1's `run 1`, handle=2's `run 2`) — proves runtime/process.h's
# pid table (decoupled from the handle table) is actually wired through
# the shell, not just present.
echo "${OUT}" | grep -q "pid=.*handle=1 " \
	|| { echo "FAIL: 'ps' did not list a process for handle=1" >&2; exit 1; }
echo "${OUT}" | grep -q "pid=.*handle=2 " \
	|| { echo "FAIL: 'ps' did not list a process for handle=2" >&2; exit 1; }
echo "${OUT}" | grep -q "unloaded handle=1" \
	|| { echo "FAIL: missing unload confirmation for handle=1" >&2; exit 1; }
echo "${OUT}" | grep -q "unloaded handle=2" \
	|| { echo "FAIL: missing unload confirmation for handle=2" >&2; exit 1; }
echo "${OUT}" | grep -q "shutdown complete" \
	|| { echo "FAIL: missing clean shutdown" >&2; exit 1; }

echo "verify-linux-console: OK"

# ---- shell: cd/pwd/ls, relative load, and .wasm overriding a builtin ----
# Separate vfs_root/run from the pane test above — this exercises
# shell/{shell,commands}.c's cwd + registry + resolve() logic specifically.
# See docs/CONSOLE.md "Shell".

SHELL_VFS_ROOT="$(mktemp -d)"
trap 'rm -rf "${VFS_ROOT}" "${SHELL_VFS_ROOT}"' EXIT

mkdir -p "${SHELL_VFS_ROOT}/mods" "${SHELL_VFS_ROOT}/bin"
cp "${ROOT}/build/mods/t0_hello.wasm" "${SHELL_VFS_ROOT}/mods/"

# The override file is dropped mid-session (via a plain `cp`, not piped
# input — see below) rather than present from the start, so the early
# `pwd` calls below exercise the real native builtin first and the later
# ones prove the override then takes over — resolve() never caches, so
# this is exactly what a real "drop a busybox-style bin in while it's
# running" workflow would look like. A real override would ship a
# genuinely different `pwd`; t0_hello.wasm stands in for "some .wasm" —
# the point here is only that it shadows the native `pwd` at all.
SHELL_OUT="$( { \
	printf 'pwd\n'; sleep 0.1; \
	printf 'ls\n'; sleep 0.1; \
	printf 'cd mods\n'; sleep 0.1; \
	printf 'pwd\n'; sleep 0.1; \
	printf 'load t0_hello.wasm\n'; sleep 0.1; \
	printf 'cd ..\n'; sleep 0.1; \
	printf 'cd /nope\n'; sleep 0.1; \
	printf 'ls /bin/pm\n'; sleep 0.1; \
	printf 'pwd\n'; sleep 0.1; \
	cp "${ROOT}/build/mods/t0_hello.wasm" "${SHELL_VFS_ROOT}/bin/pwd.wasm"; sleep 0.1; \
	printf 'pwd\n'; sleep 0.2; \
	printf '/bin/pm/pwd\n'; sleep 0.1; \
	printf 'quit\n'; sleep 0.2; \
} | timeout 10 "${RUNTIME}" --memory=16777216 --bytecode-memory=1048576 \
	--vfs-root="${SHELL_VFS_ROOT}" --console )"

echo "${SHELL_OUT}"

echo "${SHELL_OUT}" | grep -qx "mods/" \
	|| { echo "FAIL: 'ls' at / did not list mods/" >&2; exit 1; }
echo "${SHELL_OUT}" | grep -qx "/mods" \
	|| { echo "FAIL: 'cd mods' did not update pwd to /mods" >&2; exit 1; }
echo "${SHELL_OUT}" | grep -q "loaded /mods/t0_hello.wasm -> handle=1" \
	|| { echo "FAIL: 'load t0_hello.wasm' from cwd=/mods did not resolve to /mods/t0_hello.wasm" >&2; exit 1; }
echo "${SHELL_OUT}" | grep -q "cd: no such directory: /nope" \
	|| { echo "FAIL: 'cd /nope' did not refuse a nonexistent directory" >&2; exit 1; }
echo "${SHELL_OUT}" | grep -qx "/" \
	|| { echo "FAIL: native 'pwd' never printed the real root cwd" >&2; exit 1; }
echo "${SHELL_OUT}" | grep -qx "pwd" \
	|| { echo "FAIL: 'ls /bin/pm' did not list the native 'pwd' command" >&2; exit 1; }
# Once /bin/pwd.wasm exists, a bare `pwd` must run *it* instead of the
# native builtin — proven by the "exit=%d" log line only the wasm-run
# path ever emits (the native `pwd` builtin never logs one).
echo "${SHELL_OUT}" | grep -q "pwd: exit=0" \
	|| { echo "FAIL: bare 'pwd' did not run the /bin/pwd.wasm override once it existed" >&2; exit 1; }
# ...while the explicit escape hatch must keep reaching the native
# builtin even after that same override exists (it must never log an
# "exit=%d" — only the wasm-run path does that).
if echo "${SHELL_OUT}" | grep -q "/bin/pm/pwd: exit="; then
	echo "FAIL: '/bin/pm/pwd' ran the wasm override instead of the native builtin" >&2
	exit 1
fi

echo "verify-linux-console-shell: OK"

# ---- env: export propagates to a guest's WASI env (runtime/process.h) ----

ENV_VFS_ROOT="$(mktemp -d)"
trap 'rm -rf "${VFS_ROOT}" "${SHELL_VFS_ROOT}" "${ENV_VFS_ROOT}"' EXIT

mkdir -p "${ENV_VFS_ROOT}/mods"
cp "${ROOT}/build/mods/t2_env.wasm" "${ENV_VFS_ROOT}/mods/"

# t2_env prints "GREETING=(unset)" the first run (nothing exported yet),
# then "GREETING=hi-from-shell" the second — same loaded handle=1 run
# twice, only the console's ctx->env changed in between via `export`.
ENV_OUT="$( { \
	printf 'load /mods/t2_env.wasm\n'; sleep 0.1; \
	printf 'run 1 GREETING\n'; sleep 0.3; \
	printf 'focus 1\n'; sleep 0.2; \
	printf '\x01'; sleep 0.1; \
	printf 'export GREETING=hi-from-shell\n'; sleep 0.1; \
	printf 'run 1 GREETING\n'; sleep 0.3; \
	printf 'focus 1\n'; sleep 0.2; \
	printf '\x01'; sleep 0.1; printf 'env\n'; sleep 0.1; \
	printf 'unload 1\nquit\n'; sleep 0.2; \
} | timeout 10 "${RUNTIME}" --memory=16777216 --bytecode-memory=1048576 \
	--vfs-root="${ENV_VFS_ROOT}" --console )"

echo "${ENV_OUT}"

echo "${ENV_OUT}" | grep -q "GREETING=(unset)" \
	|| { echo "FAIL: t2_env before 'export' should report GREETING unset" >&2; exit 1; }
echo "${ENV_OUT}" | grep -q "GREETING=hi-from-shell" \
	|| { echo "FAIL: t2_env did not see the exported GREETING after 'export'" >&2; exit 1; }
echo "${ENV_OUT}" | grep -qx "GREETING=hi-from-shell" \
	|| { echo "FAIL: 'env' builtin did not print the exported var" >&2; exit 1; }

echo "verify-linux-console-env: OK"

# ---- run+unload race (runtime/process.h's spawn() vs. runtime.c's
# refcount — see docs/RUNTIME.md "Concurrency", hold()/release()) ----
#
# `unload` immediately follows `run` with *no* sleep in between, so the
# dispatcher thread races process.c's newly-spawned worker thread every
# time — before hold()/release() existed, this reliably (not just
# occasionally) hit a "bad handle" fprintf from runtime.c: unload() saw
# refcount==0 (the worker hadn't been scheduled yet) and freed the
# module out from under the thread about to run it. Repeated several
# times, not just once, since this is inherently a scheduling race — one
# lucky run proves nothing either way.

RACE_VFS_ROOT="$(mktemp -d)"
trap 'rm -rf "${VFS_ROOT}" "${SHELL_VFS_ROOT}" "${ENV_VFS_ROOT}" "${RACE_VFS_ROOT}"' EXIT

mkdir -p "${RACE_VFS_ROOT}/mods"
cp "${ROOT}/build/mods/t0_hello.wasm" "${RACE_VFS_ROOT}/mods/"

for _ in 1 2 3 4 5; do
	RACE_OUT="$(printf 'load /mods/t0_hello.wasm\nrun 1\nunload 1\nquit\n' \
		| timeout 10 "${RUNTIME}" --memory=16777216 --bytecode-memory=1048576 \
			--vfs-root="${RACE_VFS_ROOT}" --console 2>&1)"

	if echo "${RACE_OUT}" | grep -q "bad handle"; then
		echo "${RACE_OUT}"
		echo "FAIL: run+unload race freed a handle still in flight (see hold()/release())" >&2
		exit 1
	fi
	# Every attempt must land on exactly one of these two documented
	# outcomes — anything else (e.g. a silent no-op) would hide a bug.
	echo "${RACE_OUT}" | grep -q "unloaded handle=1" \
		|| echo "${RACE_OUT}" | grep -q "unload refused (by design)" \
		|| { echo "${RACE_OUT}"; echo "FAIL: run+unload race landed on neither documented outcome" >&2; exit 1; }
done

echo "verify-linux-console-race: OK"

# ---- Ctrl+C (SIGINT) runs the same clean teardown as `quit`/EOF, never
# the OS's default "just die" action — see port/intr.h, app.c's pump
# loop. Stdin is kept open (via process substitution — a `sleep`
# producing no output) so the loop is genuinely idle-but-alive, waiting
# on real input, when the signal lands; `timeout --signal=INT` fires it
# after 1s. --preserve-status surfaces the runtime's *own* exit code
# (0, if it shut down cleanly on its own) rather than timeout's "I sent
# a signal" 124 — a still-124 result here would mean the process never
# reacted and had to be reaped by timeout's -k fallback instead.

INTR_VFS_ROOT="$(mktemp -d)"
trap 'rm -rf "${VFS_ROOT}" "${SHELL_VFS_ROOT}" "${ENV_VFS_ROOT}" "${RACE_VFS_ROOT}" "${INTR_VFS_ROOT}"' EXIT

INTR_OUT="$(timeout --preserve-status --signal=INT -k 2 1 "${RUNTIME}" --memory=16777216 \
	--bytecode-memory=1048576 --vfs-root="${INTR_VFS_ROOT}" --console < <(sleep 5) 2>&1)"
INTR_RC=$?

echo "${INTR_OUT}"

[ "${INTR_RC}" -eq 0 ] \
	|| { echo "FAIL: SIGINT did not produce a clean (exit 0) shutdown, got rc=${INTR_RC}" >&2; exit 1; }
echo "${INTR_OUT}" | grep -q "interrupted" \
	|| { echo "FAIL: missing 'interrupted' log line — SIGINT handler did not fire" >&2; exit 1; }
echo "${INTR_OUT}" | grep -q "shutdown complete" \
	|| { echo "FAIL: SIGINT path did not reach the normal clean-shutdown message" >&2; exit 1; }

echo "verify-linux-console-sigint: OK"

# ---- sleep: basic duration + argument errors, and Ctrl+C aborting an
# in-flight sleep quickly instead of blocking shutdown on it. ----
#
# The second case is the one that actually matters: a process-directed
# SIGINT is not guaranteed to land on the dispatcher thread blocked
# inside the `sleep` builtin (see shell/commands/sleep.c) — it often
# lands on app.c's own pump-loop thread instead. If `sleep` did one big
# blocking port/sleep.h call instead of polling port/intr.h in chunks,
# this would still shut down "cleanly" eventually, but only after the
# full `sleep 30` had run its course, at which point timeout's -k
# fallback would have already force-killed it — this asserts the fast
# path, not just eventual success.

SLEEP_VFS_ROOT="$(mktemp -d)"
trap 'rm -rf "${VFS_ROOT}" "${SHELL_VFS_ROOT}" "${ENV_VFS_ROOT}" "${RACE_VFS_ROOT}" "${INTR_VFS_ROOT}" "${SLEEP_VFS_ROOT}"' EXIT

SLEEP_OUT="$(printf 'sleep 0.2\nsleep\nsleep abc\nsleep -1\nquit\n' \
	| timeout 10 "${RUNTIME}" --memory=16777216 --bytecode-memory=1048576 \
		--vfs-root="${SLEEP_VFS_ROOT}" --console 2>&1)"

echo "${SLEEP_OUT}"

echo "${SLEEP_OUT}" | grep -q "usage: sleep" \
	|| { echo "FAIL: 'sleep' with no argument did not report usage" >&2; exit 1; }
echo "${SLEEP_OUT}" | grep -q "sleep: invalid duration: abc" \
	|| { echo "FAIL: 'sleep abc' did not refuse a non-numeric duration" >&2; exit 1; }
echo "${SLEEP_OUT}" | grep -q "sleep: invalid duration: -1" \
	|| { echo "FAIL: 'sleep -1' did not refuse a negative duration" >&2; exit 1; }

SLEEP_START_MS=$(date +%s%3N)
SLEEP_INTR_OUT="$(timeout --preserve-status --signal=INT -k 2 1 "${RUNTIME}" --memory=16777216 \
	--bytecode-memory=1048576 --vfs-root="${SLEEP_VFS_ROOT}" --console \
	< <(printf 'sleep 30\n'; sleep 5) 2>&1)"
SLEEP_INTR_RC=$?
SLEEP_ELAPSED_MS=$(( $(date +%s%3N) - SLEEP_START_MS ))

echo "${SLEEP_INTR_OUT}"
echo "sleep+SIGINT elapsed: ${SLEEP_ELAPSED_MS}ms"

[ "${SLEEP_INTR_RC}" -eq 0 ] \
	|| { echo "FAIL: SIGINT during 'sleep 30' did not produce a clean (exit 0) shutdown, got rc=${SLEEP_INTR_RC}" >&2; exit 1; }
echo "${SLEEP_INTR_OUT}" | grep -q "shutdown complete" \
	|| { echo "FAIL: SIGINT during 'sleep 30' did not reach clean shutdown" >&2; exit 1; }
# Generous upper bound — well under the 30s requested, and well under
# what a stuck-until-timeout's-SIGKILL run would take (~3s to be force
# reaped, past this script's own point of ever seeing a clean exit at
# all). Real teardown is ~1s (1s to SIGINT + a poll chunk or two).
[ "${SLEEP_ELAPSED_MS}" -lt 2500 ] \
	|| { echo "FAIL: SIGINT during 'sleep 30' took ${SLEEP_ELAPSED_MS}ms — sleep builtin is not actually interruptible" >&2; exit 1; }

echo "verify-linux-console-sleep: OK"
