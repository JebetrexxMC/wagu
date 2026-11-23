# Query shield

Keeps an open.mp / SA-MP server "online" in the server browser and the MP lobby during a query flood. The server's query code is single-threaded, so a flood of `SAMP` query packets makes it look offline even when the game itself runs fine.

`setup.sh` redirects only the query packets with iptables to a cache, rate-limits them per IP, and runs the cache as a systemd service. Game packets (RakNet) still go to the server port.

| mode | from | notes |
|---|---|---|
| `pack_scan` (default) | [JebetrexxMC/samp-packet-proxy](https://github.com/JebetrexxMC/samp-packet-proxy) (dennorske) | Python 3.7+, needs 2+ CPU threads at high packet rates |
| `mpedge` | `net/go/mpedge` (gnet) | Go, one binary, also answers NetWire `MPNETPNG` pings |

```
sudo bash net/query-shield/setup.sh --ip 1.2.3.4 --port 7777
sudo bash net/query-shield/setup.sh --port 7777 --remove
```

It doesn't stop a DDoS (spoofed UDP still reaches the box) and only protects the query side.

## How it fits with mpnetd

- **Query cache** (this folder, mpedge): it sits on the server box, answers queries for it, and never touches game packets.
- **Relay** (`mpnetd relay`): it sits on a box near the players. Each player gets their own upstream socket and stays on that upstream for the whole session, because switching mid-game would drop the RakNet session. This is the same idea as [JebetrexxMC/samp-udp-proxy](https://github.com/JebetrexxMC/samp-udp-proxy) (ADRFranklin).
  - That repo is an unfinished Rust proxy (its README says WIP). It parses packets but doesn't forward them yet, so it's kept as a reference, and CI only checks that it builds.
