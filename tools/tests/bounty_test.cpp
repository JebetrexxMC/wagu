// 5.02.31: Bounty Hunter / Broker MQQ money rule test on a PC (no game, no ImGui).
// build: g++ -std=c++17 -I src -o /tmp/bounty_test tools/tests/bounty_test.cpp && /tmp/bounty_test
// Builds the real src/mp_bounty.inc against small stand-ins for the game, the chat and the network, then plays
// duel + multi kill + money-rule scenarios with a fake clock.
//   g++ -std=c++17 -I src -o /tmp/bounty_test tools/tests/bounty_test.cpp && /tmp/bounty_test
#define MP_BOUNTY_TEST 1
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <ctype.h>
#include <string>
#include <vector>
#include "net/protocol.h"

// ---- stand-ins -------------------------------------------------------------------------------------------------
static uint64_t g_Now = 1000000;
uint64_t MP_NowMs() { return g_Now; }
enum { NET_DISCONNECTED = 0, NET_CONNECTING, NET_CONNECTED };
struct FakeClient { int state = NET_CONNECTED; std::vector<PktEvent> sent; int GetState() const { return state; } void Send(const void* d, int n) { if(n == (int)sizeof(PktEvent)) { PktEvent e; memcpy(&e, d, sizeof(e)); sent.push_back(e); } } } client;
struct { char name[MP_NAME_LEN] = "Tester"; bool bountySound = false, bountyWelcomeOn = true; char bountyWelcome[MP_CHAT_LEN] = "{8B0000}Welcome {FFFFFF}{name}"; float nametagSize = 1; } settings;
struct RP { bool active = false; char name[MP_NAME_LEN] = ""; } players[MP_MAX_PLAYERS];
static uint8_t myId = 0;
static char s_szCurServer[32] = "MP LAN Server";
static char s_szDataDir[400] = "/tmp/bounty_test_mp";
static bool g_Default = true, g_Bounty = true;
static bool ModeOn2(uint8_t gm) { return client.GetState() == NET_CONNECTED && (g_Default || (g_Bounty && (gm & GM2_BOUNTY))); }
namespace MP { bool IsOnDefault() { return g_Default; } bool IsLocalAdmin() { return false; } }
struct { bool wallhack = false, nofall = false, invincible = false, freeze = false, invisible = false, fly = false, flash = false; float gravity = 1.0f; } s_Cheat;
static long long g_Granted = 0; static int g_Money = 0, g_Hp = 100, g_Arm = 0;
namespace Game {
    void AddMoney(int a) { g_Granted += a; g_Money += a; } int GetMoney() { return g_Money; }
    long long TakeMoneyGranted() { long long v = g_Granted; g_Granted = 0; return v; }
    int GetCharHealth(int) { return g_Hp; } void SetCharHealth(int, int v) { g_Hp = v; }
    int GetCharArmour(int) { return g_Arm; } void SetCharArmour(int, int v) { g_Arm = v; } bool IsReady() { return true; }
}
static void CheatMoney(int a) { g_Money += a; }   // a trainer: not through Game::AddMoney
namespace Sound { bool PlayHit(const char*, float) { return true; } bool PlayFile(const char*, float) { return true; } }
static std::vector<std::string> g_Chat;
#define COL_RED 0xFF4444FF
#define COL_GREEN 0x33CC55FF
static void SysMsg(uint32_t, const char* fmt, ...) { char b[768]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof(b), fmt, ap); va_end(ap); g_Chat.push_back(b); }
struct { void Info(const char*, ...) {} void Error(const char*, ...) {} } g_Log, *logger = &g_Log;
static bool JoinFlowActive() { return false; } static bool AuthScreenUp() { return false; } static void SoloTick() {}
static std::string WelcomeFill(const char* t) { std::string o = t; size_t p = o.find("{name}"); if(p != std::string::npos) o.replace(p, 6, settings.name); return o; }
static void SaveConfig() {}
struct WeapName { int id; const char* name; };
static const WeapName kWeaps[] = { { 24, "deagle" }, { 30, "ak47" }, { 31, "m4" } };
#define MP_WEAP_COUNT 3
#define IM_COL32(R,G,B,A) (((uint32_t)(A)<<24)|((uint32_t)(B)<<16)|((uint32_t)(G)<<8)|((uint32_t)(R)))

#include "mp_bounty.inc"

// ---- helpers ---------------------------------------------------------------------------------------------------
static int g_Fail = 0, g_Pass = 0;
#define CHECK(c, ...) do { if(c) ++g_Pass; else { ++g_Fail; printf("  FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while(0)
static bool ChatHas(const char* s) { for(auto& l : g_Chat) if(l.find(s) != std::string::npos) return true; return false; }
static void Kill(int killer, int victim, int weapon = 24) { PktDeath d; d.type = PKT_DEATH; d.victim = (uint8_t)victim; d.killer = (uint8_t)killer; d.weapon = (uint8_t)weapon; BountyOnDeath(d); }
static PktEvent Ev(int from, int op, int to) { PktEvent e; memset(&e, 0, sizeof(e)); e.type = PKT_EVENT; e.id = (uint8_t)from; e.kind = EVK_DM; e.idx = MP_EV_QUEUE; e.state = EVS_LOBBY; e.cp = (uint8_t)op; e.slot = (uint8_t)to; e.pad = MP_BOUNTY_MAGIC; return e; }
static void Join(int id, const char* n) { players[id].active = true; MP_CopyStr(players[id].name, n, sizeof(players[id].name)); }
static void Section(const char* t) { printf("\n== %s\n", t); g_Chat.clear(); client.sent.clear(); }

int main()
{
    system("rm -rf /tmp/bounty_test_mp && mkdir -p /tmp/bounty_test_mp");
    Join(0, "Tester"); Join(1, "hovanaki"); Join(2, "Babochka"); Join(3, "Snitya");
    BountyReset();

    Section("multi kills: chain inside 45 s + 20 ticks");
    const char* expect[] = { "Killing Spree!", "Double Kill!", "Triple Kill!", "Quadruple Kill!", "Mania!", "Savage!", "Rampage!", "Unstoppable!", "Godlike!", "Legendary!", "Legendary!" };
    int victims[] = { 1, 2, 3 };
    for(int i = 0; i < 11; ++i)
    {
        g_Now += (i == 0 ? 0 : 44000);   // 44 s apart: still the same chain
        Kill(0, victims[i % 3]);
        CHECK(s_Bh.banner == expect[i], "kill %d banner '%s' expected '%s'", i + 1, s_Bh.banner.c_str(), expect[i]);
    }
    printf("  11 kills 44 s apart -> %s ... %s\n", expect[0], s_Bh.banner.c_str());
    CHECK(ChatHas("Double Kill!"), "others are told about a Double Kill");

    Section("multi kills: exactly at the window edge (46 000 ms) and just past it");
    BountyReset(); Kill(0, 1); g_Now += MP_BOUNTY_CHAIN_MS; Kill(0, 2);
    CHECK(s_Bh.banner == "Double Kill!", "46.0 s later is still a Double Kill (got %s)", s_Bh.banner.c_str());
    g_Now += MP_BOUNTY_CHAIN_MS + 1; Kill(0, 3);
    CHECK(s_Bh.banner == "Killing Spree!", "46.001 s later starts a new chain (got %s)", s_Bh.banner.c_str());

    Section("multi kills: 3 minutes later is NOT a double kill");
    BountyReset(); Kill(0, 1); g_Now += 180000; Kill(0, 2);
    CHECK(s_Bh.banner == "Killing Spree!", "3 min gap -> %s", s_Bh.banner.c_str());
    printf("  kill, 3 min, kill -> %s\n", s_Bh.banner.c_str());

    Section("multi kills: dying resets the chain");
    BountyReset(); Kill(0, 1); g_Now += 5000; Kill(2, 0); g_Now += 5000; Kill(0, 3);
    CHECK(s_Bh.banner == "Killing Spree!", "after my death -> %s", s_Bh.banner.c_str());

    Section("bounty money: rewards + loss");
    BountyReset(); g_Money = 1000; g_Granted = 0; BountyStatsLoad(); const int k0 = s_Bh.myKills, d0 = s_Bh.myDeaths;
    Kill(2, 1); g_Now += 1000; Kill(2, 3); g_Now += 1000;    // Babochka: streak 2 -> bounty $300
    CHECK(BountyOf(2) == 300, "Babochka bounty %d", BountyOf(2));
    Kill(0, 2);                                              // I kill her: +$300
    CHECK(g_Money == 1300, "money after the bounty kill %d (expected 1300)", g_Money);
    CHECK(!s_Bh.lines.empty() && s_Bh.lines.back().text == "Babochka - Deagle +300.00", "reward line '%s'", s_Bh.lines.empty() ? "" : s_Bh.lines.back().text.c_str());
    CHECK(BountyOf(2) == 100, "her streak / bounty reset (%d)", BountyOf(2));
    g_Now += 1000; Kill(0, 1);                               // double kill bonus +50
    CHECK(g_Money == 1300 + 100 + 50, "double kill reward %d", g_Money);
    g_Now += 1000; Kill(3, 0, 30);
    CHECK(g_Money == 1450 - 100, "death costs $100 (%d)", g_Money);
    CHECK(s_Bh.lines.back().text == "Snitya - AK-47 -100.00", "loss line '%s'", s_Bh.lines.back().text.c_str());
    CHECK(s_Bh.myKills - k0 == 2 && s_Bh.myDeaths - d0 == 1, "K +%d D +%d", s_Bh.myKills - k0, s_Bh.myDeaths - d0);
    {   // K / D / Duel / Level are kept in bounty_stats.txt: a rejoin keeps them
        const int k = s_Bh.myKills, d = s_Bh.myDeaths, xp = s_Bh.myXp;
        BountyReset(); BountyStatsLoad();
        CHECK(s_Bh.myKills == k && s_Bh.myDeaths == d && s_Bh.myXp == xp, "stats survive a rejoin (K %d D %d xp %d)", s_Bh.myKills, s_Bh.myDeaths, s_Bh.myXp);
    }

    Section("duel: they challenge me, I accept, I win");
    BountyReset(); BountyStatsLoad(); const int duels0 = s_Bh.myDuels;
    BountyOnEvent(Ev(1, 1, 0));                              // hovanaki -> me: invite
    CHECK(s_Bh.duelInvFrom == 1 && ChatHas("challenges you to a duel"), "invite received");
    CHECK(BountyCommand("accept", ""), "/accept handled");
    CHECK(!client.sent.empty() && client.sent.back().cp == 2 && client.sent.back().slot == 1 && client.sent.back().pad == MP_BOUNTY_MAGIC, "accept packet sent to the server");
    CHECK(ChatHas("The duel between {FFFFFF}hovanaki {C08454}and {FFFFFF}Tester {C08454}has begun!"), "duel begun line");
    CHECK(s_Bh.duelWith == 1, "duelWith %d", s_Bh.duelWith);
    g_Chat.clear(); g_Now += 3000; Kill(0, 1);
    CHECK(ChatHas("{FFFFFF}Tester {C08454}won the duel against {FFFFFF}hovanaki"), "duel winner line");
    CHECK(s_Bh.myDuels - duels0 == 1 && s_Bh.duelWith == -1, "duel counted (+%d), over", s_Bh.myDuels - duels0);

    Section("duel: I challenge, they accept (their phone sends op 2 to me), they win");
    BountyReset(); client.sent.clear(); BountyStatsLoad(); const int duels1 = s_Bh.myDuels;
    CHECK(BountyCommand("duel", "2"), "/duel 2");
    CHECK(client.sent.size() == 1 && client.sent[0].cp == 1 && client.sent[0].slot == 2, "invite packet");
    BountyOnEvent(Ev(2, 2, 0));                              // Babochka accepted my duel
    CHECK(ChatHas("The duel between {FFFFFF}Tester {C08454}and {FFFFFF}Babochka"), "begun on my phone (challenger first)");
    CHECK(s_Bh.duelWith == 2, "duelWith %d", s_Bh.duelWith);
    g_Chat.clear(); Kill(2, 0);
    CHECK(ChatHas("Babochka {C08454}won the duel against {FFFFFF}Tester"), "loser sees the winner");
    CHECK(s_Bh.myDuels == duels1, "no duel win for me");

    Section("duel: someone else's duel, seen from my phone");
    BountyReset();
    BountyOnEvent(Ev(3, 2, 1));                              // Snitya accepted hovanaki's duel
    CHECK(ChatHas("The duel between {FFFFFF}hovanaki {C08454}and {FFFFFF}Snitya"), "bystander sees the duel");
    CHECK(s_Bh.duelWith == -1, "not my duel");
    g_Chat.clear(); Kill(1, 2);
    CHECK(!ChatHas("won the duel"), "a kill outside the duel pair doesn't end it");
    Kill(3, 1);
    CHECK(ChatHas("Snitya {C08454}won the duel against {FFFFFF}hovanaki"), "bystander sees the winner");

    Section("duel: decline, bad id, leave during a duel");
    BountyReset(); client.sent.clear();
    BountyOnEvent(Ev(1, 1, 0)); CHECK(BountyCommand("decline", ""), "/decline");
    CHECK(client.sent.back().cp == 3, "decline packet");
    CHECK(BountyCommand("duel", "0") && ChatHas("Usage: /duel"), "can't duel yourself");
    CHECK(BountyCommand("duel", "40") && ChatHas("Usage: /duel"), "unknown id");
    BountyOnEvent(Ev(1, 1, 0)); BountyCommand("accept", ""); BountyLeave(1);
    CHECK(s_Bh.duelWith == -1 && ChatHas("the other player left"), "leave ends the duel");
    BountyOnEvent(Ev(1, 1, 3)); CHECK(s_Bh.duelInvFrom == -1, "an invite for someone else is ignored");
    PktEvent bad = Ev(1, 1, 0); bad.pad = 0; BountyOnEvent(bad); CHECK(s_Bh.duelInvFrom == -1, "a race / DM packet without the magic is ignored");
    BountyOnEvent(Ev(1, 1, 0)); g_Now += 31000; BountyTick(1, g_Now);
    CHECK(s_Bh.duelInvFrom == -1, "an invite expires after 30 s");
    CHECK(!BountyCommand("accept", ""), "/accept with no invite falls through to other commands");

    Section("first time + welcome");
    BountyReset(); client.sent.clear(); BountyOnJoined(); g_Now += 3000; BountyTick(1, g_Now);
    CHECK(ChatHas("joined the {DC2626}Blood Deathmatch {C08454}for the first time"), "first-time greeting");
    CHECK(client.sent.size() == 1 && client.sent[0].cp == 4, "greeting sent to the others");
    CHECK(ChatHas("Welcome {FFFFFF}Tester"), "custom welcome filled");
    g_Chat.clear(); client.sent.clear(); BountyOnJoined(); g_Now += 3000; BountyTick(1, g_Now);
    CHECK(!ChatHas("for the first time") && client.sent.empty(), "second join: no first-time greeting");

    Section("Broker MQQ money rule");
    BountyReset(); g_Default = true; g_Money = 5000; g_Granted = 0; s_nMoneyRuleTaken = 0;
    client.state = NET_DISCONNECTED; DefaultRestrictTick(1, g_Now); client.state = NET_CONNECTED;   // fresh join
    g_Now += 300; DefaultRestrictTick(1, g_Now);              // baseline
    Game::AddMoney(2500000); g_Now += 300; DefaultRestrictTick(1, g_Now);   // saved progress / job pay (MP)
    CHECK(g_Money == 2505000, "MP's own money is kept (%d)", g_Money);
    CheatMoney(250000); g_Now += 300; DefaultRestrictTick(1, g_Now);       // HESOYAM
    CHECK(g_Money == 2505000, "HESOYAM +$250,000 taken back (%d)", g_Money);
    CheatMoney(999999999 - g_Money); g_Now += 300; DefaultRestrictTick(1, g_Now);   // trainer: max money
    CHECK(g_Money == 2505000, "unlimited money taken back (%d)", g_Money);
    CheatMoney(500); g_Now += 300; DefaultRestrictTick(1, g_Now);          // a game pickup / stunt bonus
    CHECK(g_Money == 2505500, "small game money kept (%d)", g_Money);
    Kill(0, 1); g_Now += 300; DefaultRestrictTick(1, g_Now);
    CHECK(g_Money == 2505600, "bounty reward kept (%d)", g_Money);
    g_Money = 99990000; Game::TakeMoneyGranted(); g_Now += 300; DefaultRestrictTick(1, g_Now);   // first look after a jump
    CHECK(g_Money == 2505600, "a jump straight to $99,990,000 is taken back (%d)", g_Money);
    g_Default = false; CheatMoney(300000); g_Now += 300; DefaultRestrictTick(1, g_Now);
    CHECK(g_Money == 2805600, "custom servers: the rule is off (%d)", g_Money);
    g_Default = true;
    CHECK(MoneyRuleTakeBack(-1, 999999999, 0) == 0, "no baseline yet -> nothing taken");
    CHECK(MoneyRuleTakeBack(100, 100 + 99999, 0) == 0, "+$99,999 is allowed");
    CHECK(MoneyRuleTakeBack(100, 100 + 100000, 0) == 100000, "+$100,000 is taken");
    CHECK(MoneyRuleTakeBack(99999000, 100000500, 2000) == 100000500 - 99999999, "the cap holds even with MP money");
    CHECK(ChatHas("unlimited money is blocked"), "the player is told");
    CHECK(DefaultBlocksCheat("money") && DefaultBlocksCheat("god") && !DefaultBlocksCheat("tp"), "money / god blocked, tp allowed");

    printf("\n%d passed, %d failed\n", g_Pass, g_Fail);
    return g_Fail ? 1 : 0;
}
