// MP NetWire library - see mpnet.h
#include "mpnet.h"
#include "../../src/net/protocol.h"   // PKT_QUERY / PKT_INFO: the same wire format the game uses

#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <fstream>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>

namespace mpnet {

static const char* kKinds[] = {"mqtt", "mp", "samp", "tcp", "http", "udp", "lobby", "tls", "unknown"};
const char* KindName(Kind k) { return kKinds[(int)k]; }
Kind KindFromName(const std::string& s)
{
    for(int i = 0; i < 8; ++i) if(s == kKinds[i]) return (Kind)i;
    if(s == "wss" || s == "https" || s == "mqtts") return Kind::TLS;
    if(s == "omp" || s == "sa-mp") return Kind::SAMP;
    if(s == "emqx" || s == "mosquitto" || s == "hivemq") return Kind::MQTT;
    return Kind::UNKNOWN;
}
const char* GradeName(Grade g)
{
    switch(g) { case Grade::GREEN: return "green"; case Grade::YELLOW: return "yellow"; case Grade::RED: return "red";
                case Grade::SKIP: return "skip"; default: return "down"; }
}

double NowMs()
{
    timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static std::string Trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

bool ParseWireLine(const std::string& line0, Wire& w, std::string* err)
{
    std::string line = Trim(line0);
    if(line.empty() || line[0] == '#') return false;
    std::vector<std::string> f; std::string cur;
    for(char c : line) { if(c == '|') { f.push_back(Trim(cur)); cur.clear(); } else cur += c; }
    f.push_back(Trim(cur));
    if(f.size() < 4) { if(err) *err = "need: id | group | kind | host:port | name | opts"; return false; }
    w = Wire();
    w.id = f[0]; w.group = f[1]; w.kind = KindFromName(f[2]);
    if(w.kind == Kind::UNKNOWN) { if(err) *err = "unknown kind '" + f[2] + "'"; return false; }
    size_t c = f[3].rfind(':');
    if(c == std::string::npos) { if(err) *err = "host:port missing ':'"; return false; }
    w.host = f[3].substr(0, c);
    long p = strtol(f[3].c_str() + c + 1, nullptr, 10);
    if(p < 0 || p > 65535 || (p == 0 && w.kind != Kind::LOBBY)) { if(err) *err = "bad port"; return false; }
    w.port = (uint16_t)p;
    w.name = f.size() > 4 && !f[4].empty() ? f[4] : w.id;
    if(f.size() > 5) {
        std::istringstream is(f[5]); std::string kv;
        while(is >> kv) { size_t e = kv.find('='); if(e != std::string::npos) w.opt[kv.substr(0, e)] = kv.substr(e + 1); }
    }
    for(char ch : w.id) if(!(isalnum((unsigned char)ch) || ch == '_' || ch == '-' || ch == '.')) { if(err) *err = "bad id"; return false; }
    return true;
}

bool LoadWires(const std::string& path, std::vector<Wire>& out, std::string* err)
{
    std::ifstream in(path);
    if(!in) { if(err) *err = "cannot open " + path; return false; }
    std::string line; int n = 0;
    while(std::getline(in, line)) {
        ++n; Wire w; std::string e;
        if(ParseWireLine(line, w, &e)) {
            bool dup = false;
            for(auto& o : out) if(o.id == w.id) { dup = true; break; }
            if(!dup) out.push_back(w);
        } else if(!e.empty()) fprintf(stderr, "%s:%d: %s\n", path.c_str(), n, e.c_str());
    }
    return true;
}

bool Resolve(const std::string& host, uint16_t port, sockaddr_in& out, std::string* ip)
{
    memset(&out, 0, sizeof(out));
    out.sin_family = AF_INET; out.sin_port = htons(port);
    if(inet_pton(AF_INET, host.c_str(), &out.sin_addr) != 1) {
        addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        if(getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) return false;
        out.sin_addr = ((sockaddr_in*)res->ai_addr)->sin_addr;
        freeaddrinfo(res);
    }
    if(ip) { char b[32]; inet_ntop(AF_INET, &out.sin_addr, b, sizeof(b)); *ip = b; }
    return true;
}

// ---- sockets ----------------------------------------------------------------------------------
static bool WaitFd(int fd, short ev, int ms)
{
    pollfd p{fd, ev, 0};
    return poll(&p, 1, ms) == 1 && (p.revents & ev);
}

// non-blocking connect with timeout; returns fd or -1, *ms = connect time
static int TcpConnect(const sockaddr_in& a, int timeoutMs, double* ms, std::string* err)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0) { if(err) *err = strerror(errno); return -1; }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    int one = 1; setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    double t0 = NowMs();
    int r = connect(fd, (const sockaddr*)&a, sizeof(a));
    if(r < 0 && errno != EINPROGRESS) { if(err) *err = strerror(errno); close(fd); return -1; }
    if(r < 0) {
        if(!WaitFd(fd, POLLOUT, timeoutMs)) { if(err) *err = "timeout"; close(fd); return -1; }
        int e = 0; socklen_t l = sizeof(e); getsockopt(fd, SOL_SOCKET, SO_ERROR, &e, &l);
        if(e) { if(err) *err = strerror(e); close(fd); return -1; }
    }
    if(ms) *ms = NowMs() - t0;
    return fd;
}

static bool SendAll(int fd, const void* d, size_t n, int timeoutMs)
{
    const uint8_t* p = (const uint8_t*)d;
    while(n) {
        if(!WaitFd(fd, POLLOUT, timeoutMs)) return false;
        ssize_t s = send(fd, p, n, MSG_NOSIGNAL);
        if(s <= 0) { if(s < 0 && (errno == EAGAIN || errno == EINTR)) continue; return false; }
        p += s; n -= (size_t)s;
    }
    return true;
}

static int RecvSome(int fd, uint8_t* b, int n, int timeoutMs)
{
    if(!WaitFd(fd, POLLIN, timeoutMs)) return -1;
    ssize_t r = recv(fd, b, n, 0);
    return r <= 0 ? -1 : (int)r;
}

static uint32_t Rand32()
{
    static std::mt19937 rng((uint32_t)time(nullptr) ^ (uint32_t)getpid());
    static std::mutex m; std::lock_guard<std::mutex> g(m);
    return rng();
}

// ---- probes -----------------------------------------------------------------------------------
// MQTT 3.1.1: CONNECT -> CONNACK (rc must be 0), then PINGREQ / PINGRESP samples
static void ProbeMqtt(const sockaddr_in& a, const Wire& w, const ProbeCfg& c, Result& r, std::vector<double>& rtt)
{
    double cms = 0;
    int fd = TcpConnect(a, c.timeoutMs, &cms, &r.error);
    r.sent = c.samples;
    if(fd < 0) return;
    std::string cid = "mpnet-" + std::to_string(Rand32() % 1000000);
    auto it_u = w.opt.find("user"), it_p = w.opt.find("pass");
    bool hasU = it_u != w.opt.end(), hasP = it_p != w.opt.end();
    std::vector<uint8_t> v;   // variable header + payload
    const uint8_t vh[] = {0, 4, 'M', 'Q', 'T', 'T', 4, 0, 0, 10};
    v.insert(v.end(), vh, vh + sizeof(vh));
    v[7] = 0x02 | (hasU ? 0x80 : 0) | (hasP ? 0x40 : 0);   // clean session
    auto str = [&](const std::string& s) { v.push_back((uint8_t)(s.size() >> 8)); v.push_back((uint8_t)s.size()); v.insert(v.end(), s.begin(), s.end()); };
    str(cid); if(hasU) str(it_u->second); if(hasP) str(it_p->second);
    std::vector<uint8_t> pkt{0x10};
    size_t len = v.size();
    do { uint8_t b = len % 128; len /= 128; if(len) b |= 0x80; pkt.push_back(b); } while(len);
    pkt.insert(pkt.end(), v.begin(), v.end());
    uint8_t buf[64];
    double t0 = NowMs();
    if(!SendAll(fd, pkt.data(), pkt.size(), c.timeoutMs)) { r.error = "send failed"; close(fd); return; }
    int n = RecvSome(fd, buf, 4, c.timeoutMs);
    if(n < 4 || buf[0] != 0x20) { r.error = n < 0 ? "no CONNACK" : "not MQTT"; close(fd); return; }
    if(buf[3] != 0) { r.error = "CONNACK rc=" + std::to_string(buf[3]) + (buf[3] == 5 ? " (not authorized)" : buf[3] == 4 ? " (bad login)" : ""); close(fd); return; }
    double connack = NowMs() - t0;   // TCP + MQTT login: not a ping sample (it is 2+ round trips)
    (void)cms;
    for(int i = 0; i < c.samples; ++i) {
        if(i) usleep(c.gapMs * 1000);
        const uint8_t ping[2] = {0xC0, 0x00};
        double s = NowMs();
        if(!SendAll(fd, ping, 2, c.timeoutMs)) break;
        int k = RecvSome(fd, buf, 2, c.timeoutMs);
        if(k >= 2 && buf[0] == 0xD0) rtt.push_back(NowMs() - s);
    }
    if(rtt.empty()) rtt.push_back(connack);   // broker without PINGRESP: the login time is all we have
    const uint8_t disc[2] = {0xE0, 0x00};
    SendAll(fd, disc, 2, 200);
    close(fd);
}

static int UdpSock(int timeoutMs)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if(fd < 0) return -1;
    timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    return fd;
}

// sends `req`, waits for a reply from the same address that `match` accepts
template<class F>
static bool UdpAsk(int fd, const sockaddr_in& a, const void* req, int n, int timeoutMs, uint8_t* buf, int cap, int* got, F match)
{
    if(sendto(fd, req, n, 0, (const sockaddr*)&a, sizeof(a)) != n) return false;
    double end = NowMs() + timeoutMs;
    while(NowMs() < end) {
        if(!WaitFd(fd, POLLIN, (int)std::max(1.0, end - NowMs()))) return false;
        sockaddr_in f{}; socklen_t fl = sizeof(f);
        int r = (int)recvfrom(fd, buf, cap, 0, (sockaddr*)&f, &fl);
        if(r <= 0) continue;
        if(f.sin_addr.s_addr != a.sin_addr.s_addr || f.sin_port != a.sin_port) continue;
        if(match(buf, r)) { *got = r; return true; }
    }
    return false;
}

// MP server: PKT_QUERY -> PKT_INFO (players, max, name) - what the game's server list sends
static void ProbeMp(const sockaddr_in& a, const ProbeCfg& c, Result& r, std::vector<double>& rtt)
{
    int fd = UdpSock(c.timeoutMs);
    if(fd < 0) { r.error = strerror(errno); return; }
    uint8_t buf[1500];
    for(int i = 0; i < c.samples; ++i) {
        if(i) usleep(c.gapMs * 1000);
        PktQuery q; q.type = PKT_QUERY; q.time = Rand32();
        int got = 0; double s = NowMs();
        r.sent++;
        bool ok = UdpAsk(fd, a, &q, sizeof(q), c.timeoutMs, buf, sizeof(buf), &got, [&](const uint8_t* b, int n) {
            if(n < (int)sizeof(PktInfo) || b[0] != PKT_INFO) return false;
            uint32_t t; memcpy(&t, b + 1, 4); return t == q.time; });
        if(!ok) continue;
        rtt.push_back(NowMs() - s);
        PktInfo in; memcpy(&in, buf, sizeof(in));
        r.players = in.players; r.maxPlayers = in.maxPlayers;
        char nm[33]; memcpy(nm, in.serverName, 32); nm[32] = 0;
        r.serverName = MP_DisplayServerName(nm);
        r.version = "MP proto " + std::to_string(in.proto);
    }
    close(fd);
}

// open.mp / SA-MP query (https://open.mp/docs/tutorials/QueryMechanism): 'p' = ping, 'i' = info
static void ProbeSamp(const sockaddr_in& a, const ProbeCfg& c, Result& r, std::vector<double>& rtt)
{
    int fd = UdpSock(c.timeoutMs);
    if(fd < 0) { r.error = strerror(errno); return; }
    uint8_t req[15], buf[2048];
    memcpy(req, "SAMP", 4); memcpy(req + 4, &a.sin_addr.s_addr, 4);
    uint16_t pl = ntohs(a.sin_port); req[8] = pl & 0xFF; req[9] = pl >> 8;
    for(int i = 0; i < c.samples; ++i) {
        if(i) usleep(c.gapMs * 1000);
        req[10] = 'p'; uint32_t tok = Rand32(); memcpy(req + 11, &tok, 4);
        int got = 0; double s = NowMs(); r.sent++;
        if(UdpAsk(fd, a, req, 15, c.timeoutMs, buf, sizeof(buf), &got, [&](const uint8_t* b, int n) {
                return n >= 15 && !memcmp(b, "SAMP", 4) && b[10] == 'p' && !memcmp(b + 11, &tok, 4); }))
            rtt.push_back(NowMs() - s);
    }
    req[10] = 'i';
    int got = 0;
    if(UdpAsk(fd, a, req, 11, c.timeoutMs, buf, sizeof(buf), &got, [](const uint8_t* b, int n) {
            return n >= 11 && !memcmp(b, "SAMP", 4) && b[10] == 'i'; })) {
        // 11 hdr, passworded u8, players u16, max u16, hostname (u32 len + str), gamemode, language
        if(got >= 16) {
            r.players = buf[12] | (buf[13] << 8); r.maxPlayers = buf[14] | (buf[15] << 8);
            if(got >= 20) {
                uint32_t hl; memcpy(&hl, buf + 16, 4);
                if(hl < 512 && 20 + (int)hl <= got) r.serverName.assign((const char*)buf + 20, hl);
                int o = 20 + (int)hl;
                if(o + 4 <= got) { uint32_t gl; memcpy(&gl, buf + o, 4); if(gl < 256 && o + 4 + (int)gl <= got) r.version = std::string((const char*)buf + o + 4, gl); }
            }
        }
    }
    close(fd);
}

// plain TCP connect time (TLS ports, WebSocket ports, SSH, playit / frp tunnels)
static void ProbeTcp(const sockaddr_in& a, const ProbeCfg& c, Result& r, std::vector<double>& rtt, bool http, const Wire& w)
{
    for(int i = 0; i < c.samples; ++i) {
        if(i) usleep(c.gapMs * 1000);
        r.sent++;
        double ms = 0; std::string e;
        int fd = TcpConnect(a, c.timeoutMs, &ms, &e);
        if(fd < 0) { r.error = e; continue; }
        if(http) {
            auto p = w.opt.find("path");
            std::string req = "HEAD " + (p != w.opt.end() ? p->second : std::string("/")) + " HTTP/1.1\r\nHost: " + w.host +
                              "\r\nUser-Agent: mpnet/1\r\nConnection: close\r\n\r\n";
            uint8_t b[256]; double s = NowMs();
            if(SendAll(fd, req.data(), req.size(), c.timeoutMs)) {
                int n = RecvSome(fd, b, sizeof(b) - 1, c.timeoutMs);
                if(n > 12 && !memcmp(b, "HTTP/", 5)) { b[n] = 0; r.version = std::string((char*)b, strcspn((char*)b, "\r\n")); ms = NowMs() - s; }
                else { r.error = "no HTTP reply"; close(fd); continue; }
            }
        }
        rtt.push_back(ms);
        close(fd);
    }
}


// TLS: a ClientHello (TLS 1.2 record, SNI) -> the first byte back must be a TLS record (handshake 0x16 or
// alert 0x15). Proves a real TLS server answered (a proxy or a dead NAT that only accepts the TCP
// connect does not), rtt = ClientHello -> first reply byte.
static std::vector<uint8_t> ClientHello(const std::string& host)
{
    std::vector<uint8_t> b, ext;
    auto u16 = [](std::vector<uint8_t>& v, size_t x) { v.push_back((uint8_t)(x >> 8)); v.push_back((uint8_t)x); };
    // server_name
    if(!host.empty() && inet_addr(host.c_str()) == INADDR_NONE) {
        u16(ext, 0); u16(ext, host.size() + 5); u16(ext, host.size() + 3); ext.push_back(0); u16(ext, host.size());
        ext.insert(ext.end(), host.begin(), host.end());
    }
    const uint8_t groups[] = {0x00, 0x0a, 0x00, 0x08, 0x00, 0x06, 0x00, 0x1d, 0x00, 0x17, 0x00, 0x18};
    const uint8_t points[] = {0x00, 0x0b, 0x00, 0x02, 0x01, 0x00};
    const uint8_t sigs[]   = {0x00, 0x0d, 0x00, 0x0e, 0x00, 0x0c, 0x04, 0x03, 0x05, 0x03, 0x08, 0x04, 0x08, 0x05, 0x04, 0x01, 0x05, 0x01};
    const uint8_t alpn[]   = {0x00, 0x10, 0x00, 0x0b, 0x00, 0x09, 0x08, 'h', 't', 't', 'p', '/', '1', '.', '1'};
    ext.insert(ext.end(), groups, groups + sizeof(groups)); ext.insert(ext.end(), points, points + sizeof(points));
    ext.insert(ext.end(), sigs, sigs + sizeof(sigs)); ext.insert(ext.end(), alpn, alpn + sizeof(alpn));
    const uint8_t suites[] = {0xc0, 0x2b, 0xc0, 0x2f, 0xc0, 0x2c, 0xc0, 0x30, 0xcc, 0xa9, 0xcc, 0xa8, 0xc0, 0x13, 0xc0, 0x14, 0x00, 0x9c, 0x00, 0x9d, 0x00, 0x2f, 0x00, 0x35};
    std::vector<uint8_t> hs;
    hs.push_back(0x03); hs.push_back(0x03);
    for(int i = 0; i < 32; ++i) hs.push_back((uint8_t)Rand32());
    hs.push_back(0);                                   // session id
    u16(hs, sizeof(suites)); hs.insert(hs.end(), suites, suites + sizeof(suites));
    hs.push_back(1); hs.push_back(0);                  // compression: null
    u16(hs, ext.size()); hs.insert(hs.end(), ext.begin(), ext.end());
    std::vector<uint8_t> msg{0x01, 0, (uint8_t)(hs.size() >> 8), (uint8_t)hs.size()};
    msg.insert(msg.end(), hs.begin(), hs.end());
    b = {0x16, 0x03, 0x01}; u16(b, msg.size()); b.insert(b.end(), msg.begin(), msg.end());
    return b;
}

static void ProbeTls(const sockaddr_in& a, const ProbeCfg& c, Result& r, std::vector<double>& rtt, const Wire& w)
{
    auto sni = w.opt.find("sni");
    std::vector<uint8_t> hello = ClientHello(sni != w.opt.end() ? sni->second : w.host);
    for(int i = 0; i < c.samples; ++i) {
        if(i) usleep(c.gapMs * 1000);
        r.sent++;
        std::string e; double ms = 0;
        int fd = TcpConnect(a, c.timeoutMs, &ms, &e);
        if(fd < 0) { r.error = e; continue; }
        uint8_t b[8]; double s = NowMs();
        if(SendAll(fd, hello.data(), hello.size(), c.timeoutMs)) {
            int n = RecvSome(fd, b, 5, c.timeoutMs);
            if(n >= 1 && (b[0] == 0x16 || b[0] == 0x15)) { rtt.push_back(NowMs() - s); if(b[0] == 0x15) r.version = "TLS alert (server is TLS, refused this hello)"; else r.version = "TLS"; }
            else r.error = n < 0 ? "no TLS reply" : "not TLS";
        }
        close(fd);
    }
}

// generic UDP echo (sockperf / netperf / mpnetd relay "ping" port): any reply = alive
static void ProbeUdp(const sockaddr_in& a, const ProbeCfg& c, Result& r, std::vector<double>& rtt)
{
    int fd = UdpSock(c.timeoutMs);
    if(fd < 0) { r.error = strerror(errno); return; }
    uint8_t buf[512];
    for(int i = 0; i < c.samples; ++i) {
        if(i) usleep(c.gapMs * 1000);
        char req[16]; uint32_t t = Rand32(); memcpy(req, "MPNETPNG", 8); memcpy(req + 8, &t, 4); memset(req + 12, 0, 4);
        int got = 0; double s = NowMs(); r.sent++;
        if(UdpAsk(fd, a, req, 16, c.timeoutMs, buf, sizeof(buf), &got, [&](const uint8_t* b, int n) { return n >= 12 && !memcmp(b + 8, &t, 4); }))
            rtt.push_back(NowMs() - s);
    }
    close(fd);
}

void GradeResult(Result& r, const std::vector<double>& rtts, const ProbeCfg& c)
{
    r.got = (int)rtts.size();
    if(r.sent <= 0) r.sent = std::max(1, c.samples);
    r.loss = 1.0 - (double)r.got / r.sent;
    if(rtts.empty()) { r.grade = Grade::DOWN; r.rttMs = -1; r.score = 1e9; if(r.error.empty()) r.error = "no reply"; return; }
    std::vector<double> s = rtts; std::sort(s.begin(), s.end());
    r.rttMs = s.size() % 2 ? s[s.size() / 2] : (s[s.size() / 2 - 1] + s[s.size() / 2]) / 2;
    r.minMs = s.front(); r.maxMs = s.back();
    double j = 0; for(size_t i = 1; i < rtts.size(); ++i) j += fabs(rtts[i] - rtts[i - 1]);
    r.jitterMs = rtts.size() > 1 ? j / (rtts.size() - 1) : 0;
    r.score = r.rttMs + 2 * r.jitterMs + 500 * r.loss;
    if(r.rttMs <= c.greenRtt && r.jitterMs <= c.greenJitter && r.loss <= c.greenLoss) r.grade = Grade::GREEN;
    else if(r.rttMs <= c.yellowRtt && r.loss <= c.yellowLoss) r.grade = Grade::YELLOW;
    else r.grade = Grade::RED;
    r.error.clear();
}

Result Probe(const Wire& w, const ProbeCfg& c)
{
    Result r; r.wire = w; r.at = time(nullptr);
    std::vector<double> rtt;
    if(w.kind == Kind::LOBBY) {   // listed through the MQTT lobby only (no public address): the caller fills it in
        r.grade = Grade::SKIP; r.error = "lobby-listed";
        auto p = w.opt.find("players"); if(p != w.opt.end()) r.players = atoi(p->second.c_str());
        auto m = w.opt.find("max"); if(m != w.opt.end()) r.maxPlayers = atoi(m->second.c_str());
        auto on = w.opt.find("online");
        if(on != w.opt.end()) { r.grade = on->second == "1" ? Grade::GREEN : Grade::DOWN; r.error = on->second == "1" ? "" : "not in the lobby"; r.loss = on->second == "1" ? 0 : 1; }
        return r;
    }
    sockaddr_in a;
    if(!Resolve(w.host, w.port, a, &r.ip)) { r.error = "DNS failed"; r.sent = c.samples; GradeResult(r, rtt, c); r.error = "DNS failed"; return r; }
    switch(w.kind) {
        case Kind::MQTT: ProbeMqtt(a, w, c, r, rtt); break;
        case Kind::MP:   ProbeMp(a, c, r, rtt); break;
        case Kind::SAMP: ProbeSamp(a, c, r, rtt); break;
        case Kind::TCP:  ProbeTcp(a, c, r, rtt, false, w); break;
        case Kind::HTTP: ProbeTcp(a, c, r, rtt, true, w); break;
        case Kind::UDP:  ProbeUdp(a, c, r, rtt); break;
        case Kind::TLS:  ProbeTls(a, c, r, rtt, w); break;
        default: r.error = "unknown kind"; break;
    }
    std::string keep = r.error;
    GradeResult(r, rtt, c);
    if(r.grade == Grade::DOWN && !keep.empty()) r.error = keep;
    return r;
}

std::vector<Result> ProbeAll(const std::vector<Wire>& wires, const ProbeCfg& c, int threads)
{
    std::vector<Result> out(wires.size());
    std::atomic<size_t> next{0};
    std::vector<std::thread> th;
    int n = std::max(1, std::min(threads, (int)wires.size()));
    for(int i = 0; i < n; ++i) th.emplace_back([&] { for(size_t k; (k = next++) < wires.size();) out[k] = Probe(wires[k], c); });
    for(auto& t : th) t.join();
    return out;
}

int BestOf(const std::vector<Result>& rs, const std::string& group)
{
    int best = -1;
    for(size_t i = 0; i < rs.size(); ++i) {
        const Result& r = rs[i];
        if(r.wire.group != group || r.grade == Grade::DOWN || r.grade == Grade::SKIP || r.rttMs < 0) continue;
        if(best < 0 || r.score < rs[best].score) best = (int)i;
    }
    return best;
}

// SA-MP host names are often cp1252 / latin-1: bytes that are not valid UTF-8 are taken as latin-1
static std::string FixUtf8(const std::string& s)
{
    std::string o;
    for(size_t i = 0; i < s.size();) {
        unsigned char c = s[i];
        int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        bool ok = n > 0 && i + n <= s.size();
        for(int k = 1; ok && k < n; ++k) ok = ((unsigned char)s[i + k] >> 6) == 2;
        if(ok) { o.append(s, i, n); i += n; }
        else { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 0x3F)); ++i; }
    }
    return o;
}

std::string JsonEscape(const std::string& s0)
{
    std::string o, s = FixUtf8(s0);
    for(unsigned char c : s) {
        switch(c) {
            case '"': o += "\\\""; break; case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break; case '\r': o += "\\r"; break; case '\t': o += "\\t"; break;
            default: if(c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); o += b; } else o += (char)c;
        }
    }
    return o;
}

static std::string Num(double v) { char b[32]; if(v < 0) return "null"; snprintf(b, sizeof(b), "%.1f", v); return b; }

std::string ToJson(const std::vector<Result>& rs, const std::string& from)
{
    std::ostringstream o;
    int up = 0, green = 0, probed = 0;
    for(auto& r : rs) { if(r.grade == Grade::SKIP) continue; ++probed; if(r.grade != Grade::DOWN) ++up; if(r.grade == Grade::GREEN) ++green; }
    o << "{\n \"updated\": " << (int64_t)time(nullptr) << ",\n \"from\": \"" << JsonEscape(from) << "\",\n";
    o << " \"total\": " << rs.size() << ", \"probed\": " << probed << ", \"up\": " << up << ", \"green\": " << green << ",\n";
    // best wire per group
    std::vector<std::string> groups;
    for(auto& r : rs) if(std::find(groups.begin(), groups.end(), r.wire.group) == groups.end()) groups.push_back(r.wire.group);
    o << " \"best\": {";
    bool first = true;
    for(auto& g : groups) { int b = BestOf(rs, g); if(b < 0) continue; o << (first ? "" : ",") << "\"" << JsonEscape(g) << "\": \"" << JsonEscape(rs[b].wire.id) << "\""; first = false; }
    o << "},\n \"wires\": [\n";
    for(size_t i = 0; i < rs.size(); ++i) {
        const Result& r = rs[i]; const Wire& w = r.wire;
        o << "  {\"id\":\"" << JsonEscape(w.id) << "\",\"group\":\"" << JsonEscape(w.group) << "\",\"kind\":\"" << KindName(w.kind)
          << "\",\"host\":\"" << JsonEscape(w.host) << "\",\"port\":" << w.port << ",\"name\":\"" << JsonEscape(w.name)
          << "\",\"grade\":\"" << GradeName(r.grade) << "\",\"rtt\":" << Num(r.rttMs) << ",\"min\":" << Num(r.minMs) << ",\"max\":" << Num(r.maxMs)
          << ",\"jitter\":" << Num(r.got > 1 ? r.jitterMs : -1) << ",\"loss\":" << (int)lround(r.loss * 100) << ",\"sent\":" << r.sent << ",\"got\":" << r.got;
        if(r.players >= 0) o << ",\"players\":" << r.players;
        if(r.maxPlayers >= 0) o << ",\"max_players\":" << r.maxPlayers;
        if(!r.serverName.empty()) o << ",\"server\":\"" << JsonEscape(r.serverName) << "\"";
        if(!r.version.empty()) o << ",\"info\":\"" << JsonEscape(r.version) << "\"";
        if(!r.error.empty()) o << ",\"error\":\"" << JsonEscape(r.error) << "\"";
        auto rg = w.opt.find("region"); if(rg != w.opt.end()) o << ",\"region\":\"" << JsonEscape(rg->second) << "\"";
        o << "}" << (i + 1 < rs.size() ? "," : "") << "\n";
    }
    o << " ]\n}\n";
    return o.str();
}

} // namespace mpnet
