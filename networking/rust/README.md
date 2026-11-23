# mpnet-agent (Rust, tokio + bore)

The NetWire agent in Rust. Built on [tokio](https://github.com/JebetrexxMC/tokio) (the async runtime, pulled from the fork through `[patch.crates-io]`) and [bore](https://github.com/JebetrexxMC/bore) (TCP tunnels, used as a library). Same `wires.txt` as mpnetd, every probe runs at the same time.

```
cargo build --release                      # Linux, macOS, Windows, Termux (pkg install rust)
A=target/release/mpnet-agent

$A probe -w ../wires.txt                   # all wires at once: mqtt, mp, samp, udp, tcp/tls/http
$A scan mpsamp.duckdns.org -p 1-2000 --udp 1922,1930,1931   # RustScan-style TCP scan + MP / SA-MP UDP query
$A own -w ../wires.txt                     # scan the MP ports of your own servers only

# bore: share a local TCP port through a bore server
$A bore-server --min-port 20000 --max-port 20100      # on a VPS / Oracle box (control port 7835), BORE_SECRET=... optional
$A tunnel --local-port 1883 --to my.vps.example       # -> my.vps.example:2xxxx reaches your local MQTT
$A tunnel --local-port 8090 --to bore.pub             # public bore.pub server
```

bore carries **TCP only**: MQTT 1883, the mpnetd status pages, SSH, a website. The MP / open.mp game ports are UDP, use ZeroTier (`../zerotier`) or playit for those.

Only scan servers you own or have permission to test.
