#!/usr/bin/env bash
# Download RustScan (fast port scanner) for this machine.
# Binaries: wagu release v5.02.57 (mirror of RustScan 2.4.1), fallback upstream bee-san/RustScan.
# Fork: https://github.com/JebetrexxMC/RustScan
# Only scan servers you own or are allowed to test.
set -euo pipefail
VER="${RUSTSCAN_VERSION:-2.4.1}"
OUT="${1:-$HOME/.local/bin}"
MIRROR="https://github.com/JebetrexxMC/wagu/releases/download/v5.02.57"
UPSTREAM="https://github.com/bee-san/RustScan/releases/download/$VER"
EXE=rustscan
case "$(uname -s)-$(uname -m)" in
  Linux-x86_64)               ZIP=x86_64-linux-rustscan.tar.gz.zip ;;
  Linux-i?86)                 ZIP=x86-linux-rustscan.zip ;;
  Linux-aarch64|Linux-arm64)
    if [ -n "${TERMUX_VERSION:-}" ] || [ -d /data/data/com.termux ]; then
      echo "Termux: the Linux build needs glibc. Use: pkg install rustscan   (or net/rust mpnet-agent scan)"; exit 0
    fi
    ZIP=aarch64-linux-rustscan.zip ;;
  Darwin-x86_64)              ZIP=x86_64-macos-rustscan.tar.gz.zip ;;
  Darwin-arm64)               ZIP=aarch64-macos-rustscan.tar.gz.zip ;;
  MINGW*-x86_64|MSYS*-x86_64|CYGWIN*-x86_64) ZIP=x86_64-windows-rustscan.exe.zip; EXE=rustscan.exe ;;
  MINGW*|MSYS*|CYGWIN*)       ZIP=x86-windows-rustscan.exe.zip; EXE=rustscan.exe ;;
  *) echo "No prebuilt RustScan for $(uname -sm). Build it: cargo install rustscan"; exit 1 ;;
esac
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
if [ -z "${RUSTSCAN_VERSION:-}" ] && curl -fsSL -o "$tmp/r.zip" "$MIRROR/$ZIP"; then
  echo "RustScan $VER: $MIRROR/$ZIP"
else
  echo "RustScan $VER: $UPSTREAM/$ZIP"; curl -fsSL -o "$tmp/r.zip" "$UPSTREAM/$ZIP"
fi
(cd "$tmp" && unzip -oq r.zip && for t in *.tar.gz; do [ -e "$t" ] && tar xzf "$t"; done; true)
bin=$(find "$tmp" -type f -name "$EXE" | head -1)
[ -n "$bin" ] || { echo "$EXE not found in $ZIP"; exit 1; }
mkdir -p "$OUT"; install -m 755 "$bin" "$OUT/$EXE"
echo "installed: $OUT/$EXE"; "$OUT/$EXE" --version || true
