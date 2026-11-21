#!/usr/bin/env bash
# 5.02.17: keep an open.mp / SA-MP server up 24/7.
#  - restarts it when it crashes or stops answering the SA-MP query (hung)
#  - timed backups of scriptfiles/ and the logs, with rotation
#  - a console FIFO: anything written to it is typed into the server console (stdin)
#  - optional MQTT: status to <topic>/status, console commands from <topic>/cmd (HiveMQ broker etc.)
# 5.02.18: a state file (state_file) + omp_status.py = a retained JSON status on <topic>/status of a HiveMQ broker
#   (players, hostname, restarts, last backup; last will {"online":false}) without mosquitto-clients.
#   install_247.sh sets all of it up as systemd services.
# It does not change what a gamemode shows: textdraws / dialogs come from the Pawn scripts.
set -u
CFG="${1:-$(dirname "$0")/raknet_247.yml}"
cfg() { local v; v=$(sed -n "s/^$1:[[:space:]]*\([^#]*\).*/\1/p" "$CFG" 2>/dev/null | head -1 | sed 's/[[:space:]]*$//'); echo "${v:-$2}"; }
DIR=$(cfg server_dir ./Server); BIN=$(cfg server_bin omp-server)
HOST=$(cfg host 127.0.0.1); PORT=$(cfg port 7777)
HEVERY=$(cfg health_every 15); HFAILS=$(cfg health_fails 4); RDELAY=$(cfg restart_delay 3); RMAX=$(cfg max_restarts_hour 30)
BEVERY=$(cfg backup_every 1800); BKEEP=$(cfg backup_keep 48); BDIR=$(cfg backup_dir ./backups)
FIFO=$(cfg console_fifo ./omp_console)
SFILE=$(cfg state_file ./omp_247.state); LASTB=""; T0=""
MQH=$(cfg mqtt_host ""); MQP=$(cfg mqtt_port 1883); MQT=$(cfg mqtt_topic wagu/openmp)
MQU=$(cfg mqtt_user ""); MQW=$(cfg mqtt_pass ""); MQE=$(cfg mqtt_pass_env "")
[ -z "$MQW" ] && [ -n "$MQE" ] && MQW=$(sed -n "s/^$MQE=//p" /etc/mp-hivemq/mp.env 2>/dev/null | head -1 | tr -d '"')
MQAUTH=""; [ -n "$MQU" ] && MQAUTH="-u $MQU"; [ -n "$MQW" ] && MQAUTH="$MQAUTH -P $MQW"

log() { echo "[247 $(date '+%F %T')] $*"; }
# 5.02.18: the status goes into the state file; omp_status.py publishes it (retained JSON on $MQT/status).
# mosquitto_pub is used only when omp_status.py is not running (old setups): plain text on $MQT/event.
st()  { printf '{"state":"%s","pid":%s,"started":%s,"restarts_hour":%s,"last_backup":"%s","t":%s}\n' "$1" "${SPID:-0}" "${T0:-0}" "${#starts[@]}" "$LASTB" "$(date +%s)" > "$SFILE.tmp" 2>/dev/null && mv -f "$SFILE.tmp" "$SFILE"; }
mq()  { st "$1"; [ -z "$STPID" ] && [ -n "$MQH" ] && command -v mosquitto_pub >/dev/null && mosquitto_pub -h "$MQH" -p "$MQP" $MQAUTH -t "$MQT/event" -m "$*" -q 0 >/dev/null 2>&1 & }

# SA-MP query 'i': "SAMP" + 4 IP bytes + port (LE) + 'i' -> any answer = alive
alive() {
  python3 - "$HOST" "$PORT" <<'PY' 2>/dev/null
import socket,struct,sys
h,p=sys.argv[1],int(sys.argv[2]); ip=socket.gethostbyname(h)
s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM); s.settimeout(2)
s.sendto(b"SAMP"+socket.inet_aton(ip)+struct.pack("<H",p)+b"i",(ip,p))
try: d=s.recv(2048); sys.exit(0 if d[:4]==b"SAMP" else 1)
except Exception: sys.exit(1)
PY
}

backup() {
  mkdir -p "$BDIR"; local f="$BDIR/backup_$(date +%Y%m%d_%H%M%S).tar.gz"
  tar -czf "$f" -C "$DIR" $(cd "$DIR" && ls -d scriptfiles log.txt logs server_log.txt config.json server.cfg 2>/dev/null) 2>/dev/null && { log "backup $f"; LASTB=$(basename "$f"); st running; }
  ls -1t "$BDIR"/backup_*.tar.gz 2>/dev/null | tail -n +$((BKEEP + 1)) | xargs -r rm -f
}

[ -x "$DIR/$BIN" ] || { log "no $DIR/$BIN - set server_dir / server_bin in $CFG"; exit 1; }
[ -p "$FIFO" ] || { rm -f "$FIFO"; mkfifo "$FIFO"; }
exec 3<>"$FIFO"   # held open so the server's stdin never sees EOF
MQPID=""; STPID=""
if [ -n "$MQH" ] && command -v python3 >/dev/null && [ "$(cfg status_sidecar 1)" != "0" ]; then   # 5.02.18
  python3 "$(dirname "$0")/omp_status.py" "$CFG" & STPID=$!
  log "status -> $MQH:$MQP $MQT/status (omp_status.py pid $STPID)"
fi
if [ -n "$MQH" ] && command -v mosquitto_sub >/dev/null; then
  ( mosquitto_sub -h "$MQH" -p "$MQP" $MQAUTH -t "$MQT/cmd" | while IFS= read -r l; do echo "$l" > "$FIFO"; done ) & MQPID=$!
  log "MQTT on $MQH:$MQP topic $MQT"
fi
SPID=""
stop() { log "stopping"; mq stopping; [ -n "$SPID" ] && kill "$SPID" 2>/dev/null; [ -n "$MQPID" ] && kill "$MQPID" 2>/dev/null; [ -n "$STPID" ] && kill "$STPID" 2>/dev/null; wait 2>/dev/null; exit 0; }
trap stop INT TERM
NOWB=0; trap 'NOWB=1' USR1   # 5.02.20: kill -USR1 <runner pid> (omp_autobot "backup <name>") = a backup now

starts=(); lastb=$(date +%s)
while :; do
  now=$(date +%s); keep=(); for t in "${starts[@]:-}"; do [ -n "$t" ] && [ $((now - t)) -lt 3600 ] && keep+=("$t"); done; starts=(); [ "${#keep[@]}" -gt 0 ] && starts=("${keep[@]}")   # 5.02.20: no empty first entry (restarts_hour was 1 too high)
  if [ "${#starts[@]}" -ge "$RMAX" ] && [ -n "${starts[0]:-}" ]; then log "$RMAX restarts in an hour - check the gamemode, waiting 10 min"; mq restart_limit; sleep 600; starts=(); fi
  starts+=("$now")
  # omp-server only reads console lines from a terminal: script(1) gives it one, fed from the FIFO
  if command -v script >/dev/null; then ( cd "$DIR" && exec script -qfec "./$BIN" /dev/null ) <&3 & SPID=$!
  else ( cd "$DIR" && exec ./"$BIN" ) <&3 & SPID=$!; fi
  T0=$(date +%s); log "started $BIN (pid $SPID) on $HOST:$PORT"; mq running
  sleep 5; fails=0
  while kill -0 "$SPID" 2>/dev/null; do
    if alive; then fails=0; else fails=$((fails + 1)); log "query failed ($fails/$HFAILS)"; fi
    if [ "$fails" -ge "$HFAILS" ]; then log "not answering - restart"; mq hung_restarting; kill "$SPID"; sleep 3; kill -9 "$SPID" 2>/dev/null; break; fi
    t=$(date +%s); if [ "$NOWB" = 1 ] || { [ "$BEVERY" -gt 0 ] 2>/dev/null && [ $((t - lastb)) -ge "$BEVERY" ]; }; then NOWB=0; backup; lastb=$t; fi
    sleep "$HEVERY" & wait $!   # 5.02.20: wait (not sleep) so USR1 is handled at once
  done
  wait "$SPID" 2>/dev/null; code=$?
  log "server stopped (exit $code) - restart in ${RDELAY}s"; mq "stopped_exit_$code"
  sleep "$RDELAY"
done
