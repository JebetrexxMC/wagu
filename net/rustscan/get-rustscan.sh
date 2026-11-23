#!/usr/bin/env bash
# Download RustScan (fast port scanner) for this machine.
# Fork: https://github.com/JebetrexxMC/RustScan (no releases) -> binaries from upstream bee-san/RustScan.
# Only scan servers you own or are allowed to test.
set -euo pipefail
VER="${RUSTSCAN_VERSION:-2.4.1}"
OUT="${1:-$HOME/.local/bin}"
BASE="https://github.com/bee-san/RustScan/releases/download/$VER"
case "$(uname -s)-$(uname -m)" in
  Linux-x86_64)          ZIP=x86_64-linux-rustscan.tar.gz.zip ;;
  Linux-aarch64|Linux-arm64) ZIP=aarch64-linux-rustscan.zip ;;
  Darwin-*)              echo "macOS: brew install rustscan"; exit 0 ;;
  *) echo "No prebuilt RustScan for $(uname -sm). Build it: cargo install rustscan"; exit 1 ;;
esac
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
echo "RustScan $VER: $BASE/$ZIP"
curl -fsSL -o "$tmp/r.zip" "$BASE/$ZIP"
(cd "$tmp" && unzip -oq r.zip && for t in *.tar.gz; do [ -e "$t" ] && tar xzf "$t"; done; true)
bin=$(find "$tmp" -type f -name rustscan | head -1)
[ -n "$bin" ] || { echo "rustscan binary not found in $ZIP"; exit 1; }
mkdir -p "$OUT"; install -m 755 "$bin" "$OUT/rustscan"
echo "installed: $OUT/rustscan"; "$OUT/rustscan" --version || true
