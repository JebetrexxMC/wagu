#!/usr/bin/env bash
# NetWire 1.4: net/cgnat/mp-gateway.sh in a 3-namespace lab (needs root, socat, iptables).
#   player 10.0.0.2 --- VPS 10.0.0.1 (pub0) | mpgw 10.79.0.1 --- 10.79.0.2 home (own LAN default route)
# A veth stands in for the WireGuard link (same rules; wg itself is the stock kernel module).
set -u
G="$(cd "$(dirname "$0")/.." && pwd)/cgnat/mp-gateway.sh"; fail=0
ENVF=/etc/mp-gateway.env; [ -e $ENVF ] && { echo "refusing: $ENVF exists (real gateway on this box?)"; exit 1; }
cleanup() { pkill -f 'socat.*(1883|1922|1923)' 2>/dev/null; for n in player vps home lan; do ip netns del $n 2>/dev/null; done; rm -f $ENVF; }
trap cleanup EXIT; cleanup
for n in player vps home lan; do ip netns add $n; ip -n $n link set lo up; done
ip link add p0 netns player type veth peer name pub0 netns vps
ip link add mpgw netns vps type veth peer name mpgw netns home
ip link add lan0 netns home type veth peer name r0 netns lan
ip -n player addr add 10.0.0.2/24 dev p0;    ip -n player link set p0 up
ip -n vps addr add 10.0.0.1/24 dev pub0;     ip -n vps link set pub0 up
ip -n vps addr add 10.79.0.1/24 dev mpgw;    ip -n vps link set mpgw up
ip -n home addr add 10.79.0.2/24 dev mpgw;   ip -n home link set mpgw up
ip -n home addr add 192.168.1.2/24 dev lan0; ip -n home link set lan0 up
ip -n lan addr add 192.168.1.1/24 dev r0;    ip -n lan link set r0 up
ip -n home route add default via 192.168.1.1
ip netns exec home socat TCP-LISTEN:1883,fork,reuseaddr SYSTEM:'echo peer=$SOCAT_PEERADDR' &
ip netns exec home socat UDP-RECVFROM:1922,fork SYSTEM:'echo peer=$SOCAT_PEERADDR' &
ip netns exec home socat UDP-RECVFROM:1923,bind=10.79.0.2,fork SYSTEM:'echo peer=$SOCAT_PEERADDR' &
IPT=iptables; ip netns exec vps $IPT -t nat -L -n >/dev/null 2>&1 || IPT=iptables-legacy
sleep 1; export WG_IF=mpgw PUB_IF=pub0
tcp() { ip netns exec player timeout 4 socat -T3 - TCP:10.0.0.1:1883 </dev/null 2>/dev/null | head -1; }
udp() { echo hi | ip netns exec player timeout 4 socat -T3 - UDP:10.0.0.1:$1 2>/dev/null | head -1; }
is() { if [ "$2" = "$3" ]; then echo "ok   $1: ${2:-(nothing)}"; else echo "FAIL $1: got '${2}' want '${3}'"; fail=1; fi; }
ip netns exec home bash "$G" route up
mode() { printf '%s\n' "$@" > $ENVF; ip netns exec vps bash "$G" rules up; }
mode SNAT=0 UDPKEEP=0
is "default tcp keeps player IP" "$(tcp)" "peer=10.0.0.2"
is "default udp (0.0.0.0 server)" "$(udp 1922)" "peer=10.79.0.1"
mode SNAT=0 UDPKEEP=1
is "udp-keep-ip (server bound 10.79.0.2)" "$(udp 1923)" "peer=10.0.0.2"
mode SNAT=1 UDPKEEP=0
is "snat tcp" "$(tcp)" "peer=10.79.0.1"
is "snat udp" "$(udp 1922)" "peer=10.79.0.1"
ip netns exec vps bash "$G" rules up >/dev/null
is "rules up twice = 1 jump" "$(ip netns exec vps $IPT -t nat -S PREROUTING | grep -c MPGW)" "1"
GW_FORCE=down ip netns exec vps bash "$G" health
s=$(date +%s%N); r=$(tcp); ms=$(( ($(date +%s%N) - s) / 1000000 ))
is "home offline -> refused at once" "$r$([ $ms -lt 1500 ] && echo fast)" "fast"
GW_FORCE=up ip netns exec vps bash "$G" health
is "home back -> forwarding" "$(tcp)" "peer=10.79.0.1"
ip netns exec vps bash "$G" rules down
is "rules down = clean" "$(ip netns exec vps $IPT-save | grep -c MPGW)" "0"
exit $fail
