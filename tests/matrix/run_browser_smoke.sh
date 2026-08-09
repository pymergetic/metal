#!/usr/bin/env bash
# Import all MODULE_MATRIX Browser=yes seats on the metal wasm image.
set -euo pipefail
METAL="$(cd "$(dirname "$0")/../.." && pwd)"
TOP="$(cd "$METAL/../.." && pwd)"
MJS="${BUILD_METAL:-$TOP/ports/webassembly/build-metal}/micropython.mjs"
SMOKE="$METAL/tests/py_smoke/matrix_import_all.py"
test -f "$MJS" || {
  echo "FAIL: missing $MJS — build with: make -C $METAL/port/webassembly"
  exit 1
}
test -f "$SMOKE"
echo "==> browser matrix import ($MJS)"
out="$(node "$MJS" -X heapsize=8m "$SMOKE")"
echo "$out" | tail -n 5
echo "$out" | grep -q 'MATRIX_BROWSER_OK 69'
echo "UNIX_BROWSER_OK (matrix import 69/69)"
