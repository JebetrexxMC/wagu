#!/usr/bin/env bash
# NetWire 1.4 - is this connection behind CGNAT? (Termux or Linux, no root)
#   bash cgnat-check.sh [router-WAN-IP]
#   bash cgnat-check.sh --json [router-WAN-IP]   one JSON line (mpadmin cgnat loop publishes it to the website)
# The router WAN IP is on the router page (Converge ZTE F670L: Status > Network Interface > WAN).
# Verdict:
#   PUBLIC  the public IP is on this box or 1 hop away - port forwards work, the regions can be online
#   CGNAT   a 100.64.0.0/10 address / the WAN IP differs - nobody can reach a broker here from outside
#   NAT?    no clear answer (no traceroute) - give the router WAN IP as the first argument
set -uo pipefail
JSON=0; [ "${1:-}" = --json ] && { JSON=1; shift; exec 3>&1 1>/dev/null; }
c() { printf '\033[%sm%s\033[0m' "$1" "$2"; }
cg() { [[ $1 =~ ^100\.(6[4-9]|[7-9][0-9]|1[01][0-9]|12[0-7])\. ]]; }        # 100.64.0.0/10
priv() { [[ $1 =~ ^(10\.|192\.168\.|172\.(1[6-9]|2[0-9]|3[01])\.) ]]; }
pubip() { local ip; for u in https://api.ipify.org https://ipv4.icanhazip.com https://ifconfig.me/ip; do
            ip=$(curl -fsS4 --max-time 8 "$u" 2>/dev/null | tr -d ' \r\n'); [[ $ip =~ ^[0-9]+(\.[0-9]+){3}$ ]] && { echo "$ip"; return; }; done; }

PUB=$(pubip); [ -n "$PUB" ] || { echo "no internet (could not read the public IP)"; exit 1; }
ORG=$(curl -fsS --max-time 8 "https://ipinfo.io/$PUB/org" 2>/dev/null)
echo "public IP: $PUB  ${ORG:+($ORG)}"
LOCAL=$(ip -4 -o addr show scope global 2>/dev/null | awk '{print $4}' | cut -d/ -f1 | tr '\n' ' ')
echo "this box:  ${LOCAL:-?}"; N=""; HOPS=""
WAN="${1:-${ROUTER_WAN:-}}"; [ -n "$WAN" ] && echo "router WAN: $WAN"

V=""; WHY=""
for a in $LOCAL; do
  [ "$a" = "$PUB" ] && { V=PUBLIC; WHY="the public IP is on this box"; }
  cg "$a" && { V=CGNAT; WHY="this box has the carrier address $a (100.64.0.0/10) - mobile data or the ISP"; }
done
if [ -z "$V" ] && [ -n "$WAN" ]; then
  if [ "$WAN" = "$PUB" ]; then V=PUBLIC; WHY="router WAN = public IP"
  elif cg "$WAN" || priv "$WAN"; then V=CGNAT; WHY="router WAN $WAN is a carrier / private address, the internet sees $PUB"
  else V=CGNAT; WHY="router WAN $WAN differs from the public IP $PUB"; fi
fi
if [ -z "$V" ]; then
  if command -v traceroute >/dev/null; then T=$(traceroute -n -m 8 -w 1 -q 1 "$PUB" 2>/dev/null | tail -n +2)
  elif command -v tracepath >/dev/null; then T=$(tracepath -n -m 8 "$PUB" 2>/dev/null)
  else T=""; fi
  if [ -n "$T" ]; then
    HOPS=$(echo "$T" | awk '$2 ~ /^[0-9.]+$/ {print $2}')
    N=$(echo "$HOPS" | grep -c .); echo "traceroute to own public IP: $N hop(s): $(echo $HOPS)"
    for h in $HOPS; do cg "$h" && { V=CGNAT; WHY="hop $h is a carrier NAT address"; }; done
    [ -z "$V" ] && { [ "$N" -le 1 ] && { V=PUBLIC; WHY="own public IP is 1 hop away (the router)"; } || { V=CGNAT; WHY="own public IP is $N hops away (more than the router)"; }; }
  else
    V="NAT?"; WHY="install traceroute (pkg install traceroute / apt install traceroute) or give the router WAN IP"
  fi
fi

case "$V" in PUBLIC) col='1;32';; CGNAT) col='1;31';; *) col='1;33';; esac
echo; echo "verdict: $(c "$col" "$V") - $WHY"

echo; echo "region names (duckdns):"; NAMES=""
for n in mpsamp eusamp nasamp ocsamp; do
  r=$(getent ahostsv4 "$n.duckdns.org" 2>/dev/null | awk 'NR==1{print $1}')
  [ -z "$r" ] && command -v nslookup >/dev/null && r=$(nslookup "$n.duckdns.org" 2>/dev/null | awk '/^Address/ && !/#/{print $2}' | tail -1)
  printf '  %-8s -> %-16s %s\n' "$n" "${r:-?}" "$([ "$r" = "$PUB" ] && echo '(this connection)' || echo '')"
  NAMES="$NAMES${NAMES:+,}\"$n\":\"${r:-}\""
done
if [ $JSON = 1 ]; then
  js() { printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g'; }
  printf '{"time":%s,"verdict":"%s","why":"%s","public_ip":"%s","isp":"%s","lan":"%s","wan":"%s","hops":"%s","names":{%s},"device":"%s"}\n' \
    "$(date +%s)" "$V" "$(js "$WHY")" "$PUB" "$(js "${ORG:-}")" "$(js "$LOCAL")" "${WAN:-}" "$(echo $HOPS)" "$NAMES" "$(js "$(getprop ro.product.model 2>/dev/null || hostname)")" >&3
  exit 0
fi

echo
if [ "$V" = CGNAT ]; then cat <<T
what to do (Converge / any CGNAT):
  1. ask the ISP for a public IP (Converge: "request public IP" / "static IP add-on"; say it is for a game server).
     After that: router bridge mode is optional (gist marfillaster/5cfdc5d2c9e0bed3d8979f07944c051a puts the
     F670L in bridge mode behind your own router - it removes the double NAT, NOT the carrier NAT).
  2. or a VPS gateway now: bash mp-gateway.sh vps (on a VPS)  +  bash mp-gateway.sh home ... (here)
     then: bash duckdns-mp.sh pin <VPS_IP>.  Ping = player->VPS + VPS->home (PH<->Singapore ~35-60 ms each way leg).
  3. lowest ping: run the broker + servers on the Singapore / Manila VPS itself (HiveMQ/global/README.md).
T
elif [ "$V" = PUBLIC ]; then cat <<T
port forwards on the router to this box ($(echo $LOCAL | awk '{print $1}')):
  TCP 1883 8883 8884   UDP 1922-1932 (MP direct UDP)   UDP 7777-7780 (open.mp servers)
  then: bash duckdns-mp.sh once   - the regions go from standby to up on the next NetWire round (5 min)
T
fi
