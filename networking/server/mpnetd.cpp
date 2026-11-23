// mpnetd - MP NetWire server (NetWire 1.0)
//
//   mpnetd probe  -w wires.txt [-w more.txt] [-o wires.json] [--samples 5] [--from "GitHub Actions"]
//        probe every wire once, write the JSON (stdout if no -o), exit 0 when at least one wire is up
//   mpnetd watch  -w wires.txt -o DIR [-i 60] [--http 8090]
//        probe forever, write DIR/wires.json + DIR/traffic.json; --http serves them (CORS open)
//   mpnetd relay  -l 7777 -u host:port [-u host2:port2 ...] [--kind mp|samp|udp] [--http 8091]
//        UDP relay for a VPS / Oracle box near the players: every new player goes over the upstream with
//        the best score right now (re-probed every 15 s), a player stays on the same upstream while
//        connected (switching mid-game would drop the RakNet / MP session). Per-player traffic counters
//        (packets, bytes, rate, last seen) are on http://host:8091/traffic.json - a live monitor in the
//        spirit of Sniffnet, without root or pcap.
//        The relay also answers "MPNETPNG" pings on its own port, so it is a "udp" wire itself.
#include "../lib/mpnet.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>

using namespace mpnet;

static std::atomic<bool> g_run{true};
static void OnSig(int) { g_run = false; }

static void Usage()
{
    fprintf(stderr,
        "mpnetd - MP NetWire\n"
        "  mpnetd probe -w wires.txt [-w more.txt] [-o out.json] [--samples N] [--timeout MS] [--from TEXT]\n"
        "  mpnetd watch -w wires.txt -o DIR [-i SEC] [--http PORT]\n"
        "  mpnetd relay -l PORT -u HOST:PORT [-u ...] [--kind mp|samp|udp] [--http PORT] [--idle SEC]\n");
}

static bool WriteFile(const std::string& p, const std::string& s)
{
    std::string tmp = p + ".tmp";
    std::ofstream o(tmp, std::ios::binary); if(!o) return false;
    o << s; o.close();
    return rename(tmp.c_str(), p.c_str()) == 0;
}

// ---- interface counters (/proc/net/dev; Linux, VPS, Termux on older Android) -------------------
static std::string IfaceJson()
{
    std::ifstream in("/proc/net/dev");
    std::ostringstream o; o << "[";
    std::string line; int n = 0; bool first = true;
    while(std::getline(in, line)) {
        if(++n <= 2) continue;
        size_t c = line.find(':'); if(c == std::string::npos) continue;
        std::string name = line.substr(0, c); name.erase(0, name.find_first_not_of(' '));
        std::istringstream is(line.substr(c + 1));
        unsigned long long v[16] = {0}; for(int i = 0; i < 16 && is >> v[i]; ++i) {}
        if(name == "lo") continue;
        o << (first ? "" : ",") << "{\"if\":\"" << JsonEscape(name) << "\",\"rx_bytes\":" << v[0] << ",\"rx_packets\":" << v[1]
          << ",\"rx_drop\":" << v[3] << ",\"tx_bytes\":" << v[8] << ",\"tx_packets\":" << v[9] << ",\"tx_drop\":" << v[11] << "}";
        first = false;
    }
    o << "]";
    return o.str();
}

// ---- tiny HTTP status server -------------------------------------------------------------------
using Pages = std::function<std::string(const std::string& path, std::string& type)>;
static void HttpServe(uint16_t port, Pages pages)
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(port); a.sin_addr.s_addr = INADDR_ANY;
    if(bind(s, (sockaddr*)&a, sizeof(a)) < 0 || listen(s, 16) < 0) { fprintf(stderr, "http :%u: %s\n", port, strerror(errno)); close(s); return; }
    fprintf(stderr, "http status on :%u\n", port);
    while(g_run) {
        pollfd p{s, POLLIN, 0};
        if(poll(&p, 1, 500) != 1) continue;
        int c = accept(s, nullptr, nullptr); if(c < 0) continue;
        timeval tv{2, 0}; setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        char req[2048]; int n = (int)recv(c, req, sizeof(req) - 1, 0);
        if(n > 0) {
            req[n] = 0;
            char m[8] = {0}, path[512] = {0};
            sscanf(req, "%7s %511s", m, path);
            std::string type = "application/json";
            std::string body = pages(path, type);
            std::string code = body.empty() ? "404 Not Found" : "200 OK";
            if(body.empty()) body = "{\"error\":\"not found\"}\n";
            std::string h = "HTTP/1.1 " + code + "\r\nContent-Type: " + type + "; charset=utf-8\r\nAccess-Control-Allow-Origin: *\r\n"
                            "Cache-Control: no-store\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n";
            send(c, h.data(), h.size(), MSG_NOSIGNAL);
            if(strcmp(m, "HEAD")) send(c, body.data(), body.size(), MSG_NOSIGNAL);
        }
        close(c);
    }
    close(s);
}

// ---- probe / watch -----------------------------------------------------------------------------
struct Args {
    std::vector<std::string> wires, ups;
    std::string out, from = "mpnetd", kind = "mp";
    int samples = 5, timeout = 1500, interval = 60, http = 0, listen = 0, idle = 120;
};

static bool ParseArgs(int argc, char** argv, Args& a)
{
    for(int i = 2; i < argc; ++i) {
        std::string k = argv[i];
        auto val = [&]() -> const char* { if(i + 1 >= argc) { fprintf(stderr, "%s needs a value\n", k.c_str()); exit(2); } return argv[++i]; };
        if(k == "-w") a.wires.push_back(val());
        else if(k == "-u") a.ups.push_back(val());
        else if(k == "-o") a.out = val();
        else if(k == "-i") a.interval = std::max(5, atoi(val()));
        else if(k == "-l") a.listen = atoi(val());
        else if(k == "--samples") a.samples = std::max(1, std::min(50, atoi(val())));
        else if(k == "--timeout") a.timeout = std::max(100, atoi(val()));
        else if(k == "--from") a.from = val();
        else if(k == "--http") a.http = atoi(val());
        else if(k == "--kind") a.kind = val();
        else if(k == "--idle") a.idle = std::max(10, atoi(val()));
        else { fprintf(stderr, "unknown option %s\n", k.c_str()); return false; }
    }
    return true;
}

static std::vector<Wire> Load(const Args& a)
{
    std::vector<Wire> w;
    for(auto& f : a.wires) { std::string e; if(!LoadWires(f, w, &e)) fprintf(stderr, "%s\n", e.c_str()); }
    return w;
}

static int CmdProbe(const Args& a)
{
    auto wires = Load(a);
    if(wires.empty()) { fprintf(stderr, "no wires\n"); return 2; }
    ProbeCfg c; c.samples = a.samples; c.timeoutMs = a.timeout;
    auto rs = ProbeAll(wires, c, 24);
    std::string js = ToJson(rs, a.from);
    if(a.out.empty()) fputs(js.c_str(), stdout); else WriteFile(a.out, js);
    int up = 0;
    for(auto& r : rs) {
        if(r.grade != Grade::DOWN && r.grade != Grade::SKIP) ++up;
        fprintf(stderr, "%-7s %-22s %-6s %7s ms  loss %3d%%  %s\n", GradeName(r.grade), r.wire.id.c_str(), KindName(r.wire.kind),
                r.rttMs >= 0 ? std::to_string((int)lround(r.rttMs)).c_str() : "-", (int)lround(r.loss * 100), r.error.c_str());
    }
    return up ? 0 : 1;
}

static int CmdWatch(const Args& a)
{
    if(a.out.empty()) { fprintf(stderr, "watch needs -o DIR\n"); return 2; }
    std::mutex m; std::string last = "{}";
    std::thread http;
    if(a.http) http = std::thread(HttpServe, (uint16_t)a.http, [&](const std::string& p, std::string&) -> std::string {
        if(p == "/" || p == "/wires.json") { std::lock_guard<std::mutex> g(m); return last; }
        if(p == "/traffic.json") return "{\"updated\":" + std::to_string(time(nullptr)) + ",\"ifaces\":" + IfaceJson() + "}\n";
        if(p == "/health") return "{\"ok\":true}\n";
        return ""; });
    ProbeCfg c; c.samples = a.samples; c.timeoutMs = a.timeout;
    while(g_run) {
        auto wires = Load(a);   // re-read: a new line in wires.txt is a new wire, no restart
        auto rs = ProbeAll(wires, c, 24);
        std::string js = ToJson(rs, a.from);
        { std::lock_guard<std::mutex> g(m); last = js; }
        WriteFile(a.out + "/wires.json", js);
        WriteFile(a.out + "/traffic.json", "{\"updated\":" + std::to_string(time(nullptr)) + ",\"ifaces\":" + IfaceJson() + "}\n");
        for(int i = 0; i < a.interval * 2 && g_run; ++i) usleep(500000);
    }
    if(http.joinable()) http.join();
    return 0;
}

// ---- relay -------------------------------------------------------------------------------------
struct Up { Wire w; sockaddr_in addr{}; Result last; bool ok = false; };
struct Session {
    sockaddr_in client{}; int fd = -1; int up = -1;
    uint64_t inPk = 0, inB = 0, outPk = 0, outB = 0;   // in = client -> server, out = server -> client
    double first = 0, lastC = 0, lastS = 0;
};
static std::string Key(const sockaddr_in& a) { char b[40]; snprintf(b, sizeof(b), "%s:%u", inet_ntoa(a.sin_addr), ntohs(a.sin_port)); return b; }

static int CmdRelay(const Args& a)
{
    if(!a.listen || a.ups.empty()) { Usage(); return 2; }
    std::vector<Up> ups;
    for(size_t i = 0; i < a.ups.size(); ++i) {
        Wire w; std::string e;
        if(!ParseWireLine("up" + std::to_string(i) + " | relay | " + a.kind + " | " + a.ups[i] + " | upstream " + a.ups[i], w, &e)) { fprintf(stderr, "-u %s: %s\n", a.ups[i].c_str(), e.c_str()); return 2; }
        Up u; u.w = w; ups.push_back(u);
    }
    int ls = socket(AF_INET, SOCK_DGRAM, 0);
    int big = 1 << 20; setsockopt(ls, SOL_SOCKET, SO_RCVBUF, &big, sizeof(big)); setsockopt(ls, SOL_SOCKET, SO_SNDBUF, &big, sizeof(big));
    sockaddr_in la{}; la.sin_family = AF_INET; la.sin_port = htons(a.listen); la.sin_addr.s_addr = INADDR_ANY;
    if(bind(ls, (sockaddr*)&la, sizeof(la)) < 0) { fprintf(stderr, "bind :%d: %s\n", a.listen, strerror(errno)); return 1; }
    fprintf(stderr, "relay :%d -> %zu upstream(s), kind %s\n", a.listen, ups.size(), a.kind.c_str());

    std::mutex m;
    std::map<std::string, Session> ss;
    std::atomic<int> best{-1};
    ProbeCfg pc; pc.samples = 3; pc.timeoutMs = 1000;
    std::thread prober([&] {
        while(g_run) {
            std::vector<Wire> ws; for(auto& u : ups) ws.push_back(u.w);
            auto rs = ProbeAll(ws, pc, 8);
            int b = -1;
            {
                std::lock_guard<std::mutex> g(m);
                for(size_t i = 0; i < ups.size(); ++i) {
                    ups[i].last = rs[i]; ups[i].ok = rs[i].grade != Grade::DOWN;
                    if(!ups[i].addr.sin_port) Resolve(ups[i].w.host, ups[i].w.port, ups[i].addr);
                    if(ups[i].ok && (b < 0 || rs[i].score < rs[b].score)) b = (int)i;
                }
            }
            if(b < 0) b = 0;   // nobody answers the probe: still try the first one (query may be blocked)
            if(b != best) fprintf(stderr, "best upstream: %s (%s ms)\n", ups[b].w.host.c_str(), ups[b].last.rttMs >= 0 ? std::to_string((int)ups[b].last.rttMs).c_str() : "?");
            best = b;
            for(int i = 0; i < 30 && g_run; ++i) usleep(500000);
        }
    });
    while(best < 0 && g_run) usleep(50000);

    auto traffic = [&]() {
        std::lock_guard<std::mutex> g(m);
        std::ostringstream o; double now = NowMs();
        uint64_t ti = 0, to = 0;
        o << "{\"updated\":" << time(nullptr) << ",\"listen\":" << a.listen << ",\"best\":" << best.load() << ",\"upstreams\":[";
        for(size_t i = 0; i < ups.size(); ++i)
            o << (i ? "," : "") << "{\"host\":\"" << JsonEscape(ups[i].w.host) << "\",\"port\":" << ups[i].w.port << ",\"grade\":\"" << GradeName(ups[i].last.grade)
              << "\",\"rtt\":" << (ups[i].last.rttMs >= 0 ? std::to_string((int)lround(ups[i].last.rttMs)) : "null") << "}";
        o << "],\"sessions\":[";
        bool first = true;
        for(auto& kv : ss) {
            const Session& s = kv.second; double age = std::max(1.0, (now - s.first) / 1000.0);
            ti += s.inB; to += s.outB;
            o << (first ? "" : ",") << "{\"client\":\"" << kv.first << "\",\"up\":" << s.up << ",\"in_pk\":" << s.inPk << ",\"in_bytes\":" << s.inB
              << ",\"out_pk\":" << s.outPk << ",\"out_bytes\":" << s.outB << ",\"kbps\":" << (int)((s.inB + s.outB) * 8 / age / 1000)
              << ",\"idle_s\":" << (int)((now - std::max(s.lastC, s.lastS)) / 1000) << ",\"age_s\":" << (int)age << "}";
            first = false;
        }
        o << "],\"total_in_bytes\":" << ti << ",\"total_out_bytes\":" << to << ",\"ifaces\":" << IfaceJson() << "}\n";
        return o.str();
    };
    std::thread http;
    if(a.http) http = std::thread(HttpServe, (uint16_t)a.http, [&](const std::string& p, std::string&) -> std::string {
        if(p == "/" || p == "/traffic.json") return traffic();
        if(p == "/health") return "{\"ok\":true}\n";
        return ""; });

    uint8_t buf[65536];
    double lastGc = NowMs();
    while(g_run) {
        std::vector<pollfd> pf; std::vector<std::string> keys;
        pf.push_back({ls, POLLIN, 0}); keys.push_back("");
        { std::lock_guard<std::mutex> g(m); for(auto& kv : ss) { pf.push_back({kv.second.fd, POLLIN, 0}); keys.push_back(kv.first); } }
        if(poll(pf.data(), pf.size(), 500) <= 0) goto gc;
        if(pf[0].revents & POLLIN) {
            for(int burst = 0; burst < 256; ++burst) {
                sockaddr_in from{}; socklen_t fl = sizeof(from);
                int n = (int)recvfrom(ls, buf, sizeof(buf), MSG_DONTWAIT, (sockaddr*)&from, &fl);
                if(n <= 0) break;
                if(n == 16 && !memcmp(buf, "MPNETPNG", 8)) { sendto(ls, buf, n, 0, (sockaddr*)&from, fl); continue; }   // relay ping
                std::string k = Key(from);
                std::lock_guard<std::mutex> g(m);
                auto it = ss.find(k);
                if(it == ss.end()) {
                    Session s; s.client = from; s.up = best; s.first = NowMs();
                    s.fd = socket(AF_INET, SOCK_DGRAM, 0);
                    int b2 = 256 * 1024; setsockopt(s.fd, SOL_SOCKET, SO_RCVBUF, &b2, sizeof(b2));
                    if(!ups[s.up].addr.sin_port) Resolve(ups[s.up].w.host, ups[s.up].w.port, ups[s.up].addr);
                    connect(s.fd, (sockaddr*)&ups[s.up].addr, sizeof(sockaddr_in));
                    fcntl(s.fd, F_SETFL, fcntl(s.fd, F_GETFL) | O_NONBLOCK);
                    fprintf(stderr, "+ %s -> %s:%u\n", k.c_str(), ups[s.up].w.host.c_str(), ups[s.up].w.port);
                    it = ss.emplace(k, s).first;
                }
                Session& s = it->second;
                if(send(s.fd, buf, n, 0) == n) { s.inPk++; s.inB += n; }
                s.lastC = NowMs();
            }
        }
        for(size_t i = 1; i < pf.size(); ++i) {
            if(!(pf[i].revents & POLLIN)) continue;
            std::lock_guard<std::mutex> g(m);
            auto it = ss.find(keys[i]); if(it == ss.end()) continue;
            Session& s = it->second;
            for(int burst = 0; burst < 256; ++burst) {
                int n = (int)recv(s.fd, buf, sizeof(buf), 0);
                if(n <= 0) break;
                if(sendto(ls, buf, n, 0, (sockaddr*)&s.client, sizeof(s.client)) == n) { s.outPk++; s.outB += n; }
                s.lastS = NowMs();
            }
        }
    gc:
        if(NowMs() - lastGc > 5000) {
            lastGc = NowMs();
            std::lock_guard<std::mutex> g(m);
            for(auto it = ss.begin(); it != ss.end();) {
                if(NowMs() - it->second.lastC > a.idle * 1000.0) { fprintf(stderr, "- %s (idle)\n", it->first.c_str()); close(it->second.fd); it = ss.erase(it); }
                else ++it;
            }
        }
    }
    prober.join();
    if(http.joinable()) http.join();
    for(auto& kv : ss) close(kv.second.fd);
    close(ls);
    return 0;
}

int main(int argc, char** argv)
{
    signal(SIGINT, OnSig); signal(SIGTERM, OnSig); signal(SIGPIPE, SIG_IGN);
    if(argc < 2) { Usage(); return 2; }
    std::string cmd = argv[1];
    Args a;
    if(!ParseArgs(argc, argv, a)) { Usage(); return 2; }
    if(cmd == "probe") return CmdProbe(a);
    if(cmd == "watch") return CmdWatch(a);
    if(cmd == "relay") return CmdRelay(a);
    Usage();
    return 2;
}
