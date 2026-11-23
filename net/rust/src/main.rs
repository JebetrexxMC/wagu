//! mpnet-agent - MP NetWire agent (tokio).
//!
//!   mpnet-agent probe -w wires.txt [-w more.txt]           async probe of every wire (same file as mpnetd)
//!   mpnet-agent scan HOST [-p 1-1024|1883,8883] [--udp 1922,7777] [--batch 2000] [--timeout 800]
//!        RustScan-style fast TCP connect scan + MP / SA-MP UDP query on the --udp ports.
//!        Only scan servers you own or are allowed to test (your VPS, Oracle box, home PC).
//!   mpnet-agent own [-w wires.txt]                         scan the MP ports of every own wire (region / vps / oracle / xdp / relay / custom)
//!   mpnet-agent tunnel --local-port 1883 [--to bore.pub] [--port 0] [--secret S]
//!        bore TCP tunnel: expose a local TCP port (MQTT broker, mpnetd --http status, SSH) through a bore
//!        server. bore is TCP only: the MP / SA-MP game ports are UDP and need playit / frp / ZeroTier.
//!   mpnet-agent bore-server [--min-port 20000] [--max-port 21000] [--secret S]   your own bore server (VPS, port 7835)
use anyhow::{bail, Context, Result};
use clap::{Parser, Subcommand};
use futures::stream::{self, StreamExt};
use std::collections::HashMap;
use std::net::{IpAddr, SocketAddr};
use std::time::{Duration, Instant};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::{lookup_host, TcpStream, UdpSocket};
use tokio::time::timeout;

const PKT_QUERY: u8 = 14; // src/net/protocol.h ePacket
const PKT_INFO: u8 = 15;
const MP_TCP_PORTS: &[u16] = &[22, 80, 443, 1883, 7681, 8083, 8084, 8090, 8091, 8883, 8884, 7835];
const MP_UDP_PORTS: &[u16] = &[1922, 1930, 1931, 1932, 1933, 1934, 7777, 7778, 7780];

#[derive(Parser)]
#[command(name = "mpnet-agent", version, about = "MP NetWire agent: probes, port scan, bore tunnels")]
struct Cli {
    #[command(subcommand)]
    cmd: Cmd,
}

#[derive(Subcommand)]
enum Cmd {
    /// Probe every wire of wires.txt (async, all at once)
    Probe {
        #[arg(short, long = "wires", default_value = "net/wires.txt")]
        w: Vec<String>,
        #[arg(long, default_value_t = 4)]
        samples: usize,
        #[arg(long, default_value_t = 1500)]
        timeout: u64,
    },
    /// Fast TCP connect scan (RustScan style) + MP / SA-MP UDP query
    Scan {
        host: String,
        #[arg(short, long)]
        ports: Option<String>,
        #[arg(long)]
        udp: Option<String>,
        #[arg(long, default_value_t = 2000)]
        batch: usize,
        #[arg(long, default_value_t = 800)]
        timeout: u64,
    },
    /// Scan the MP ports of every own server in wires.txt
    Own {
        #[arg(short, long = "wires", default_value = "net/wires.txt")]
        w: Vec<String>,
        #[arg(long, default_value_t = 800)]
        timeout: u64,
    },
    /// UDP ping reflector on mio (no async runtime): answers "MPNETPNG" and SA-MP 'p' pings
    Reflect {
        #[arg(long, default_value = "0.0.0.0:7790")]
        bind: String,
    },
    /// bore client: expose a local TCP port
    Tunnel {
        #[arg(long)]
        local_port: u16,
        #[arg(long, default_value = "localhost")]
        local_host: String,
        #[arg(long, default_value = "bore.pub")]
        to: String,
        #[arg(long, default_value_t = 0)]
        port: u16,
        #[arg(long, env = "BORE_SECRET")]
        secret: Option<String>,
    },
    /// bore server (control port 7835)
    BoreServer {
        #[arg(long, default_value_t = 20000)]
        min_port: u16,
        #[arg(long, default_value_t = 21000)]
        max_port: u16,
        #[arg(long, env = "BORE_SECRET")]
        secret: Option<String>,
    },
}

// ---- wires.txt (same format as the C++ library) ----
#[derive(Clone, Debug)]
struct Wire {
    id: String,
    group: String,
    kind: String,
    host: String,
    port: u16,
    #[allow(dead_code)]
    name: String,
    opt: HashMap<String, String>,
}

fn parse_wire(line: &str) -> Option<Wire> {
    let l = line.trim();
    if l.is_empty() || l.starts_with('#') {
        return None;
    }
    let f: Vec<&str> = l.split('|').map(|s| s.trim()).collect();
    if f.len() < 4 {
        return None;
    }
    let (host, port) = f[3].rsplit_once(':')?;
    let mut opt = HashMap::new();
    if f.len() > 5 {
        for kv in f[5].split_whitespace() {
            if let Some((k, v)) = kv.split_once('=') {
                opt.insert(k.to_string(), v.to_string());
            }
        }
    }
    Some(Wire {
        id: f[0].into(),
        group: f[1].into(),
        kind: f[2].into(),
        host: host.into(),
        port: port.parse().ok()?,
        name: f.get(4).filter(|s| !s.is_empty()).unwrap_or(&f[0]).to_string(),
        opt,
    })
}

fn load(files: &[String]) -> Result<Vec<Wire>> {
    let mut v: Vec<Wire> = Vec::new();
    for f in files {
        let s = std::fs::read_to_string(f).with_context(|| format!("read {f}"))?;
        for w in s.lines().filter_map(parse_wire) {
            if !v.iter().any(|o| o.id == w.id) {
                v.push(w);
            }
        }
    }
    Ok(v)
}

async fn resolve(host: &str, port: u16) -> Result<SocketAddr> {
    lookup_host((host, port)).await?.find(|a| a.is_ipv4()).context("DNS failed")
}

// ---- probes ----
async fn udp_ask(addr: SocketAddr, req: &[u8], to: Duration, ok: impl Fn(&[u8]) -> bool) -> Option<(Duration, Vec<u8>)> {
    let s = UdpSocket::bind("0.0.0.0:0").await.ok()?;
    s.connect(addr).await.ok()?;
    let t0 = Instant::now();
    s.send(req).await.ok()?;
    let mut b = vec![0u8; 2048];
    let end = t0 + to;
    while Instant::now() < end {
        match timeout(end - Instant::now(), s.recv(&mut b)).await {
            Ok(Ok(n)) if ok(&b[..n]) => return Some((t0.elapsed(), b[..n].to_vec())),
            Ok(Ok(_)) => continue,
            _ => return None,
        }
    }
    None
}

async fn mp_query(addr: SocketAddr, to: Duration) -> Option<(Duration, String, u8, u8)> {
    let t: u32 = fastrand_u32();
    let mut q = vec![PKT_QUERY];
    q.extend_from_slice(&t.to_le_bytes());
    let (d, b) = udp_ask(addr, &q, to, |b| b.len() >= 42 && b[0] == PKT_INFO && b[1..5] == t.to_le_bytes()).await?;
    let name = String::from_utf8_lossy(&b[10..42]).trim_end_matches('\0').to_string();
    Some((d, name, b[7], b[8]))
}

async fn samp_query(addr: SocketAddr, to: Duration, op: u8) -> Option<(Duration, Vec<u8>)> {
    let SocketAddr::V4(a4) = addr else { return None };
    let mut q = b"SAMP".to_vec();
    q.extend_from_slice(&a4.ip().octets());
    q.extend_from_slice(&a4.port().to_le_bytes());
    q.push(op);
    let tok = fastrand_u32().to_le_bytes();
    if op == b'p' {
        q.extend_from_slice(&tok);
    }
    udp_ask(addr, &q, to, move |b| b.len() >= 11 && &b[..4] == b"SAMP" && b[10] == op && (op != b'p' || (b.len() >= 15 && b[11..15] == tok))).await
}

async fn mqtt_probe(addr: SocketAddr, w: &Wire, to: Duration, samples: usize) -> Result<Vec<Duration>> {
    let mut s = timeout(to, TcpStream::connect(addr)).await.context("timeout")??;
    s.set_nodelay(true)?;
    let user = w.opt.get("user");
    let pass = w.opt.get("pass");
    let mut v: Vec<u8> = vec![0, 4, b'M', b'Q', b'T', b'T', 4, 0x02 | if user.is_some() { 0x80 } else { 0 } | if pass.is_some() { 0x40 } else { 0 }, 0, 10];
    let mut put = |x: &str| {
        v.extend_from_slice(&(x.len() as u16).to_be_bytes());
        v.extend_from_slice(x.as_bytes());
    };
    put(&format!("mpnet-rs-{}", fastrand_u32() % 1_000_000));
    if let Some(u) = user { put(u) }
    if let Some(p) = pass { put(p) }
    let mut pkt = vec![0x10u8];
    let mut len = v.len();
    loop {
        let mut b = (len % 128) as u8;
        len /= 128;
        if len > 0 { b |= 0x80 }
        pkt.push(b);
        if len == 0 { break }
    }
    pkt.extend_from_slice(&v);
    s.write_all(&pkt).await?;
    let mut ack = [0u8; 4];
    timeout(to, s.read_exact(&mut ack)).await.context("no CONNACK")??;
    if ack[0] != 0x20 { bail!("not MQTT") }
    if ack[3] != 0 { bail!("CONNACK rc={}", ack[3]) }
    let mut out = Vec::new();
    for i in 0..samples {
        if i > 0 { tokio::time::sleep(Duration::from_millis(150)).await }
        let t0 = Instant::now();
        s.write_all(&[0xC0, 0]).await?;
        let mut r = [0u8; 2];
        if timeout(to, s.read_exact(&mut r)).await.is_ok() && r[0] == 0xD0 {
            out.push(t0.elapsed());
        }
    }
    let _ = s.write_all(&[0xE0, 0]).await;
    Ok(out)
}

struct Res { rtt: Vec<Duration>, sent: usize, info: String, err: String }

async fn probe(w: &Wire, samples: usize, to: Duration) -> Res {
    let mut r = Res { rtt: vec![], sent: samples, info: String::new(), err: String::new() };
    if w.kind == "lobby" {
        r.sent = 0;
        r.info = format!("lobby-listed, online={}", w.opt.get("online").map(|s| s.as_str()).unwrap_or("?"));
        return r;
    }
    let addr = match resolve(&w.host, w.port).await { Ok(a) => a, Err(e) => { r.err = e.to_string(); return r } };
    match w.kind.as_str() {
        "mqtt" => match mqtt_probe(addr, w, to, samples).await { Ok(v) => r.rtt = v, Err(e) => r.err = e.to_string() },
        "mp" => for _ in 0..samples {
            if let Some((d, n, p, m)) = mp_query(addr, to).await { r.rtt.push(d); r.info = format!("{n} {p}/{m}") }
        },
        "samp" => {
            for _ in 0..samples { if let Some((d, _)) = samp_query(addr, to, b'p').await { r.rtt.push(d) } }
            if let Some((_, b)) = samp_query(addr, to, b'i').await {
                if b.len() >= 20 {
                    let pl = u16::from_le_bytes([b[12], b[13]]);
                    let mx = u16::from_le_bytes([b[14], b[15]]);
                    let hl = u32::from_le_bytes([b[16], b[17], b[18], b[19]]) as usize;
                    let hn: String = b.get(20..20 + hl).map(|x| x.iter().map(|&c| c as char).collect()).unwrap_or_default();
                    r.info = format!("{hn} {pl}/{mx}");
                }
            }
        }
        "udp" => for _ in 0..samples {
            let t = fastrand_u32().to_le_bytes();
            let mut q = b"MPNETPNG".to_vec(); q.extend_from_slice(&t); q.extend_from_slice(&[0; 4]);
            if let Some((d, _)) = udp_ask(addr, &q, to, |b| b.len() >= 12 && b[8..12] == t).await { r.rtt.push(d) }
        },
        _ => for _ in 0..samples {   // tcp / tls / http: connect time
            let t0 = Instant::now();
            match timeout(to, TcpStream::connect(addr)).await {
                Ok(Ok(_)) => r.rtt.push(t0.elapsed()),
                Ok(Err(e)) => r.err = e.to_string(),
                Err(_) => r.err = "timeout".into(),
            }
        },
    }
    if r.rtt.is_empty() && r.err.is_empty() { r.err = "no reply".into() }
    r
}

fn grade(r: &Res) -> (&'static str, Option<f64>, f64) {
    if r.sent == 0 { return ("skip", None, 0.0) }
    if r.rtt.is_empty() { return ("down", None, 1.0) }
    let mut v: Vec<f64> = r.rtt.iter().map(|d| d.as_secs_f64() * 1000.0).collect();
    let jit = if v.len() > 1 { v.windows(2).map(|w| (w[1] - w[0]).abs()).sum::<f64>() / (v.len() - 1) as f64 } else { 0.0 };
    v.sort_by(|a, b| a.partial_cmp(b).unwrap());
    let med = v[v.len() / 2];
    let loss = 1.0 - r.rtt.len() as f64 / r.sent as f64;
    let g = if med <= 120.0 && jit <= 30.0 && loss <= 0.2 { "green" } else if med <= 250.0 && loss <= 0.5 { "yellow" } else { "red" };
    (g, Some(med), loss)
}

// ---- scan ----
fn parse_ports(s: &str) -> Result<Vec<u16>> {
    let mut v = Vec::new();
    for part in s.split(',').filter(|p| !p.is_empty()) {
        if let Some((a, b)) = part.split_once('-') {
            let (a, b): (u16, u16) = (a.parse()?, b.parse()?);
            if a > b { bail!("bad range {part}") }
            v.extend(a..=b);
        } else {
            v.push(part.parse()?);
        }
    }
    Ok(v)
}

async fn scan(host: &str, tcp: &[u16], udp: &[u16], batch: usize, to: Duration) -> Result<()> {
    let ip: IpAddr = resolve(host, 0).await?.ip();
    let t0 = Instant::now();
    let mut open: Vec<(u16, f64)> = stream::iter(tcp.iter().copied())
        .map(|p| async move {
            let s = Instant::now();
            match timeout(to, TcpStream::connect((ip, p))).await {
                Ok(Ok(_)) => Some((p, s.elapsed().as_secs_f64() * 1000.0)),
                _ => None,
            }
        })
        .buffer_unordered(batch.max(1))
        .filter_map(|x| async move { x })
        .collect()
        .await;
    open.sort_by_key(|x| x.0);
    println!("{host} ({ip}): {} TCP ports in {:.1}s, open: {}", tcp.len(), t0.elapsed().as_secs_f64(),
        if open.is_empty() { "none".to_string() } else { open.iter().map(|(p, ms)| format!("{p} ({ms:.0} ms)")).collect::<Vec<_>>().join(", ") });
    for &p in udp {
        let a = SocketAddr::new(ip, p);
        if let Some((d, n, pl, mx)) = mp_query(a, to).await {
            println!("  udp {p}: MP server \"{n}\" {pl}/{mx} ({:.0} ms)", d.as_secs_f64() * 1000.0);
        } else if let Some((d, _)) = samp_query(a, to, b'p').await {
            println!("  udp {p}: open.mp / SA-MP server ({:.0} ms)", d.as_secs_f64() * 1000.0);
        } else {
            println!("  udp {p}: no MP / SA-MP answer");
        }
    }
    Ok(())
}

fn fastrand_u32() -> u32 {
    use std::sync::atomic::{AtomicU64, Ordering};
    static S: AtomicU64 = AtomicU64::new(0);
    let mut x = S.load(Ordering::Relaxed);
    if x == 0 { x = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos() as u64 | 1 }
    x ^= x << 13; x ^= x >> 7; x ^= x << 17;
    S.store(x, Ordering::Relaxed);
    (x >> 16) as u32
}

// mio (fork github.com/JebetrexxMC/mio): one thread, edge-triggered epoll / kqueue, no runtime.
// Small enough for a 1-core VPS or a Termux phone that should only answer pings 24/7.
fn reflect(bind: &str) -> Result<()> {
    use mio::{net::UdpSocket, Events, Interest, Poll, Token};
    let mut poll = Poll::new()?;
    let mut sock = UdpSocket::bind(bind.parse()?)?;
    poll.registry().register(&mut sock, Token(0), Interest::READABLE)?;
    println!("reflect: udp {} (MPNETPNG + SA-MP p)", sock.local_addr()?);
    let mut events = Events::with_capacity(256);
    let mut buf = [0u8; 2048];
    let mut n_ok: u64 = 0;
    loop {
        poll.poll(&mut events, None)?;
        for _ in events.iter() {
            loop {
                match sock.recv_from(&mut buf) {
                    Ok((n, from)) => {
                        let b = &buf[..n];
                        let reply = (n == 16 && &b[..8] == b"MPNETPNG") || (n == 15 && &b[..4] == b"SAMP" && b[10] == b'p');
                        if reply {
                            let _ = sock.send_to(b, from);
                            n_ok += 1;
                            if n_ok % 10000 == 0 { println!("reflect: {n_ok} pings"); }
                        }
                    }
                    Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => break,
                    Err(e) => return Err(e.into()),
                }
            }
        }
    }
}

#[tokio::main]
async fn main() -> Result<()> {
    match Cli::parse().cmd {
        Cmd::Probe { w, samples, timeout: t } => {
            let wires = load(&w)?;
            let to = Duration::from_millis(t);
            let res: Vec<(Wire, Res)> = stream::iter(wires.into_iter())
                .map(|w| async move { let r = probe(&w, samples, to).await; (w, r) })
                .buffered(64)
                .collect()
                .await;
            let mut up = 0;
            for (w, r) in &res {
                let (g, med, loss) = grade(r);
                if g != "down" && g != "skip" { up += 1 }
                println!("{g:<7} {:<24} {:<6} {:>7} loss {:>3}%  {}", w.id, w.kind,
                    med.map(|m| format!("{m:.0} ms")).unwrap_or("-".into()), (loss * 100.0).round(), if r.err.is_empty() { &r.info } else { &r.err });
            }
            println!("{up}/{} up", res.iter().filter(|(_, r)| r.sent > 0).count());
        }
        Cmd::Scan { host, ports, udp, batch, timeout: t } => {
            let tcp = match ports { Some(p) => parse_ports(&p)?, None => MP_TCP_PORTS.to_vec() };
            let udp = match udp { Some(p) => parse_ports(&p)?, None => vec![] };
            scan(&host, &tcp, &udp, batch, Duration::from_millis(t)).await?;
        }
        Cmd::Own { w, timeout: t } => {
            let wires = load(&w)?;
            let own = ["region", "vps", "oracle", "xdp", "relay", "custom", "zerotier"];
            let mut hosts: Vec<String> = wires.iter().filter(|w| own.contains(&w.group.as_str()) && w.kind != "lobby").map(|w| w.host.clone()).collect();
            hosts.sort();
            hosts.dedup();
            for h in hosts {
                if let Err(e) = scan(&h, MP_TCP_PORTS, MP_UDP_PORTS, 200, Duration::from_millis(t)).await { println!("{h}: {e}") }
            }
        }
        Cmd::Reflect { bind } => {
            tokio::task::spawn_blocking(move || reflect(&bind)).await??;
        }
        Cmd::Tunnel { local_port, local_host, to, port, secret } => {
            let c = bore_cli::client::Client::new(&local_host, local_port, &to, port, secret.as_deref()).await?;
            println!("tunnel: {to}:{} -> {local_host}:{local_port} (TCP)", c.remote_port());
            c.listen().await?;
        }
        Cmd::BoreServer { min_port, max_port, secret } => {
            println!("bore server: control 7835, tunnels {min_port}-{max_port}");
            bore_cli::server::Server::new(min_port..=max_port, secret.as_deref()).listen().await?;
        }
    }
    Ok(())
}
