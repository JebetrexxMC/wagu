#!/usr/bin/env bash
# Put a VPS / Oracle box / Linux PC on your ZeroTier network so MP LAN Server and mpnetd can be reached
# by ZeroTier IP from every phone and PC on the same network (no port forwarding, no public IP).
# Usage: sudo bash zerotier-join.sh <NETWORK_ID>
# Fork: https://github.com/JebetrexxMC/ZeroTierOne (official installer used here).
set -euo pipefail
NET="${1:-${ZEROTIER_NETWORK:-}}"
[ -n "$NET" ] || { echo "usage: sudo bash $0 <16-hex NETWORK_ID>  (create one free at https://my.zerotier.com)"; exit 1; }
[[ "$NET" =~ ^[0-9a-fA-F]{16}$ ]] || { echo "network id must be 16 hex characters"; exit 1; }
command -v zerotier-cli >/dev/null || curl -fsSL https://install.zerotier.com | bash
systemctl enable --now zerotier-one 2>/dev/null || true
zerotier-cli join "$NET"
echo "joined $NET - approve this device in my.zerotier.com (Members), then:"
for i in $(seq 1 30); do
  ip=$(zerotier-cli get "$NET" ip 2>/dev/null || true)
  [ -n "$ip" ] && [ "$ip" != "-" ] && { echo "ZeroTier IP: $ip"; echo "add to net/wires.txt:"; echo "zt-$(hostname -s) | zerotier | mp | $ip:1922 | MP over ZeroTier"; exit 0; }
  sleep 2
done
echo "no IP yet (not approved?) - check: zerotier-cli listnetworks"
