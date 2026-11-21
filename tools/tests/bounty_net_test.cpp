// 5.02.31: Bounty Hunter over a real local server. Start one first:
//   (cd server && ./mp_server 1922 --no-online)        <- the default server (Broker MQQ)
// then: 3 phones (the real MPClient) join it and play a duel, a multi kill run and the money rule.
//   g++ -O1 -std=c++17 -pthread -I src -I third_party/mbedtls/include tools/tests/bounty_net_test.cpp src/net/client.cpp -o /tmp/bounty_net_test && /tmp/bounty_net_test 1922
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <string>
#include <vector>
#include "net/client.h"
#include "net/homekit.h"
#include "net/online.h"
#include <chrono>
// the phone's clock + the online helpers MPClient asks (a LAN test: no broker, no network changes)
uint64_t MP_NowMs() { return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
namespace Online { uint32_t NetGeneration() { return 0; } int CongestionMs() { return 0; } }

static int g_Fail = 0, g_Pass = 0;
#define CHECK(c, ...) do { if(c) { ++g_Pass; printf("  ok   "); } else { ++g_Fail; printf("  FAIL "); } printf(__VA_ARGS__); printf("\n"); } while(0)

struct Phone : INetEvents
{
    const char* nm; MPClient c; int id = -1; uint32_t flags = 0; uint8_t pad = 0; bool perms = false, connected = false;
    std::vector<PktEvent> ev; std::vector<PktDeath> deaths; std::vector<std::string> msgs; std::vector<PktDamage> dmg;
    explicit Phone(const char* n) : nm(n) {}
    void OnConnected(const PktWelcome& w) override { id = w.yourId; connected = true; }
    void OnRejected(uint8_t r) override { printf("  %s rejected (%d)\n", nm, r); }
    void OnConnectFailed() override { printf("  %s connect failed\n", nm); }
    void OnLostConnection() override { connected = false; }
    void OnPlayerJoin(const PktPlayerJoin&) override {}
    void OnPlayerLeave(const PktPlayerLeave&) override {}
    void OnSync(const PktSync&, const SyncExt*) override {}
    void OnChat(const PktChat&) override {}
    void OnDeath(const PktDeath& d) override { deaths.push_back(d); }
    void OnDamage(const PktDamage& d) override { dmg.push_back(d); }
    void OnServerMessage(const PktServerMsg& m) override { msgs.push_back(m.text); }
    void OnSetColor(const PktSetColor&) override {}
    void OnPause(const PktPause&) override {}
    void OnWorld(const PktWorld&) override {}
    void OnStatus(const PktStatus&) override {}
    void OnPerms(const PktPerms& p) override { perms = true; pad = p.pad; }
    void OnEvent(const PktEvent& e) override { ev.push_back(e); }
    void Poll() { c.Poll(this); flags = c.GetServerFlags(); }
};
static void PumpAll(std::vector<Phone*>& ps, int ms) { for(int t = 0; t < ms; t += 20) { for(auto* p : ps) p->Poll(); usleep(20000); } }
static void SendEv(Phone& p, int op, int to)
{
    PktEvent e; memset(&e, 0, sizeof(e)); e.type = PKT_EVENT; e.id = (uint8_t)p.id; e.kind = EVK_DM; e.idx = MP_EV_QUEUE; e.state = EVS_LOBBY;
    e.cp = (uint8_t)op; e.slot = (uint8_t)to; e.pad = 0xB7; p.c.Send(&e, sizeof(e));
}
static bool GotEv(Phone& p, int from, int op, int to) { for(auto& e : p.ev) if(e.id == from && e.cp == op && e.slot == to && e.pad == 0xB7 && e.kind == EVK_DM && e.idx == MP_EV_QUEUE) return true; return false; }
static void Hit(Phone& from, Phone& to, int weapon)
{
    PktDamage d; memset(&d, 0, sizeof(d)); d.type = PKT_DAMAGE; d.target = (uint8_t)to.id; d.weapon = (uint8_t)weapon; d.amount = 46.2f; from.c.Send(&d, sizeof(d));
}
static void Die(Phone& victim, Phone& killer, int weapon)
{
    PktDeath d; d.type = PKT_DEATH; d.victim = (uint8_t)victim.id; d.killer = (uint8_t)killer.id; d.weapon = (uint8_t)weapon; victim.c.Send(&d, sizeof(d));
}
static int DeathsBy(Phone& p, int killer) { int n = 0; for(auto& d : p.deaths) if(d.killer == killer) ++n; return n; }

int main(int argc, char** argv)
{
    const int port = argc > 1 ? atoi(argv[1]) : MP_DEFAULT_PORT;
    Phone a("Tester"), b("hovanaki"), c("Babochka");
    std::vector<Phone*> ps = { &a, &b, &c };
    for(auto* p : ps) { p->c.Connect("127.0.0.1", (uint16_t)port, p->nm, 0); PumpAll(ps, 600); }
    PumpAll(ps, 2500);
    printf("\n== join Broker MQQ (port %d)\n", port);
    for(auto* p : ps) CHECK(p->connected && p->id >= 0, "%s joined as id %d", p->nm, p->id);
    if(!a.connected || !b.connected || !c.connected) { printf("\nnot connected - is the server running?\n"); return 2; }
    const bool def = port == MP_DEFAULT_PORT;   // Broker MQQ: the phone always has Bounty Hunter on (ModeOn2)
    if(def) printf("  info: default server - Bounty Hunter is always on (PktPerms.pad = 0x%02X)\n", a.pad);
    else CHECK(a.perms && (a.pad & GM2_BOUNTY), "custom server: Bounty Hunter from --gamemodes (PktPerms.pad = 0x%02X)", a.pad);
    if(def) CHECK(a.flags & MP_SRVF_BROWSERFMT, "BrowserFormat join / leave on (flags 0x%X)", a.flags);

    printf("\n== duel: Tester challenges hovanaki, hovanaki accepts\n");
    for(auto* p : ps) p->ev.clear();
    SendEv(a, 1, b.id); PumpAll(ps, 500);
    CHECK(GotEv(b, a.id, 1, b.id), "hovanaki got the invite from Tester");
    CHECK(GotEv(c, a.id, 1, b.id), "Babochka got it too (her phone ignores it: not for her)");
    CHECK(!GotEv(a, a.id, 1, b.id), "not echoed back to Tester");
    SendEv(b, 2, a.id); PumpAll(ps, 500);
    CHECK(GotEv(a, b.id, 2, a.id), "Tester got the accept -> \"The duel between Tester and hovanaki has begun!\"");
    CHECK(GotEv(c, b.id, 2, a.id), "Babochka sees the duel start");
    SendEv(c, 4, 255); PumpAll(ps, 500);
    CHECK(GotEv(a, c.id, 4, 255) && GotEv(b, c.id, 4, 255), "first-time greeting from Babochka reaches everyone");

    printf("\n== duel ends: Tester kills hovanaki (Deagle)\n");
    for(auto* p : ps) p->deaths.clear();
    Hit(a, b, 24); PumpAll(ps, 300); Die(b, a, 24); PumpAll(ps, 600);
    for(auto* p : ps) CHECK(DeathsBy(*p, a.id) == 1, "%s saw hovanaki killed by Tester (kill credit kept)", p->nm);

    printf("\n== multi kill: Tester kills Babochka, hovanaki, Babochka within 45 s\n");
    for(auto* p : ps) p->deaths.clear();
    PumpAll(ps, 5500);   // spawn protection after the last death
    Hit(a, c, 31); PumpAll(ps, 300); Die(c, a, 31); PumpAll(ps, 5500);
    Hit(a, b, 31); PumpAll(ps, 300); Die(b, a, 31); PumpAll(ps, 5500);
    Hit(a, c, 31); PumpAll(ps, 300); Die(c, a, 31); PumpAll(ps, 800);
    for(auto* p : ps) CHECK(DeathsBy(*p, a.id) == 3, "%s saw all 3 kills by Tester (%d) -> Killing Spree, Double Kill, Triple Kill", p->nm, DeathsBy(*p, a.id));

    printf("\n== a kill without any hit first (fake kill)\n");
    for(auto* p : ps) p->deaths.clear();
    PumpAll(ps, 5500); Die(b, c, 24); PumpAll(ps, 600);
    bool stripped = !b.deaths.empty() && b.deaths[0].killer == MP_INVALID_ID;
    printf("  info: kill credit %s by the anticheat (no PKT_DAMAGE before the death)\n", stripped ? "removed" : "kept");

    if(def) {
    printf("\n== Broker MQQ money rule (server side)\n");
    a.msgs.clear();
    PktProgress pr; memset(&pr, 0, sizeof(pr)); pr.type = PKT_PROGRESS; pr.op = 1; pr.ver = 1; pr.health = 100; pr.skin = 0;
    pr.money = 5000; a.c.Send(&pr, sizeof(pr)); PumpAll(ps, 400);
    bool warned = false; for(auto& m : a.msgs) if(m.find("unlimited money") != std::string::npos) warned = true;
    CHECK(!warned, "a normal save ($5,000) is accepted");
    pr.money = 999999999; a.c.Send(&pr, sizeof(pr)); PumpAll(ps, 400);
    warned = false; for(auto& m : a.msgs) if(m.find("unlimited money") != std::string::npos) warned = true;
    CHECK(warned, "a $999,999,999 save is refused and Tester is told");
    a.msgs.clear(); pr.money = 5000 + 2000000; a.c.Send(&pr, sizeof(pr)); PumpAll(ps, 400);
    warned = false; for(auto& m : a.msgs) if(m.find("unlimited money") != std::string::npos) warned = true;
    CHECK(warned, "+$2,000,000 within 2 minutes is refused");
    }

    for(auto* p : ps) p->c.Disconnect(true);
    PumpAll(ps, 300);
    printf("\n%d passed, %d failed\n", g_Pass, g_Fail);
    return g_Fail ? 1 : 0;
}
