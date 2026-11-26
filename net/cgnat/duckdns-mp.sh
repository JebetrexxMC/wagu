#!/usr/bin/env bash
# NetWire 1.4 - DuckDNS for the 4 MP region names (mpsamp, eusamp, nasamp, ocsamp).
# From JebetrexxMC/duckdns-systemd (update only when the IP changes, systemd timer, log) and
# honel/duckdns-updater-script (ipify + "OK"/"KO" check), plus a pin for the CGNAT gateway:
# behind CGNAT the home IP is useless to players, so the names must point at the gateway VPS.
#
#   duckdns-mp.sh token <TOKEN>     save the token (Termux: same file termux-broker.sh uses)
#   duckdns-mp.sh once              update now (pinned IP, else this machine's public IP)
#   duckdns-mp.sh pin <VPS_IP>      always use the gateway VPS IP (net/cgnat/mp-gateway.sh)
#   duckdns-mp.sh unpin             back to this machine's public IP
#   duckdns-mp.sh install           Linux: systemd timer every 5 min   Termux: termux-broker loop
#   duckdns-mp.sh status            what each name resolves to vs the public / pinned IP
set -uo pipefail
NAMES="${MP_DUCK_NAMES:-mpsamp,eusamp,nasamp,ocsamp}"
case "${PREFIX:-}" in *com.termux*) D="$HOME/mp-broker";; *) D="${MP_DUCK_DIR:-/etc/mp-duckdns}";; esac
mkdir -p "$D" 2>/dev/null || { echo "run as root (or set MP_DUCK_DIR)"; exit 1; }
TOK="$D/duckdns.token"; PIN="$D/duckdns.pin"; LAST="$D/duckdns.ip"; LOG="$D/duckdns.log"
log() { echo "$(date '+%F %T') $*" | tee -a "$LOG"; tail -n 200 "$LOG" > "$LOG.t" && mv "$LOG.t" "$LOG"; }
pubip() { local ip; for u in https://api.ipify.org https://ipv4.icanhazip.com https://ifconfig.me/ip; do
            ip=$(curl -fsS4 --max-time 8 "$u" 2>/dev/null | tr -d ' \r\n'); [[ $ip =~ ^[0-9]+(\.[0-9]+){3}$ ]] && { echo "$ip"; return; }; done; }
want() { [ -s "$PIN" ] && cat "$PIN" || pubip; }
resolve() { getent ahostsv4 "$1.duckdns.org" 2>/dev/null | awk 'NR==1{print $1}' || true; }

update() {
  [ -s "$TOK" ] || { echo "no token: duckdns-mp.sh token <TOKEN> (duckdns.org, top of the page)"; return 1; }
  local ip r; ip=$(want); [ -n "$ip" ] || { log "no public IP (offline?)"; return 1; }
  [ "${1:-}" != force ] && [ "$(cat "$LAST" 2>/dev/null)" = "$ip" ] && { echo "unchanged $ip"; return 0; }
  r=$(curl -fsS --max-time 15 "https://www.duckdns.org/update?domains=$NAMES&token=$(cat "$TOK")&ip=$ip" 2>&1)
  echo "$r" > "$D/duckdns.last"
  if [ "$r" = OK ]; then echo "$ip" > "$LAST"; log "OK $NAMES -> $ip$([ -s "$PIN" ] && echo ' (gateway pin)')"
  else log "KO ($r) - wrong token or names not on this account"; return 1; fi
}

case "${1:-status}" in
  token) [ -n "${2:-}" ] || { echo "duckdns-mp.sh token <TOKEN>"; exit 2; }; umask 077; echo "$2" > "$TOK"; echo "token saved in $TOK"; update force ;;
  once) update force ;;
  cron) update ;;
  pin) [[ ${2:-} =~ ^[0-9]+(\.[0-9]+){3}$ ]] || { echo "duckdns-mp.sh pin <VPS_IP>"; exit 2; }; echo "$2" > "$PIN"; update force ;;
  unpin) rm -f "$PIN"; update force ;;
  install)
    if [[ ${PREFIX:-} == *com.termux* ]]; then
      echo "Termux: the termux-broker.sh loop refreshes DuckDNS every 5 min and uses $PIN when it is there."
    else
      install -m 755 "$(readlink -f "$0")" /usr/local/sbin/mp-duckdns; S=/usr/local/sbin/mp-duckdns
      printf '[Unit]\nDescription=MP DuckDNS (%s)\nAfter=network-online.target\nWants=network-online.target\n[Service]\nType=oneshot\nExecStart=/bin/bash %s cron\n' "$NAMES" "$S" > /etc/systemd/system/mp-duckdns.service
      printf '[Unit]\nDescription=MP DuckDNS every 5 min\n[Timer]\nOnBootSec=30s\nOnUnitActiveSec=5min\nPersistent=true\n[Install]\nWantedBy=timers.target\n' > /etc/systemd/system/mp-duckdns.timer
      systemctl daemon-reload && systemctl enable --now mp-duckdns.timer && echo "mp-duckdns.timer on (journalctl -u mp-duckdns, log $LOG)"
    fi ;;
  status)
    pub=$(pubip); echo "public IP of this machine: ${pub:-?}"; [ -s "$PIN" ] && echo "pinned (gateway VPS):      $(cat "$PIN")"
    w=$( [ -s "$PIN" ] && cat "$PIN" || echo "$pub")
    for n in ${NAMES//,/ }; do r=$(resolve "$n"); printf '  %-8s -> %-16s %s\n' "$n" "${r:-?}" "$([ "$r" = "$w" ] && echo ok || echo "should be $w (duckdns-mp.sh once)")"; done
    [ -s "$LOG" ] && tail -n 3 "$LOG"; true ;;
  *) sed -n '2,13p' "$0" ;;
esac
