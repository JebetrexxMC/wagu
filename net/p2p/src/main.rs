// mpnet-p2p: MP LAN discovery without a broker, on rust-libp2p (fork github.com/JebetrexxMC/rust-libp2p).
// Every node finds the others on the same LAN / Wi-Fi hotspot / ZeroTier network with mDNS, says which MP
// server it hosts (identify agent string "mpnet-p2p/1 server=host:port name=..."), and pings them
// (libp2p ping = RTT between the boxes). One JSON line per event, so a script or the launcher can read it.
//   mpnet-p2p --server 192.168.1.10:7777 --name "LAN party"   # a box that hosts a server
//   mpnet-p2p                                                  # a player box: lists the servers it finds
//   mpnet-p2p --dial /ip4/1.2.3.4/tcp/4001                     # across the internet (no mDNS there)
use clap::Parser;
use futures::StreamExt;
use libp2p::{identify, mdns, noise, ping, swarm::{NetworkBehaviour, SwarmEvent}, tcp, yamux, Multiaddr};
use std::time::Duration;

#[derive(Parser)]
#[command(version, about)]
struct Args {
    /// MP / open.mp server this box hosts (host:port)
    #[arg(long)]
    server: Option<String>,
    #[arg(long, default_value = "")]
    name: String,
    /// TCP port for libp2p (0 = any)
    #[arg(long, default_value_t = 4001)]
    port: u16,
    /// peer to dial directly (multiaddr)
    #[arg(long)]
    dial: Vec<Multiaddr>,
    /// no mDNS (tests, networks that block multicast)
    #[arg(long)]
    no_mdns: bool,
    /// stop after this many seconds (0 = run forever)
    #[arg(long, default_value_t = 0)]
    seconds: u64,
}

#[derive(NetworkBehaviour)]
struct B {
    ping: ping::Behaviour,
    identify: identify::Behaviour,
    mdns: libp2p::swarm::behaviour::toggle::Toggle<mdns::tokio::Behaviour>,
}

fn js(s: &str) -> String { s.replace('\\', "\\\\").replace('"', "\\\"") }

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let a = Args::parse();
    let agent = match &a.server {
        Some(s) => format!("mpnet-p2p/1 server={s} name={}", a.name.replace(' ', "_")),
        None => "mpnet-p2p/1 player".to_string(),
    };
    let no_mdns = a.no_mdns;
    let mut swarm = libp2p::SwarmBuilder::with_new_identity()
        .with_tokio()
        .with_tcp(tcp::Config::default(), noise::Config::new, yamux::Config::default)?
        .with_behaviour(|key| {
            let id = key.public().to_peer_id();
            Ok(B {
                ping: ping::Behaviour::new(ping::Config::new().with_interval(Duration::from_secs(5))),
                identify: identify::Behaviour::new(identify::Config::new("/mpnet/1".into(), key.public()).with_agent_version(agent.clone())),
                mdns: if no_mdns { None } else { mdns::tokio::Behaviour::new(mdns::Config::default(), id).ok() }.into(),
            })
        })?
        .with_swarm_config(|c| c.with_idle_connection_timeout(Duration::from_secs(60)))
        .build();
    swarm.listen_on(format!("/ip4/0.0.0.0/tcp/{}", a.port).parse()?)?;
    println!("{{\"event\":\"start\",\"peer\":\"{}\",\"agent\":\"{}\"}}", swarm.local_peer_id(), js(&agent));
    for d in &a.dial { swarm.dial(d.clone())?; }
    let deadline = if a.seconds > 0 { Some(tokio::time::Instant::now() + Duration::from_secs(a.seconds)) } else { None };
    loop {
        let ev = match deadline {
            Some(t) => match tokio::time::timeout_at(t, swarm.select_next_some()).await { Ok(e) => e, Err(_) => break },
            None => swarm.select_next_some().await,
        };
        match ev {
            SwarmEvent::NewListenAddr { address, .. } => println!("{{\"event\":\"listen\",\"addr\":\"{address}\"}}"),
            SwarmEvent::Behaviour(BEvent::Mdns(mdns::Event::Discovered(list))) => {
                for (peer, addr) in list {
                    println!("{{\"event\":\"found\",\"peer\":\"{peer}\",\"addr\":\"{addr}\"}}");
                    let _ = swarm.dial(addr);
                }
            }
            SwarmEvent::Behaviour(BEvent::Identify(identify::Event::Received { peer_id, info, .. })) => {
                if let Some(s) = info.agent_version.split_whitespace().find_map(|w| w.strip_prefix("server=")) {
                    let name = info.agent_version.split_whitespace().find_map(|w| w.strip_prefix("name=")).unwrap_or("").replace('_', " ");
                    println!("{{\"event\":\"server\",\"peer\":\"{peer_id}\",\"server\":\"{}\",\"name\":\"{}\"}}", js(s), js(&name));
                } else {
                    println!("{{\"event\":\"player\",\"peer\":\"{peer_id}\"}}");
                }
            }
            SwarmEvent::Behaviour(BEvent::Ping(ping::Event { peer, result: Ok(rtt), .. })) => {
                println!("{{\"event\":\"ping\",\"peer\":\"{peer}\",\"ms\":{:.2}}}", rtt.as_secs_f64() * 1000.0)
            }
            SwarmEvent::ConnectionClosed { peer_id, .. } => println!("{{\"event\":\"gone\",\"peer\":\"{peer_id}\"}}"),
            _ => {}
        }
    }
    Ok(())
}
