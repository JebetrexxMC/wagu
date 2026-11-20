#!/bin/sh
# 5.02.05: weekly open.mp crossplay sweep (run from the wagu repo root, Linux host).
#   sh tools/omp_sweep/sweep.sh [out.tsv]
# 1. reads api.open.mp/servers, takes the 12 busiest servers
# 2. joins each with test/sampgate_test (the phone's crossplay code, no phone needed) and sorts the result:
#      stayed     - joined (InitGame) and still connected after chat + /command + movement
#      kicked     - joined, then the server closed the connection (own rules / anti-bot / accounts)
#      launcher   - never sends the game start (InitGame): servers that only take their own launcher
#      no_answer  - the server never answered the connection
#      wrong_pw   - password server, a wrong password gave "Wrong server password." (expected)
# 3. password join: the busiest password server on the list with a wrong password, and a local omp-server 1.6.7
#    with a password (right password must join and stay, wrong and empty must say "Wrong server password.")
# Writes a TSV (date, rank, ip:port, players, password, version, result, status, kick message, name).
set -u
OUT=${1:-tools/omp_sweep/results/$(date -u +%Y-%m-%d).tsv}
mkdir -p "$(dirname "$OUT")"
T=/tmp/sampgate_test
if [ ! -x "$T" ]; then
  make -C server -j"$(nproc)" >/dev/null 2>&1 || { echo "server libs did not build"; exit 1; }
  R=../third_party/omp-raknet
  (cd test && g++ -O1 -std=c++17 -pthread -I../src -I../third_party/mbedtls/include -D__stdcall= -DRAKNET_LEGACY -DPACKETS_PER_TICK=3 \
    -I$R/Include/raknet -I$R sampgate_test.cpp ../src/net/sampgate.cpp ../src/net/client.cpp ../src/net/server.cpp ../src/net/lan.cpp \
    ../src/net/online.cpp ../src/net/authcrypto.cpp ../src/net/samp_import.cpp ../server/obj/*.a -ldl -o "$T") || { echo "sampgate_test did not build"; exit 1; }
fi
curl -fsS --max-time 30 https://api.open.mp/servers -o /tmp/omp_servers.json || { echo "api.open.mp did not answer"; exit 1; }
python3 - > /tmp/omp_top.tsv <<'PY'
import json
s = json.load(open('/tmp/omp_servers.json'))
s.sort(key=lambda x: -x.get('pc', 0))
top = s[:12]
pw = [x for x in s if x.get('pa')][:1]
for i, x in enumerate(top + [dict(p, _pw=1) for p in pw]):
    print('\t'.join([str(i + 1 if i < 12 else 'pw'), x['ip'], str(x.get('pc', 0)), '1' if x.get('pa') else '0', x.get('vn', ''), x.get('hn', '').replace('\t', ' ')[:60]]))
PY
day=$(date -u +%Y-%m-%d)
printf 'date\trank\taddress\tplayers\tpassword\tversion\tresult\tstatus\tlast_message\tname\n' > "$OUT"
classify() {  # $1 = log
  if grep -q "Wrong server password" "$1"; then echo wrong_pw
  elif grep -q "^ok   still connected" "$1" && grep -q "^ok   gate joined the PC server" "$1"; then echo stayed
  elif grep -q "^ok   gate joined the PC server" "$1"; then echo kicked
  elif grep -q "did not answer" "$1"; then echo no_answer
  else echo launcher; fi
}
while IFS="$(printf '\t')" read -r rank addr pc pa ver name; do
  host=${addr%:*}; port=${addr##*:}; pw=""; [ "$pa" = 1 ] && pw="mp-sweep-wrong"
  MP_GATE_WAIT=20000 timeout 70 "$T" "$host" "$port" $pw > /tmp/omp_sweep_one.log 2>&1
  res=$(classify /tmp/omp_sweep_one.log)
  st=$(grep "status:" /tmp/omp_sweep_one.log | tail -1 | sed 's/^ *status: //' | tr '\t' ' ')
  last=$(grep "  msg " /tmp/omp_sweep_one.log | grep -v "\[open.mp\]" | tail -1 | sed 's/^ *msg [0-9A-F]*: //' | tr '\t' ' ' | cut -c1-120)
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$day" "$rank" "$addr" "$pc" "$pa" "$ver" "$res" "$st" "$last" "$name" >> "$OUT"
  echo "$rank $res $name"
done < /tmp/omp_top.tsv
# local password test on a real open.mp server
V=v1.6.7.3218
if [ ! -x /tmp/Server/omp-server ]; then
  curl -fsL --retry 3 -o /tmp/omp.tgz "https://github.com/openmultiplayer/open.mp/releases/download/$V/open.mp-linux-x86.tar.gz" && tar xzf /tmp/omp.tgz -C /tmp
fi
if [ -x /tmp/Server/omp-server ]; then
  python3 -c "import json;p='/tmp/Server/config.json';c=json.load(open(p));c['password']='mp2026';c.setdefault('network',{})['port']=7799;json.dump(c,open(p,'w'),indent=1)"
  (cd /tmp/Server && timeout 120 ./omp-server > /tmp/omp_local.log 2>&1 &)
  sleep 5
  for pw in mp2026 wrongpw ""; do
    MP_GATE_WAIT=4000 timeout 40 "$T" 127.0.0.1 7799 $pw > /tmp/omp_sweep_one.log 2>&1
    res=$(classify /tmp/omp_sweep_one.log); want=wrong_pw; [ "$pw" = mp2026 ] && want=stayed
    ok=FAIL; [ "$res" = "$want" ] && ok=ok
    printf '%s\tlocal\t127.0.0.1:7799\t0\t1\tomp %s\t%s\tpassword \"%s\" -> %s (want %s)\t\tlocal omp-server password test\n' "$day" "$V" "$res" "$pw" "$ok" "$want" >> "$OUT"
    echo "local pw '$pw': $res ($ok)"
  done
  pkill -f omp-server
fi
echo "wrote $OUT"
