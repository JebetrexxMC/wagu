#!/usr/bin/env bash
# Creates the MP admin services on an OpenZiti controller (ziti CLI must be logged in: ziti edge login ...).
#   services.sh <edge-router-name> <identity-name>
set -euo pipefail
[ $# -eq 2 ] || { echo "services.sh <edge-router-name> <identity-name>"; exit 2; }
ROUTER=$1; ME=$2
ziti edge create identity "$ME" -a mp-admins -o "$ME.jwt" || true
mk() { # mk <name> <intercept host> <port> <host addr> <host port>
  ziti edge create config "$1-host" host.v1 "{\"protocol\":\"tcp\",\"address\":\"$4\",\"port\":$5}" || true
  ziti edge create config "$1-intercept" intercept.v1 "{\"protocols\":[\"tcp\"],\"addresses\":[\"$2\"],\"portRanges\":[{\"low\":$3,\"high\":$3}]}" || true
  ziti edge create service "$1" --configs "$1-host,$1-intercept" -a mp-admin-services || true
}
mk mp-ssh mp-ssh.ziti 22 127.0.0.1 22
mk mp-mqtt-admin mp-mqtt.ziti 1883 127.0.0.1 1883
mk mp-status mp-status.ziti 80 127.0.0.1 8090
ziti edge create service-policy mp-admin-dial Dial --service-roles '#mp-admin-services' --identity-roles '#mp-admins' || true
ziti edge create service-policy mp-admin-bind Bind --service-roles '#mp-admin-services' --identity-roles "@$ROUTER" || true
ziti edge create service-edge-router-policy mp-admin-routers --service-roles '#mp-admin-services' --edge-router-roles '#all' || true
ziti edge create edge-router-policy mp-admins-routers --identity-roles '#mp-admins' --edge-router-roles '#all' || true
echo "enrol $ME.jwt in Ziti Desktop / Mobile Edge; then: ssh user@mp-ssh.ziti"
