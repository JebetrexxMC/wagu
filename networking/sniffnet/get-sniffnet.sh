#!/bin/bash
# Download Sniffnet (network traffic monitor, MIT / Apache-2.0, https://github.com/GyulyVGC/sniffnet;
# wagu's fork: https://github.com/JebetrexxMC/sniffnet) for this PC and start it.
# The fork has no release builds, so the files come from the upstream release (same code).
#   bash get-sniffnet.sh            -> latest release for this Linux PC (AppImage), runs it
#   bash get-sniffnet.sh --deb      -> installs the .deb instead (needs sudo)
#   bash get-sniffnet.sh --print    -> only prints the download links (Windows / macOS too)
set -e
REPO=GyulyVGC/sniffnet
TAG=$(curl -fsSL "https://api.github.com/repos/$REPO/releases/latest" | sed -n 's/.*"tag_name": *"\([^"]*\)".*/\1/p' | head -1)
[ -n "$TAG" ] || TAG=v1.6.0
BASE="https://github.com/$REPO/releases/download/$TAG"
case "$(uname -m)" in x86_64) A=amd64 ;; aarch64|arm64) A=arm64 ;; armv7l) A=armhf ;; i?86) A=i386 ;; *) A=amd64 ;; esac
if [ "${1:-}" = "--print" ]; then
  echo "Sniffnet $TAG"
  echo "  Windows x64:   $BASE/Sniffnet_Windows_x64.msi"
  echo "  Windows arm64: $BASE/Sniffnet_Windows_arm64.msi"
  echo "  macOS (Apple): $BASE/Sniffnet_macOS_AppleSilicon.dmg"
  echo "  macOS (Intel): $BASE/Sniffnet_macOS_Intel.dmg"
  echo "  Linux $A:     $BASE/Sniffnet_LinuxAppImage_$A.AppImage"
  exit 0
fi
if [ "${1:-}" = "--deb" ]; then
  curl -fL -o /tmp/sniffnet.deb "$BASE/Sniffnet_LinuxDEB_$A.deb"
  sudo apt-get install -y /tmp/sniffnet.deb
  echo "Installed. Start it from the menu or run: sniffnet"
  exit 0
fi
OUT="$HOME/.local/bin/Sniffnet.AppImage"
mkdir -p "$(dirname "$OUT")"
curl -fL -o "$OUT" "$BASE/Sniffnet_LinuxAppImage_$A.AppImage"
chmod +x "$OUT"
echo "Sniffnet $TAG -> $OUT"
echo "Capturing needs the pcap permission once: sudo setcap cap_net_raw,cap_net_admin=eip \"$OUT\" (or run it with sudo)."
echo "MP filters: see networking/sniffnet/mp-filters.txt"
exec "$OUT"
