#!/usr/bin/env bash
# mpadmin - MP admin console for Termux (5.02.71). One place on the phone for the VPS, the firewall, the global
# VPS network and MP's logs, built on the Termux add-ons (https://github.com/orgs/termux/repositories):
#   Termux:API (notifications, battery, wifi)   Termux:Boot (start at phone boot)   Termux:Widget (home-screen buttons)
#   termux-services (runit: the edge client runs in the background)   proot-distro (Debian lab on the phone)
#   termux-auth (password for Termux itself)
#
# Install (one line in Termux):
#   curl -fsSL https://raw.githubusercontent.com/JebetrexxMC/wagu/main/tools/termux_admin/mpadmin.sh -o $PREFIX/bin/mpadmin && chmod +x $PREFIX/bin/mpadmin && mpadmin setup
#
#   mpadmin                 menu
#   mpadmin setup           packages + omp247 (talks to omp_autobot on the VPS over HiveMQ) + edge client + buttons
#   mpadmin status          one screen: phone, network, VPS bot, firewall, fastest VPS
#   mpadmin fw [install|status|off]        xdp-firewall with the MP rules on the VPS, through the autobot (no SSH)
#   mpadmin bot [ping|update|<omp247 cmd>] the VPS bot (omp247)
#   mpadmin edge [on|off|once|log]         fastest VPS of the global VPS network, in the background + notifications
#   mpadmin ssh [key|run CMD|login]        SSH key for the VPS (then: mpadmin ssh run 'uptime')
#   mpadmin lab             global center + 2 VPS test inside proot-distro Debian on the phone
#   mpadmin log             MP's mp_log.txt / crash log from the game folder -> Downloads (to send)
#   mpadmin lock            Termux password (termux-auth)
#   mpadmin cgnat [WAN-IP]  NetWire 1.4: is the home line behind Converge CGNAT? (no router password needed)
#   mpadmin gw [status|rules|off]          CGNAT gateway on the VPS, through the autobot
#   mpadmin duck [status|once|pin IP|unpin|token T]   the 4 region names (net/cgnat/duckdns-mp.sh)
#   mpadmin udp <host> [port]              UDP ping (net/udp-bench); the VPS side: mpadmin bot udpbench <host>
#   mpadmin widget | boot | update | config
set -u
RAW=https://raw.githubusercontent.com/JebetrexxMC/wagu/main
D="$HOME/.mpadmin"; CONF="$D/conf"; mkdir -p "$D"; chmod 700 "$D"
TERMUX=0; case "${PREFIX:-}" in *com.termux*) TERMUX=1;; esac
P="${PREFIX:-/usr}"
SELF="$(command -v mpadmin 2>/dev/null || echo "$0")"
SVC="$P/var/service/mp-edge"
GAME_DIRS="$HOME/storage/shared/Android_unprotected/data/com.rockstargames.gtasa/files/TYFMC/MP $HOME/storage/shared/Android/data/com.rockstargames.gtasa/files/TYFMC/MP"

c() { printf '\033[%sm%s\033[0m' "$1" "$2"; }
say() { echo "$(c '1;36' '[mpadmin]') $*"; }
has() { command -v "$1" >/dev/null 2>&1; }
ask() { local v; read -r -p "$1 [${2:-}]: " v </dev/tty; echo "${v:-${2:-}}"; }
pkgi() { if [ $TERMUX = 1 ]; then pkg install -y "$@"; elif has apt-get; then sudo apt-get install -y "$@"; else echo "install: $*"; fi; }
notify() {   # notify <id> <title> <text>
  if has termux-notification; then termux-notification --id "mp-$1" -t "$2" -c "$3" --priority high 2>/dev/null
  else echo "$(date '+%F %T') $2: $3"; fi
}

save() { umask 077; { for k in CENTER SEEDS SSH_TARGET; do printf '%s=%q\n' "$k" "${!k:-}"; done; } > "$CONF"; }
load() { CENTER=""; SEEDS=""; SSH_TARGET=""; [ -f "$CONF" ] && . "$CONF"; return 0; }
config() {
  load
  CENTER=$(ask "global center URL (empty = none, use seeds)" "$CENTER")
  SEEDS=$(ask "VPS seeds, space separated (e.g. http://mpsamp.duckdns.org:8003)" "$SEEDS")
  SSH_TARGET=$(ask "SSH login of the VPS (e.g. root@1.2.3.4, empty = none)" "$SSH_TARGET")
  save; say "saved in $CONF"
}

getfiles() {   # omp247 + omp_autobot (VPS bot client) and the edge client
  curl -fsSL "$RAW/tools/omp_247/termux_omp247.sh" -o "$P/bin/omp247" && chmod +x "$P/bin/omp247" || return 1
  curl -fsSL "$RAW/net/global-satellite-vps-network/edge_client/edge_client.py" -o "$D/edge_client.py" || return 1
  say "omp247 + edge_client.py updated"
}

setup() {
  say "packages (python, curl, openssh, termux-api, termux-services)"
  if [ $TERMUX = 1 ]; then pkg update -y >/dev/null 2>&1; pkgi python curl openssh termux-api termux-services; else pkgi python3 curl openssh-client; fi
  getfiles || { say "download failed - check the internet"; return 1; }
  [ -f "$CONF" ] || config
  say "VPS bot login (HiveMQ broker of the VPS, user admin)"; omp247 config </dev/tty || true
  widget; [ $TERMUX = 1 ] && boot
  [ $TERMUX = 1 ] && [ ! -d "$HOME/storage" ] && { say "allow storage (for mp_log.txt)"; termux-setup-storage; }
  say "done - type: mpadmin"
}

bot() { has omp247 || getfiles || return 1; omp247 "${@:-ping}"; }
fw() {
  local s="${1:-status}"
  case "$s" in install|status|off|conf) ;; *) echo "mpadmin fw install|status|off"; return 1;; esac
  [ "$s" = install ] && say "installing xdp-firewall on the VPS (build takes a few minutes, the bot answers when done)"
  bot xdpfw "$s"; local r=$?
  if [ "$s" = install ]; then
    if [ $r = 0 ]; then notify fw "MP firewall" "xdp-firewall is running on the VPS"; else notify fw "MP firewall" "install failed - see Termux"; fi
  fi
  return $r
}

# NetWire 1.4: CGNAT kit (net/cgnat, net/udp-bench) - fetched on first use
netget() { [ -f "$D/$(basename "$1")" ] && [ "${2:-}" != new ] || curl -fsSL "$RAW/net/$1" -o "$D/$(basename "$1")" || { say "download failed"; return 1; }; }
cgnat() { has traceroute || pkgi traceroute; netget cgnat/cgnat-check.sh new && bash "$D/cgnat-check.sh" "$@"; }
duck() { netget cgnat/duckdns-mp.sh && bash "$D/duckdns-mp.sh" "${@:-status}"; }
udpb() { [ -n "${1:-}" ] || { echo "mpadmin udp <host> [port]"; return 2; }; netget udp-bench/udp-bench.sh && bash "$D/udp-bench.sh" rtt "$1" "${2:-1939}" 50; }
gw() { bot gateway "${1:-status}"; }

edge_args() { load; local a=""; [ -n "$CENTER" ] && a="--center $CENTER"; for s in $SEEDS; do a="$a --seed $s"; done; echo "$a"; }
edge() {
  [ -f "$D/edge_client.py" ] || getfiles || return 1
  local a; a=$(edge_args); [ -n "$a" ] || { say "set the global center or VPS seeds first"; config; a=$(edge_args); }
  case "${1:-once}" in
    once) python3 "$D/edge_client.py" $a --once --wires-out "$D/mp_wires.txt" | python3 -c '
import json,sys
d=json.load(sys.stdin); a=d.get("active") or {}
print("active:", a.get("continent","none"), a.get("url",""), "link:", d.get("link",""))
for r in d.get("ranked",[]): print("  %-5s %s ms" % (r.get("continent"), r.get("rtt")))' ;;
    on)
      if [ $TERMUX = 0 ] || ! has sv; then
        say "no termux-services here - running in the foreground (Ctrl+C stops)"; edge_loop "$a"; return
      fi
      mkdir -p "$SVC/log"
      printf '#!%s/bin/sh\nexec 2>&1\nexec %s edge-loop\n' "$P" "$SELF" > "$SVC/run"
      printf '#!%s/bin/sh\nmkdir -p %s/var/log/sv/mp-edge\nexec svlogd -tt %s/var/log/sv/mp-edge\n' "$P" "$P" "$P" > "$SVC/log/run"
      chmod +x "$SVC/run" "$SVC/log/run"; sv-enable mp-edge 2>/dev/null; sv up mp-edge
      say "edge client runs in the background (termux-services). Log: mpadmin edge log" ;;
    off) has sv && { sv down mp-edge; sv-disable mp-edge 2>/dev/null; }; say "edge client stopped" ;;
    log) tail -n 40 "$P/var/log/sv/mp-edge/current" 2>/dev/null || echo "(no log yet)" ;;
    *) echo "mpadmin edge on|off|once|log" ;;
  esac
}
edge_loop() {   # foreground: edge client + a notification on every pick / failover / outage
  local a="${1:-$(edge_args)}"
  has termux-wake-lock && termux-wake-lock
  python3 -u "$D/edge_client.py" $a --wires-out "$D/mp_wires.txt" | while IFS= read -r l; do
    echo "$l"
    case "$l" in
      *failover*) notify edge "MP: switched VPS" "${l#* }" ;;
      *"no VPS answers"*) notify edge "MP: no VPS answers" "${l#* }" ;;
      *pick:*) notify edge "MP: VPS" "${l#* }" ;;
    esac
  done
}

sshx() {
  load; has ssh || pkgi openssh
  case "${1:-key}" in
    key)
      [ -f "$HOME/.ssh/id_ed25519" ] || ssh-keygen -t ed25519 -N "" -f "$HOME/.ssh/id_ed25519" -C "mpadmin@$(hostname 2>/dev/null || echo phone)"
      [ -n "$SSH_TARGET" ] || { SSH_TARGET=$(ask "SSH login of the VPS (e.g. root@1.2.3.4)" ""); save; }
      [ -n "$SSH_TARGET" ] || return 1
      say "copying the key to $SSH_TARGET (type the VPS password once)"
      ssh-copy-id -i "$HOME/.ssh/id_ed25519.pub" "$SSH_TARGET" 2>/dev/null \
        || ssh "$SSH_TARGET" "mkdir -p ~/.ssh && chmod 700 ~/.ssh && cat >> ~/.ssh/authorized_keys" < "$HOME/.ssh/id_ed25519.pub"
      ssh -o BatchMode=yes "$SSH_TARGET" true && say "key works - no password needed now" ;;
    run) shift; [ -n "$SSH_TARGET" ] || { say "mpadmin ssh key first"; return 1; }; ssh -t "$SSH_TARGET" "$@" ;;
    login) [ -n "$SSH_TARGET" ] || { say "mpadmin ssh key first"; return 1; }; ssh -t "$SSH_TARGET" ;;
    *) echo "mpadmin ssh key|run CMD|login" ;;
  esac
}

lab() {   # global center + 2 VPS + edge client, all inside proot-distro Debian (pydantic needs glibc wheels)
  [ $TERMUX = 1 ] || { say "lab is for Termux; on Linux run net/global-satellite-vps-network directly"; return 1; }
  has proot-distro || pkgi proot-distro
  proot-distro list 2>/dev/null | grep -q "debian.*installed" || proot-distro install debian
  proot-distro login debian -- bash -c '
set -e
command -v pip3 >/dev/null || { apt-get update -qq; apt-get install -y -qq python3-pip curl git >/dev/null; }
python3 -c "import fastapi, uvicorn, httpx" 2>/dev/null || pip3 install -q --break-system-packages fastapi uvicorn httpx pydantic
[ -d /root/wagu ] || git clone -q --depth 1 --filter=blob:none --sparse https://github.com/JebetrexxMC/wagu /root/wagu
cd /root/wagu && git sparse-checkout set net && git pull -q --ff-only || true
cd net/global-satellite-vps-network
PORT=8000 python3 global_center/global_center.py >/tmp/gc.log 2>&1 & GC=$!; sleep 2
CONTINENT=ASIA PORT=8003 PUBLIC_URL=http://127.0.0.1:8003 GLOBAL_CENTER_URL=http://127.0.0.1:8000/global/sync GAME_HOST=mpsamp.duckdns.org SYNC_S=1 python3 continental_vps/continental_vps.py >/tmp/a.log 2>&1 & A=$!
CONTINENT=EU PORT=8002 PUBLIC_URL=http://127.0.0.1:8002 GLOBAL_CENTER_URL=http://127.0.0.1:8000/global/sync GAME_HOST=eusamp.duckdns.org SYNC_S=1 python3 continental_vps/continental_vps.py >/tmp/e.log 2>&1 & E=$!
sleep 4; echo "center: $(curl -s 127.0.0.1:8000/health)"
python3 edge_client/edge_client.py --center http://127.0.0.1:8000 --once | python3 -c "import json,sys;d=json.load(sys.stdin);print(\"edge pick:\",d[\"active\"][\"continent\"])"
kill $A; echo "ASIA VPS stopped"; sleep 1
python3 edge_client/edge_client.py --center http://127.0.0.1:8000 --seed http://127.0.0.1:8002 --once | python3 -c "import json,sys;d=json.load(sys.stdin);print(\"edge pick now:\",(d.get(\"active\") or {}).get(\"continent\"))"
kill $GC $E 2>/dev/null; echo "lab done"'
}

mplog() {
  [ $TERMUX = 1 ] && [ ! -d "$HOME/storage" ] && termux-setup-storage
  local d f out="$HOME/storage/downloads/mp-logs-$(date +%Y%m%d-%H%M%S)" found=0
  for d in $GAME_DIRS; do [ -d "$d" ] || continue
    mkdir -p "$out"
    for f in mp_log.txt mp_log_prev.txt; do [ -f "$d/$f" ] && cp "$d/$f" "$out/" && found=1; done
    # AML writes aml_crashlog.txt in the game data folder (3 levels above files/TYFMC/MP)
    for f in "$d"/../../../aml_crashlog*.txt; do [ -f "$f" ] && cp "$f" "$out/" && found=1; done
  done
  [ $found = 1 ] || { say "no mp_log.txt found (open MP once, and allow storage)"; return 1; }
  say "copied to Downloads/$(basename "$out") - send these files"
  echo "--- last lines of mp_log.txt"; tail -n 15 "$out/mp_log.txt" 2>/dev/null
}

status() {
  load
  echo "$(c '1;33' '== MP admin status') $(date '+%F %T')"
  if has termux-battery-status; then
    termux-battery-status 2>/dev/null | python3 -c 'import json,sys;b=json.load(sys.stdin);print("phone: battery %s%% %s, %s C" % (b.get("percentage"),b.get("status","").lower(),b.get("temperature")))' 2>/dev/null
    termux-wifi-connectioninfo 2>/dev/null | python3 -c 'import json,sys;w=json.load(sys.stdin);print("wifi:", w.get("ssid","-"), "%s dBm" % w.get("rssi","?"))' 2>/dev/null
  fi
  echo "--- VPS bot"; bot ping 2>&1 | tail -2
  echo "--- firewall"; bot xdpfw status 2>&1 | grep -E "Active:|prog/xdp|maps pinned|FAILED|unknown|no answer" | head -4
  echo "--- fastest VPS"; edge once 2>&1 | head -4
  has sv && { echo "--- background edge"; sv status mp-edge 2>/dev/null || echo "off"; }
}

widget() {
  mkdir -p "$HOME/.shortcuts"; local n
  for n in "MP Admin:" "MP Status:status" "MP Firewall:fw status" "MP Firewall install:fw install" "MP Fastest VPS:edge once" "MP Logs:log"; do
    printf '#!%s/bin/bash\n%s %s\nread -r -p "press Enter to close" _\n' "$P" "$SELF" "${n#*:}" > "$HOME/.shortcuts/${n%%:*}"
    chmod +x "$HOME/.shortcuts/${n%%:*}"; done
  say "6 home-screen buttons in ~/.shortcuts (Termux:Widget app)"
}
boot() {
  mkdir -p "$HOME/.termux/boot"
  printf '#!%s/bin/sh\ntermux-wake-lock\n[ -d %s ] && sv up mp-edge 2>/dev/null\n' "$P" "$SVC" > "$HOME/.termux/boot/mpadmin"
  chmod +x "$HOME/.termux/boot/mpadmin"; say "Termux:Boot: background edge client starts after a phone restart (when it is on)"
}
lock() { has passwd || pkgi termux-auth; say "set a password for Termux (asked on the next Termux start when you use: login)"; passwd; }
update() { curl -fsSL "$RAW/tools/termux_admin/mpadmin.sh" -o "$SELF.new" && chmod +x "$SELF.new" && mv "$SELF.new" "$SELF" && getfiles && say "mpadmin updated"; }

menu() {
  load
  while true; do
    echo; echo "$(c '1;36' '== mpadmin') center ${CENTER:-none}  ssh ${SSH_TARGET:-none}"
    echo " 1) status (phone, VPS bot, firewall, fastest VPS)"
    echo " 2) firewall on the VPS: install     3) firewall status     4) firewall off"
    echo " 5) VPS bot ping                    6) update the VPS bot  7) omp247 console (open.mp servers)"
    echo " 8) fastest VPS now                 9) background edge on  0) background edge off   l) edge log"
    echo " k) SSH key for the VPS             s) SSH into the VPS    t) Debian lab test (proot-distro)"
    echo " n) CGNAT check (home line)         G) CGNAT gateway on the VPS      d) DuckDNS region names"
    echo " g) MP logs -> Downloads            w) buttons  b) boot  p) Termux password  u) update  c) settings  q) exit"
    local k; read -r -p "> " k </dev/tty || return 0
    case "$k" in
      1) status;; 2) fw install;; 3) fw status;; 4) fw off;; 5) bot ping;; 6) bot botupdate;; 7) omp247;;
      8) edge once;; 9) edge on;; 0) edge off;; l) edge log;; k) sshx key;; s) sshx login;; t) lab;;
      n) cgnat;; G) gw status;; d) duck status;;
      g) mplog;; w) widget;; b) boot;; p) lock;; u) update; exec "$SELF";; c) config;; q|Q) return 0;; *) echo "?";;
    esac
  done
}

case "${1:-menu}" in
  menu) menu;; setup) setup;; status) status;; config) config;; update) update;; widget) widget;; boot) boot;; lock) lock;;
  fw|firewall) shift; fw "$@";; bot) shift; bot "$@";; edge) shift; edge "$@";; edge-loop) edge_loop;;
  ssh) shift; sshx "$@";; lab) lab;; log|logs) mplog;;
  cgnat) shift; cgnat "$@";; gw|gateway) shift; gw "$@";; duck|duckdns) shift; duck "$@";; udp) shift; udpb "$@";;
  *) sed -n '2,28p' "$0";;
esac
