#!/usr/bin/env bash
# NetWire 1.4 - look at the home line FROM OUTSIDE (GitHub runner / any VPS, root for nmap -sS):
#   sudo bash outside-probe.sh [host]   (default mpsamp.duckdns.org)  -> JSON on stdout, notes on stderr
# What answers for the IP: ICMP ping, traceroute (does the path reach it?), TCP RST / timeout per port
# and the RST TTL (how far away the thing that says "refused" is). Not proof - the phone check
# (cgnat-check.sh on the home Wi-Fi) is - but it shows if the router is reachable at all.
set -uo pipefail
H="${1:-mpsamp.duckdns.org}"
IP=$(getent ahostsv4 "$H" | awk 'NR==1{print $1}'); [ -n "$IP" ] || { echo '{"error":"no dns"}'; exit 1; }
PORTS="22,80,443,1883,7547,8080,8883,8884,51821,23456"
P=$(ping -c 4 -W 2 -q "$IP" 2>/dev/null | awk -F'[/ ]' '/rtt/{print $8}')
TR=$(traceroute -n -I -q 1 -w 2 -m 30 "$IP" 2>/dev/null | tail -n +2)
LAST=$(echo "$TR" | awk '$2 ~ /^[0-9.]+$/ {h=$1; ip=$2} END{print h" "ip}')
REACH=false; echo "$TR" | awk '{print $2}' | grep -qx "$IP" && REACH=true
NM=$(nmap -Pn -sS -n --reason -p "$PORTS" --max-retries 2 "$IP" 2>/dev/null | awk '/^[0-9]+\/tcp/')
echo "traceroute:" >&2; echo "$TR" >&2; echo "nmap:" >&2; echo "$NM" >&2
python3 - "$H" "$IP" "${P:-}" "$LAST" "$REACH" "$NM" <<'PY'
import json, re, sys, time
h, ip, ping, last, reach, nm = sys.argv[1:7]
ports = {}
for l in nm.splitlines():
    m = re.match(r"(\d+)/tcp\s+(\S+)\s+\S+\s+(.*)", l)
    if m:
        t = re.search(r"ttl (\d+)", m.group(3)); ports[m.group(1)] = {"state": m.group(2), "reason": m.group(3).split()[0], "ttl": int(t.group(1)) if t else None}
st = [p["state"] for p in ports.values()]
lh = last.split()
if "open" in st:
    v, why = "PUBLIC", "a port is open - the line is reachable"
elif st and all(s == "filtered" for s in st):
    v, why = "NO ANSWER", "every port times out - CGNAT, or the router drops everything from outside"
elif "closed" in st:
    ttls = sorted({p["ttl"] for p in ports.values() if p["state"] == "closed" and p["ttl"]})
    v, why = "SOMETHING ANSWERS", "ports refused (RST, ttl %s) - either your router owns this IP and nothing is forwarded, or Converge's CGNAT box refuses for many homes. The phone check decides." % ttls
else:
    v, why = "UNKNOWN", "no data"
print(json.dumps({"time": int(time.time()), "host": h, "ip": ip, "ping_ms": float(ping) if ping else None,
                  "trace_reached": reach == "true", "trace_last_hop": int(lh[0]) if lh else None, "trace_last_ip": lh[1] if len(lh) > 1 else None,
                  "ports": ports, "verdict": v, "why": why}, indent=1))
PY
