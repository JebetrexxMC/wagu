#!/usr/bin/env bash
# omp247 - phone + server console for the open.mp 24/7 servers (Termux, HiveMQ web console, any Linux).  5.02.20c
#
# One line in Termux (https://github.com/termux/termux-app):
#   curl -fsSL https://raw.githubusercontent.com/JebetrexxMC/wagu/main/tools/omp_247/termux_omp247.sh -o $PREFIX/bin/omp247 && chmod +x $PREFIX/bin/omp247 && omp247
#
# It asks your HiveMQ broker details once (saved in ~/.omp247/conf, chmod 600) and then talks
# to omp_autobot on your server(s) over MQTT: no placeholders to type, no commands to remember.
#   omp247              menu
#   omp247 install      find every open.mp / SA-MP server folder on the server and run it 24/7
#   omp247 find|list|status|logs|restart|backup|update|ping   same as the menu
#   omp247 watch        live status of every server
#   omp247 setup        first-time server setup (on the server itself, or over SSH from the phone)
# On the server (HiveMQ web console menu p) 9, or SSH) omp247 is installed by install_247.sh and reads the
# server's own broker login (/etc/omp-247/autobot.env or /etc/mp-hivemq/mp.env): nothing to type.
#   omp247 widget       home-screen buttons for Termux:Widget  (https://github.com/termux/termux-widget)
#   omp247 boot         phone notifications when a server goes offline, started by Termux:Boot
#                       (https://github.com/termux/termux-boot + https://github.com/termux/termux-api)
#   omp247 config       change the saved settings
set -u
RAW=https://raw.githubusercontent.com/JebetrexxMC/wagu/main/tools/omp_247
REL=https://github.com/JebetrexxMC/wagu/releases/latest/download
D="$HOME/.omp247"; CONF="$D/conf"; BOT="$D/omp_autobot.py"
mkdir -p "$D"; chmod 700 "$D"
TERMUX=0; [ -n "${PREFIX:-}" ] && [ -d "${PREFIX:-/x}/bin" ] && case "$PREFIX" in *com.termux*) TERMUX=1;; esac
SUDO=sudo; [ "$(id -u)" = 0 ] && SUDO=
SELF="$(command -v omp247 2>/dev/null || echo "$0")"

need() {   # install what is missing (pkg on Termux, apt/dnf elsewhere)
  local miss=()
  command -v curl >/dev/null || miss+=(curl)
  command -v python3 >/dev/null || command -v python >/dev/null || miss+=(python)
  [ "${1:-}" = ssh ] && { command -v ssh >/dev/null || miss+=(openssh); }
  [ ${#miss[@]} -eq 0 ] && return 0
  echo "installing: ${miss[*]}"
  if [ $TERMUX = 1 ]; then pkg install -y "${miss[@]}"
  elif command -v apt-get >/dev/null; then $SUDO apt-get install -y $(printf '%s\n' "${miss[@]}" | sed 's/^python$/python3/;s/^openssh$/openssh-client/')
  elif command -v dnf >/dev/null; then $SUDO dnf install -y $(printf '%s\n' "${miss[@]}" | sed 's/^python$/python3/;s/^openssh$/openssh-clients/')
  else echo "please install: ${miss[*]}"; return 1; fi
}
PY() { if command -v python3 >/dev/null; then python3 "$@"; else python "$@"; fi; }
getbot() { [ -s "$BOT" ] && [ "${1:-}" != force ] && return 0
  curl -fsSL "$RAW/omp_autobot.py" -o "$BOT.new" && mv "$BOT.new" "$BOT" || { echo "cannot download omp_autobot.py"; return 1; }; }

ask() { local v; read -r -p "$1 [${2:-}]: " v </dev/tty; echo "${v:-${2:-}}"; }
config() {
  [ -f "$CONF" ] && . "$CONF"
  echo "== HiveMQ broker (the same one your MP app and server use) =="
  MQTT_HOST=$(ask "broker host (IP or name)" "${MQTT_HOST:-}")
  MQTT_TLS=$(ask "TLS? y/n (y for HiveMQ Cloud)" "${MQTT_TLS:-n}")
  case "$MQTT_TLS" in y*|Y*|1) MQTT_TLS=y; dp=8883;; *) MQTT_TLS=n; dp=1883;; esac
  MQTT_PORT=$(ask "broker port" "${MQTT_PORT:-$dp}")
  MQTT_USER=$(ask "user (admin of the kit)" "${MQTT_USER:-admin}")
  local pw; read -r -s -p "password${MQTT_PASS:+ [keep]}: " pw </dev/tty; echo; MQTT_PASS="${pw:-${MQTT_PASS:-}}"
  BOT_NAME=$(ask "bot (server host name, or all)" "${BOT_NAME:-all}")
  BOT_TOKEN=$(ask "bot token (empty if none)" "${BOT_TOKEN:-}")
  SSH_TARGET=$(ask "SSH login for first setup, e.g. root@1.2.3.4 (empty = skip)" "${SSH_TARGET:-}")
  umask 077
  { for k in MQTT_HOST MQTT_PORT MQTT_TLS MQTT_USER MQTT_PASS BOT_NAME BOT_TOKEN SSH_TARGET; do
      printf '%s=%q\n' "$k" "${!k}"; done; } > "$CONF"
  chmod 600 "$CONF"; echo "saved in $CONF"
}
autoconf() {   # 5.02.20c: on the server the broker login is already there - nothing to ask
  local f=/etc/omp-247/autobot.env e=/etc/mp-hivemq/mp.env v
  gv() { sed -n "s/^$1=//p" "$2" 2>/dev/null | tail -1 | tr -d '"'"'"; }
  if [ -r "$f" ] && [ -n "$(gv MQTT_HOST $f)" ]; then
    MQTT_HOST=$(gv MQTT_HOST $f); MQTT_PORT=$(gv MQTT_PORT $f); MQTT_USER=$(gv MQTT_USER $f); MQTT_PASS=$(gv MQTT_PASS $f)
    [ -z "$MQTT_PASS" ] && [ "$MQTT_USER" = bridge ] && MQTT_PASS=$(gv BRIDGE_PASS $e)
    v=$(gv MQTT_TLS $f); case "$v" in 1|y*|true) MQTT_TLS=y;; *) MQTT_TLS=n;; esac
    BOT_NAME=$(gv BOT_NAME $f); BOT_TOKEN=$(gv BOT_TOKEN $f)
  elif [ -r "$e" ] && [ -n "$(gv ADMIN_PASS $e)$(gv BRIDGE_PASS $e)" ]; then
    MQTT_HOST=127.0.0.1; MQTT_PORT=1883; MQTT_TLS=n
    if [ -n "$(gv ADMIN_PASS $e)" ]; then MQTT_USER=admin; MQTT_PASS=$(gv ADMIN_PASS $e); else MQTT_USER=bridge; MQTT_PASS=$(gv BRIDGE_PASS $e); fi
    BOT_NAME=all; BOT_TOKEN=
  else return 1; fi
  MQTT_PORT=${MQTT_PORT:-1883}; BOT_NAME=${BOT_NAME:-all}; SSH_TARGET=local
  umask 077; { for k in MQTT_HOST MQTT_PORT MQTT_TLS MQTT_USER MQTT_PASS BOT_NAME BOT_TOKEN SSH_TARGET; do
      printf '%s=%q\n' "$k" "${!k}"; done; } > "$CONF"; chmod 600 "$CONF"
  echo "omp247: using this server's broker ($MQTT_USER@$MQTT_HOST:$MQTT_PORT) - nothing to set up"
}
load() { [ -f "$CONF" ] || autoconf || config; . "$CONF"; [ -n "${MQTT_HOST:-}" ] || { echo "no broker host set"; config; . "$CONF"; }; }
mqargs() { A=(-H "$MQTT_HOST" -p "$MQTT_PORT"); [ -n "$MQTT_USER" ] && A+=(-u "$MQTT_USER")
  [ "$MQTT_TLS" = y ] && A+=(--tls); export MQTT_PASS; }

cmd() {   # send one command to the bot(s) and print the answer
  need && getbot && load || return 1; mqargs
  local w=30; case "$1" in install|update) w=900;; find) w=150;; backup|logs) w=90;; esac
  BOT_TOKEN="${BOT_TOKEN:-}" PY "$BOT" send "${A[@]}" -b "${BOT_NAME:-all}" -w "$w" "$@"
}
watch() { need && getbot && load || return 1; mqargs; PY "$BOT" watch "${A[@]}" "$@"; }

setup() {   # first time on the server: 24/7 runner for every server found + the bot, all automatic
  if [ $TERMUX = 0 ] && command -v systemctl >/dev/null && { [ "${SSH_TARGET:-}" = local ] || [ ! -f "$CONF" ]; } \
     && { [ -r /etc/mp-hivemq/mp.env ] || [ -r /etc/omp-247/autobot.env ] || [ -d /opt/omp-247 ]; }; then   # we are on the server
    cd /tmp && curl -fsSLO "$REL/install_247.sh" && $SUDO bash install_247.sh; local r=$?; [ -f "$CONF" ] || autoconf; return $r
  fi
  need ssh && load || return 1
  [ "${SSH_TARGET:-}" = local ] && SSH_TARGET=
  [ -n "${SSH_TARGET:-}" ] || { SSH_TARGET=$(ask "SSH login, e.g. root@1.2.3.4" ""); [ -n "$SSH_TARGET" ] || return 1
    sed -i '/^SSH_TARGET=/d' "$CONF"; printf 'SSH_TARGET=%q\n' "$SSH_TARGET" >> "$CONF"; }
  local tls=0; [ "$MQTT_TLS" = y ] && tls=1
  echo "connecting to $SSH_TARGET (type the server password if asked)..."
  ssh -t "$SSH_TARGET" "MQTT_HOST=$(printf %q "$MQTT_HOST") MQTT_PORT=$MQTT_PORT MQTT_TLS=$tls MQTT_USER=$(printf %q "$MQTT_USER") \
    MQTT_PASS=$(printf %q "$MQTT_PASS") BOT_TOKEN=$(printf %q "${BOT_TOKEN:-}"); export MQTT_HOST MQTT_PORT MQTT_TLS MQTT_USER MQTT_PASS BOT_TOKEN; \
    S=sudo; [ \$(id -u) = 0 ] && S=; [ -x /opt/mp/kit/update.sh ] && \$S /opt/mp/kit/update.sh; \
    cd /tmp && curl -fsSLO $REL/install_247.sh && \$S -E bash install_247.sh"
}

widget() {   # Termux:Widget buttons (~/.shortcuts)
  mkdir -p "$HOME/.shortcuts"; local n c
  for n in "OMP Status:status" "OMP Install all:install" "OMP Restart:restart" "OMP Backup:backup" "OMP Logs:logs" "OMP Live:watch" "OMP Menu:"; do
    c="${n#*:}"; printf '#!/data/data/com.termux/files/usr/bin/bash\n%s %s\nread -r -p "press Enter to close" _\n' "$SELF" "$c" > "$HOME/.shortcuts/${n%%:*}"
    chmod +x "$HOME/.shortcuts/${n%%:*}"; done
  echo "added 7 buttons in ~/.shortcuts - install Termux:Widget and add its widget to the home screen"
}

notify() {   # background: phone notification when a server goes offline or comes back
  load || return 1; getbot || return 1; mqargs; command -v termux-notification >/dev/null || [ $TERMUX = 0 ] || pkg install -y termux-api
  declare -A last
  while true; do
    PY "$BOT" watch "${A[@]}" 2>&1 | while read -r name st c _r; do
      [ "$name" = live ] && continue
      [ "$name" = bot ] && { name="bot $st"; st="$c"; }
      [ "${last[$name]:-}" = "$st" ] && continue; prev="${last[$name]:-}"; last[$name]="$st"
      [ -z "$prev" ] && [ "$st" != OFFLINE ] && [ "$st" != offline ] && continue
      if command -v termux-notification >/dev/null; then termux-notification --id "omp-$name" -t "open.mp $name" -c "$name is $st" --priority high
      else echo "$(date '+%F %T') $name is $st"; fi
    done; sleep 15
  done
}
boot() {
  [ $TERMUX = 1 ] || { echo "Termux:Boot is Termux only - on Linux run: omp247 notify &"; return 1; }
  pkg install -y termux-api >/dev/null 2>&1; mkdir -p "$HOME/.termux/boot"
  printf '#!/data/data/com.termux/files/usr/bin/sh\ntermux-wake-lock\nnohup %s notify > %s/notify.log 2>&1 &\n' "$SELF" "$D" > "$HOME/.termux/boot/omp247-notify"
  chmod +x "$HOME/.termux/boot/omp247-notify"; nohup "$SELF" notify > "$D/notify.log" 2>&1 &
  echo "notifications on (now and after every phone restart). Needs the Termux:Boot and Termux:API apps."
}
update() { curl -fsSL "$RAW/termux_omp247.sh" -o "$SELF.new" && chmod +x "$SELF.new" && mv "$SELF.new" "$SELF" && getbot force && echo "omp247 updated"; }

menu() {
  need && getbot && load || return 1
  while true; do
    echo; echo "== omp247  broker $MQTT_HOST:$MQTT_PORT  bot ${BOT_NAME:-all} =="
    echo " 1) server setup (here, or over SSH)  2) install every server (auto)"
    echo " 3) servers found on the machine      4) list installed"
    echo " 5) status   6) logs   7) restart    8) backup   9) live status"
    echo " w) home-screen buttons   n) offline notifications   u) update   c) settings   0) exit"
    local k; read -r -p "> " k </dev/tty || return 0
    case "$k" in
      1) setup;; 2) cmd install;; 3) cmd find;; 4) cmd list;; 5) cmd status;; 6) cmd logs;;
      7) cmd restart;; 8) cmd backup;; 9) watch;; w) widget;; n) boot;; u) update; exec "$SELF";;
      c) config; . "$CONF";; 0|q) return 0;; *) echo "?";;
    esac
  done
}

case "${1:-menu}" in
  menu) menu;; config) config;; setup) setup;; watch) shift; watch "$@";; widget) widget;; boot) boot;; notify) notify;; update) update;;
  install|find|list|status|logs|restart|start|stop|backup|ping|help) cmd "$@";;
  *) sed -n '2,20p' "$0";;
esac
