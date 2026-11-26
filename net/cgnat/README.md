# NetWire 1.4 - CGNAT: why the regions are on standby, and how to bring them online

The 4 region names (mpsamp / eusamp / nasamp / ocsamp.duckdns.org) point at a home line on Converge
(143.44.193.152). From the internet 1883 is refused and 8883 / 8884 / UDP 1922 time out, so the game
uses the public lobby (that is the blue "standby" on the website since 5.02.72). Converge puts most
home fiber behind **CGNAT**: the public IP is shared by many homes, so a port forward on the
ZTE F670L can't work - the traffic never reaches your router.

| Script | Where | What |
|---|---|---|
| `cgnat-check.sh [router-WAN-IP]` | Termux / Linux, no root | Public IP + ISP, carrier address check (100.64.0.0/10), traceroute hop count to your own IP, where the duckdns names point, what to do next. No router password needed (Termux: `pkg install traceroute`) |
| `mp-gateway.sh` | VPS + home box | WireGuard gateway: a VPS with a public IP forwards TCP 1883 / 8883 / 8884 and UDP 1922-1932, 1939, 7777-7780 to the home broker / servers (10.79.0.2). Kernel DNAT, no socat |
| `duckdns-mp.sh` | home box / VPS / Termux | Updates the 4 names only when the IP changes, `pin <VPS_IP>` for the gateway, systemd timer |
| `../udp-bench/udp-bench.sh` | anywhere | UDP echo server + client: real game-path ping, jitter, loss |
| `../../HiveMQ/global/mosquitto-docker/` | VPS / home Linux | The MP broker as a Docker container (host network) |

## Check (phone on the home Wi-Fi)

```sh
pkg install curl traceroute
curl -fsSLO https://raw.githubusercontent.com/JebetrexxMC/wagu/main/net/cgnat/cgnat-check.sh
bash cgnat-check.sh
```

Your phone's Wi-Fi address (192.168.1.x, gateway 192.168.1.1) is the LAN behind the F670L - normal,
it says nothing about CGNAT. The script checks the path from the router to the internet. If you can
open the router page later (the login is on the sticker under the F670L), pass its WAN IP:
`bash cgnat-check.sh <WAN-IP>`. A WAN IP of 100.64.x - 100.127.x, 10.x or 172.16-31.x = CGNAT.

## Fix A - public IP from Converge (best, free or small fee)

Ask Converge support for a **public IP** (say: "I host a game server, please remove CGNAT / give me
a public IPv4"). Then forward on the F670L to the box running the broker:
TCP 1883 8883 8884, UDP 1922-1932, UDP 7777-7780 - and `bash duckdns-mp.sh once`.
Bridge mode ([marfillaster's F670L gist](https://gist.github.com/marfillaster/5cfdc5d2c9e0bed3d8979f07944c051a))
only removes the double NAT when you put your own router behind the F670L; it does not remove CGNAT.

## Fix B - VPS gateway (works today, behind any CGNAT)

```sh
# on the VPS (public IP, Singapore is closest to PH):
bash mp-gateway.sh vps            # prints the command for the home side
# on the home Linux box (PC / Raspberry Pi) running the broker + servers:
bash mp-gateway.sh home <VPS_IP> <VPS_PUBKEY>
# back on the VPS:
bash mp-gateway.sh add-home <HOME_PUBKEY>
bash duckdns-mp.sh token <TOKEN> && bash duckdns-mp.sh pin <VPS_IP>
```

Phone broker (Termux, no root): `bash mp-gateway.sh phone <VPS_IP> <VPS_PUBKEY>` on the VPS prints a
config + QR for the **WireGuard app**; then `add-home <PUB> --snat`. Run `bash duckdns-mp.sh pin <VPS_IP>`
in Termux too - the termux-broker.sh loop then keeps the names on the VPS.

Player IPs: TCP keeps the real IP (policy routing, table 79). UDP is masqueraded by default because a
server bound to 0.0.0.0 answers from its LAN address; bind open.mp / SA-MP to 10.79.0.2 and use
`add-home <PUB> --udp-keep-ip` to keep the real IP on UDP too. Tested in a 3-namespace lab
(player / VPS / home with its own LAN default route): all three modes behave as described.

`mp-gateway.sh status` shows the tunnel handshake, the packets per forwarded port and the
VPS -> home ping. Also available as `gateway status` on the autobot, `mpadmin gw` in Termux and
`g` in the HiveMQ console.

## Check on a loop + outside probe

- Phone: `mpadmin cgnat loop on` runs the check every 30 min (Termux:Boot restarts it) and publishes the result, retained, to
  `thirtyfox/mp5/cgnat/<device>` on the public brokers. The website Termux console (termux.html) types `bash cgnat-check.sh`
  by itself every 10 min and shows each phone's latest verdict.
- GitHub: `.github/workflows/cgnat-probe.yml` probes 143.44.193.152 from outside every hour (`outside-probe.sh`: TCP SYN on
  22 80 443 1883 7547 8080 8883 8884 51821 23456, traceroute, ping) and writes `cgnat.json` + `history.json` to the
  `cgnat-status` branch. From outside you can't prove CGNAT; "SOMETHING ANSWERS" (RST) or "NO ANSWER" just means
  no forward reaches the broker. The phone check is the real test.

## Home + public lobby together (home-bridge.sh)

`bash home-bridge.sh on asia` (or `mpadmin bridge on asia`, autobot `bridge on`) runs MpBridge next to the home broker:
the home 24/7 servers are mirrored into the public lobby and the game frames (s/<tok>, c/<cid>) are carried both ways.
Wi-Fi offline: the public lobby stays up (the game already falls back to it). Wi-Fi back: the bridge reconnects
(1-30 s backoff) and lists the servers again; in the local test it was listed again 8 s after the link returned. `off` / `status` / `log`.

## Ping

The gateway makes the regions reachable (standby -> up) and enables the direct-UDP path, but every
packet goes player -> VPS -> home. From PH both legs to Singapore are roughly 35-60 ms, so expect
~80-110 ms - better than the ~200 ms to the EU / US public lobbies, not 40 ms. **~40 ms** needs
the broker and the 24/7 servers running on the Singapore / Manila VPS itself (HiveMQ/global/README.md,
`HiveMQ/global/mosquitto-docker/`), no home leg at all. `udp-bench.sh rtt <host>` measures each leg.

Credits: [cgnat-vps-gateway](https://github.com/JebetrexxMC/cgnat-vps-gateway),
[duckdns-systemd](https://github.com/JebetrexxMC/duckdns-systemd),
[duckdns-updater-script](https://github.com/honel/duckdns-updater-script),
[udp-ping-c](https://github.com/JebetrexxMC/udp-ping-c), [udp-latency](https://github.com/JebetrexxMC/udp-latency),
[mosquitto-docker-compose](https://github.com/JebetrexxMC/mosquitto-docker-compose),
[setup-mosquitto-with-docker](https://github.com/JebetrexxMC/setup-mosquitto-with-docker),
[topic: cgnat](https://github.com/topics/cgnat).
