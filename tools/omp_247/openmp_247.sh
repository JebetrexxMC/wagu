#!/usr/bin/env bash
# 5.02.17: keep an open.mp / SA-MP server up 24/7.
#  - restarts it when it crashes or stops answering the SA-MP query (hung)
#  - timed backups of scriptfiles/ and the logs, with rotation
#  - a console FIFO: anything written to it is typed into the server console (stdin)
#  - optional MQTT: status to <topic>/status, console commands from <topic>/cmd (HiveMQ broker etc.)
# It does not change what a gamemode shows: textdraws / dialogs come from the Pawn scripts.
set -u
CFG="${1:-$(dirname "$0")/raknet_247.yml}"
cfg() { local v; v=$(sed -n "s/^$1:[[:space:]]*\([^#]*\).*/\1/p" "$CFG" 2>/dev/null | head -1 | sed 's/[[:space:]]*$//'); echo "${v:-$2}"; }
DIR=$(cfg server_dir ./Server); BIN=$(cfg server_bin omp-server)
HOST=$(cfg host 127.0.0.1); PORT=$(cfg port 7777)
HEVERY=$(cfg health_every 15); HFAILS=$(cfg health_fails 4); RDELAY=$(cfg restart_delay 3); RMAX=$(cfg max_restarts_hour 30)
BEVERY=$(cfg backup_every 1800); BKEEP=$(cfg backup_keep 48); BDIR=$(cfg backup_dir ./backups)
FIFO=$(cfg console_fifo ./omp_console)
MQH=$(cfg mqtt_host ""); MQP=$(cfg mqtt_port 1883); MQT=$(cfg mqtt_topic wagu/openmp)

log() { echo "[247 $(date '+%F %T')] $*"; }
mq()  { [ -n "$MQH" ] && command -v mosquitto_pub >/dev/null && mosquitto_pub -h "$MQH" -p "$MQP" -t "$MQT/status" -m "$*" -q 0 >/dev/null 2>&1 & }

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
  [ "$BEVERY" -gt 0 ] 2>/dev/null || return 0
  mkdir -p "$BDIR"; local f="$BDIR/backup_$(date +%Y%m%d_%H%M%S).tar.gz"
  tar -czf "$f" -C "$DIR" $(cd "$DIR" && ls -d scriptfiles log.txt logs server_log.txt 2>/dev/null) 2>/dev/null && log "backup $f"
  ls -1t "$BDIR"/backup_*.tar.gz 2>/dev/null | tail -n +$((BKEEP + 1)) | xargs -r rm -f
}

[ -x "$DIR/$BIN" ] || { log "no $DIR/$BIN - set server_dir / server_bin in $CFG"; exit 1; }
[ -p "$FIFO" ] || { rm -f "$FIFO"; mkfifo "$FIFO"; }
exec 3<>"$FIFO"   # held open so the server's stdin never sees EOF
MQPID=""
if [ -n "$MQH" ] && command -v mosquitto_sub >/dev/null; then
  ( mosquitto_sub -h "$MQH" -p "$MQP" -t "$MQT/cmd" | while IFS= read -r l; do echo "$l" > "$FIFO"; done ) & MQPID=$!
  log "MQTT on $MQH:$MQP topic $MQT"
fi
SPID=""
stop() { log "stopping"; mq "stopping"; [ -n "$SPID" ] && kill "$SPID" 2>/dev/null; [ -n "$MQPID" ] && kill "$MQPID" 2>/dev/null; wait 2>/dev/null; exit 0; }
trap stop INT TERM

starts=(); lastb=$(date +%s)
while :; do
  now=$(date +%s); keep=(); for t in "${starts[@]:-}"; do [ -n "$t" ] && [ $((now - t)) -lt 3600 ] && keep+=("$t"); done; starts=("${keep[@]:-}")
  if [ "${#starts[@]}" -ge "$RMAX" ] && [ -n "${starts[0]:-}" ]; then log "$RMAX restarts in an hour - check the gamemode, waiting 10 min"; mq "restart limit"; sleep 600; starts=(); fi
  starts+=("$now")
  # omp-server only reads console lines from a terminal: script(1) gives it one, fed from the FIFO
  if command -v script >/dev/null; then ( cd "$DIR" && exec script -qfec "./$BIN" /dev/null ) <&3 & SPID=$!
  else ( cd "$DIR" && exec ./"$BIN" ) <&3 & SPID=$!; fi
  log "started $BIN (pid $SPID) on $HOST:$PORT"; mq "started pid $SPID"
  sleep 5; fails=0
  while kill -0 "$SPID" 2>/dev/null; do
    if alive; then fails=0; else fails=$((fails + 1)); log "query failed ($fails/$HFAILS)"; fi
    if [ "$fails" -ge "$HFAILS" ]; then log "not answering - restart"; mq "hung, restarting"; kill "$SPID"; sleep 3; kill -9 "$SPID" 2>/dev/null; break; fi
    t=$(date +%s); if [ "$BEVERY" -gt 0 ] 2>/dev/null && [ $((t - lastb)) -ge "$BEVERY" ]; then backup; lastb=$t; fi
    sleep "$HEVERY"
  done
  wait "$SPID" 2>/dev/null; code=$?
  log "server stopped (exit $code) - restart in ${RDELAY}s"; mq "stopped exit $code"
  sleep "$RDELAY"
done
