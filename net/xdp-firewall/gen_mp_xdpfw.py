#!/usr/bin/env python3
"""Writes an xdp-firewall (JebetrexxMC/xdp-firewall, gamemann/XDP-Firewall) config for an MP VPS.

  python3 gen_mp_xdpfw.py                          # print the config (interface = default route)
  python3 gen_mp_xdpfw.py --iface ens3 --out /etc/xdpfw/xdpfw.conf
  python3 gen_mp_xdpfw.py --allow 203.0.113.7 --pps 30000

Rules are checked top to bottom, the first match wins, no match = pass (everything that is not MP is untouched):
  1. the other MP VPS / brokers from net/wires.txt (groups region, vps, xdp, relay) and --allow: always pass;
  2. UDP to the MP game ports 1922-1932: drop empty packets and packets bigger than the server's 1024 byte buffer
     (the same checks as HiveMQ/xdp/mp_xdp.c);
  3. UDP 1922-1932 from one IP above --pps packets/s: drop + block that IP for --block s. The default is high (20000/s)
     because many phones on mobile data share one carrier IP (CGNAT); one phone sends ~20-60 packets/s;
  4. UDP reflection floods (DNS 53, NTP 123, SSDP 1900, memcached 11211, chargen 19) to the game ports: drop;
  5. TCP SYN floods to MQTT 1883, TLS/WSS 8883-8884, NetWire 8090-8091 and the global VPS network 8000-8005: drop
     + block for --block s when the IP sends more than --syn packets/s (all its packets count, the same CGNAT reason
     as above, so the default is 5000/s; real SYN floods are 100k+/s).
The per-IP limits need ENABLE_RL_IP in src/common/config.h (install-vps.sh turns it on).
"""
import argparse
import os
import socket
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = "1922-1932"


def default_iface():
    try:
        out = subprocess.run(["ip", "route", "get", "1.1.1.1"], capture_output=True, text=True, timeout=3).stdout.split()
        return out[out.index("dev") + 1]
    except Exception:
        return "eth0"


def wire_ips(path):
    ips = set()
    if not os.path.exists(path):
        return ips
    for line in open(path, encoding="utf-8"):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        f = [x.strip() for x in line.split("|")]
        if len(f) < 4 or f[1] not in ("region", "vps", "xdp", "relay"):
            continue
        host = f[3].rsplit(":", 1)[0]
        try:
            for ai in socket.getaddrinfo(host, None, socket.AF_INET):
                ips.add(ai[4][0])
        except OSError:
            pass
    return ips


def rule(**kv):
    body = ",\n".join(f"        {k} = {('true' if v else 'false') if isinstance(v, bool) else (repr(v).replace(chr(39), chr(34)) if isinstance(v, str) else v)}"
                      for k, v in kv.items())
    return "    {\n" + body + "\n    }"


def build(a):
    allow = sorted(wire_ips(a.wires) | set(a.allow))
    rules = [rule(enabled=True, action=1, block_time=0, src_ip=ip) for ip in allow]
    # 14 ethernet + 20 IPv4 + 8 UDP = 42 bytes: no payload. 42 + 1024 = biggest MP packet.
    rules += [
        rule(enabled=True, action=0, block_time=0, udp_enabled=True, udp_dport=GAME, max_len=42),
        rule(enabled=True, action=0, block_time=0, udp_enabled=True, udp_dport=GAME, min_len=42 + 1024 + 1),
        rule(enabled=True, action=0, block_time=a.block, ip_pps=a.pps, udp_enabled=True, udp_dport=GAME),
    ]
    for sport in (53, 123, 1900, 11211, 19):
        rules.append(rule(enabled=True, action=0, block_time=0, udp_enabled=True, udp_sport=sport, udp_dport=GAME))
    for dport in (1883, "8883-8884", "8090-8091", "8000-8005"):
        rules.append(rule(enabled=True, action=0, block_time=a.block, ip_pps=a.syn,
                          tcp_enabled=True, tcp_syn=True, tcp_dport=dport))
    head = [
        "# MP VPS xdp-firewall config - written by net/xdp-firewall/gen_mp_xdpfw.py (first match wins, no match = pass)",
        f"# allow list: {len(allow)} MP VPS / broker IPs from net/wires.txt + --allow",
        "verbose = 2;",
        f'log_file = "{a.log}";',
        f'interface = "{a.iface or default_iface()}";',
        "pin_maps = true;",
        "update_time = 15;",
        "no_stats = false;",
        "stats_per_second = true;",
        "stdout_update_time = 1000;",
        "",
    ]
    return "\n".join(head) + "filters = (\n" + ",\n".join(rules) + "\n);\n"


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--iface", help="network interface (default: the default route's)")
    p.add_argument("--wires", default=os.path.join(HERE, "..", "wires.txt"))
    p.add_argument("--allow", action="append", default=[], help="IPv4 that is never limited (repeat)")
    p.add_argument("--pps", type=int, default=20000, help="UDP packets/s per source IP to the game ports")
    p.add_argument("--syn", type=int, default=5000,
                   help="packets/s of one source IP above which its new TCP connections (SYN) to the MQTT / web ports drop")
    p.add_argument("--block", type=int, default=30, help="seconds an IP stays blocked after a limit hit")
    p.add_argument("--log", default="/var/log/xdpfw.log")
    # plain drops (bad size, reflection) use block_time 0: one odd packet from a real player must not block them
    p.add_argument("--out")
    a = p.parse_args()
    text = build(a)
    if a.out:
        os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
        open(a.out, "w").write(text)
        print(f"wrote {a.out}")
    else:
        print(text, end="")


if __name__ == "__main__":
    main()
