#!/usr/bin/env bash
# Scan the MP ports of YOUR OWN servers (region / vps / oracle / xdp / relay / custom groups in wires.txt).
# Uses rustscan when installed, else the built-in scanner of mpnet-agent (networking/rust).
# Only scan hosts you own or are allowed to test.
set -euo pipefail
cd "$(dirname "$0")/.."
PORTS="${PORTS:-22,1883,7835,8090,8091,8883,8884,1922,1930,1931,7777}"
hosts=$(awk -F'|' '!/^[[:space:]]*#/ && NF>=4 { g=$2; gsub(/ /,"",g); a=$4; gsub(/ /,"",a);
  if (g ~ /^(region|vps|oracle|xdp|relay|custom|zerotier)$/) { sub(/:[0-9]+$/,"",a); if (a!="lobby") print a } }' wires.txt | sort -u)
[ -n "$hosts" ] || { echo "no own hosts in wires.txt"; exit 0; }
if command -v rustscan >/dev/null; then
  for h in $hosts; do echo "== $h"; rustscan -a "$h" -p "$PORTS" -b 500 -t 1500 --ulimit 5000 -g || true; done
else
  A=rust/target/release/mpnet-agent
  [ -x "$A" ] || (cd rust && cargo build --release -q)
  "$A" own -w wires.txt
fi
