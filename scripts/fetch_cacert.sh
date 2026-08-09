#!/usr/bin/env bash
# Refresh Mozilla CA bundle into pack/VFS asset path.
# Usage: ./scripts/fetch_cacert.sh [out_path]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$ROOT/src/pymergetic/metal/net/tls/cacert.pem}"
URL="${METAL_CACERT_URL:-https://curl.se/ca/cacert.pem}"
mkdir -p "$(dirname "$OUT")"
tmp="$(mktemp)"
curl -fsSL "$URL" -o "$tmp"
# Basic sanity: PEM markers present and non-trivial size.
grep -q "BEGIN CERTIFICATE" "$tmp"
sz=$(wc -c <"$tmp")
test "$sz" -gt 100000
mv "$tmp" "$OUT"
echo "wrote $OUT ($sz bytes) from $URL"
echo "Stage into VFS as /etc/ssl/cert.pem (or pack path) and call pm_metal_net_tls_load_ca_file()."
