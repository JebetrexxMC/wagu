#!/usr/bin/env bash
# MP VPS: build + install xdp-firewall (JebetrexxMC/xdp-firewall) with the MP rules from gen_mp_xdpfw.py.
#
#   sudo bash install-vps.sh                 # build, install, write /etc/xdpfw/xdpfw.conf, start xdpfw.service
#   sudo bash install-vps.sh --iface ens3    # other interface (default: the default route's)
#   sudo bash install-vps.sh --keep-mp-xdp   # keep HiveMQ/xdp (mp-xdp.service) loaded too (see README)
#   sudo bash install-vps.sh conf            # only rewrite the config (xdpfw reloads it within 15 s)
#   sudo bash install-vps.sh status | off
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC=/opt/xdp-firewall
REPO=https://github.com/JebetrexxMC/xdp-firewall.git
CONF=/etc/xdpfw/xdpfw.conf
CMD=install; IFACE=""; KEEP=0; EXTRA=()
while [ $# -gt 0 ]; do
  case "$1" in
    install|conf|status|off) CMD=$1 ;;
    --iface) IFACE=$2; shift ;;
    --keep-mp-xdp) KEEP=1 ;;
    *) EXTRA+=("$1") ;;   # passed to gen_mp_xdpfw.py (--allow IP, --pps N, --syn N, --block S)
  esac; shift
done
[ "$(id -u)" = 0 ] || { echo "run as root (sudo)"; exit 1; }
log() { echo "[mp-xdpfw] $*"; }

write_conf() {
  mkdir -p /etc/xdpfw
  python3 "$HERE/gen_mp_xdpfw.py" ${IFACE:+--iface "$IFACE"} "${EXTRA[@]}" --out "$CONF"
}

case "$CMD" in
  conf) write_conf; exit 0 ;;
  status)
    systemctl --no-pager status xdpfw | head -5 || true
    ip -d link show | grep -E "^[0-9]+:|prog/xdp" || true
    ls /sys/fs/bpf/xdpfw 2>/dev/null && echo "maps pinned: /sys/fs/bpf/xdpfw"
    journalctl -u xdpfw -n 5 --no-pager 2>/dev/null || true
    exit 0 ;;
  off) systemctl disable --now xdpfw 2>/dev/null || true; log "xdpfw stopped (the service detaches XDP on stop)"; exit 0 ;;
esac

export DEBIAN_FRONTEND=noninteractive
log "dependencies"
apt-get update -qq
apt-get install -y -qq git python3 libconfig-dev llvm clang libelf-dev build-essential libpcap-dev m4 gcc-multilib >/dev/null
apt-get install -y -qq "linux-tools-$(uname -r)" >/dev/null 2>&1 || apt-get install -y -qq bpftool >/dev/null 2>&1 || true

if [ -d "$SRC/.git" ]; then git -C "$SRC" pull -q --ff-only; git -C "$SRC" submodule update --init -q
else git clone -q --recursive "$REPO" "$SRC"; fi

# per source IP limits (ip_pps rules) are off by default in the build-time config
sed -i 's|^//#define ENABLE_RL_IP$|#define ENABLE_RL_IP|' "$SRC/src/common/config.h"
grep -q '^#define ENABLE_RL_IP$' "$SRC/src/common/config.h" || { log "could not enable ENABLE_RL_IP in config.h"; exit 1; }

log "build (LibXDP + firewall)"
(cd "$SRC" && ./install.sh --libxdp)

if [ "$KEEP" = 0 ] && systemctl is-active -q mp-xdp 2>/dev/null; then
  log "stopping mp-xdp.service (xdpfw does the same size + rate checks; --keep-mp-xdp to keep both)"
  systemctl disable --now mp-xdp || true
fi

write_conf
install -m 644 "$SRC/other/xdpfw.service" /etc/systemd/system/xdpfw.service
systemctl daemon-reload
systemctl enable --now xdpfw
sleep 2
systemctl is-active -q xdpfw && log "xdpfw running on $(grep -E '^interface' "$CONF")" || { journalctl -u xdpfw -n 20 --no-pager; exit 1; }
log "ban an IP for good: xdpfw-add -m 2 -d 198.51.100.9   |   unban: xdpfw-del -m 2 -d 198.51.100.9"
