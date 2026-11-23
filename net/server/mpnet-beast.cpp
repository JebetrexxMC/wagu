// mpnet-beast: HTTP query bridge on Boost.Beast (fork github.com/JebetrexxMC/beast).
// Browsers and the website can't send UDP, so this answers
//   GET /query?host=wap-fresh.tun.ply.gg&port=56616   -> SA-MP / open.mp 'i' info as JSON
//   GET /health                                        -> ok
// for the server list on mpsamp-open.github.io and the launcher on networks that block UDP.
// Answers are cached 5 s per server; one client may ask 10 times per 10 s.
//   make -C net beast [BEAST=/path/to/beast/include]   ->  build/mpnet-beast -p 8092
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio.hpp>
#include <chrono>
#include <cstring>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <thread>

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;
using tcp = asio::ip::tcp;
using udp = asio::ip::udp;
using clk = std::chrono::steady_clock;

static std::mutex g_mu;
static std::map<std::string, std::pair<clk::time_point, std::string>> g_cache;
static std::map<std::string, std::pair<clk::time_point, int>> g_rate;

static std::string jesc(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
        else if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += (char)c;
    }
    return o;
}

// SA-MP query 'i': "SAMP" ip4 port(LE) 'i' -> password(1) players(2) max(2) host(4+n) mode(4+n) lang(4+n)
static std::string query(const std::string& host, unsigned short port) {
    asio::io_context io;
    udp::resolver r(io);
    boost::system::error_code ec;
    auto res = r.resolve(udp::v4(), host, std::to_string(port), ec);
    if (ec || res.empty()) return "{\"ok\":false,\"error\":\"resolve\"}";
    udp::endpoint ep = *res.begin();
    udp::socket s(io, udp::v4());
    unsigned char q[11] = {'S', 'A', 'M', 'P'};
    auto ip = ep.address().to_v4().to_bytes();
    memcpy(q + 4, ip.data(), 4);
    q[8] = port & 0xff; q[9] = port >> 8; q[10] = 'i';
    struct timeval tv = {1, 0};
    setsockopt(s.native_handle(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    for (int attempt = 0; attempt < 2; attempt++) {
        auto t0 = clk::now();
        s.send_to(asio::buffer(q), ep, 0, ec);
        unsigned char b[2048];
        udp::endpoint from;
        size_t n = s.receive_from(asio::buffer(b), from, 0, ec);
        if (ec || n < 11 + 5 || memcmp(b, "SAMP", 4) || b[10] != 'i') continue;
        double ms = std::chrono::duration<double, std::milli>(clk::now() - t0).count();
        size_t p = 11;
        auto u16 = [&](size_t o) { return b[o] | (b[o + 1] << 8); };
        auto str = [&](std::string& out) {
            if (p + 4 > n) return false;
            uint32_t l = b[p] | (b[p + 1] << 8) | (b[p + 2] << 16) | ((uint32_t)b[p + 3] << 24);
            p += 4;
            if (p + l > n) return false;
            out.assign((const char*)b + p, l); p += l; return true;
        };
        int pw = b[p]; int pl = u16(p + 1); int mx = u16(p + 3); p += 5;
        std::string hn, mode, lang;
        str(hn); str(mode); str(lang);
        char head[160];
        snprintf(head, sizeof head, "{\"ok\":true,\"ms\":%.1f,\"password\":%s,\"players\":%d,\"max\":%d,", ms, pw ? "true" : "false", pl, mx);
        return head + std::string("\"hostname\":\"") + jesc(hn) + "\",\"mode\":\"" + jesc(mode) + "\",\"language\":\"" + jesc(lang) + "\"}";
    }
    return "{\"ok\":false,\"error\":\"no answer\"}";
}

static std::string param(const std::string& target, const std::string& key) {
    auto q = target.find('?');
    if (q == std::string::npos) return "";
    std::string s = "&" + target.substr(q + 1);
    auto k = s.find("&" + key + "=");
    if (k == std::string::npos) return "";
    auto v = k + key.size() + 2;
    return s.substr(v, s.find('&', v) - v);
}

static void serve(tcp::socket sock) {
    beast::error_code ec;
    beast::flat_buffer buf;
    std::string who = sock.remote_endpoint(ec).address().to_string();
    for (;;) {
        http::request<http::string_body> req;
        http::read(sock, buf, req, ec);
        if (ec) return;
        http::response<http::string_body> res{http::status::ok, req.version()};
        res.set(http::field::server, "mpnet-beast");
        res.set(http::field::access_control_allow_origin, "*");
        res.set(http::field::content_type, "application/json");
        std::string t(req.target());
        if (t.rfind("/health", 0) == 0) {
            res.body() = "ok\n";
            res.set(http::field::content_type, "text/plain");
        } else if (t.rfind("/query", 0) == 0) {
            std::string host = param(t, "host");
            int port = atoi(param(t, "port").c_str());
            bool okHost = !host.empty() && host.size() < 254 && host.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-") == std::string::npos;
            bool limited = false;
            {
                std::lock_guard<std::mutex> l(g_mu);
                auto& r = g_rate[who];
                if (clk::now() - r.first > std::chrono::seconds(10)) r = {clk::now(), 0};
                limited = ++r.second > 10;
            }
            if (!okHost || port <= 0 || port > 65535) { res.result(http::status::bad_request); res.body() = "{\"ok\":false,\"error\":\"host and port\"}"; }
            else if (limited) { res.result(http::status::too_many_requests); res.body() = "{\"ok\":false,\"error\":\"slow down\"}"; }
            else {
                std::string key = host + ":" + std::to_string(port), body;
                {
                    std::lock_guard<std::mutex> l(g_mu);
                    auto it = g_cache.find(key);
                    if (it != g_cache.end() && clk::now() - it->second.first < std::chrono::seconds(5)) body = it->second.second;
                }
                if (body.empty()) {
                    body = query(host, (unsigned short)port);
                    std::lock_guard<std::mutex> l(g_mu);
                    if (g_cache.size() > 4096) g_cache.clear();
                    g_cache[key] = {clk::now(), body};
                }
                res.body() = body;
            }
        } else {
            res.result(http::status::not_found);
            res.body() = "{\"ok\":false,\"error\":\"GET /query?host=&port= or /health\"}";
        }
        res.keep_alive(req.keep_alive());
        res.prepare_payload();
        http::write(sock, res, ec);
        if (ec || !res.keep_alive()) return;
    }
}

int main(int argc, char** argv) {
    unsigned short port = 8092;
    for (int i = 1; i + 1 < argc; i++) if (!strcmp(argv[i], "-p")) port = (unsigned short)atoi(argv[i + 1]);
    asio::io_context io;
    tcp::acceptor acc(io, {tcp::v4(), port});
    std::cout << "mpnet-beast on :" << port << " (/query?host=&port=, /health)" << std::endl;
    for (;;) {
        tcp::socket s(io);
        acc.accept(s);
        std::thread(serve, std::move(s)).detach();
    }
}
