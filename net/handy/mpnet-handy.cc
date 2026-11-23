// mpnet-handy: one epoll loop (handy, fork github.com/JebetrexxMC/handy) that
//   - answers NetWire UDP pings ("MPNETPNG" + token, 16 bytes) and SA-MP 'p' pings on --udp (default 7790)
//   - serves GET /stats on --http (default 8093): pings answered, dropped, uptime, as JSON
// Small enough for a Termux phone or a Raspberry Pi hotspot (see net/raspap) next to the players.
#include <handy/handy.h>
#include <atomic>
#include <cstring>
#include <ctime>
using namespace handy;

int main(int argc, const char* argv[]) {
    unsigned short udpPort = 7790, httpPort = 8093;
    for (int i = 1; i + 1 < argc; i++) {
        if (!strcmp(argv[i], "--udp")) udpPort = (unsigned short)atoi(argv[i + 1]);
        if (!strcmp(argv[i], "--http")) httpPort = (unsigned short)atoi(argv[i + 1]);
    }
    setloglevel("WARN");
    EventBase base;
    Signal::signal(SIGINT, [&] { base.exit(); });
    Signal::signal(SIGTERM, [&] { base.exit(); });
    static std::atomic<unsigned long long> answered{0}, dropped{0};
    time_t started = time(nullptr);

    UdpServerPtr udp = UdpServer::startServer(&base, "", udpPort);
    exitif(!udp, "udp %d: bind failed", udpPort);
    udp->onMsg([](const UdpServerPtr& p, Buffer buf, Ip4Addr peer) {
        const char* b = buf.data();
        size_t n = buf.size();
        bool ping = (n == 16 && !memcmp(b, "MPNETPNG", 8)) || (n == 15 && !memcmp(b, "SAMP", 4) && b[10] == 'p');
        if (ping) { p->sendTo(b, n, peer); answered++; }
        else dropped++;
    });

    HttpServer http(&base);
    exitif(http.bind("", httpPort), "http %d: bind failed", httpPort);
    http.onGet("/stats", [&](const HttpConnPtr& con) {
        char body[256];
        snprintf(body, sizeof body, "{\"answered\":%llu,\"dropped\":%llu,\"uptime\":%ld,\"udp\":%d}\n",
                 (unsigned long long)answered, (unsigned long long)dropped, (long)(time(nullptr) - started), udpPort);
        HttpResponse& r = con.getResponse();
        r.headers["Content-Type"] = "application/json";
        r.headers["Access-Control-Allow-Origin"] = "*";
        r.body = Slice(body);
        con.sendResponse();
    });
    printf("mpnet-handy: udp %d (MPNETPNG + SA-MP p), http %d (/stats)\n", udpPort, httpPort);
    fflush(stdout);
    base.loop();
}
