#!/usr/bin/env bash
# Vendors the Metal net stack deps (Monocypher, mbedTLS, nghttp2, curl).
# See docs/SOURCETREE.md "Vendoring". Order: crypto → TLS → h2 → curl.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

"${ROOT}/scripts/setup-monocypher.sh"
"${ROOT}/scripts/setup-mbedtls.sh"
"${ROOT}/scripts/setup-nghttp2.sh"
"${ROOT}/scripts/setup-curl.sh"

echo "setup-net: OK (monocypher + mbedtls + nghttp2 + curl)"
