#!/usr/bin/env bash
# 5.02.48: the 24/7 open.mp crossplay server (PC + Android in one world), run by mp-247-servers.yml
# next to the MP featured servers. Works on any Linux x86_64 box too (VPS, WSL, home PC).
#   ./omp_crossplay.sh start [region]   download open.mp 1.6.7 if needed, install mp-crossplay.so, start (restarts by itself)
#   ./omp_crossplay.sh stop             stop the server (and the playit tunnel)
#   ./omp_crossplay.sh status [file]    ask the server (SA-MP query) and write the status JSON for the website
#
# Ports (UDP):  7778 = PC players (open.mp launcher / SA-MP 0.3.7 + 0.3.DL client)
#               7780 = Android phones (MP mod) - they also find it in the MP internet lobby through the broker,
#                      so phones need no open port at all.
# PC players need a public UDP address. A GitHub runner has none, so the PC side goes through a playit.gg
# UDP tunnel (5.02.50: playit_agent.py runs the playit agent from release v5.02.30 here - nothing to
# download on a computer; link the playit account from the website console mpsamp-open.github.io/playit.html).
# The address comes from the tunnel by itself; OMP_PUBLIC_ADDR (repo variable) overrides it.
# 5.02.52: or our own open-source tunnel (frp): FRP_SERVER (variable, host:7000) + FRP_TOKEN (secret) from
# HiveMQ/frps-setup.sh on the HiveMQ VPS - used first; playit then only runs with PLAYIT=on.
# On a VPS with an open UDP 7778: OMP_PUBLIC_ADDR=<vps ip>:7778 and PLAYIT=off.
#
# Optional env: OMP_NAME, OMP_GAMEMODE (simpletdm | derby | gungame, default simpletdm), OMP_MAX (100),
#               OMP_PORT (7778), OMP_MOBILE_PORT (7780), OMP_DIR (omp/), OMP_COMPONENT (mp-crossplay.so path)
cd "$(dirname "$0")" || exit 1
HERE=$PWD
V=v1.6.7.3218                                   # = the open.mp-sdk commit mp-crossplay.so is built against
OMP_DIR="${OMP_DIR:-$HERE/omp}"
SRV="$OMP_DIR/Server"
PORT="${OMP_PORT:-}"
# 5.02.57: SA-MP/open.mp key the game packets on the server port, so behind playit open.mp listens on the tunnel's
# public port (playit_agent.py writes it to playit.port and points the tunnel's local port there). frp keeps 7778 = 7778.
if [ -z "$PORT" ] && [ -z "${FRP_SERVER:-}" ] && [ "${PLAYIT:-auto}" != off ] && grep -qxE '[0-9]{2,5}' playit.port 2>/dev/null; then
    PORT=$(cat playit.port)
fi
PORT="${PORT:-7778}"
MPORT="${OMP_MOBILE_PORT:-7780}"
NAME="${OMP_NAME:-MP Crossplay 24/7 [PC + Mobile]}"
MODE="${OMP_GAMEMODE:-simpletdm}"
MAX="${OMP_MAX:-100}"
COMP="${OMP_COMPONENT:-$HERE/../crossplay/out/linux/mp-crossplay.so}"
mkdir -p logs run

stop_all() {
    [ -f run/omp.pid ] && kill "$(cat run/omp.pid)" 2>/dev/null; rm -f run/omp.pid
    pkill -f "$SRV/omp-server" 2>/dev/null
    [ -f run/playit.pid ] && kill "$(cat run/playit.pid)" 2>/dev/null; rm -f run/playit.pid
    pkill -f playit-linux-amd64 2>/dev/null
    [ -f run/frpc.pid ] && kill "$(cat run/frpc.pid)" 2>/dev/null; rm -f run/frpc.pid
    pkill -f "$OMP_DIR/frpc" 2>/dev/null
    return 0
}

install_server() {
    if [ ! -x "$SRV/omp-server" ]; then
        echo "  downloading open.mp server $V"
        mkdir -p "$OMP_DIR"
        curl -fsL --retry 3 -o "$OMP_DIR/omp.tgz" \
            "https://github.com/openmultiplayer/open.mp/releases/download/$V/open.mp-linux-x86.tar.gz" || return 1
        tar xzf "$OMP_DIR/omp.tgz" -C "$OMP_DIR" && rm -f "$OMP_DIR/omp.tgz" || return 1
    fi
    [ -f "$COMP" ] || { echo "  mp-crossplay.so missing ($COMP) - build it: ZIG=zig bash crossplay/build.sh linux"; return 1; }
    cp -f "$COMP" "$SRV/components/mp-crossplay.so"
    # config.json: our port, no artwork web server (it would take TCP 7777 from the VICE City server),
    # not announced on the open.mp list (the runner's own IP is not reachable - the website lists the tunnel address)
    NAME="$NAME" PORT="$PORT" MODE="$MODE" MAX="$MAX" python3 - "$SRV/config.json" <<'PY'
import json, os, sys
p = sys.argv[1]; c = json.load(open(p))
c["name"] = os.environ["NAME"]; c["announce"] = False; c["password"] = ""
c["max_players"] = int(os.environ["MAX"]); c["max_bots"] = int(os.environ["MAX"])
c.setdefault("network", {})["port"] = int(os.environ["PORT"])
if os.environ.get("OMP_BIND"): c["network"]["bind"] = os.environ["OMP_BIND"]   # tests: relay on the same port
c["network"]["allow_037_clients"] = True                     # SA-MP 0.3.7 client + open.mp launcher
c.setdefault("artwork", {})["enable"] = False
c.setdefault("rcon", {})["enable"] = False
c["website"] = "mpsamp-open.github.io/pc.html"
c.setdefault("pawn", {})["main_scripts"] = [os.environ["MODE"] + " 1"]
c.setdefault("game", {})["mode"] = "Crossplay PC + Android"
json.dump(c, open(p, "w"), indent=1)
PY
    # mp_crossplay.cfg (read by the component at start - see crossplay/README.md)
    cat > "$SRV/mp_crossplay.cfg" <<EOF
port=$MPORT
name=$NAME
region=$REGION
online=1
max=$MAX
EOF
}

start_frp() {   # 5.02.52: our own open-source tunnel (frp) to the box set up with HiveMQ/frps-setup.sh
    [ -n "${FRP_SERVER:-}" ] && [ -n "${FRP_TOKEN:-}" ] || return 1
    if [ -f run/frpc.pid ] && kill -0 "$(cat run/frpc.pid)" 2>/dev/null; then return 0; fi
    local v=0.72.0 host="${FRP_SERVER%:*}" sport="${FRP_SERVER##*:}" rport="${FRP_REMOTE_PORT:-7778}"
    [ "$sport" = "$FRP_SERVER" ] && sport=7000
    if [ ! -x "$OMP_DIR/frpc" ]; then
        mkdir -p "$OMP_DIR"
        curl -fsSL "https://github.com/fatedier/frp/releases/download/v$v/frp_${v}_linux_amd64.tar.gz" \
            | tar -xz -C "$OMP_DIR" --strip-components=1 "frp_${v}_linux_amd64/frpc" || { echo "  PC tunnel: frpc download failed"; return 1; }
    fi
    umask 077; cat > run/frpc.toml <<CFG
serverAddr = "$host"
serverPort = $sport
auth.method = "token"
auth.token = "$FRP_TOKEN"
loginFailExit = false
log.to = "$HERE/logs/frpc.log"
log.maxDays = 1

[[proxies]]
name = "mp-crossplay-pc-${REGION:-Asia}"
type = "udp"
localIP = "127.0.0.1"
localPort = $PORT
remotePort = $rport
CFG
    umask 022
    ( set -m; nohup bash -c "while true; do '$OMP_DIR/frpc' -c '$HERE/run/frpc.toml' > /dev/null 2>&1; sleep 3; done" > /dev/null 2>&1 & echo $! > run/frpc.pid )
    echo "$host:$rport" > run/frp.addr
    echo "  PC tunnel: frp -> $host:$rport (UDP, own server $host:$sport)"
}

start_tunnel() {   # 5.02.50: playit.gg agent from release v5.02.30 (playit_agent.py: account link, key, UDP tunnel, address)
    start_frp && [ "${PLAYIT:-auto}" != on ] && return 0          # 5.02.52: own frp server first
    [ "${PLAYIT:-auto}" = off ] && { echo "  PC tunnel: playit off (PLAYIT=off)"; return 0; }
    local ver; ver=$(md5sum "$HERE/playit_agent.py" | cut -c1-12)
    if [ -f run/playit.pid ] && kill -0 "$(cat run/playit.pid)" 2>/dev/null; then
        [ "$(cat run/playit.ver 2>/dev/null)" = "$ver" ] && return 0
        kill "$(cat run/playit.pid)" 2>/dev/null; pkill -f playit-linux-amd64 2>/dev/null; sleep 1   # 5.02.55: new code -> restart
    fi
    echo "$ver" > run/playit.ver
    ( set -m; nohup python3 "$HERE/playit_agent.py" >> logs/playit_agent.log 2>&1 & echo $! > run/playit.pid )
    echo "  PC tunnel: playit.gg agent started (UDP $PORT) - live on mpsamp-open.github.io/playit.html"
}

status() {   # SA-MP query 'i' (works on open.mp): online, players, max -> JSON for docs/pc.html
    local out="${1:-$HERE/omp247.json}"
    PORT="$PORT" NAME="$NAME" MPORT="$MPORT" REGION="$REGION" ADDR="${OMP_PUBLIC_ADDR:-$(cat run/frp.addr 2>/dev/null || cat run/playit.addr 2>/dev/null)}" MODE="$MODE" \
    TUNNEL="$([ -f run/frpc.pid ] && echo frp || { [ -f run/playit.pid ] && echo playit; } || echo none)" python3 - "$out" <<'PY'
import json, os, socket, struct, sys, time
port = int(os.environ["PORT"]); res = {"online": False, "players": 0, "max": 0}
try:
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.settimeout(2)
    s.sendto(b"SAMP" + bytes([127, 0, 0, 1]) + struct.pack("<H", port) + b"i", ("127.0.0.1", port))
    d = s.recv(2048)[11:]
    pw, players, mx = d[0], struct.unpack("<H", d[1:3])[0], struct.unpack("<H", d[3:5])[0]
    res = {"online": True, "players": players, "max": mx}
except Exception:
    pass
res.update({"name": os.environ["NAME"], "addr": os.environ["ADDR"], "port": port,
            "mobile_port": int(os.environ["MPORT"]), "region": os.environ["REGION"], "gamemode": os.environ["MODE"],
            "tunnel": os.environ["TUNNEL"], "server": "open.mp 1.6.7 + mp-crossplay",
            "clients": ["open.mp launcher", "SA-MP 0.3.7", "SA-MP 0.3.DL", "MP for Android"],
            "updated": int(time.time())})
json.dump(res, open(sys.argv[1], "w"), indent=1)
print("  open.mp crossplay: %s, %d/%d players" % ("online" if res["online"] else "OFFLINE", res["players"], res["max"]))
PY
}

case "${1:-start}" in
    stop) stop_all; echo "open.mp crossplay stopped"; exit 0 ;;
    status) REGION="${REGION:-Asia}"; status "${2:-}"; exit 0 ;;
    start) REGION="${2:-${REGION:-Asia}}" ;;
    *) echo "usage: $0 start [region] | stop | status [file]"; exit 1 ;;
esac

echo "open.mp crossplay server, region $REGION:"
if [ -f run/omp.pid ] && kill -0 "$(cat run/omp.pid)" 2>/dev/null && [ "$(cat run/omp.port 2>/dev/null || echo "$PORT")" != "$PORT" ]; then
    echo "  PC port changed $(cat run/omp.port) -> $PORT (the tunnel's public port): restarting open.mp"
    kill "$(cat run/omp.pid)" 2>/dev/null; pkill -f "$SRV/omp-server" 2>/dev/null; rm -f run/omp.pid; sleep 2
fi
if [ -f run/omp.pid ] && kill -0 "$(cat run/omp.pid)" 2>/dev/null; then
    echo "  already running (PC UDP $PORT, phones UDP $MPORT)"
else
    install_server || { echo "  open.mp crossplay NOT started"; exit 1; }
    ( cd "$SRV" && while true; do
          ./omp-server < /dev/null >> "$HERE/logs/omp.log" 2>&1
          echo "[omp_crossplay] omp-server exited ($?), back in 3 s" >> "$HERE/logs/omp.log"; sleep 3
      done ) &
    echo $! > run/omp.pid; echo "$PORT" > run/omp.port
    echo "  started: \"$NAME\" ($MODE), PC UDP $PORT, phones UDP $MPORT + MP lobby"
fi
start_tunnel
exit 0
