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
# tunnel when PLAYIT_SECRET is set (repo secret), and the address players use is OMP_PUBLIC_ADDR
# (repo variable, e.g. 147.185.221.20:12345 - the tunnel's address in the playit.gg dashboard).
# On a VPS with an open UDP 7778 just set OMP_PUBLIC_ADDR=<vps ip>:7778 and leave PLAYIT_SECRET empty.
#
# Optional env: OMP_NAME, OMP_GAMEMODE (simpletdm | derby | gungame, default simpletdm), OMP_MAX (100),
#               OMP_PORT (7778), OMP_MOBILE_PORT (7780), OMP_DIR (omp/), OMP_COMPONENT (mp-crossplay.so path)
cd "$(dirname "$0")" || exit 1
HERE=$PWD
V=v1.6.7.3218                                   # = the open.mp-sdk commit mp-crossplay.so is built against
OMP_DIR="${OMP_DIR:-$HERE/omp}"
SRV="$OMP_DIR/Server"
PORT="${OMP_PORT:-7778}"
MPORT="${OMP_MOBILE_PORT:-7780}"
NAME="${OMP_NAME:-MP Crossplay 24/7 [PC + Mobile]}"
MODE="${OMP_GAMEMODE:-simpletdm}"
MAX="${OMP_MAX:-100}"
COMP="${OMP_COMPONENT:-$HERE/../crossplay/out/linux/mp-crossplay.so}"
mkdir -p logs run

stop_all() {
    [ -f run/omp.pid ] && kill "$(cat run/omp.pid)" 2>/dev/null; rm -f run/omp.pid
    pkill -f "$SRV/omp-server" 2>/dev/null
    command -v docker > /dev/null && docker rm -f mp-playit > /dev/null 2>&1
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

start_tunnel() {   # playit.gg agent (UDP tunnel for the PC side); the tunnel itself is made once in the playit dashboard
    [ -n "${PLAYIT_SECRET:-}" ] || { echo "  PC tunnel: off (no PLAYIT_SECRET) - PC players need OMP_PUBLIC_ADDR on a VPS"; return 0; }
    command -v docker > /dev/null || { echo "  PC tunnel: docker missing"; return 0; }
    if [ "$(docker inspect -f '{{.State.Running}}' mp-playit 2>/dev/null)" = true ]; then return 0; fi
    docker rm -f mp-playit > /dev/null 2>&1
    docker run -d --name mp-playit --restart unless-stopped --network host \
        -e SECRET_KEY="$PLAYIT_SECRET" ghcr.io/playit-cloud/playit-agent:latest > /dev/null \
        && echo "  PC tunnel: playit.gg agent started (UDP $PORT -> ${OMP_PUBLIC_ADDR:-the dashboard address})" \
        || echo "  PC tunnel: playit agent did not start"
}

status() {   # SA-MP query 'i' (works on open.mp): online, players, max -> JSON for docs/pc.html
    local out="${1:-$HERE/omp247.json}"
    PORT="$PORT" NAME="$NAME" MPORT="$MPORT" REGION="$REGION" ADDR="${OMP_PUBLIC_ADDR:-}" MODE="$MODE" \
    TUNNEL="$([ -n "${PLAYIT_SECRET:-}" ] && echo playit || echo none)" python3 - "$out" <<'PY'
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
if [ -f run/omp.pid ] && kill -0 "$(cat run/omp.pid)" 2>/dev/null; then
    echo "  already running (PC UDP $PORT, phones UDP $MPORT)"
else
    install_server || { echo "  open.mp crossplay NOT started"; exit 1; }
    ( cd "$SRV" && while true; do
          ./omp-server < /dev/null >> "$HERE/logs/omp.log" 2>&1
          echo "[omp_crossplay] omp-server exited ($?), back in 3 s" >> "$HERE/logs/omp.log"; sleep 3
      done ) &
    echo $! > run/omp.pid
    echo "  started: \"$NAME\" ($MODE), PC UDP $PORT, phones UDP $MPORT + MP lobby"
fi
start_tunnel
exit 0
