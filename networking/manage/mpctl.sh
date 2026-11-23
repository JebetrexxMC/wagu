#!/usr/bin/env bash
# mpctl - manage an MP server box (region VPS, Oracle Always Free, home Linux PC).
# Works from XPipe (networking/xpipe), plain SSH (ssh box 'bash -s' < mpctl.sh status) or locally.
set -uo pipefail
BASE="${MP_BASE:-/opt/mp}"
SRC="${MP_SRC:-$HOME/wagu}"
NET="${MP_NETBIN:-/usr/local/bin}"
SUDO=""; [ "$(id -u)" -eq 0 ] || SUDO="sudo"
MP_PORTS="1883 8883 8884 1922 1930 1931 7777 8090 8091 7835"

units() { systemctl list-units --all --no-legend --plain 'mp-*' 'hivemq*' 'mpnet-*' 2>/dev/null | awk '{print $1}'; }

cmd_status() {
  echo "== $(hostname) $(uptime -p 2>/dev/null)"
  for u in $(units); do printf '%-28s %s\n' "$u" "$(systemctl is-active "$u")"; done
  echo "== ports"; cmd_ports_local
  if curl -fsS -m 3 http://127.0.0.1:8090/health >/dev/null 2>&1; then
    echo "== mpnetd watch"; curl -fsS -m 3 http://127.0.0.1:8090/health; echo
  fi
}
cmd_ports_local() {
  for p in $MP_PORTS; do
    l=$(ss -Hlntu "( sport = :$p )" 2>/dev/null | awk '{print $1}' | sort -u | tr '\n' ' ')
    printf '  %-5s %s\n' "$p" "${l:-closed}"
  done
}
cmd_ports() {
  cmd_ports_local
  ip=$(curl -fsS -m 4 https://api.ipify.org 2>/dev/null || true)
  [ -n "$ip" ] && echo "public IP: $ip (check from outside: mpnet-agent scan $ip -p 1883,8883,8884 --udp 1922,1930,1931)"
}
cmd_restart() { $SUDO systemctl restart "${1:-mp-server}" && systemctl --no-pager status "${1:-mp-server}" | head -5; }
cmd_start()   { $SUDO systemctl start "${1:-mp-server}"; }
cmd_stop()    { $SUDO systemctl stop "${1:-mp-server}"; }
cmd_logs()    { journalctl -u "${1:-mp-server}" -n "${2:-100}" --no-pager; }

cmd_update() {
  if [ ! -d "$SRC/.git" ]; then
    git clone --depth 1 --filter=blob:none --sparse https://github.com/JebetrexxMC/wagu "$SRC" &&
      git -C "$SRC" sparse-checkout set networking src/net
  else git -C "$SRC" pull --ff-only; fi
  make -C "$SRC/networking" host && $SUDO install -m 755 "$SRC/networking/build/host/mpnetd" "$NET/mpnetd" &&
    echo "mpnetd updated: $NET/mpnetd"
  systemctl is-active -q mpnet-watch 2>/dev/null && $SUDO systemctl restart mpnet-watch
  systemctl is-active -q mpnet-relay 2>/dev/null && $SUDO systemctl restart mpnet-relay
  true
}

cmd_netwire() {
  # mpnetd watch on :8090 and a relay on :7777 (status :8091). RELAY_UP="host:port host2:port2", RELAY_KIND=mp|samp|udp
  [ -x "$NET/mpnetd" ] || cmd_update
  up="${RELAY_UP:-mpsamp.duckdns.org:1922 eusamp.duckdns.org:1922}"; args=""
  for u in $up; do args="$args -u $u"; done
  $SUDO mkdir -p /var/lib/mpnet
  $SUDO tee /etc/systemd/system/mpnet-watch.service >/dev/null <<UNIT
[Unit]
Description=MP NetWire watch (status on :8090)
After=network-online.target
Wants=network-online.target
[Service]
ExecStart=$NET/mpnetd watch -w $SRC/networking/wires.txt -o /var/lib/mpnet -i 60 --http 8090
Restart=always
RestartSec=5
DynamicUser=yes
StateDirectory=mpnet
[Install]
WantedBy=multi-user.target
UNIT
  $SUDO tee /etc/systemd/system/mpnet-relay.service >/dev/null <<UNIT
[Unit]
Description=MP NetWire relay (:7777 -> fastest upstream, status :8091)
After=network-online.target
Wants=network-online.target
[Service]
ExecStart=$NET/mpnetd relay -l ${RELAY_PORT:-7777}$args --kind ${RELAY_KIND:-mp} --http 8091
Restart=always
RestartSec=5
DynamicUser=yes
[Install]
WantedBy=multi-user.target
UNIT
  $SUDO systemctl daemon-reload && $SUDO systemctl enable --now mpnet-watch mpnet-relay
  systemctl --no-pager is-active mpnet-watch mpnet-relay
}

cmd_zerotier() { $SUDO bash "$SRC/networking/zerotier/zerotier-join.sh" "${1:-}"; }

cmd_tunnel() {
  # share a local TCP port (MQTT 1883, status 8090, SSH 22...) via bore; UDP game ports need ZeroTier / playit
  port="${1:?usage: mpctl tunnel LOCAL_PORT [bore-server]}"; to="${2:-${BORE_SERVER:-bore.pub}}"
  if [ -x "$SRC/networking/rust/target/release/mpnet-agent" ]; then
    exec "$SRC/networking/rust/target/release/mpnet-agent" tunnel --local-port "$port" --to "$to"
  elif command -v bore >/dev/null; then exec bore local "$port" --to "$to"
  else echo "need bore (cargo install bore-cli) or mpnet-agent (cd networking/rust && cargo build --release)"; exit 1; fi
}

usage() {
  cat <<U
mpctl status | ports | start|stop|restart|logs [unit] | update | netwire | zerotier NETWORK_ID | tunnel PORT [server]
  units default to mp-server. MP_BASE=$BASE MP_SRC=$SRC
U
}
c="${1:-status}"; shift || true
case "$c" in
  status|ports|start|stop|restart|logs|update|netwire|zerotier|tunnel) "cmd_$c" "$@" ;;
  *) usage ;;
esac
