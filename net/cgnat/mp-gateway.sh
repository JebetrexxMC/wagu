#!/usr/bin/env bash
# NetWire 1.4 - CGNAT gateway: a cheap VPS with a public IP forwards the MP ports through WireGuard
# to the home broker / servers that sit behind Converge (or any) CGNAT. Idea from
# JebetrexxMC/cgnat-vps-gateway, but kernel DNAT instead of socat + Cloudflare (Cloudflare can't
# carry UDP or raw MQTT, so the game ports never worked through it).
#
#   VPS (public IP)                       home box (behind CGNAT: Linux PC, Pi, Debian in Termux=no)
#   mp-gateway.sh vps                 ->  prints:  mp-gateway.sh home <VPS_IP> <VPS_PUBKEY>
#   mp-gateway.sh add-home <PUB> [--snat]   <-  printed by the home side (or by: phone)
#   mp-gateway.sh phone <VPS_IP> <VPS_PUBKEY>  WireGuard-app config (+QR) for an Android/Termux broker
#   mp-gateway.sh status | off
#   mp-gateway.sh rules up|down        (VPS, called by wg-quick PostUp/PostDown)
#   mp-gateway.sh health               (VPS, timer every 20 s: home offline -> refuse at once = instant
#                                       fallback to the public lobby, back to forwarding when it returns)
#   mp-gateway.sh route up|down        (home, called by wg-quick PostUp/PostDown)
#
# Players connect to the VPS IP (point the duckdns names at it: net/cgnat/duckdns-mp.sh pin <VPS_IP>).
# KEEP_IP mode (default for a Linux home box): the server sees the real player IP - the home side
# sends its replies back through the tunnel by policy routing (table 79), no SNAT on the VPS.
# UDP: a game server bound to 0.0.0.0 answers from its LAN address, so by default the VPS masquerades
# UDP only (works with every server; it sees 10.79.0.1). Bind the servers to 10.79.0.2 (open.mp
# config.json "network":{"bind":"10.79.0.2"}, SA-MP server.cfg "bind 10.79.0.2") and use
# add-home <PUB> --udp-keep-ip to keep the real IP on UDP too.
# --snat mode (phone / WireGuard app): the VPS masquerades everything (10.79.0.1 for everyone).
# Ping = player->VPS + VPS->home. 'status' prints the VPS->home part.
set -euo pipefail

ENV=/etc/mp-gateway.env; [ -r "$ENV" ] && . "$ENV"     # SNAT, UDPKEEP, GW_TCP, GW_UDP saved by vps / add-home
IF="${WG_IF:-mpgw}"; NET=10.79.0; VPS_IP_IN=$NET.1; HOME_IP=$NET.2
PORT="${WG_PORT:-51821}"; MTU="${WG_MTU:-1380}"; TABLE=79
TCP_PORTS="${GW_TCP:-1883 8883 8884}"            # MQTT, MQTT TLS, website WSS
UDP_PORTS="${GW_UDP:-1922:1932 1939 7777:7780}"  # MP direct UDP (3.11), udp-bench echo, open.mp / SA-MP servers
CONF=/etc/wireguard/$IF.conf; SELF="$(readlink -f "$0")"

die() { echo "mp-gateway: $*" >&2; exit 1; }
ipt() { "$IPT" -w "$@"; }
pickipt() { IPT=iptables; "$IPT" -w -t nat -L -n >/dev/null 2>&1 || IPT=iptables-legacy
            "$IPT" -w -t nat -L -n >/dev/null 2>&1 || die "iptables not working (apt install iptables)"; }
pubif() { [ -n "${PUB_IF:-}" ] && { echo "$PUB_IF"; return; }; ip -4 route get 1.1.1.1 | sed -n 's/.* dev \([^ ]*\).*/\1/p'; }
pubip() { curl -fsS4 --max-time 6 https://api.ipify.org 2>/dev/null || ip -4 -o addr show dev "$(pubif)" | awk '{print $4}' | cut -d/ -f1 | head -1; }
need() { [ "$(id -u)" = 0 ] || die "run as root"
         command -v wg >/dev/null || { apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq wireguard-tools iptables; }
         [ "$SELF" = /usr/local/sbin/mp-gateway ] || { install -m 755 "$SELF" /usr/local/sbin/mp-gateway; SELF=/usr/local/sbin/mp-gateway; }
         umask 077; mkdir -p /etc/wireguard; K=/etc/wireguard/$IF.key; [ -f "$K" ] || wg genkey > "$K"; PUB=$(wg pubkey < "$K"); }
snat() { [ "${SNAT:-0}" = 1 ]; }
udpkeep() { [ "${UDPKEEP:-0}" = 1 ]; }
saveenv() { SNAT=$1; UDPKEEP=$2; printf 'SNAT=%s\nUDPKEEP=%s\nGW_TCP="%s"\nGW_UDP="%s"\n' "$1" "$2" "$TCP_PORTS" "$UDP_PORTS" > "$ENV"; }

# ---- VPS: DNAT the MP ports to the home box (own chains, safe to run again) ----
rules() {
  local mode="${1:-up}" o t ch base; pickipt; o=$(pubif); [ -n "$o" ] || die "no public interface"
  while read -r t ch base; do
    ipt -t "$t" -S "$base" 2>/dev/null | grep -- "-j $ch\$" | sed 's/^-A //' | while read -r r; do eval "ipt -t $t -D $r"; done || true
    ipt -t "$t" -F "$ch" 2>/dev/null || true; ipt -t "$t" -X "$ch" 2>/dev/null || true
  done <<L
nat MPGW_PRE PREROUTING
nat MPGW_POST POSTROUTING
filter MPGW_FWD FORWARD
filter MPGW_DOWN NONE
mangle MPGW_MSS FORWARD
L
  [ "$mode" = down ] && { echo "gateway rules removed"; return; }
  sysctl -qw net.ipv4.ip_forward=1
  ipt -t nat -N MPGW_PRE; ipt -t nat -A PREROUTING -i "$o" -j MPGW_PRE
  for p in $TCP_PORTS; do ipt -t nat -A MPGW_PRE -p tcp --dport "$p" -j DNAT --to-destination $HOME_IP; done
  for p in $UDP_PORTS; do ipt -t nat -A MPGW_PRE -p udp --dport "$p" -j DNAT --to-destination $HOME_IP; done
  ipt -t nat -N MPGW_POST; ipt -t nat -A POSTROUTING -j MPGW_POST
  if snat; then ipt -t nat -A MPGW_POST -o "$IF" -j MASQUERADE
  elif ! udpkeep; then ipt -t nat -A MPGW_POST -o "$IF" -p udp -j MASQUERADE; fi
  ipt -N MPGW_FWD; ipt -I FORWARD 1 -j MPGW_FWD
  ipt -N MPGW_DOWN; ipt -A MPGW_FWD -j MPGW_DOWN          # filled by 'health' while the home box is offline
  ipt -A MPGW_FWD -i "$o" -o "$IF" -d $HOME_IP -j ACCEPT
  ipt -A MPGW_FWD -i "$IF" -o "$o" -s $HOME_IP -j ACCEPT
  ipt -t mangle -N MPGW_MSS; ipt -t mangle -A FORWARD -j MPGW_MSS
  ipt -t mangle -A MPGW_MSS -o "$IF" -p tcp --tcp-flags SYN,RST SYN -j TCPMSS --clamp-mss-to-pmtu 2>/dev/null \
    || echo "note: no TCPMSS in this kernel - MSS clamp skipped (MTU $MTU still works)"
  echo "gateway rules up: $o tcp[$TCP_PORTS] udp[$UDP_PORTS] -> $HOME_IP via $IF ($(snat && echo 'snat: everyone shows as 10.79.0.1' || { udpkeep && echo 'real player IPs' || echo 'real IPs on tcp, udp shows 10.79.0.1'; }))"
}

# ---- VPS: home offline (Wi-Fi / power down) -> refuse at once, so the game switches to the public
# lobby (GitHub / website network, the 5.02.72 standby path) right away instead of waiting for timeouts.
health() {
  pickipt; local age=999999 st hs now
  now=$(date +%s)
  for hs in $(wg show "$IF" latest-handshakes 2>/dev/null | awk '{print $2}'); do [ "$hs" -gt 0 ] && [ $((now - hs)) -lt "$age" ] && age=$((now - hs)); done
  st=up; [ "$age" -gt 180 ] && st=down; st="${GW_FORCE:-$st}"
  ipt -L MPGW_DOWN -n >/dev/null 2>&1 || { echo "no gateway rules (run: mp-gateway.sh rules up)"; return 1; }
  local have; have=$(ipt -S MPGW_DOWN | grep -c '^-A' || true)
  if [ $st = down ] && [ "$have" = 0 ]; then
    ipt -A MPGW_DOWN -o "$IF" -p tcp -j REJECT --reject-with tcp-reset
    ipt -A MPGW_DOWN -o "$IF" -p udp -j REJECT --reject-with icmp-port-unreachable
    logger -t mp-gateway "home offline (last handshake ${age}s) - refusing, players use the public lobby" 2>/dev/null || true
  elif [ $st = up ] && [ "$have" != 0 ]; then
    ipt -F MPGW_DOWN
    logger -t mp-gateway "home back online - forwarding again" 2>/dev/null || true
  fi
  echo "home $st (last handshake $([ $age = 999999 ] && echo never || echo "${age}s ago"))"
}

# ---- home: answers to players go back through the tunnel (KEEP_IP mode) ----
route() {
  while ip rule del from $HOME_IP table $TABLE 2>/dev/null; do :; done
  ip route flush table $TABLE 2>/dev/null || true
  [ "${1:-up}" = down ] && return
  ip route add default via $VPS_IP_IN dev "$IF" table $TABLE
  ip rule add from $HOME_IP table $TABLE priority 7900
  echo "home route up: replies from $HOME_IP leave through $IF (players keep their own IP)"
}

case "${1:-}" in
  vps)
    need
    cat > "$CONF" <<C
[Interface]
Address = $VPS_IP_IN/24
ListenPort = $PORT
MTU = $MTU
PrivateKey = $(cat "$K")
PostUp = $SELF rules up
PostDown = $SELF rules down
C
    saveenv "${SNAT:-0}" "${UDPKEEP:-0}"
    systemctl enable --now "wg-quick@$IF" >/dev/null
    printf '[Unit]\nDescription=MP gateway: home online check\n[Service]\nType=oneshot\nExecStart=%s health\n' "$SELF" > /etc/systemd/system/mp-gateway-health.service
    printf '[Unit]\nDescription=MP gateway: home online check every 20 s\n[Timer]\nOnBootSec=40s\nOnUnitActiveSec=20s\nAccuracySec=2s\n[Install]\nWantedBy=timers.target\n' > /etc/systemd/system/mp-gateway-health.timer
    systemctl daemon-reload; systemctl enable --now mp-gateway-health.timer >/dev/null
    for p in $TCP_PORTS; do ss -ltnH "sport = :$p" | grep -q . && echo "note: tcp $p is also used on this VPS - the gateway DNAT wins for traffic from outside"; done
    echo "VPS ready. Open UDP $PORT in the provider firewall. On the home box run:"
    echo "  bash mp-gateway.sh home $(pubip) $PUB"
    echo "or for a phone broker (WireGuard app):  bash mp-gateway.sh phone $(pubip) $PUB" ;;
  add-home)
    need; [ $# -ge 2 ] || die "add-home <home-pubkey> [--snat | --udp-keep-ip]"
    case "${3:-}" in --snat) saveenv 1 0;; --udp-keep-ip) saveenv 0 1;; *) saveenv 0 0;; esac
    wg set "$IF" peer "$2" allowed-ips $HOME_IP/32 persistent-keepalive 25
    wg-quick save "$IF" 2>/dev/null || true
    rules up
    echo "home peer added. Point the region names at this VPS:  bash duckdns-mp.sh pin $(pubip)" ;;
  home)
    need; [ $# -eq 3 ] || die "home <VPS_IP> <VPS_PUBKEY>"
    cat > "$CONF" <<C
[Interface]
Address = $HOME_IP/24
MTU = $MTU
PrivateKey = $(cat "$K")
Table = off
PostUp = $SELF route up
PostDown = $SELF route down

[Peer]
PublicKey = $3
Endpoint = $2:$PORT
AllowedIPs = 0.0.0.0/0
PersistentKeepalive = 25
C
    systemctl enable --now "wg-quick@$IF" >/dev/null
    echo "home side up. On the VPS run:  bash mp-gateway.sh add-home $PUB"
    echo "the broker / servers must listen on 0.0.0.0 (or $HOME_IP) - the MP kit does by default" ;;
  phone)
    need; [ $# -eq 3 ] || die "phone <VPS_IP> <VPS_PUBKEY>"
    PK=$(wg genkey); PP=$(echo "$PK" | wg pubkey)
    C="[Interface]
PrivateKey = $PK
Address = $HOME_IP/32
MTU = $MTU

[Peer]
PublicKey = $3
Endpoint = $2:$PORT
AllowedIPs = $VPS_IP_IN/32
PersistentKeepalive = 25"
    echo "$C" > /root/mpgw-phone.conf; echo "$C"; echo
    command -v qrencode >/dev/null && qrencode -t ansiutf8 < /root/mpgw-phone.conf
    echo "import it in the WireGuard app (scan the QR or copy /root/mpgw-phone.conf), turn it on, keep Termux broker running."
    echo "then on the VPS:  bash mp-gateway.sh add-home $PP --snat" ;;
  rules) rules "${2:-up}" ;;
  route) route "${2:-up}" ;;
  health) health ;;
  status)
    w=$(wg show "$IF" 2>/dev/null | sed -n '1,12p' || true); echo "${w:-no $IF tunnel on this machine}"
    if [ -f "$CONF" ] && grep -q ListenPort "$CONF"; then
      health | sed 's/^/  /'; pickipt; echo "--- forwarded (packets / port):"; ipt -t nat -L MPGW_PRE -n -v 2>/dev/null | awk 'NR>2{print "  "$1"\t"$4" "$NF" "$(NF-1)}'
      r=$(ping -c 3 -q -W 2 $HOME_IP 2>/dev/null | grep rtt || true); echo "  VPS -> home (added ping): ${r:-home box not answering}"
    else
      r=$(ping -c 3 -q -W 2 $VPS_IP_IN 2>/dev/null | grep rtt || true); echo "  home -> VPS: ${r:-VPS not answering}"
      ip rule | grep -q "lookup $TABLE" && echo "  keep-IP route: on" || echo "  keep-IP route: off"
    fi ;;
  off) systemctl disable --now "wg-quick@$IF" 2>/dev/null || true; echo "gateway off (config kept in $CONF)" ;;
  *) sed -n '2,26p' "$0" ;;
esac
