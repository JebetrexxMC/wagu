# Sniffnet for MP

[Sniffnet](https://github.com/GyulyVGC/sniffnet) (fork: [JebetrexxMC/sniffnet](https://github.com/JebetrexxMC/sniffnet)) lets you comfortably watch your network traffic: which hosts a program talks to, how many bytes and packets each one moves, which countries and services are involved, all in a desktop app (Windows / macOS / Linux). Use it on the PC that hosts an MP server, the open.mp crossplay server, a VPS with a desktop, or the PC you play SA-MP on.

- `get-sniffnet.sh` downloads the latest Sniffnet release (the fork has no builds of its own, so the files come from upstream) and starts it. `--print` lists the Windows / macOS links, `--deb` installs the Debian package.
- `mp-filters.txt` lists every MP port plus one BPF filter (Sniffnet 1.4.1+) that shows only MP, open.mp / SA-MP and MP lobby traffic.

## Phones and headless servers

Sniffnet is a desktop app. It doesn't run on Android, and it needs a screen. For phones and VPS boxes without a desktop, `mpnetd` has the same kind of live view without root or pcap:

- `mpnetd relay ... --http 8091`: open `http://<box>:8091/traffic.json` for every player going through the relay (packets and bytes each way, kbit/s, idle time, upstream) and the interface counters.
- `mpnetd watch ... --http 8090`: `/traffic.json` has the interface counters (`/proc/net/dev`), and `/wires.json` has the grade of every wire.

A capture saved in Sniffnet (Export, PCAP) can be opened again in Sniffnet or in Wireshark.
