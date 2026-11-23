# MP NetWire

Networking library and server for wagu. A **wire** is any network path MP, the website or a server uses:

| group | what | probe |
|---|---|---|
| `default` | MP LAN Server (Broker MQQ) | seen in the MP lobby |
| `featured` | the 24/7 featured servers (BCW, MW3, Mega Vil, DayZ, League…) | seen in the MP lobby |
| `crossplay` | MP Crossplay 24/7 (open.mp 1.6.7 + mp-crossplay, playit tunnel) | SA-MP query |
| `region` | HiveMQ region brokers (Asia / Europe / NA / Oceania): MQTT 1883, TLS 8883, WSS 8884 | MQTT login + ping, TLS hello |
| `vps` | MP game ports on the region VPS | MP PKT_QUERY |
| `lobby` | free public lobby: Mosquitto, HiveMQ, EMQX | MQTT login + ping |
| `emqx` | EMQX / HiveMQ WebSocket and TLS ports | TLS hello / TCP |
| `zerotier` | MP servers on your ZeroTier network (virtual LAN) | MP query, from a box on the same network |
| `oracle`, `xdp`, `relay` | Oracle Always Free boxes, XDP fast-path boxes (`HiveMQ/xdp`), mpnetd relays | MP query / UDP ping |
| `custom` | every other server in the MP lobby, plus your own lines | lobby / any |
| `omp` | top open.mp / SA-MP servers from api.open.mp | SA-MP query |
| `web` | website, GitHub raw, open.mp API | HTTP / TLS |

```
net/
  lib/mpnet.h, mpnet.cpp   library: probes, grades, best-of-group, JSON (C++17, POSIX sockets, no Boost/OpenSSL)
  server/mpnetd.cpp        probe / watch / relay
  wires.txt                the wire list - one line = one wire
  tools/sources.py         wires that change by themselves (crossplay address, lobby servers, open.mp top list)
  tools/render.py          badges, 24 h history, README card for the netwire-status branch
  tests/run.sh             local test: parser, probes, relay best-pick, relay ping, traffic counters
  rust/                    mpnet-agent: Rust + tokio probe, RustScan-style scan, bore tunnel client/server
  rustscan/                RustScan download + scan of your own servers
  zerotier/                ZeroTier join script + MP LAN over ZeroTier
  xpipe/                   XPipe SSH hosts for the MP boxes
  manage/mpctl.sh          server management: status, restart, logs, update, netwire services, zerotier, tunnel
  sniffnet/                Sniffnet (traffic monitor) download + MP filters
  swift/                   MPNet Swift package (Moya) + MPNetNIO (SwiftNIO SA-MP query) for iOS / macOS
  swift-pulse/             MPNetPulse: Moya -> Pulse logger plugin (iOS 15 / macOS 13)
  py/mpprobe.py            async probe with asyncio, uvloop or trio
  go/mpedge                gnet UDP edge: MPNETPNG pings + SA-MP / MP query cache in front of a server
  go/mplatency             24/7 latency + location monitor (5 s probes, dashboard, Discord / Telegram alerts)
  streaming/               infinite-streaming test stream for the MP URL radio
  server/mpnet-dns.c       libevent evdns: every wire host resolved at once (make dns)
  server/mpnet-ws.cpp      POCO WebSocket: wires.json pushed live on change (make ws)
  gns/                     GameNetworkingSockets reliable / unreliable lane test
  mirror/                  Unity Mirror NetworkManager that joins the best wire
  firezone/, netalertx/    docker compose: private admin lanes, LAN watch
  p2p/                     mpnet-p2p: LAN server discovery without a broker (rust-libp2p: mDNS, identify, ping)
  server/mpnet-beast.cpp   Boost.Beast HTTP -> SA-MP query bridge for the website (make beast)
  handy/                   mpnet-handy: ping reflector + /stats (handy, epoll)
  dotnet/                  mpnet-dotnetty: SA-MP query + NetWire ping for .NET (DotNetty)
  android/                 NetWire for Kotlin apps (Fuel, Fast Android Networking)
  query-shield/            iptables + query cache (pack_scan.py or mpedge) so query floods don't show "offline"
  wireguard/, cjdns/, ziti/  relay <-> server tunnel, IPv6 mesh admin lanes, zero-trust admin services
  raspap/                  RaspAP plugin: a Raspberry Pi LAN-party hotspot with mpnetd
  scanopy/, hpsocket/      network map of a box; HP-Socket (reference)
  skydive/, opensnitch/    live flows of UDP 7777; per-program outgoing firewall rule
  containerlab/            phone - relay - server lab, netem lag / loss on demand
  pjon/                    mp-pjon-beacon: MP LAN servers announced over PJON LocalUDP (boards + boxes)
  kuma/                    service mesh for the mpnet HTTP side (UDP 7777 excluded)
  k8s/                     Dockerfile, Deployment / Service, NetworkPolicy, Calico policy, CNI conflist
  REFERENCES.md            the repos this was checked against
```

## Build

```
make -C net            # build/mpnetd
make -C net test
make -C net android NDK=/path/to/android-ndk   # build/android/{arm64-v8a,armeabi-v7a}/mpnetd (Termux, adb shell)
```

## Run

```
# one round, JSON to stdout
mpnetd probe -w net/wires.txt

# 24/7 on a VPS / Oracle box, status on :8090 (/wires.json, /traffic.json, /health)
mpnetd watch -w net/wires.txt -o /var/lib/mpnet -i 60 --http 8090

# UDP relay near the players: new players go over the fastest upstream, :8091/traffic.json = live traffic
mpnetd relay -l 7777 -u eusamp.duckdns.org:1922 -u mpsamp.duckdns.org:1922 --kind mp --http 8091
mpnetd relay -l 7778 -u wap-fresh.tun.ply.gg:56616 --kind samp            # open.mp / SA-MP server
```

Grades: **green** under 120 ms median, jitter at most 30 ms, at most 20 % loss. **yellow** under 250 ms, at most 50 % loss. **red** is anything slower or lossier. **down** means no reply. Score = ping + 2 × jitter + 500 × loss (lower is better). The best wire of a group is the one with the lowest score.

## Server management

| tool | for |
|---|---|
| [`rust/`](rust) mpnet-agent | `probe` all wires concurrently (tokio), `scan` / `own` ports, `tunnel` + `bore-server` (bore) |
| [`rustscan/`](rustscan) | RustScan 2.4.1: fast check of which MP ports are open on your own boxes |
| [`zerotier/`](zerotier) | virtual LAN, so MP LAN Server and UDP game ports work between PCs and phones without port forwarding |
| [`xpipe/`](xpipe) + [`manage/mpctl.sh`](manage/mpctl.sh) | every MP box in one XPipe window, with status / restart / logs / update / install-netwire buttons |

```
ssh mp-asia 'bash -s' < net/manage/mpctl.sh status
mpctl netwire                  # mpnetd watch :8090 + relay :7777 as systemd services
mpctl tunnel 1883 bore.pub     # share MQTT through bore (TCP only)
```

## NetWire 1.2 tools

```
python3 net/py/mpprobe.py --loop uvloop          # or asyncio / trio
cd net/go && go run ./mpedge -listen :7777 -backend 127.0.0.1:7778   # query shield for an open.mp / SA-MP server
net/rust/target/release/mpnet-agent reflect --bind 0.0.0.0:7790      # mio ping reflector
make -C net dns && net/build/mpnet-dns net/wires.txt                 # DNS check of every wire host
make -C net ws && net/build/mpnet-ws -f wires.json -p 9090           # ws://host:9090/live
cmake -S net/gns -B /tmp/gns -G Ninja && cmake --build /tmp/gns && /tmp/gns/mpgns
```

## NetWire 1.3 tools

```
net/rust/target/release/mpnet-agent ping wap-fresh.tun.ply.gg:56616 --samp   # smol ping
cd net/p2p && cargo run --release -- --server 192.168.1.10:7777 --name "LAN"  # host box
cd net/p2p && cargo run --release                                             # player box: lists servers found
make -C net beast && net/build/mpnet-beast -p 8092                            # /query?host=&port=
cmake -S net/handy -B /tmp/hb -G Ninja && cmake --build /tmp/hb && /tmp/hb/mpnet-handy
cd net/dotnet && dotnet run -- wap-fresh.tun.ply.gg 56616
sudo bash net/query-shield/setup.sh --ip <public ip> --port 7777
```

## 24/7 on GitHub

`.github/workflows/netwire-247.yml` builds and tests on every change here (Linux, Android NDK, and the Swift package on macOS). It then probes every 5 minutes in ~6 h shifts that chain themselves. Results go to the `netwire-status` branch, the badges in the main README and [mpsamp-open.github.io/network.html](https://mpsamp-open.github.io/network.html). `wires.txt` is re-read from main every round, so a new line shows up within 5 minutes. Turn it off with the repository variable `NETWIRE = off`.

## What it can and can't do

- It **measures** every path and **picks** the best one, both for the status page and for the relay. It can't make a far server closer. A relay only helps when it runs on a box near the players and has a better route to the server than the player's ISP.
- GitHub probes run from GitHub's data centres, so the page shows what is up and which path is fastest from there, not a player's own ping.
- The relay keeps a player on one upstream for the whole session, because switching mid-game would drop the MP / RakNet session. The server sees the relay's IP for every player behind it, the same as any UDP proxy (see ADRFranklin/samp-udp-proxy).
- The phone game doesn't use mpnetd yet. It already pings the 4 regions and picks the fastest (docs/regions.txt). The PC launcher (`launcher/`) reads `wires.json` for the crossplay path, and Kotlin apps can do the same (`android/`).
- Everything here is open source in this repository: mpnetd (`server/mpnetd.cpp`), the wire list (`wires.txt`), and the status files on the `netwire-status` branch.
