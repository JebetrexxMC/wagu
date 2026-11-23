# References

Repos checked for MP NetWire (NetWire 1.0) and what came out of each.

| Repo | What it is | Used for NetWire |
|---|---|---|
| [JebetrexxMC/cpp-netlib](https://github.com/JebetrexxMC/cpp-netlib) / [cpp-netlib org](https://github.com/cpp-netlib) (url, uri, asio) | C++ network library (Boost.Asio based) | API ideas (resolver, async client, URI handling). Not vendored: it needs Boost + OpenSSL, which are too big for the NDK / Termux builds. mpnet uses plain POSIX sockets. |
| [networktocode](https://github.com/orgs/networktocode/repositories) (66 repos: netutils, ntc-templates, diffsync, awesome-network-automation…) | network automation (Python, Ansible, Nautobot) | The "inventory file + scheduled checks + diff" idea behind `wires.txt` and the netwire-status history. Their tools target routers and switches (SSH/NETCONF), not game servers, so none is a runtime dependency. |
| [topics/networking](https://github.com/topics/networking) | GitHub topic | Survey of probe and relay designs. |
| [JebetrexxMC/sniffnet](https://github.com/JebetrexxMC/sniffnet) | desktop traffic monitor (Rust, iced, pcap) | `sniffnet/`: download/start script + MP port filters. The relay's `/traffic.json` is the headless / phone version of the same view. |
| [JebetrexxMC/Moya](https://github.com/JebetrexxMC/Moya) | Swift network abstraction layer (Alamofire) | `swift/`: MPNet package, `MPNetAPI` is a Moya `TargetType` for the status files. |
| [JebetrexxMC/RuView](https://github.com/JebetrexxMC/RuView) | WiFi CSI sensing (presence, vital signs; Rust) | Not related to game networking (it reads WiFi channel state for sensing, not traffic); nothing used. |
| [JebetrexxMC/dotnetcore-microservices-poc](https://github.com/JebetrexxMC/dotnetcore-microservices-poc) | .NET insurance sales microservices demo | Health-check / service-registry pattern only; no code used (different stack, not networking). |
| [JebetrexxMC/open_insure](https://github.com/JebetrexxMC/open_insure) | insurance policy software (TypeScript) | Not related to networking; nothing used. |
| open.mp [Shared/NetCode](https://github.com/openmultiplayer/open.mp) + [query mechanism docs](https://open.mp/docs/tutorials/QueryMechanism) | open.mp protocol | `samp` probe (`p` ping, `i` info). |
| [ADRFranklin/samp-udp-proxy](https://github.com/ADRFranklin/samp-udp-proxy) | UDP proxy in front of SA-MP | relay design (per-client upstream socket, idle timeout). |
| wagu `src/net/protocol.h`, `HiveMQ/xdp`, `HiveMQ/global`, `tools/lobby_status.py` | MP's own network code | `mp` probe uses PKT_QUERY/PKT_INFO from protocol.h directly; brokers / regions / XDP / lobby come from these. |
