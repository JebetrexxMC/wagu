# mplatency: 24/7 server latency + location monitor

Stdlib-only Go. Probes every wire in `net/wires.txt` (or `-t` targets) every 5 s with goroutines,
keeps last / avg / min / max / jitter / loss / uptime, looks up locations once (ip-api batch, cached 6 h)
and serves a dashboard.

```sh
cd net/go && go build -o mplatency ./mplatency
./mplatency -wires ../wires.txt -http :8099          # dashboard http://host:8099/  (+ /status.json /metrics /healthz)
./mplatency -t vps=mp://1.2.3.4:1922 -t samp://5.6.7.8:7777 -once   # one round, exit 1 if any is down
nohup ./mplatency -wires ../wires.txt -quiet > mplatency.log 2>&1 &   # background (Termux too)
```

Kinds: `tcp`, `tls`, `http`, `mqtt` (TCP connect), `samp` (open.mp / SA-MP `p` query), `mp` (MP PKT_QUERY),
`udp` (NetWire echo). Alerts: `-discord URL`, `-telegram-token T -telegram-chat ID`
(or `MP_DISCORD_WEBHOOK`, `MP_TELEGRAM_TOKEN`, `MP_TELEGRAM_CHAT`).

systemd: copy `mplatency.service` to `/etc/systemd/system/`, fix the paths, then
`systemctl enable --now mplatency`.
