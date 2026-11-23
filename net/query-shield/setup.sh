#!/usr/bin/env bash
# MP query shield for an open.mp / SA-MP server on a Linux VPS (run as root).
# Query packets ("SAMP" ... i/r/c/d/p) are redirected by iptables to a cache on PROXY_PORT; game traffic
# stays on the server port untouched. The cache answers from a copy it refreshes itself, so a query flood
# doesn't make the server show "offline" in the browser / MP lobby.
#   bash setup.sh --ip 1.2.3.4 --port 7777                 # pack_scan.py (JebetrexxMC/samp-packet-proxy)
#   bash setup.sh --ip 1.2.3.4 --port 7777 --mode mpedge   # net/go/mpedge (gnet), same iptables rules
#   bash setup.sh --port 7777 --remove                     # take the rules + service away
set -euo pipefail
IP=""; PORT=7777; PROXY=7778; MODE=pack_scan; RATE=10; REMOVE=0
while [ $# -gt 0 ]; do
  case "$1" in
    --ip) IP=$2; shift ;;
    --port) PORT=$2; shift ;;
    --proxy-port) PROXY=$2; shift ;;
    --mode) MODE=$2; shift ;;
    --rate) RATE=$2; shift ;;
    --remove) REMOVE=1 ;;
    *) echo "unknown: $1"; exit 2 ;;
  esac; shift
done
[ "$(id -u)" = 0 ] || { echo "run as root"; exit 1; }
R1=(-t nat PREROUTING -p udp --dport "$PORT" -m string --algo bm --string SAMP -j REDIRECT --to-port "$PROXY")
R2=(INPUT -p udp --dport "$PROXY" -m string --algo bm --string SAMP -m hashlimit '!' --hashlimit-upto "$RATE/sec" --hashlimit-burst "$((RATE + 5))" --hashlimit-mode srcip --hashlimit-name mpquery -j DROP)
rule() { # rule add|del <args>: idempotent
  local op=$1; shift
  local t=(); if [ "$1" = -t ]; then t=(-t "$2"); shift 2; fi
  local chain=$1; shift
  if iptables "${t[@]}" -C "$chain" "$@" 2>/dev/null; then
    [ "$op" = del ] && iptables "${t[@]}" -D "$chain" "$@"
  else
    [ "$op" = add ] && iptables "${t[@]}" -I "$chain" "$@"
  fi
  return 0
}
UNIT=/etc/systemd/system/mp-query-shield.service
if [ $REMOVE = 1 ]; then
  rule del "${R1[@]}"; rule del "${R2[@]}"
  systemctl disable --now mp-query-shield 2>/dev/null || true; rm -f "$UNIT"; systemctl daemon-reload
  echo "removed"; exit 0
fi
[ -n "$IP" ] || { echo "--ip <server public ip> is needed"; exit 2; }
mkdir -p /opt/mp-query-shield
case "$MODE" in
  pack_scan)
    curl -fsSL https://raw.githubusercontent.com/JebetrexxMC/samp-packet-proxy/master/pack_scan.py -o /opt/mp-query-shield/pack_scan.py
    sed -i -e "s/^SERVER_PORT = .*/SERVER_PORT = $PORT/" -e "s/^PROXY_PORT = .*/PROXY_PORT = $PROXY/" \
           -e "s/^SAMP_SERVER_ADDRESS = .*/SAMP_SERVER_ADDRESS = \"$IP\"/" /opt/mp-query-shield/pack_scan.py
    CMD="/usr/bin/python3 /opt/mp-query-shield/pack_scan.py" ;;
  mpedge)
    command -v mpedge >/dev/null || { echo "build it first: cd net/go && go build -o /usr/local/bin/mpedge ./mpedge"; exit 1; }
    CMD="$(command -v mpedge) -listen :$PROXY -backend 127.0.0.1:$PORT" ;;
  *) echo "--mode pack_scan or mpedge"; exit 2 ;;
esac
cat > "$UNIT" <<UNIT
[Unit]
Description=MP query shield ($MODE) for udp $PORT
After=network-online.target
[Service]
ExecStart=$CMD
Restart=always
RestartSec=2
[Install]
WantedBy=multi-user.target
UNIT
systemctl daemon-reload; systemctl enable --now mp-query-shield
rule add "${R1[@]}"; rule add "${R2[@]}"
echo "query shield on: udp $PORT queries -> $MODE on $PROXY, max $RATE queries/s per IP"
echo "the rules are not saved across reboots: apt install iptables-persistent && netfilter-persistent save"
