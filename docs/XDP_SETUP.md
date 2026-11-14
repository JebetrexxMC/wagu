# XDP setup for the MP server ports

XDP runs eBPF filters in the network card driver, before Linux handles a packet. Bad traffic is dropped
at almost no CPU cost. It protects the server's bandwidth and CPU during a flood. It does **not** lower ping.

- Needs a Linux VPS with root, kernel 5.x or 6.x. Use Ubuntu 24.04 / 26.04 or Debian 12 / 13; the "4-core"
  is your VPS plan. XDP does not run on phones (Termux has no root kernel access), so use Termux or Termius
  only to SSH into the VPS.
- Two layers, both tested together (see "Tested" below):
  1. **xdp-filter** (from [xdp-tools](https://github.com/xdp-project/xdp-tools)). Drops floods from known
     amplification source ports and any IPs you block.
  2. **mp_xdp** (`HiveMQ/xdp/mp_xdp.c`, already part of the MP VPS kit). Drops empty, oversized and type-0
     packets on the game ports, and limits each IP's packets per second.

## Ports

| Port | Proto | Service |
|---|---|---|
| 1922 | UDP | MP LAN / main game server |
| 1930 | UDP | Brief Case War |
| 1931 | UDP | MW3 |
| 1932 | UDP | Mega Vil |
| 7777, 7780 | UDP | Vice City servers |
| 11111 | UDP | sockperf (latency test) |
| 22 | TCP | SSH |
| 80 | TCP | web / status |
| 1883, 8883, 8884 | TCP | HiveMQ MQTT, MQTT TLS, WebSocket |
| 7681 | TCP | ttyd web console |
| 8000 | TCP | status API |

## 1. Install

```sh
sudo apt update
sudo apt install -y xdp-tools bpftool clang llvm libbpf-dev   # Ubuntu 26.04 ships xdp-tools 1.6.2
ip -br link                                                  # find the interface, e.g. eth0 / ens3
```

## 2. Easy way: the MP kit does it for you

Add this to `/etc/mp-hivemq/mp.env`, then restart:

```sh
XDP=1
XDP_FILTER=1
# optional:
XDP_PPS=20000                     # packets per second per IP on the game ports (0 = no limit)
XDP_BLOCK="203.0.113.5"           # IPs dropped completely
XDP_FILTER_SRC="17 19 161 389 520 1900 3702 11211"   # default list
```

```sh
sudo systemctl restart mp-xdp
sudo /opt/mp/console/mp-xdp.sh status          # both layers + counters
sudo /opt/mp/console/mp-xdp.sh block 203.0.113.5
sudo /opt/mp/console/mp-xdp.sh unblock 203.0.113.5
sudo /opt/mp/console/mp-xdp.sh allow 198.51.100.7   # never rate-limit this IP
sudo /opt/mp/console/mp-xdp.sh stop            # remove everything
```

`mp-xdp.sh` loads mp_xdp with `xdp-loader`, then xdp-filter, so both run under libxdp's dispatcher.
xdp-filter runs first. When native mode fails it falls back to skb (generic) mode, and when xdp-tools
can't be installed it runs mp_xdp alone.

## 3. Manual way: xdp-filter rules

Replace `eth0` with your interface.

```sh
# policy "allow": everything passes except what the rules drop
sudo xdp-filter load -m native -p allow eth0        # if this fails: -m skb
# drop UDP floods coming FROM amplification ports (QOTD, chargen, SNMP, CLDAP, RIP, SSDP, WS-Discovery, memcached)
for p in 17 19 161 389 520 1900 3702 11211; do sudo xdp-filter port $p -m src -p udp; done
# block one attacker
sudo xdp-filter ip 203.0.113.5 -m src
sudo xdp-filter ip 203.0.113.5 -m src -r     # unblock
sudo xdp-filter status                       # rules + hit counters
sudo xdp-filter unload eth0                  # remove
```

DNS (source port 53) and NTP (123) are **not** in the list, because the server needs those replies.

Do not use `-p deny`. Deny mode drops everything not listed, including ARP. Within a minute the VPS loses
its gateway and goes offline, SSH included. Even with static ARP, ping and the replies to the server's own
traffic (DNS, NTP, HTTPS for updates) are dropped. Never block your own IP either: SSH goes with it.

### Running xdp-filter next to mp_xdp by hand

`mp-xdp.sh start` without `XDP_FILTER=1` attaches mp_xdp directly. After that, xdp-filter says
`Existing program is not using a dispatcher, can't replace; unload first`. Load mp_xdp through
xdp-loader first:

```sh
sudo /opt/mp/console/mp-xdp.sh stop
sudo xdp-loader load -m native eth0 /opt/mp/xdp/mp_xdp.o
sudo xdp-filter load -m native -p allow eth0
for p in 17 19 161 389 520 1900 3702 11211; do sudo xdp-filter port $p -m src -p udp; done
sudo xdp-loader status eth0     # xdp_dispatcher => xdpfilt_alw_all (prio 10), mp_xdp (prio 50)
```

(Easier: set `XDP_FILTER=1` and use `mp-xdp.sh start`. It also fills mp_xdp's port list and limits.)

## Tested

The test ran on a veth pair, so the filtered side only received test packets. The setup was kernel 6.1 with
xdp-tools 1.6.2, in native and skb mode, using `mp-xdp.sh` with `XDP_FILTER=1` and `XDP_PPS=100`:

| Traffic | Result |
|---|---|
| UDP 1922, 1930, 1931, 1932, 7777, 7780, 11111 | pass |
| TCP 22, 80, 1883, 8883, 8884, 7681, 8000, 3306 | pass |
| DNS replies (src 53), NTP replies (src 123), HTTPS replies (src 443) | pass |
| UDP from src 19, 389, 1900, 3702, 11211 | drop |
| ping, ARP | pass |
| 2000 packets in a burst from one IP to 1922 | 200 passed (burst), 1800 dropped by the rate limit |
| `block` IP / `unblock` IP | all dropped / all pass again |
| `stop` | no XDP left on the interface |

With `-p deny` the same test lost ARP, so the host went unreachable.

## Optional: xdp-proxy

[xdp-proxy](https://github.com/ThirtyFoxMCx00E1/xdp-proxy) (fork of gamemann/xdp-proxy) forwards a UDP port to another server in XDP.
Use it only to relay players to a second box. It is not needed for filtering.

## ISO downloads (release v4.13.45)

The Ubuntu ISO is split into two parts (GitHub allows files up to 2 GB):

```sh
# Linux / Termux
cat ubuntu-26.04.1-live-server-amd64.iso.part0* > ubuntu-26.04.1-live-server-amd64.iso
sha256sum -c ubuntu-26.04.1-live-server-amd64.iso.sha256
```

```bat
:: Windows (cmd)
copy /b ubuntu-26.04.1-live-server-amd64.iso.part00+ubuntu-26.04.1-live-server-amd64.iso.part01 ubuntu-26.04.1-live-server-amd64.iso
certutil -hashfile ubuntu-26.04.1-live-server-amd64.iso SHA256
```

SHA-256: `cc8a95cde20f6ced61a322420de00f10cc3c90ced545daa46cb9c1a117f1d927`.
It is also on [archive.org](https://archive.org/details/ubuntu-26.04.1-live-server-amd64).
