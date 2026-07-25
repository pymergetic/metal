#!/usr/bin/env bash
# Vendors the Metal net crypto/TLS deps (Monocypher + mbedTLS).
# Stack is lwIP + mbedTLS — no libcurl / nghttp2.
# See docs/SOURCETREE.md "Vendoring".
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"

"${ROOT}/scripts/setup.d/deps/monocypher.sh"
"${ROOT}/scripts/setup.d/deps/mbedtls.sh"

echo "setup-net: OK (monocypher + mbedtls)"
