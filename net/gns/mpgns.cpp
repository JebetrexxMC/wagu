// mpgns: MP NetWire lane test on GameNetworkingSockets.
// Starts a listen socket and a client in one process (or "server PORT" / "client IP:PORT"),
// sends a burst of reliable messages (chat / inventory style) and unreliable ones (position
// style), and prints round-trip time, loss and what GNS itself measured (ping, quality).
// It shows what MP would get from a reliable-UDP lane next to its RakNet traffic.
#include <steam/steamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

static ISteamNetworkingSockets* g_api;
static HSteamNetConnection g_srvConn = k_HSteamNetConnection_Invalid, g_cliConn = k_HSteamNetConnection_Invalid;
static HSteamListenSocket g_listen = k_HSteamListenSocket_Invalid;
static bool g_connected = false;

static double NowMs() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

static void OnStatus(SteamNetConnectionStatusChangedCallback_t* info) {
    auto st = info->m_info.m_eState;
    if (info->m_info.m_hListenSocket != k_HSteamListenSocket_Invalid && st == k_ESteamNetworkingConnectionState_Connecting) {
        g_api->AcceptConnection(info->m_hConn);
        g_srvConn = info->m_hConn;
    }
    if (st == k_ESteamNetworkingConnectionState_Connected && info->m_hConn == g_cliConn) g_connected = true;
    if (st == k_ESteamNetworkingConnectionState_ClosedByPeer || st == k_ESteamNetworkingConnectionState_ProblemDetectedLocally) {
        std::printf("connection closed: %s\n", info->m_info.m_szEndDebug);
        g_api->CloseConnection(info->m_hConn, 0, nullptr, false);
    }
}

static void Pump() {
    g_api->RunCallbacks();
    // server side: echo everything back with the same send flags
    if (g_srvConn != k_HSteamNetConnection_Invalid) {
        ISteamNetworkingMessage* msgs[64];
        int n = g_api->ReceiveMessagesOnConnection(g_srvConn, msgs, 64);
        for (int i = 0; i < n; i++) {
            int flags = (msgs[i]->m_nFlags & k_nSteamNetworkingSend_Reliable) ? k_nSteamNetworkingSend_Reliable : k_nSteamNetworkingSend_UnreliableNoDelay;
            g_api->SendMessageToConnection(g_srvConn, msgs[i]->m_pData, msgs[i]->m_cbSize, flags, nullptr);
            msgs[i]->Release();
        }
    }
}

int main(int argc, char** argv) {
    SteamDatagramErrMsg err;
    if (!GameNetworkingSockets_Init(nullptr, err)) { std::fprintf(stderr, "GNS init: %s\n", err); return 1; }
    g_api = SteamNetworkingSockets();
    SteamNetworkingUtils()->SetGlobalCallback_SteamNetConnectionStatusChanged(OnStatus);

    std::string mode = argc > 1 ? argv[1] : "local";
    int port = 27777;
    std::string target = "127.0.0.1:27777";
    if (mode == "server" && argc > 2) port = std::atoi(argv[2]);
    if (mode == "client" && argc > 2) target = argv[2];

    if (mode != "client") {
        SteamNetworkingIPAddr a; a.Clear(); a.m_port = (uint16)port;
        g_listen = g_api->CreateListenSocketIP(a, 0, nullptr);
        if (g_listen == k_HSteamListenSocket_Invalid) { std::fprintf(stderr, "listen on %d failed\n", port); return 1; }
        std::printf("listening on udp %d\n", port);
        if (mode == "server") for (;;) { Pump(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    }
    SteamNetworkingIPAddr to; to.Clear();
    if (!to.ParseString(target.c_str())) { std::fprintf(stderr, "bad address %s\n", target.c_str()); return 1; }
    g_cliConn = g_api->ConnectByIPAddress(to, 0, nullptr);
    double t0 = NowMs();
    while (!g_connected && NowMs() - t0 < 5000) { Pump(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    if (!g_connected) { std::fprintf(stderr, "could not connect to %s\n", target.c_str()); return 1; }
    std::printf("connected to %s in %.1f ms (handshake + crypto)\n", target.c_str(), NowMs() - t0);

    const int N = 200;
    std::vector<double> sentAt(2 * N, 0), rtt;
    for (int i = 0; i < 2 * N; i++) {
        char pkt[64]; std::memset(pkt, 0, sizeof pkt);
        std::memcpy(pkt, &i, 4);
        int flags = i < N ? k_nSteamNetworkingSend_Reliable : k_nSteamNetworkingSend_UnreliableNoDelay;
        sentAt[i] = NowMs();
        g_api->SendMessageToConnection(g_cliConn, pkt, sizeof pkt, flags, nullptr);
        if (i % 10 == 9) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        Pump();
    }
    int gotRel = 0, gotUnrel = 0;
    t0 = NowMs();
    while ((gotRel < N || gotUnrel < N) && NowMs() - t0 < 3000) {
        Pump();
        ISteamNetworkingMessage* msgs[64];
        int n = g_api->ReceiveMessagesOnConnection(g_cliConn, msgs, 64);
        for (int k = 0; k < n; k++) {
            int id = -1;
            if (msgs[k]->m_cbSize >= 4) std::memcpy(&id, msgs[k]->m_pData, 4);
            if (id >= 0 && id < 2 * N) { rtt.push_back(NowMs() - sentAt[id]); (id < N ? gotRel : gotUnrel)++; }
            msgs[k]->Release();
        }
        std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
    double sum = 0, mx = 0;
    for (double r : rtt) { sum += r; if (r > mx) mx = r; }
    SteamNetConnectionRealTimeStatus_t rs;
    g_api->GetConnectionRealTimeStatus(g_cliConn, &rs, 0, nullptr);
    std::printf("reliable   %d/%d back\nunreliable %d/%d back\nrtt avg %.2f ms, max %.2f ms\n", gotRel, N, gotUnrel, N,
                rtt.empty() ? 0 : sum / rtt.size(), mx);
    std::printf("GNS: ping %d ms, quality local %.2f remote %.2f, out %.0f B/s\n", rs.m_nPing, rs.m_flConnectionQualityLocal,
                rs.m_flConnectionQualityRemote, rs.m_flOutBytesPerSec);
    g_api->CloseConnection(g_cliConn, 0, "done", true);
    for (int i = 0; i < 50; i++) { Pump(); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
    GameNetworkingSockets_Kill();
    return gotRel == N ? 0 : 2;
}
