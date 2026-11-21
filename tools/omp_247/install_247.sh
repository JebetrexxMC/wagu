#!/usr/bin/env bash
# 5.02.18: install openmp_247.sh as a systemd service (24/7 open.mp / SA-MP server + backups + HiveMQ status).
# Run as root on the server:
#   sudo bash tools/omp_247/install_247.sh /path/to/Server [name] [port]
#     /path/to/Server  folder with omp-server (or samp03svr) + config.json / server.cfg
#     name             status name -> topic thirtyfox/mp4/omp/<name>   (default: myserver)
#     port             server port                                      (default: 7777)
# With the MP HiveMQ kit installed (/etc/mp-hivemq/mp.env) the status goes to the local broker
# as user "bridge" (password BRIDGE_PASS from mp.env). Without it MQTT stays off (edit the yml).
# Uninstall: systemctl disable --now omp-247-<name>; rm /etc/systemd/system/omp-247-<name>.service
set -eu
[ "$(id -u)" = 0 ] || { echo "run as root (sudo)"; exit 1; }
SRV=$(readlink -f "${1:?usage: install_247.sh /path/to/Server [name] [port]}")
NAME=${2:-myserver}; PORT=${3:-7777}
case "$NAME" in *[!A-Za-z0-9_-]*) echo "name: letters, digits, _ and - only"; exit 1;; esac
HERE=$(cd "$(dirname "$0")" && pwd)
BIN=omp-server; [ -x "$SRV/omp-server" ] || { [ -x "$SRV/samp03svr" ] && BIN=samp03svr; }
[ -x "$SRV/$BIN" ] || { echo "no omp-server / samp03svr in $SRV"; exit 1; }
OWNER=$(stat -c %U "$SRV")

# tools: python3 (health check + status), script (console FIFO); mosquitto-clients only for <topic>/cmd
if command -v apt-get >/dev/null; then
  need=""; command -v python3 >/dev/null || need="$need python3"; command -v script >/dev/null || need="$need bsdutils"
  command -v mosquitto_sub >/dev/null || need="$need mosquitto-clients"
  [ -n "$need" ] && { apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq $need || echo "warning: could not install$need"; }
fi

ETC=/etc/omp-247; LIB=/opt/omp-247; mkdir -p "$ETC" "$LIB" /var/backups/omp-247/"$NAME"
install -m 755 "$HERE/openmp_247.sh" "$LIB/openmp_247.sh"
install -m 755 "$HERE/omp_status.py" "$LIB/omp_status.py"
chown "$OWNER" /var/backups/omp-247/"$NAME"

MQH=""; MQU=""; MQE=""
if [ -r /etc/mp-hivemq/mp.env ] && grep -q '^BRIDGE_PASS=' /etc/mp-hivemq/mp.env; then
  MQH=127.0.0.1; MQU=bridge; MQE=BRIDGE_PASS
  chmod 640 /etc/mp-hivemq/mp.env; chgrp "$(id -gn "$OWNER")" /etc/mp-hivemq/mp.env 2>/dev/null || true
fi
PUB=$(hostname -I 2>/dev/null | awk '{print $1}')

CFG="$ETC/$NAME.yml"
if [ -e "$CFG" ]; then echo "keeping existing $CFG"; else
cat > "$CFG" <<EOF
# written by install_247.sh (5.02.18) - see $LIB/openmp_247.sh for every key
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
mqtt_port: 1883
mqtt_user: $MQU
mqtt_pass:
mqtt_pass_env: $MQE
mqtt_topic: thirtyfox/mp4/omp/$NAME
name: $NAME
public_host: $PUB
status_every: 10
EOF
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
sleep 2; systemctl --no-pager --lines=5 status "omp-247-$NAME" || true
echo
echo "Installed: omp-247-$NAME   config $CFG   backups /var/backups/omp-247/$NAME"
echo "  logs:     journalctl -u omp-247-$NAME -f"
echo "  console:  echo 'gmx' > $SRV/omp_console"
[ -n "$MQH" ] && echo "  status:   mosquitto_sub -h 127.0.0.1 -u bridge -P \"\$BRIDGE_PASS\" -t 'thirtyfox/mp4/omp/$NAME/#' -v" \
             || echo "  MQTT off: no /etc/mp-hivemq/mp.env - set mqtt_host/mqtt_user/mqtt_pass in $CFG, then systemctl restart omp-247-$NAME"
