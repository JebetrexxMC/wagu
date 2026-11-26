#!/usr/bin/env bash
# NetWire 1.4 - UDP ping between two machines (the real game path, not ICMP).
# Builds JebetrexxMC/udp-ping-c (tiny C echo server + client); 'rtt' is a built-in Python client in the
# style of JebetrexxMC/udp-latency (index + ns timestamp per packet): min / avg / p95 / jitter / loss.
#   udp-bench.sh server [port]            echo server (default UDP 1939, forwarded by net/cgnat/mp-gateway.sh)
#   udp-bench.sh rtt <host> [port] [n]    n packets, 50 ms apart (default 100) -> summary line
#   udp-bench.sh ping <host> [port] [s]   udp-ping-c client for s seconds (default 10)
# Any UDP echo answers rtt: this server, mpnetd relay ping, sockperf. Measures ping - it does not lower it.
# Termux: pkg install clang git python
set -uo pipefail
PORT_DEF=1939; B="${XDG_CACHE_HOME:-$HOME/.cache}/mp-udp-bench"; SRC=https://github.com/JebetrexxMC/udp-ping-c
build() {
  [ -x "$B/server" ] && [ -x "$B/ping" ] && return
  command -v git >/dev/null || { echo "need git"; exit 1; }
  CC=$(command -v clang || command -v cc || command -v gcc) || { echo "need clang or gcc"; exit 1; }
  rm -rf "$B/src" && git clone -q --depth 1 "$SRC" "$B/src" || { echo "clone failed"; exit 1; }
  "$CC" -O2 -o "$B/server" "$B/src/src/server.c" && "$CC" -O2 -o "$B/ping" "$B/src/src/client.c" || { echo "build failed"; exit 1; }
}
ip4() { [[ $1 =~ ^[0-9]+(\.[0-9]+){3}$ ]] && { echo "$1"; return; }; getent ahostsv4 "$1" 2>/dev/null | awk 'NR==1{print $1}'; }
case "${1:-}" in
  server) build; echo "UDP echo on ${2:-$PORT_DEF} (Ctrl+C stops)"; exec "$B/server" -p "${2:-$PORT_DEF}" ;;
  ping) [ -n "${2:-}" ] || { echo "udp-bench.sh ping <host> [port] [seconds]"; exit 2; }; build
        h=$(ip4 "$2"); [ -n "$h" ] || { echo "cannot resolve $2"; exit 1; }
        timeout -s INT "${4:-10}" "$B/ping" -d "$h" -p "${3:-$PORT_DEF}" -i 500000 -t 1000000 ;;
  rtt) [ -n "${2:-}" ] || { echo "udp-bench.sh rtt <host> [port] [count]"; exit 2; }
       python3 - "$2" "${3:-$PORT_DEF}" "${4:-100}" <<'PY'
import socket, sys, time
host, port, n = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
addr = (socket.gethostbyname(host), port)
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.settimeout(1.0)
rtts = []
for i in range(1, n + 1):
    t0 = time.perf_counter_ns()
    s.sendto(b"MPB %d %d" % (i, t0), addr)   # text, no 0 byte: udp-ping-c echoes strlen() bytes
    end = time.monotonic() + 1.0
    while time.monotonic() < end:
        try: d, _ = s.recvfrom(2048)
        except socket.timeout: break
        if d.split(b" ")[:2] == [b"MPB", b"%d" % i]:
            rtts.append((time.perf_counter_ns() - t0) / 1e6); break
    time.sleep(0.05)
if not rtts: print("%s:%d  no reply (%d sent) - port closed / not forwarded / CGNAT" % (host, port, n)); sys.exit(1)
r = sorted(rtts); j = sum(abs(a - b) for a, b in zip(rtts, rtts[1:])) / max(1, len(rtts) - 1)
print("%s:%d  udp rtt min %.1f  avg %.1f  p95 %.1f  max %.1f ms  jitter %.1f ms  loss %.0f%% (%d/%d)" % (
    host, port, r[0], sum(r) / len(r), r[int(len(r) * .95) - 1 if len(r) > 1 else 0], r[-1], j, 100 * (n - len(r)) / n, len(r), n))
PY
  ;;
  *) sed -n '2,10p' "$0" ;;
esac
