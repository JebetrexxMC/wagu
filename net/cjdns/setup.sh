#!/usr/bin/env bash
# cjdns (fork github.com/JebetrexxMC/cjdns): build, config, systemd. Root, Debian-based.
set -euo pipefail
[ "$(id -u)" = 0 ] || { echo "run as root"; exit 1; }
CONF=/etc/cjdroute.conf
if [ "${1:-}" = peer ]; then
  [ $# -eq 4 ] || { echo "setup.sh peer <ip:port> <password> <publicKey>"; exit 2; }
  python3 - "$CONF" "$2" "$3" "$4" <<'PY'
import json, re, sys
p, addr, pw, key = sys.argv[1:]
txt = open(p).read()
txt = re.sub(r'(?m)^\s*//.*$', '', txt)                          # cjdroute.conf allows // comments
txt = re.sub(r'/\*.*?\*/', '', txt, flags=re.S)
c = json.loads(txt)
c["interfaces"]["UDPInterface"][0].setdefault("connectTo", {})[addr] = {"password": pw, "publicKey": key}
json.dump(c, open(p, "w"), indent=4)
print("peer added:", addr)
PY
  systemctl restart cjdns; exit 0
fi
apt-get update -qq && apt-get install -y -qq git build-essential python3 nodejs curl
command -v cargo >/dev/null || { curl -fsSL https://sh.rustup.rs | sh -s -- -y; . "$HOME/.cargo/env"; }
[ -d /opt/cjdns ] || git clone --depth 1 https://github.com/JebetrexxMC/cjdns /opt/cjdns
cd /opt/cjdns && ./do
install -m 755 cjdroute /usr/local/bin/cjdroute
[ -f "$CONF" ] || { /usr/local/bin/cjdroute --genconf > "$CONF"; chmod 600 "$CONF"; }
cat > /etc/systemd/system/cjdns.service <<'UNIT'
[Unit]
Description=cjdns mesh (MP admin lanes)
After=network-online.target
[Service]
ExecStart=/bin/sh -c '/usr/local/bin/cjdroute --nobg < /etc/cjdroute.conf'
Restart=always
[Install]
WantedBy=multi-user.target
UNIT
systemctl daemon-reload && systemctl enable --now cjdns
sleep 2; ip -6 addr show tun0 | grep -o 'fc[0-9a-f:]*' | head -1
