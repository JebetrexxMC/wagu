# xdp-firewall for the MP VPS

[JebetrexxMC/xdp-firewall](https://github.com/JebetrexxMC/xdp-firewall) (fork of gamemann/XDP-Firewall) is a
stateless eBPF/XDP firewall: it drops packets on the network card before Linux handles them, so a (D)DoS flood
never reaches mp_server, Mosquitto/HiveMQ or the global VPS network. This folder gives it the MP rules.

| file | what |
|---|---|
| `gen_mp_xdpfw.py` | writes `/etc/xdpfw/xdpfw.conf` for an MP VPS (rules below). `--iface`, `--allow IP`, `--pps`, `--syn`, `--block`, `--out`. |
| `install-vps.sh` | `sudo bash install-vps.sh`: clones + builds the fork with `ENABLE_RL_IP` (per-IP limits), writes the config, starts `xdpfw.service`. Also `conf`, `status`, `off`, `--keep-mp-xdp`. |
| `cfgcheck.c` | checks a config with the firewall's own loader code (no root, no network card), see below. |

## Rules (first match wins, no match = pass)

1. other MP VPS / brokers (net/wires.txt groups `region`, `vps`, `xdp`, `relay`) and `--allow`: always pass;
2. UDP to the game ports 1922-1932: drop empty packets and packets bigger than the server's 1024 byte buffer;
3. UDP 1922-1932, one source IP above 20000 packets/s: drop + block it 30 s. High on purpose: many phones on
   mobile data share one carrier IP (CGNAT), one phone sends ~20-60 packets/s;
4. UDP reflection (DNS 53, NTP 123, SSDP 1900, memcached 11211, chargen 19) to the game ports: drop;
5. TCP SYN to MQTT 1883, TLS / WSS 8883-8884, NetWire 8090-8091, global VPS network 8000-8005 from an IP above
   5000 packets/s: drop + block 30 s.

SSH and everything else is never touched. Plain drops (2, 4) use `block_time = 0`, so one odd packet from a real
player does not block them. Permanent ban: `xdpfw-add -m 2 -d IP`, remove: `xdpfw-del -m 2 -d IP`.

## With HiveMQ/xdp (mp-xdp.service)

`HiveMQ/xdp/mp_xdp.c` does rules 2 and 3 for UDP only. xdp-firewall adds IPv6, TCP, reflection and timed blocks,
so `install-vps.sh` stops mp-xdp by default. `--keep-mp-xdp` keeps both; that only works when both are loaded
through libxdp (`xdp-loader`), which then runs them one after the other.

## Check a config

```bash
git clone --recursive https://github.com/JebetrexxMC/xdp-firewall /tmp/xdpfw && cd /tmp/xdpfw
sudo apt install -y libconfig-dev libbpf-dev clang
awk '/^int main\(void\)/{exit} {print}' tests/userspace.c > /tmp/c.c && sed -n '/^int main(int/,$p' PATH/TO/cfgcheck.c >> /tmp/c.c
clang -I src -I modules/xdp-tools/headers -I modules/xdp-tools/lib/libbpf/src -w -D__LIBXDP_STATIC__ \
  -ffunction-sections -fdata-sections -Wl,--gc-sections -o /tmp/cfgcheck /tmp/c.c src/loader/utils/{config,helpers,xdp,stats,logging}.c -lconfig -lbpf
python3 PATH/TO/gen_mp_xdpfw.py --log "" --out /tmp/mp.conf && /tmp/cfgcheck /tmp/mp.conf | head -1   # interfaces=1 filters=13
```
