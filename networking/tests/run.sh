#!/bin/bash
# MP NetWire local test: wire parser, MP / UDP probes, relay best-upstream pick, relay traffic counters.
set -u
cd "$(dirname "$0")/.."
D=build/mpnetd; T=$(mktemp -d); fail=0
ok() { echo "ok   $*"; }; bad() { echo "FAIL $*"; fail=1; }
trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$T"' EXIT

# 1. every line of wires.txt parses
$D probe -w wires.txt --samples 1 --timeout 1 -o /dev/null 2> "$T/parse.txt"
grep -q "wires.txt:" "$T/parse.txt" && bad "wires.txt has bad lines: $(grep wires.txt: "$T/parse.txt")" || ok "wires.txt parses ($(grep -vc '^\s*#\|^\s*$' wires.txt) wires)"

# 2. two fake MP servers: fast (5 ms) and slow (120 ms)
python3 tests/fake_mp.py 19001 5 FAST & python3 tests/fake_mp.py 19002 120 SLOW & sleep 0.5
printf 'fast | custom | mp | 127.0.0.1:19001 | fast\nslow | custom | mp | 127.0.0.1:19002 | slow\ndead | custom | mp | 127.0.0.1:19009 | dead\n' > "$T/w.txt"
$D probe -w "$T/w.txt" --samples 3 --timeout 400 -o "$T/p.json" 2>/dev/null
python3 - "$T/p.json" <<'PY' && ok "MP probe: fast green, slow yellow, dead down, best = fast" || bad "MP probe grades"
import json,sys; d=json.load(open(sys.argv[1])); g={w["id"]:w for w in d["wires"]}
assert g["fast"]["grade"]=="green" and g["fast"]["server"]=="FAST" and g["fast"]["players"]==3, g["fast"]
assert g["slow"]["grade"]=="yellow", g["slow"]; assert g["dead"]["grade"]=="down"; assert d["best"]["custom"]=="fast"
PY

# 3. relay: slow listed first, the relay must still pick fast
$D relay -l 19100 -u 127.0.0.1:19002 -u 127.0.0.1:19001 --kind mp --http 19101 2> "$T/relay.log" & sleep 3
python3 - <<'PY' && ok "relay forwards to the best upstream (FAST) and back" || bad "relay forward"
import socket; s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM); s.settimeout(2)
for i in range(20): s.sendto(b"hello%d"%i,("127.0.0.1",19100)); d,_=s.recvfrom(2048); assert d.startswith(b"ECHO:FAST:hello"), d
PY
printf 'relay | relay | udp | 127.0.0.1:19100 | relay ping\n' > "$T/r.txt"
$D probe -w "$T/r.txt" --samples 3 --timeout 500 -o "$T/r.json" 2>/dev/null && grep -q '"grade":"green"' "$T/r.json" && ok "relay answers the udp wire ping" || bad "relay ping"
curl -s localhost:19101/traffic.json > "$T/t.json"
python3 - "$T/t.json" <<'PY' && ok "traffic.json counts the session (20 in / 20 out)" || bad "traffic.json"
import json,sys; d=json.load(open(sys.argv[1])); s=d["sessions"]; assert len(s)==1 and s[0]["in_pk"]==20 and s[0]["out_pk"]==20 and s[0]["up"]==1, d
PY
exit $fail
