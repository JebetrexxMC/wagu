// 5.01.01 test: one phone never gets two slots. A rejoin (new network path / restarted game / late direct join
// after the relay fallback) and a rebind of a player whose name the server changed ([..] tags stripped) take over
// the old slot: same id, and the other players read no "left the server" + "joined the server".
//   g++ -O2 -std=c++17 -pthread -I../src -I../third_party/mbedtls/include rejoin_test.cpp ../src/net/client.cpp ../src/net/server.cpp ../server/obj/libmbedtls.a -o rejoin_test && ./rejoin_test
#include <stdio.h>
#include <string.h>
#include <thread>
#include <chrono>
#include <vector>
#define private public
#include "net/client.h"
#undef private
#include "net/server.h"

struct Ev : INetEvents {
    bool ok = false; int id = -1; int joins = 0, leaves = 0; char lastMsg[128] = "";
    void OnConnected(const PktWelcome& w) override { ok = true; id = w.yourId; }
    void OnRejected(uint8_t) override {}
    void OnConnectFailed() override {} void OnLostConnection() override {}
    void OnPlayerJoin(const PktPlayerJoin&) override { ++joins; } void OnPlayerLeave(const PktPlayerLeave&) override { ++leaves; }
    void OnSync(const PktSync&, const SyncExt*) override {} void OnChat(const PktChat&) override {}
    void OnDeath(const PktDeath&) override {} void OnDamage(const PktDamage&) override {}
    void OnServerMessage(const PktServerMsg& m) override { snprintf(lastMsg, sizeof(lastMsg), "%s", m.text); }
    void OnSetColor(const PktSetColor&) override {} void OnPause(const PktPause&) override {}
    void OnWorld(const PktWorld&) override {} void OnStatus(const PktStatus&) override {}
};
static int fails = 0;
#define CHECK(c, m) do { if(c) printf("PASS  %s\n", m); else { printf("FAIL  %s\n", m); ++fails; } } while(0)
static void Pump(std::vector<MPClient*> c, std::vector<Ev*> e, int ms)
{ for(int t = 0; t < ms; t += 10) { for(size_t i = 0; i < c.size(); ++i) c[i]->Poll(e[i]); std::this_thread::sleep_for(std::chrono::milliseconds(10)); } }

int main()
{
    MPServer srv;
    if(!srv.Start(19501, "Rejoin Test", [](const char* l){ printf("   [srv] %s\n", l); }, 50)) { printf("server failed\n"); return 1; }
    MPClient a, b; Ev ea, eb;
    a.SetUid("alicephone0000001"); b.SetUid("juanphone00000001");
    a.Connect("127.0.0.1", 19501, "Alice", 0); Pump({&a}, {&ea}, 600);
    b.Connect("127.0.0.1", 19501, "[CG]Juan", 0); Pump({&a, &b}, {&ea, &eb}, 800);
    CHECK(ea.ok && eb.ok, "Alice + [CG]Juan joined");
    const int bid = eb.id; const int aj = ea.joins, al = ea.leaves;
    CHECK(srv.GetPlayerCount() == 2, "2 players");

    // 1) the same phone joins again from a new socket (restarted game / late direct join / auto-reconnect)
    MPClient b2; Ev eb2; b2.SetUid("juanphone00000001");
    b2.Connect("127.0.0.1", 19501, "[CG]Juan", 0); Pump({&a, &b2}, {&ea, &eb2}, 800);
    CHECK(eb2.ok && eb2.id == bid, "rejoin takes over the same id");
    CHECK(srv.GetPlayerCount() == 2, "still 2 players (no second slot)");
    CHECK(ea.leaves == al && ea.joins == aj, "Alice reads no left + joined");

    // 2) a rebind (network switch) from a third socket - the server name is "CGJuan" (brackets stripped)
    MPClient b3; Ev eb3; b3.SetUid("juanphone00000001");
    b3.Connect("127.0.0.1", 19501, "[CG]Juan", 0);
    b3.m_eState = NET_CONNECTED; b3.m_nServerBuild = MP_BUILD; b3.SendJoin(MP_JOIN_REBIND);
    Pump({&a, &b3}, {&ea, &eb3}, 800);
    CHECK(srv.GetPlayerCount() == 2, "rebind with a changed name keeps one slot");
    CHECK(ea.leaves == al && ea.joins == aj, "Alice still reads no left + joined");

    // 3) a different phone with the same name is still a new player
    MPClient c; Ev ec; c.SetUid("otherphone0000001");
    c.Connect("127.0.0.1", 19501, "[CG]Juan", 0); Pump({&a, &c}, {&ea, &ec}, 800);
    CHECK(ec.ok && ec.id != bid && srv.GetPlayerCount() == 3, "another phone = another player");

    srv.Stop();
    printf(fails ? "\n%d FAILED\n" : "\nALL PASSED\n", fails);
    return fails ? 1 : 0;
}
