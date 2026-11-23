// MP NetWire - networking library for wagu (NetWire 1.0)
//
// A "wire" is one network path the game, the website or a server can use: an MQTT broker (region
// HiveMQ / EMQX / Mosquitto / public lobby), an MP server (UDP PKT_QUERY), an open.mp / SA-MP server
// (UDP SA-MP query), a VPS / Oracle / relay port (TCP or UDP), or an HTTP endpoint.
// The library probes every wire a few times, gives it a grade (green / yellow / red / down) from
// ping, jitter and packet loss, and picks the best wire of a group. mpnetd (../server) uses it for
// the 24/7 status page and as a UDP relay that always sends new players over the best upstream.
//
// Plain POSIX sockets, C++17, no Boost / OpenSSL: builds on Linux, Termux and the Android NDK.
#pragma once
#include <stdint.h>
#include <string>
#include <vector>
#include <map>
#include <netinet/in.h>

namespace mpnet {

enum class Kind { MQTT, MP, SAMP, TCP, HTTP, UDP, LOBBY, TLS, UNKNOWN };
enum class Grade { GREEN, YELLOW, RED, DOWN, SKIP };

const char* KindName(Kind k);
Kind KindFromName(const std::string& s);
const char* GradeName(Grade g);

struct Wire {
    std::string id;       // unique, [a-z0-9_-]
    std::string group;    // default, featured, crossplay, region, lobby, emqx, vps, oracle, xdp, relay, custom, omp
    Kind kind = Kind::UNKNOWN;
    std::string host;
    uint16_t port = 0;
    std::string name;     // human name for the page
    std::map<std::string, std::string> opt;   // user=, pass=, path=, region=, players= (lobby), max=
};

struct Result {
    Wire wire;
    Grade grade = Grade::DOWN;
    int sent = 0, got = 0;
    double rttMs = -1;     // median
    double minMs = -1, maxMs = -1;
    double jitterMs = 0;   // mean |rtt[i] - rtt[i-1]|
    double loss = 1.0;     // 0..1
    double score = 1e9;    // lower = better (rtt + 2*jitter + 500*loss)
    std::string ip;        // resolved address
    std::string error;
    // info some probes return
    int players = -1, maxPlayers = -1;
    std::string serverName, version;
    int64_t at = 0;        // unix seconds
};

struct ProbeCfg {
    int samples = 5;
    int timeoutMs = 1500;
    int gapMs = 150;
    // grade limits (ms / loss)
    double greenRtt = 120, greenJitter = 30, greenLoss = 0.2;
    double yellowRtt = 250, yellowLoss = 0.5;
};

// wires.txt: one wire per line, '|' separated, '#' comments:
//   id | group | kind | host:port | name | key=value key=value
bool LoadWires(const std::string& path, std::vector<Wire>& out, std::string* err = nullptr);
bool ParseWireLine(const std::string& line, Wire& w, std::string* err = nullptr);

Result Probe(const Wire& w, const ProbeCfg& cfg = ProbeCfg());
// probe many wires in parallel (threads), keeps the input order
std::vector<Result> ProbeAll(const std::vector<Wire>& wires, const ProbeCfg& cfg = ProbeCfg(), int threads = 16);
void GradeResult(Result& r, const std::vector<double>& rtts, const ProbeCfg& cfg);

// best (lowest score, not down) result of a group, -1 if none
int BestOf(const std::vector<Result>& rs, const std::string& group);

std::string ToJson(const std::vector<Result>& rs, const std::string& probeFrom);
std::string JsonEscape(const std::string& s);

// helpers
bool Resolve(const std::string& host, uint16_t port, sockaddr_in& out, std::string* ip = nullptr);
double NowMs();

} // namespace mpnet
