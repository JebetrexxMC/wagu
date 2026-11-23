# RustScan for MP

[RustScan](https://github.com/JebetrexxMC/RustScan) is a very fast port scanner. Here it answers one question: **which MP ports are really open on my own servers?** (MQTT 1883 / 8883 / 8884, MP game 1922 / 1930 / 1931, mpnetd 8090 / 8091, bore 7835, SSH 22).

```
bash net/rustscan/get-rustscan.sh          # RustScan 2.4.1 -> ~/.local/bin
bash net/rustscan/scan-own.sh              # every own host in wires.txt
rustscan -a mpsamp.duckdns.org -p 1883,8883,8884,22 -g
```

The binaries come from wagu release [v5.02.57](https://github.com/JebetrexxMC/wagu/releases/tag/v5.02.57) (upstream bee-san/RustScan as fallback):

| file | for |
|---|---|
| `x86_64-linux-rustscan.tar.gz.zip` | Linux PC / VPS, 64-bit |
| `x86-linux-rustscan.zip` | Linux, 32-bit |
| `aarch64-linux-rustscan.zip` | ARM Linux: Oracle Ampere, Raspberry Pi 64-bit (not Termux) |
| `x86_64-macos-rustscan.tar.gz.zip` / `aarch64-macos-rustscan.tar.gz.zip` | Mac Intel / Apple Silicon |
| `x86_64-windows-rustscan.exe.zip` / `x86-windows-rustscan.exe.zip` | Windows 64 / 32-bit (unzip, run `rustscan.exe` in a terminal) |

On a phone (Termux): `pkg install rustscan`, or use `mpnet-agent scan` below.

No RustScan? `scan-own.sh` falls back to `mpnet-agent own` (same idea, written with tokio, see `../rust`). That one also asks the MP / SA-MP query on the UDP game ports, which a TCP scanner can't see:

```
mpnet-agent scan mpsamp.duckdns.org -p 1-2000 --udp 1922,1930,1931
```

Only scan servers you own or have permission to test.
