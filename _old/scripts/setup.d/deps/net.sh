#!/usr/bin/env bash
# Vendors the Metal net crypto/TLS deps (Monocypher + mbedTLS).
# Stack is lwIP + mbedTLS — no libcurl / nghttp2.
# See docs/SOURCETREE.md "Vendoring".
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
"${HERE}/monocypher.sh"
"${HERE}/mbedtls.sh"
"${HERE}/crypt_blowfish.sh"
"${HERE}/dropbear.sh"

echo "setup-net: OK (monocypher + mbedtls + crypt_blowfish + dropbear)"
