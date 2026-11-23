# References

Repos checked for MP NetWire (NetWire 1.0) and what came out of each.

| Repo | What it is | Used for NetWire |
|---|---|---|
| [JebetrexxMC/cpp-netlib](https://github.com/JebetrexxMC/cpp-netlib) / [cpp-netlib org](https://github.com/cpp-netlib) (url, uri, asio) | C++ network library (Boost.Asio based) | API ideas (resolver, async client, URI handling). Not vendored: it needs Boost + OpenSSL, which are too big for the NDK / Termux builds. mpnet uses plain POSIX sockets. |
| [networktocode](https://github.com/orgs/networktocode/repositories) (66 repos: netutils, ntc-templates, diffsync, awesome-network-automation…) | network automation (Python, Ansible, Nautobot) | The "inventory file + scheduled checks + diff" idea behind `wires.txt` and the netwire-status history. Their tools target routers and switches (SSH/NETCONF), not game servers, so none is a runtime dependency. |
| [topics/networking](https://github.com/topics/networking) | GitHub topic | Survey of probe and relay designs. |
| [JebetrexxMC/sniffnet](https://github.com/JebetrexxMC/sniffnet) | desktop traffic monitor (Rust, iced, pcap) | `sniffnet/`: download/start script + MP port filters. The relay's `/traffic.json` is the headless / phone version of the same view. |
| [JebetrexxMC/Moya](https://github.com/JebetrexxMC/Moya) | Swift network abstraction layer (Alamofire) | `swift/`: MPNet package, `MPNetAPI` is a Moya `TargetType` for the status files. |
| [JebetrexxMC/tokio](https://github.com/JebetrexxMC/tokio) | Rust async runtime (1.54.1) | `rust/`: mpnet-agent runs on it, pulled from the fork via `[patch.crates-io]`. All probes of a round run at the same time. |
| [JebetrexxMC/bore](https://github.com/JebetrexxMC/bore) | TCP tunnel (bore-cli 0.6.0, control port 7835) | `rust/`: used as a library for `mpnet-agent tunnel` / `bore-server`, plus the `bore-pub` web wire. TCP only, so not for the UDP game ports. |
| [JebetrexxMC/RustScan](https://github.com/JebetrexxMC/RustScan) | fast port scanner (Rust) | `rustscan/`: download script (upstream 2.4.1 binaries, the fork has no releases) + own-server scan; its batched connect scan is copied in `mpnet-agent scan`. |
| [JebetrexxMC/ZeroTierOne](https://github.com/JebetrexxMC/ZeroTierOne) | virtual LAN / SD-WAN (1.16.2) | `zerotier/`: join script, `zerotier` wire group, `zerotier-api` web wire. Not built from source (official installer and apps). |
| [JebetrexxMC/xpipe](https://github.com/JebetrexxMC/xpipe) | desktop connection hub (24.13) | `xpipe/` SSH hosts + `manage/mpctl.sh` script. |
| [JebetrexxMC/RuView](https://github.com/JebetrexxMC/RuView) | WiFi CSI sensing (presence, vital signs; Rust) | Not related to game networking (it reads WiFi channel state for sensing, not traffic); nothing used. |
| [JebetrexxMC/dotnetcore-microservices-poc](https://github.com/JebetrexxMC/dotnetcore-microservices-poc) | .NET insurance sales microservices demo | Health-check / service-registry pattern only; no code used (different stack, not networking). |
| [JebetrexxMC/open_insure](https://github.com/JebetrexxMC/open_insure) | insurance policy software (TypeScript) | Not related to networking; nothing used. |
| open.mp [Shared/NetCode](https://github.com/openmultiplayer/open.mp) + [query mechanism docs](https://open.mp/docs/tutorials/QueryMechanism) | open.mp protocol | `samp` probe (`p` ping, `i` info). |
| [ADRFranklin/samp-udp-proxy](https://github.com/ADRFranklin/samp-udp-proxy) | UDP proxy in front of SA-MP | relay design (per-client upstream socket, idle timeout). |
| wagu `src/net/protocol.h`, `HiveMQ/xdp`, `HiveMQ/global`, `tools/lobby_status.py` | MP's own network code | `mp` probe uses PKT_QUERY/PKT_INFO from protocol.h directly; brokers / regions / XDP / lobby come from these. |

## NetWire 1.2

| Repo | What it is | Used for NetWire |
|---|---|---|
| [JebetrexxMC/uvloop](https://github.com/JebetrexxMC/uvloop) | fast asyncio event loop (libuv) | `py/mpprobe.py --loop uvloop` |
| [JebetrexxMC/trio](https://github.com/JebetrexxMC/trio) | structured-concurrency async I/O for Python | `py/mpprobe.py --loop trio` |
| [JebetrexxMC/gnet](https://github.com/JebetrexxMC/gnet) | event-driven Go networking (epoll / kqueue) | `go/mpedge`: UDP ping reflector + SA-MP / MP query cache in front of a game server (`replace` to the fork in go.mod) |
| [JebetrexxMC/mio](https://github.com/JebetrexxMC/mio) | Rust non-blocking I/O (tokio's base) | `mpnet-agent reflect`: one-thread ping reflector; patched to the fork for tokio too |
| [JebetrexxMC/libevent](https://github.com/JebetrexxMC/libevent) | C event library with async DNS | `server/mpnet-dns.c`: all wire hosts resolved at once (evdns), JSON out |
| [JebetrexxMC/poco](https://github.com/JebetrexxMC/poco) | C++ libraries (Net, JSON, Util) | `server/mpnet-ws.cpp`: wires.json pushed over WebSocket on change |
| [JebetrexxMC/GameNetworkingSockets](https://github.com/JebetrexxMC/GameNetworkingSockets) | Valve's reliable/unreliable UDP messages with encryption | `gns/mpgns`: lane test (reliable + unreliable burst, RTT, quality). Not in the game: SA-MP / open.mp servers speak RakNet. |
| [JebetrexxMC/swift-nio](https://github.com/JebetrexxMC/swift-nio) | Apple's event-driven networking for Swift | `swift/` target `MPNetNIO`: SA-MP query over UDP |
| [JebetrexxMC/Pulse](https://github.com/JebetrexxMC/Pulse) | network logger for Apple platforms | `pulse/` MPNetPulse: Moya plugin storing every MPNet call in LoggerStore |
| [JebetrexxMC/Mirror](https://github.com/JebetrexxMC/Mirror) | Unity networking | `mirror/MPNetWireManager.cs`: join the best wire (not compiled in CI) |
| [JebetrexxMC/firezone](https://github.com/JebetrexxMC/firezone) | zero-trust access on WireGuard | `firezone/`: gateway compose for private admin lanes |
| [JebetrexxMC/NetAlertX](https://github.com/JebetrexxMC/NetAlertX) | LAN device scanner + alerts | `netalertx/`: compose for home / LAN-party MP servers |
| [JebetrexxMC/calico](https://github.com/JebetrexxMC/calico) | Kubernetes networking + policy | `k8s/calico-policy.yaml` |
| [JebetrexxMC/cni](https://github.com/JebetrexxMC/cni) | container network interface spec + libs | `k8s/10-mpnet.conflist` |
| [JebetrexxMC/kubernetes-network-policy-recipes](https://github.com/JebetrexxMC/kubernetes-network-policy-recipes) | NetworkPolicy examples | `k8s/networkpolicy.yaml` (deny all, then allow the game port) |
| [JebetrexxMC/DevOps-Guide](https://github.com/JebetrexxMC/DevOps-Guide) | DevOps / networking notes | Reference for the k8s, Docker and CI parts. Docs only. |
| [JebetrexxMC/theByteBook](https://github.com/JebetrexxMC/theByteBook) | cloud-native networking book (Chinese) | Reference: Linux kernel networking, eBPF/XDP, service mesh (the XDP boxes). Docs only. |
| [JebetrexxMC/MultiplayerNetworkingResources](https://github.com/JebetrexxMC/MultiplayerNetworkingResources) | list of game networking articles | Reference: lag compensation, snapshot interpolation, relay design. Docs only. |
| [JebetrexxMC/tauri](https://github.com/JebetrexxMC/tauri) + [openmultiplayer/launcher](https://github.com/openmultiplayer/launcher) | desktop app framework + the open.mp launcher | `launcher/`: MP Launcher for PC, own code following the open.mp launcher's join flow (MPL-2.0; no open.mp names or art used) |
