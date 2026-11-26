#!/usr/bin/env bash
# NetWire 1.4 - MP broker in Docker: logins mp / web / bridge / admin, ACL thirtyfox/#, 1883 + TLS 8883 + WSS 8884.
#   sudo MP_PASS=... DOMAIN=mpsamp.duckdns.org EMAIL=you@mail bash setup.sh
# Without DOMAIN only 1883 runs. Behind CGNAT: DOMAIN must point at the gateway VPS
# (net/cgnat/duckdns-mp.sh pin <VPS_IP>) and the VPS forwards TCP 80 too for Let's Encrypt (GW_TCP="80 1883 8883 8884").
set -euo pipefail
cd "$(dirname "$0")"; [ "$(id -u)" = 0 ] || { echo "run as root (sudo)"; exit 1; }
command -v docker >/dev/null || { curl -fsSL https://get.docker.com | sh; }
docker compose version >/dev/null 2>&1 || { echo "need the docker compose plugin (apt install docker-compose-plugin)"; exit 1; }
gen() { tr -dc 'A-Za-z0-9' </dev/urandom | head -c 20; }
MP_PASS=${MP_PASS:-$(gen)}; WEB_PASS=${WEB_PASS:-$(gen)}; BRIDGE_PASS=${BRIDGE_PASS:-$(gen)}; ADMIN_PASS=${ADMIN_PASS:-$(gen)}
DOMAIN=${DOMAIN:-}; EMAIL=${EMAIL:-}
mkdir -p config certs log; chmod 777 log
[ -f ../../config/99-mp-network.conf ] && { cp ../../config/99-mp-network.conf /etc/sysctl.d/99-mp-network.conf; sysctl --system >/dev/null || true; }

: > config/passwd
for u in "mp $MP_PASS" "web $WEB_PASS" "bridge $BRIDGE_PASS" "admin $ADMIN_PASS"; do
  set -- $u; docker run --rm -v "$PWD/config:/c" eclipse-mosquitto:2 mosquitto_passwd -b /c/passwd "$1" "$2"
done
cat > config/acl <<'ACL'
user mp
topic readwrite thirtyfox/#
user bridge
topic readwrite thirtyfox/#
user web
topic read thirtyfox/#
user admin
topic readwrite #
ACL

TLS=""
if [ -n "$DOMAIN" ]; then
  command -v certbot >/dev/null || apt-get install -y -qq certbot
  certbot certonly --standalone --non-interactive --agree-tos -m "${EMAIL:-admin@$DOMAIN}" -d "$DOMAIN" || true
  LE=/etc/letsencrypt/live/$DOMAIN
  if [ -f "$LE/fullchain.pem" ]; then
    cp -L "$LE/fullchain.pem" "$LE/privkey.pem" certs/
    TLS="listener 8883
certfile /mosquitto/certs/fullchain.pem
keyfile /mosquitto/certs/privkey.pem
listener 8884
protocol websockets
certfile /mosquitto/certs/fullchain.pem
keyfile /mosquitto/certs/privkey.pem"
    printf '#!/bin/sh\ncp -L %s/fullchain.pem %s/privkey.pem %s/certs/ && docker restart mp-mosquitto\n' "$LE" "$LE" "$PWD" \
      > /etc/letsencrypt/renewal-hooks/deploy/mp-mosquitto-docker.sh
    chmod +x /etc/letsencrypt/renewal-hooks/deploy/mp-mosquitto-docker.sh
  else echo "Let's Encrypt failed (is $DOMAIN pointing here and TCP 80 open?) - only 1883 for now"; fi
fi
chmod -R a+r config certs
cat > config/mosquitto.conf <<CONF
# MP region broker (NetWire 1.4, Docker) - same rules as HiveMQ/global/edge-mosquitto.sh
per_listener_settings false
allow_anonymous false
password_file /mosquitto/config/passwd
acl_file /mosquitto/config/acl
persistence false
max_inflight_messages 0
max_queued_messages 200
max_packet_size 262144
max_keepalive 120
set_tcp_nodelay true
log_dest file /mosquitto/log/mosquitto.log
listener 1883
$TLS
CONF
docker compose up -d
printf '\nMP broker (Docker) running.\n  mp     %s\n  web    %s\n  bridge %s\n  admin  %s\n' "$MP_PASS" "$WEB_PASS" "$BRIDGE_PASS" "$ADMIN_PASS"
echo "saved nowhere else - copy them now. Region block: ../regions-template.txt"
