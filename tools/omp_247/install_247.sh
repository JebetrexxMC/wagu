#!/usr/bin/env bash
# 5.02.18: install openmp_247.sh as a systemd service (24/7 open.mp / SA-MP server + backups + HiveMQ status).
# 5.02.20: works as a single downloaded file (fetches openmp_247.sh / omp_status.py / omp_autobot.py itself),
#          installs the MQTT autobot (omp-autobot), any broker via MQTT_HOST / MQTT_USER / MQTT_PASS.
# Run as root on the server:
#   curl -fsSLO https://github.com/JebetrexxMC/wagu/releases/latest/download/install_247.sh
#   sudo bash install_247.sh /path/to/Server [name] [port]
#     /path/to/Server  folder with omp-server (or samp03svr) + config.json / server.cfg
#     name             status name -> topic thirtyfox/mp4/omp/<name>/status   (default: myserver)
#     port             server port                                              (default: 7777)
#   sudo bash install_247.sh --bot        only the autobot (then: "install <name> <port> <dir>" over MQTT)
# Broker: the MP HiveMQ kit (/etc/mp-hivemq/mp.env, user bridge) is used by itself. Another broker
# (HiveMQ Cloud etc.): MQTT_HOST=xxx.hivemq.cloud MQTT_PORT=8883 MQTT_TLS=1 MQTT_USER=u MQTT_PASS=p sudo -E bash install_247.sh ...
# No autobot: OMP_BOT=0. Extra safety: BOT_TOKEN=secret (every command must start with it).
# Uninstall: systemctl disable --now omp-247-<name> omp-autobot; rm /etc/systemd/system/omp-247-<name>.service
set -eu
RAW=https://raw.githubusercontent.com/JebetrexxMC/wagu/main/tools/omp_247
if [ -n "${PREFIX:-}" ] && [ -d "${PREFIX:-}" ] && case "$PREFIX" in *com.termux*) true;; *) false;; esac; then
  echo "This is Termux. open.mp servers are x86-64 Linux programs (no Android build), so they run on your"
  echo "VPS / PC, not on the phone. From Termux, control the server's autobot instead:"
  echo "  pkg install python && curl -fsSLO $RAW/omp_autobot.py"
  echo "  python3 omp_autobot.py send -H <broker> -u admin -P '<pass>' install myserver 7777 /home/samp/Server"
  echo "or run this installer on the server over SSH:  ssh root@<server> 'curl -fsSLO $RAW/install_247.sh && bash install_247.sh /home/samp/Server myserver 7777'"
  exit 1
fi
[ "$(id -u)" = 0 ] || { echo "run as root (sudo)"; exit 1; }
command -v systemctl >/dev/null || { echo "needs systemd (systemctl) - on other systems run openmp_247.sh by hand"; exit 1; }
BOTONLY=0; [ "${1:-}" = "--bot" ] && { BOTONLY=1; shift; }
HERE=$(cd "$(dirname "$0")" && pwd)

# tools: python3 (health check, status, autobot), curl, script (console FIFO); mosquitto-clients for <topic>/cmd
if command -v apt-get >/dev/null; then
  need=""; command -v python3 >/dev/null || need="$need python3"; command -v script >/dev/null || need="$need bsdutils"
  command -v curl >/dev/null || need="$need curl"; command -v mosquitto_sub >/dev/null || need="$need mosquitto-clients"
  [ -n "$need" ] && { apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq $need || echo "warning: could not install$need"; }
fi

ETC=/etc/omp-247; LIB=/opt/omp-247; mkdir -p "$ETC" "$LIB"
for f in openmp_247.sh omp_status.py omp_autobot.py install_247.sh; do   # next to this file, else the newest from GitHub
  if [ -f "$HERE/$f" ]; then [ "$HERE/$f" -ef "$LIB/$f" ] || install -m 755 "$HERE/$f" "$LIB/$f"
  else curl -fsSL "$RAW/$f" -o "$LIB/$f.new" && chmod 755 "$LIB/$f.new" && mv -f "$LIB/$f.new" "$LIB/$f" || { echo "could not download $f"; exit 1; }; fi
done

# broker: env vars > the MP HiveMQ kit > off
MQH=${MQTT_HOST:-}; MQP=${MQTT_PORT:-1883}; MQU=${MQTT_USER:-}; MQW=${MQTT_PASS:-}; MQE=""; MQTLS=${MQTT_TLS:-0}
if [ -z "$MQH" ] && [ -r /etc/mp-hivemq/mp.env ] && grep -q '^BRIDGE_PASS=' /etc/mp-hivemq/mp.env; then
  MQH=127.0.0.1; MQU=bridge; MQE=BRIDGE_PASS
fi

bot() {   # the MQTT autobot (root: it installs / restarts the 24/7 services)
  [ "${OMP_BOT:-1}" = 0 ] && return 0
  [ -n "$MQH" ] || { echo "autobot: no broker (no /etc/mp-hivemq/mp.env, no MQTT_HOST) - skipped"; return 0; }
  local name; name=$(hostname -s 2>/dev/null || hostname); name=${BOT_NAME:-$name}
  umask 077
  { echo "MQTT_HOST=$MQH"; echo "MQTT_PORT=$MQP"; echo "MQTT_USER=$MQU"; [ -n "$MQW" ] && echo "MQTT_PASS=$MQW"
    echo "MQTT_TLS=$MQTLS"; echo "BOT_NAME=$name"; [ -n "${BOT_TOKEN:-}" ] && echo "BOT_TOKEN=$BOT_TOKEN"; } > "$ETC/autobot.env"
  umask 022
  cat > /etc/systemd/system/omp-autobot.service <<EOF
[Unit]
Description=omp_autobot - open.mp 24/7 servers over MQTT (thirtyfox/mp4/omp/bot/$name/cmd)
After=network-online.target hivemq.service
Wants=network-online.target

[Service]
ExecStart=/usr/bin/python3 $LIB/omp_autobot.py serve
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
EOF
  systemctl daemon-reload; systemctl enable -q omp-autobot; systemctl restart omp-autobot
  echo "autobot: omp-autobot running - commands on thirtyfox/mp4/omp/bot/$name/cmd, answers on .../$name/reply"
}

if [ "$BOTONLY" = 1 ]; then bot; exit 0; fi

SRV=$(readlink -f "${1:?usage: install_247.sh /path/to/Server [name] [port]   or   install_247.sh --bot}")
NAME=${2:-myserver}; PORT=${3:-7777}
case "$NAME" in bot|*[!A-Za-z0-9_-]*|"") echo "name: letters, digits, _ and - only (not \"bot\")"; exit 1;; esac
BIN=omp-server; [ -x "$SRV/omp-server" ] || { [ -x "$SRV/samp03svr" ] && BIN=samp03svr; }
[ -x "$SRV/$BIN" ] || { echo "no omp-server / samp03svr in $SRV"; exit 1; }
OWNER=$(stat -c %U "$SRV")
mkdir -p /var/backups/omp-247/"$NAME"; chown "$OWNER" /var/backups/omp-247/"$NAME"
if [ "$MQE" = BRIDGE_PASS ]; then   # the runner (not root) reads BRIDGE_PASS from mp.env
  chmod 640 /etc/mp-hivemq/mp.env; chgrp "$(id -gn "$OWNER")" /etc/mp-hivemq/mp.env 2>/dev/null || true
fi
PUB=$(hostname -I 2>/dev/null | awk '{print $1}')

CFG="$ETC/$NAME.yml"
if [ -e "$CFG" ]; then echo "keeping existing $CFG"; else
cat > "$CFG" <<EOF
# written by install_247.sh - see $LIB/openmp_247.sh for every key
server_dir: $SRV
server_bin: $BIN
host: 127.0.0.1
port: $PORT
health_every: 15
health_fails: 4
restart_delay: 3
max_restarts_hour: 30
backup_every: 1800
backup_keep: 48
backup_dir: /var/backups/omp-247/$NAME
console_fifo: $SRV/omp_console
state_file: $SRV/omp_247.state
mqtt_host: $MQH
mqtt_port: $MQP
mqtt_tls: $MQTLS
mqtt_user: $MQU
mqtt_pass: $MQW
mqtt_pass_env: $MQE
mqtt_topic: thirtyfox/mp4/omp/$NAME
name: $NAME
public_host: $PUB
status_every: 10
EOF
[ -n "$MQW" ] && { chown "$OWNER" "$CFG"; chmod 600 "$CFG"; }
fi

UNIT=/etc/systemd/system/omp-247-$NAME.service
cat > "$UNIT" <<EOF
[Unit]
Description=open.mp server 24/7 ($NAME) - MP openmp_247.sh
After=network-online.target hivemq.service
Wants=network-online.target

[Service]
Type=simple
User=$OWNER
WorkingDirectory=$SRV
ExecStart=/bin/bash $LIB/openmp_247.sh $CFG
Restart=always
RestartSec=5
KillMode=control-group
TimeoutStopSec=30

[Install]
WantedBy=multi-user.target
EOF
systemctl daemon-reload
systemctl enable --now "omp-247-$NAME"
bot
sleep 2; systemctl --no-pager --lines=5 status "omp-247-$NAME" || true
echo
echo "Installed: omp-247-$NAME   config $CFG   backups /var/backups/omp-247/$NAME"
echo "  logs:     journalctl -u omp-247-$NAME -f"
echo "  console:  echo 'gmx' > $SRV/omp_console"
[ -n "$MQH" ] && echo "  status:   thirtyfox/mp4/omp/$NAME/status on $MQH:$MQP" \
             || echo "  MQTT off: no /etc/mp-hivemq/mp.env - set mqtt_host/mqtt_user/mqtt_pass in $CFG, then systemctl restart omp-247-$NAME"
