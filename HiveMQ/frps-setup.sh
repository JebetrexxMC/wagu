#!/usr/bin/env bash
# 5.02.52: our own open-source PC tunnel server for MP Crossplay 24/7 (frp, github.com/fatedier/frp).
# Run once as root on the always-on box with a public IP (the HiveMQ / 24/7 VPS, e.g. mpsamp.duckdns.org):
#   curl -fsSL https://mpsamp-open.github.io/HiveMQ/frps-setup.sh | sudo bash
# It installs frps as a systemd service: TCP 7000 = the 24/7 host connects here (frpc, token auth),
# UDP 7778 = PC players (open.mp / SA-MP), forwarded to the open.mp crossplay server on the 24/7 host.
# Then: repo secret FRP_TOKEN = the token it prints, repo variable FRP_SERVER = <this box>:7000.
# Open on the firewall / router: TCP 7000 and UDP 7778.
set -eu
V="${FRP_VERSION:-0.72.0}"
case "$(uname -m)" in x86_64) A=amd64 ;; aarch64|arm64) A=arm64 ;; armv7l) A=arm_hf ;; *) echo "unsupported CPU $(uname -m)"; exit 1 ;; esac
say() { printf '\033[1;32m[mp-frps]\033[0m %s\n' "$*"; }
[ "$(id -u)" = 0 ] || { echo "run as root (sudo bash)"; exit 1; }
say "installing frps $V ($A)"
t=$(mktemp -d); curl -fsSL "https://github.com/fatedier/frp/releases/download/v$V/frp_${V}_linux_$A.tar.gz" | tar -xz -C "$t"
install -m 755 "$t/frp_${V}_linux_$A/frps" /usr/local/bin/frps; rm -rf "$t"
mkdir -p /etc/mp-frps
[ -s /etc/mp-frps/token ] || head -c 24 /dev/urandom | od -An -tx1 | tr -d ' \n' > /etc/mp-frps/token
chmod 600 /etc/mp-frps/token
cat > /etc/mp-frps/frps.toml <<CFG
bindPort = ${FRP_BIND_PORT:-7000}
auth.method = "token"
auth.token = "$(cat /etc/mp-frps/token)"
allowPorts = [{ start = 7778, end = 7799 }]
transport.tcpMux = true
CFG
chmod 600 /etc/mp-frps/frps.toml
cat > /etc/systemd/system/mp-frps.service <<'UNIT'
[Unit]
Description=MP Crossplay PC tunnel (frps)
After=network-online.target
Wants=network-online.target
[Service]
ExecStart=/usr/local/bin/frps -c /etc/mp-frps/frps.toml
Restart=always
RestartSec=3
[Install]
WantedBy=multi-user.target
UNIT
systemctl daemon-reload; systemctl enable --now mp-frps; systemctl restart mp-frps
command -v ufw > /dev/null && ufw status | grep -q active && { ufw allow 7000/tcp; ufw allow 7778:7799/udp; } || true
sleep 1; systemctl --no-pager --lines=0 status mp-frps | grep -E 'Active:' || true
say "done. Add these to github.com/JebetrexxMC/wagu -> Settings -> Secrets and variables -> Actions:"
say "  secret   FRP_TOKEN  = $(cat /etc/mp-frps/token)"
say "  variable FRP_SERVER = $(curl -fsS4 --max-time 5 https://api.ipify.org 2>/dev/null || hostname -I | awk '{print $1}'):${FRP_BIND_PORT:-7000}   (or your duckdns name:7000)"
say "open TCP ${FRP_BIND_PORT:-7000} + UDP 7778 on the router / cloud firewall. PC players join <this box>:7778"
