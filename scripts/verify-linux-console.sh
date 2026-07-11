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

echo "${SHELL_OUT}" | grep -qx "\[INFO\] mods/" \
	|| { echo "FAIL: 'ls' at / did not list mods/" >&2; exit 1; }
echo "${SHELL_OUT}" | grep -qx "\[INFO\] /mods" \
	|| { echo "FAIL: 'cd mods' did not update pwd to /mods" >&2; exit 1; }
echo "${SHELL_OUT}" | grep -q "loaded /mods/t0_hello.wasm -> handle=1" \
	|| { echo "FAIL: 'load t0_hello.wasm' from cwd=/mods did not resolve to /mods/t0_hello.wasm" >&2; exit 1; }
echo "${SHELL_OUT}" | grep -q "cd: no such directory: /nope" \
	|| { echo "FAIL: 'cd /nope' did not refuse a nonexistent directory" >&2; exit 1; }
echo "${SHELL_OUT}" | grep -qx "\[INFO\] /" \
	|| { echo "FAIL: native 'pwd' never printed the real root cwd" >&2; exit 1; }
echo "${SHELL_OUT}" | grep -qx "\[INFO\] pwd" \
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
echo "${ENV_OUT}" | grep -qx "\[INFO\] GREETING=hi-from-shell" \
	|| { echo "FAIL: 'env' builtin did not print the exported var" >&2; exit 1; }

echo "verify-linux-console-env: OK"
