#!/usr/bin/env bash
# NetWire 1.4 - home broker + public lobby TOGETHER (Termux phone or home Linux box).
# Runs MpBridge (HiveMQ/bridge/MpBridge.java, "mirror" mode) next to the home broker:
#   - the 24/7 servers on the home broker are also listed in the public lobby (broker.hivemq.com,
#     test.mosquitto.org, broker.emqx.io) and their game traffic is carried both ways, so players on
#     the public lobby (~200 ms, the website / GitHub network) and players on the home region play together;
#   - home Wi-Fi offline: the public lobby keeps going on its own (the game is already there - 5.02.72 standby);
#   - Wi-Fi back: the bridge reconnects by itself (1 s .. 30 s backoff) and lists the home servers in the
#     public lobby again - both networks back together, nobody has to do anything.
#
#   home-bridge.sh on [region]   start + keep running (Termux: loop + Termux:Boot, Linux: systemd). region: asia(0) europe(1) na(2) oceania(3)
#   home-bridge.sh off | status | log
# Login on the home broker: MP_USER / MP_PASS (default: the MP login termux-broker.sh makes), MP_HOME=host:port (127.0.0.1:1883).
set -uo pipefail
RAW=https://raw.githubusercontent.com/JebetrexxMC/wagu/main
TERMUX=0; case "${PREFIX:-}" in *com.termux*) TERMUX=1;; esac
if [ $TERMUX = 1 ]; then D="$HOME/mp-broker/bridge"; else D="${MP_BRIDGE_DIR:-/opt/mp-homebridge}"; fi
U="${MP_USER:-mp}"; PW="${MP_PASS:-M6oehkIzqcJKSTalT8nL}"; HOME_BR="${MP_HOME:-127.0.0.1:1883}"
PUBS="${MP_PUBLIC:-broker.hivemq.com:1883 test.mosquitto.org:1883 broker.emqx.io:1883}"
JAVA="${JAVA:-java}"

region() { case "${1:-asia}" in 0|asia|Asia) echo 0;; 1|europe|eu) echo 1;; 2|na|NA) echo 2;; 3|oceania|oc) echo 3;; *) echo 0;; esac; }
setup() {
  mkdir -p "$D" || { echo "cannot write $D (run as root or set MP_BRIDGE_DIR)"; exit 1; }
  if ! command -v "$JAVA" >/dev/null; then
    echo "[MP] installing Java 21 (one time)"
    if [ $TERMUX = 1 ]; then pkg install -y openjdk-21; else DEBIAN_FRONTEND=noninteractive apt-get install -y -qq openjdk-21-jre-headless || apt-get install -y -qq openjdk-17-jre-headless; fi
  fi
  if [ -n "${MP_BRIDGE_SRC:-}" ]; then cp "$MP_BRIDGE_SRC" "$D/MpBridge.java"
  elif curl -fsSL "$RAW/HiveMQ/bridge/MpBridge.java" -o "$D/MpBridge.java.new"; then mv "$D/MpBridge.java.new" "$D/MpBridge.java"; fi
  [ -s "$D/MpBridge.java" ] || { echo "could not get MpBridge.java"; exit 1; }
  { echo "# home-bridge.sh (NetWire 1.4): home broker + public lobby together"
    echo "Home = mqtt://$U:$PW@$HOME_BR"
    for p in $PUBS; do echo "mirror = mqtt://$p"; done
    echo "mirror_region = $(region "${1:-asia}")"
    echo "expire = 45"; } > "$D/bridge.conf"
  chmod 600 "$D/bridge.conf"
}
running() { pgrep -f "MpBridge.java $D/bridge.conf" >/dev/null; }

case "${1:-status}" in
  on)
    setup "${2:-asia}"
    if [ $TERMUX = 0 ] && command -v systemctl >/dev/null && [ "$(id -u)" = 0 ]; then
      printf '[Unit]\nDescription=MP home bridge (home broker + public lobby)\nAfter=network-online.target mosquitto.service\nWants=network-online.target\n[Service]\nExecStart=%s %s/MpBridge.java %s/bridge.conf\nRestart=always\nRestartSec=5\n[Install]\nWantedBy=multi-user.target\n' \
        "$(command -v "$JAVA")" "$D" "$D" > /etc/systemd/system/mp-home-bridge.service
      systemctl daemon-reload; systemctl enable -q mp-home-bridge; systemctl restart mp-home-bridge
      echo "mp-home-bridge running (systemd, restarts by itself). Log: home-bridge.sh log"
    else
      cat > "$D/loop.sh" <<L
#!/usr/bin/env bash
# restarts MpBridge within 5 s if it stops (no Wi-Fi, phone asleep, crash)
while [ -f "$D/on" ]; do
  "$JAVA" "$D/MpBridge.java" "$D/bridge.conf" >> "$D/bridge.log" 2>&1
  tail -n 400 "$D/bridge.log" > "$D/bridge.log.t" && mv "$D/bridge.log.t" "$D/bridge.log"
  sleep 5
done
L
      chmod +x "$D/loop.sh"; touch "$D/on"
      pgrep -f "$D/loop.sh" >/dev/null || { command -v termux-wake-lock >/dev/null && termux-wake-lock; nohup "$D/loop.sh" >/dev/null 2>&1 & }
      if [ $TERMUX = 1 ]; then mkdir -p "$HOME/.termux/boot"; printf '#!%s/bin/bash\n[ -f %s/on ] && nohup %s/loop.sh >/dev/null 2>&1 &\n' "$PREFIX" "$D" "$D" > "$HOME/.termux/boot/mp-home-bridge"; chmod +x "$HOME/.termux/boot/mp-home-bridge"; fi
      echo "home bridge running in the background (restarts by itself$([ $TERMUX = 1 ] && echo ', starts after a phone reboot with Termux:Boot')). Log: home-bridge.sh log"
    fi ;;
  off)
    rm -f "$D/on"; pkill -f "$D/loop.sh" 2>/dev/null; pkill -f "MpBridge.java $D/bridge.conf" 2>/dev/null
    command -v systemctl >/dev/null && systemctl disable --now -q mp-home-bridge 2>/dev/null; echo "home bridge stopped" ;;
  status)
    if running || systemctl is-active -q mp-home-bridge 2>/dev/null; then echo "home bridge: running"; else echo "home bridge: stopped (home-bridge.sh on)"; fi
    L="$D/bridge.log"; [ -f "$L" ] || L=/dev/null
    { [ -s "$L" ] && cat "$L"; journalctl -u mp-home-bridge -n 200 -o cat 2>/dev/null; } | grep -E 'connected|lost|public lobby|status' | tail -n 6 ;;
  log) { tail -n 40 "$D/bridge.log" 2>/dev/null; journalctl -u mp-home-bridge -n 40 -o cat --no-pager 2>/dev/null; } ;;
  *) sed -n '2,15p' "$0" ;;
esac
