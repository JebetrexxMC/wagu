#!/bin/bash
# 5.02.13: crossplay check against a real local open.mp server (run from the wagu repo root, Linux x86_64 host).
#   bash tools/omp_sweep/local_test.sh
# 1. two pretend phones (Phone_A / Phone_B) join omp-server 1.6.7 (simpletdm) through the gate, press < Spawn >,
#    and each one must see the other's chat as "Name: text"
# 2. one phone stays on, the server is killed and restarted: the gate must reconnect and spawn again
# Needs 32-bit libs for omp-server (CI: libc6:i386 libstdc++6:i386 libatomic1:i386).
set -u
T=/tmp/sampgate_test; V=v1.6.7.3218; fails=0
R=../third_party/omp-raknet
make -C server -j"$(nproc)" > /tmp/server_build.log 2>&1 || { echo "server libs did not build"; grep -E "error|No such file|No rule" /tmp/server_build.log | head -20; exit 1; }
(cd test && g++ -O1 -std=c++17 -pthread -w -I../src -I../third_party/mbedtls/include -D__stdcall= -DRAKNET_LEGACY -DPACKETS_PER_TICK=3 \
  -I$R/Include/raknet -I$R sampgate_test.cpp ../src/net/sampgate.cpp ../src/net/client.cpp ../src/net/server.cpp ../src/net/lan.cpp \
  ../src/net/online.cpp ../src/net/authcrypto.cpp ../src/net/samp_import.cpp ../server/obj/*.a -ldl -o "$T") || { echo "sampgate_test did not build"; exit 1; }
[ -x /tmp/Server/omp-server ] || { curl -fsL --retry 3 -o /tmp/omp.tgz "https://github.com/openmultiplayer/open.mp/releases/download/$V/open.mp-linux-x86.tar.gz" && tar xzf /tmp/omp.tgz -C /tmp; }
python3 -c "import json;p='/tmp/Server/config.json';c=json.load(open(p));c['password']='';c.setdefault('network',{})['port']=7799;c['pawn']['main_scripts']=['simpletdm 1'];json.dump(c,open(p,'w'),indent=1)"
start() { (cd /tmp/Server && timeout "$1" ./omp-server > /tmp/omp_local.log 2>&1 &); sleep 4; }
check() { if grep -q "$2" "$1"; then echo "ok   $3"; else echo "FAIL $3"; fails=$((fails+1)); fi; }

start 60
(MP_GATE_WAIT=5000 MP_TEST_REMOTE_ONLY=1 MP_TEST_SPAWN=1 MP_TEST_HOLD=8000 MP_TEST_PORT=22000 MP_TEST_NAME=Phone_A timeout 60 "$T" 127.0.0.1 7799 > /tmp/lt_a.log 2>&1 &)
sleep 2
MP_GATE_WAIT=5000 MP_TEST_REMOTE_ONLY=1 MP_TEST_SPAWN=1 MP_TEST_PORT=22100 MP_TEST_NAME=Phone_B timeout 60 "$T" 127.0.0.1 7799 > /tmp/lt_b.log 2>&1
sleep 8; pkill -f omp-server
for x in a b; do check /tmp/lt_$x.log "Spawn button spawned" "Phone_${x^^}: < Spawn > spawns on the PC server"; done
check /tmp/lt_a.log "Phone_B: {FFFFFF}hello from the phone" "Phone_A sees Phone_B's chat"
check /tmp/lt_b.log "Phone_A: {FFFFFF}hello from the phone" "Phone_B sees Phone_A's chat"

start 100
(MP_GATE_WAIT=5000 MP_TEST_REMOTE_ONLY=1 MP_TEST_HOLD=40000 MP_TEST_PORT=22400 MP_TEST_NAME=Phone_C timeout 90 "$T" 127.0.0.1 7799 > /tmp/lt_c.log 2>&1 &)
sleep 14; pkill -9 -f omp-server; sleep 6; start 70; sleep 36; pkill -f omp-server
check /tmp/lt_c.log "Reconnecting (1 of 10)" "lost connection -> reconnecting"
[ "$(grep -c 'SPO_CLASSSEL show 0 spawned 1' /tmp/lt_c.log)" -ge 2 ] && echo "ok   spawned again after the reconnect" || { echo "FAIL spawned again after the reconnect"; fails=$((fails+1)); }
check /tmp/lt_c.log "ok   still connected" "still connected at the end"
echo; [ $fails -eq 0 ] && echo "all ok" || echo "$fails FAILED"
exit $fails
