#!/usr/bin/env bash
# WireGuard tunnel between an MP game server (10.77.0.1) and an mpnetd relay box (10.77.0.2).
#   wg-relay.sh server                          -> prints the server public key, waits for the relay
#   wg-relay.sh relay <server-pubkey> <ip:port>  -> prints the relay public key: add it on the server with
#   wg set wg-mp peer <relay-pubkey> allowed-ips 10.77.0.2/32
set -euo pipefail
[ "$(id -u)" = 0 ] || { echo "run as root"; exit 1; }
command -v wg >/dev/null || { apt-get update -qq && apt-get install -y -qq wireguard-tools; }
umask 077; mkdir -p /etc/wireguard
K=/etc/wireguard/wg-mp.key
[ -f $K ] || wg genkey > $K
PUB=$(wg pubkey < $K)
case "${1:-}" in
  server)
    cat > /etc/wireguard/wg-mp.conf <<C
[Interface]
Address = 10.77.0.1/24
ListenPort = 51820
MTU = 1380
PrivateKey = $(cat $K)
C
    systemctl enable --now wg-quick@wg-mp
    echo "server public key: $PUB  (open udp 51820)" ;;
  relay)
    [ $# -eq 3 ] || { echo "wg-relay.sh relay <server-pubkey> <server-ip:51820>"; exit 2; }
    cat > /etc/wireguard/wg-mp.conf <<C
[Interface]
Address = 10.77.0.2/24
MTU = 1380
PrivateKey = $(cat $K)

[Peer]
PublicKey = $2
Endpoint = $3
AllowedIPs = 10.77.0.1/32
PersistentKeepalive = 25
C
    systemctl enable --now wg-quick@wg-mp
    echo "relay public key: $PUB"
    echo "on the server: wg set wg-mp peer $PUB allowed-ips 10.77.0.2/32 && wg-quick save wg-mp" ;;
  *) echo "wg-relay.sh server | relay <server-pubkey> <ip:port>"; exit 2 ;;
esac
