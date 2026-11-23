# RustScan for MP

[RustScan](https://github.com/JebetrexxMC/RustScan) is a very fast port scanner. Here it answers one question: **which MP ports are really open on my own servers?** (MQTT 1883 / 8883 / 8884, MP game 1922 / 1930 / 1931, mpnetd 8090 / 8091, bore 7835, SSH 22).

```
bash networking/rustscan/get-rustscan.sh          # RustScan 2.4.1 -> ~/.local/bin (Linux x86_64 / arm64)
bash networking/rustscan/scan-own.sh              # every own host in wires.txt
rustscan -a mpsamp.duckdns.org -p 1883,8883,8884,22 -g
```

No RustScan? `scan-own.sh` falls back to `mpnet-agent own` (same idea, written with tokio, see `../rust`). That one also asks the MP / SA-MP query on the UDP game ports, which a TCP scanner can't see:

```
mpnet-agent scan mpsamp.duckdns.org -p 1-2000 --udp 1922,1930,1931
```

Only scan servers you own or have permission to test.
