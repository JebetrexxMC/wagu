// 5.02: open.mp / SA-MP crossplay built in (net/sampgate.cpp), both roles, on the host.
//   REMOTE: a phone client -> private MP server on 127.0.0.1 -> SampGate::StartRemote -> a real open.mp
//           server (omp-server, default 127.0.0.1:7777). Checks: joined (InitGame), the server's
//           messages / dialogs / textdraws reach the phone, the phone's chat + movement go there.
//   SERVER: SampGate::StartServer on UDP 7791 + a SA-MP query.
// Build (from test/, after `make -C ../server`):
//   R=../third_party/omp-raknet; g++ -O1 -std=c++17 -pthread -I../src -I../third_party/mbedtls/include \
//     -D__stdcall= -DRAKNET_LEGACY -DPACKETS_PER_TICK=3 -I$R/Include/raknet -I$R sampgate_test.cpp \
//     ../src/net/sampgate.cpp ../src/net/client.cpp ../src/net/server.cpp ../src/net/lan.cpp ../src/net/online.cpp \
//     ../src/net/authcrypto.cpp ../src/net/samp_import.cpp ../server/obj/*.a -ldl -o sampgate_test
//   ./sampgate_test [omp host] [omp port]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include "net/client.h"
#include "net/server.h"
#include <set>
#include <map>
#include "net/samp.h"
#include "net/sampgate.h"
#include "net/cgrp.h"
#include "net/samp_keybar.h"

struct Ev : INetEvents {
    bool ok = false; int msgs = 0, dialogs = 0, tds = 0, samp = 0, hello = 0, joins = 0, syncs = 0, cg = 0;
    std::map<int, int> newOps; std::vector<PktSamp> out;   // 5.02.26
    std::set<int> pcList; int vehOps = 0;   // 5.02.25: SPO_PCLIST (the PC server's player list)
    int classShow = 0, classSpawned = 0;   // 5.02.13: SPO_CLASSSEL (the phone's < Spawn > on PC servers)
    std::vector<std::string> lines;
    void OnConnected(const PktWelcome&) override { ok = true; }
    void OnRejected(uint8_t) override {}
    void OnConnectFailed() override {}
    void OnLostConnection() override {}
    void OnPlayerJoin(const PktPlayerJoin& j) override { ++joins; printf("  join: %s\n", j.name); }
    void OnPlayerLeave(const PktPlayerLeave&) override {}
    void OnSync(const PktSync&, const SyncExt*) override { ++syncs; }
    void OnChat(const PktChat& c) override { printf("  chat [%d]: %s\n", c.id, c.text); }
    void OnDeath(const PktDeath&) override {}
    void OnDamage(const PktDamage&) override {}
    void OnServerMessage(const PktServerMsg& m) override { ++msgs; lines.push_back(m.text); printf("  msg %08X: %s\n", m.color, m.text); }
    void OnSetColor(const PktSetColor&) override {}
    void OnPause(const PktPause&) override {}
    void OnStatus(const PktStatus&) override {}
    void OnWorld(const PktWorld&) override {}
    int lastDlg = -1; uint8_t lastStyle = 0;
    void OnDialog(const PktDialog& d) override { ++dialogs; lastDlg = d.dialogId; lastStyle = d.style; printf("  dialog %u style %u [%s|%s]: %.60s\n     %.200s\n", d.dialogId, d.style, d.button1, d.button2, d.caption, d.info); }
    std::map<int, PktTextDraw> tdAll; bool tdClicked = false;   // 5.02.26
    void OnTextDraw(const PktTextDraw& t) override { ++tds; if(t.show) tdAll[t.tdId] = t; else tdAll.erase(t.tdId);
        if(getenv("MP_TEST_TDDUMP") && t.show) printf("  TD %u sel %u font %u pos %.1f %.1f letter %.3f %.3f box %u boxX %.1f col %08X boxcol %08X bg %08X align %u \"%.60s\"\n", t.tdId, t.pad[0], t.font, t.x, t.y, t.letterX, t.letterY, t.useBox, t.boxX, t.color, t.boxColor, t.bgColor, t.align, t.text);
        if(tds <= 3) printf("  textdraw %u show %u: %.40s\n", t.tdId, t.show, t.text); }
    void OnSamp(const uint8_t* d, int) override { ++samp;
        if(d[1] == SPO_CLASSSEL) { PktSamp p; memcpy(&p, d, sizeof(p)); if(p.a) ++classShow; else if(p.b) ++classSpawned; printf("  SPO_CLASSSEL show %u spawned %u class %d\n", p.a, p.b, p.val); }
        if(d[1] == SPO_VEHICLE) ++vehOps;
        if(d[1] == SPO_TDSELECT && getenv("MP_TEST_TDCLICK") && !tdClicked)
        {   PktSamp p; memcpy(&p, d, sizeof(p)); if(p.a) { tdClicked = true; PktSamp k; memset(&k, 0, sizeof(k)); k.type = PKT_SAMP; k.op = SPO_TDCLICK; k.val = atoi(getenv("MP_TEST_TDCLICK")); out.push_back(k); printf("  clicking textdraw %d\n", k.val); } }
        if(d[1] >= SPO_HUD && d[1] <= SPO_EDIT) { PktSamp p; memcpy(&p, d, sizeof(p)); ++newOps[d[1]]; printf("  5.02.26 op %u a %u b %u val %d %.60s\n", d[1], p.a, p.b, p.val, p.text);
            if(d[1] == SPO_EDIT && p.a)
            {   // the phone's edit panel: X+ then Save (edit), Pick (select)
                PktSamp k; memset(&k, 0, sizeof(k)); k.type = PKT_SAMP; k.op = SPO_EDIT;
                if(p.a == 3) { k.a = 30; out.push_back(k); }
                else { k.a = 2; out.push_back(k); k.a = 10; out.push_back(k); k.a = 20; out.push_back(k); }
            } }
        if(d[1] == SPO_VEHMOD || d[1] == SPO_ACTION) {}
        if(d[1] == SPO_PCLIST) { PktSamp p; memcpy(&p, d, sizeof(p)); if(p.a == 1) pcList.insert(p.val); else if(p.a == 2) pcList.erase(p.val); else pcList.clear(); }
        if(d[1] == SPO_HELLO) { ++hello; printf("  SPO_HELLO: %s\n", (const char*)d + offsetof(PktSamp, text)); } }
    float px = 1958.f, py = 1343.f, pz = 15.4f;   // 5.02.10: follows the server's SetPlayerPos like the phone does
    void OnCg(const uint8_t* d, int n) override { ++cg; if(n >= (int)sizeof(PktCgAct)) { const PktCgAct* a = (const PktCgAct*)d; if(a->act == CGA_TELE) { px = a->pos[0]; py = a->pos[1]; pz = a->pos[2]; printf("  tele %.1f %.1f %.1f\n", px, py, pz); } } }
};

static int fails = 0;
#define CHECK(c, msg) do { if(c) printf("ok   %s\n", msg); else { printf("FAIL %s\n", msg); ++fails; } } while(0)

int main(int argc, char** argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    const char* host = argc > 1 ? argv[1] : "127.0.0.1"; uint16_t port = (uint16_t)(argc > 2 ? atoi(argv[2]) : 7777);

    // ---- query ----
    SampQueryInfo qi; bool q = SampGate::Query(host, port, qi, 1500);
    CHECK(q, "SA-MP query of the PC server");
    if(q) printf("  %s | %s | %d/%d | ping %d | %s\n", qi.hostname.c_str(), qi.gamemode.c_str(), qi.players, qi.maxPlayers, qi.ping, qi.version.c_str());
    char h[64]; uint16_t pp = 0;
    CHECK(SampGate::ParseAddr("1.2.3.4:7778", h, sizeof(h), &pp) && pp == 7778 && !strcmp(h, "1.2.3.4"), "ParseAddr ip:port");
    CHECK(SampGate::SampName("Thirty Fox!") == "Thirty_Fox", "SampName");

    // ---- REMOTE role ----
    MPServer proxy; proxy.SetPersistent(true);
    const int basePort = getenv("MP_TEST_PORT") ? atoi(getenv("MP_TEST_PORT")) : 19791;   // 5.02.10: parallel sweeps
    if(!proxy.Start(basePort, "proxy", [](const char*){}, 4)) { printf("proxy failed\n"); return 1; }
    const char* pw = argc > 3 ? argv[3] : nullptr;   // 5.02.01: a password server (open.mp tab: the password is asked first)
    SampGate gate; gate.StartRemote(&proxy, host, port, pw);
    MPClient cl; Ev ev; cl.Connect("127.0.0.1", basePort, getenv("MP_TEST_NAME") ? getenv("MP_TEST_NAME") : "Phone Tester", 106);   // 5.02.11: two testers = each one is the other's PC player
    uint32_t seq = 1;
    auto pump = [&](int ms, bool move) {
        uint64_t e = MP_NowMs() + ms;
        while(MP_NowMs() < e)
        {
            cl.Poll(&ev);
            for(auto& k : ev.out) cl.Send(&k, sizeof(k));
            ev.out.clear();
            if(move && cl.GetState() == NET_CONNECTED && seq % 4 == 0)
            {
                PktSync p; memset(&p, 0, sizeof(p)); p.type = PKT_SYNC; p.seq = seq; p.d.pos[0] = ev.px + (float)(seq % 40) * 0.02f; p.d.pos[1] = ev.py; p.d.pos[2] = ev.pz;
                p.d.heading = 90; p.d.health = 100; p.d.skin = 106; p.d.speed = 3.0f; cl.Send(&p, sizeof(p));
            }
            ++seq;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    };
    // 5.02.05: big PC servers (500+ players) can take longer: MP_GATE_WAIT=<ms> (default 6000)
    pump(getenv("MP_GATE_WAIT") ? atoi(getenv("MP_GATE_WAIT")) : 6000, true);
    CHECK(ev.ok, "phone joined the private MP server");
    CHECK(gate.RemoteJoined(), "gate joined the PC server (InitGame)");
    printf("  status: %s\n", gate.Status().c_str());
    CHECK(ev.hello > 0, "phone got SPO_HELLO with the PC server name");
    printf("  PC player list: %d, ghost players (joins): %d, syncs %d, server car ops %d\n", (int)ev.pcList.size(), ev.joins, ev.syncs, ev.vehOps);
    CHECK(ev.msgs > 0, "phone got the PC server's messages");
    PktChat c; memset(&c, 0, sizeof(c)); c.type = PKT_CHAT; MP_CopyStr(c.text, "hello from the phone", sizeof(c.text)); cl.Send(&c, sizeof(c));
    pump(1500, true);
    PktSamp cmd; memset(&cmd, 0, sizeof(cmd)); cmd.type = PKT_SAMP; cmd.op = SPO_CMD; MP_CopyStr(cmd.text, "/help", sizeof(cmd.text)); cl.Send(&cmd, sizeof(cmd));
    pump(2500, true);
    if(getenv("MP_TEST_SPAM"))
    {   // 5.02.17: a player tapping Spawn 8 times in a row (Horizonte RP log) - the gate sends one RequestSpawn per answer
        PktSamp sp; memset(&sp, 0, sizeof(sp)); sp.type = PKT_SAMP; sp.op = SPO_SPAWNREQ; sp.a = 0;
        for(int i = 0; i < 8; ++i) { cl.Send(&sp, sizeof(sp)); pump(120, false); }
        pump(2500, true);
    }
    {   // 5.02.13: press the phone's Spawn (class selection) - the server must spawn us (SPO_CLASSSEL b = 1)
        CHECK(ev.classShow > 0, "phone got the PC server's class selection (< Spawn >)");
        PktSamp sp; memset(&sp, 0, sizeof(sp)); sp.type = PKT_SAMP; sp.op = SPO_SPAWNREQ; sp.a = 2; cl.Send(&sp, sizeof(sp));   // > next class
        pump(800, false);
        sp.a = 0; cl.Send(&sp, sizeof(sp));   // Spawn
        pump(3000, true);
        CHECK(ev.classSpawned > 0, "Spawn button spawned the phone on the PC server");
    }
    if(const char* dpw = getenv("MP_TEST_DIALOG"))
    {   // 5.02.14: answer the PC server's dialogs like a player (button 1, first item, MP_TEST_DIALOG as the text) - login flows
        for(int n = 0; n < 8; ++n)
        {
            for(int w = 0; w < 15 && ev.lastDlg < 0; ++w) pump(1000, true);
            if(ev.lastDlg < 0) break;
            PktDialogResp r; memset(&r, 0, sizeof(r)); r.type = PKT_DIALOG_RESPONSE; r.response = 1; r.dialogId = (uint16_t)ev.lastDlg; r.listItem = 0;
            MP_CopyStr(r.input, dpw, sizeof(r.input)); printf("  -> answer dialog %d\n", ev.lastDlg); ev.lastDlg = -1;
            cl.Send(&r, sizeof(r)); pump(3000, true);
        }
        printf("  after the dialogs: class shown %d, spawned %d\n", ev.classShow, ev.classSpawned);
    }
    if(getenv("MP_TEST_KEYS"))
    {   // 5.02.15: the crossplay key bar, frame by frame (60 fps) through the phone's own key logic (net/samp_keybar.h).
        // keybar_test.sh checks the server's OnPlayerKeyStateChange / OnDialogResponse / OnPlayerClickTextDraw log.
        SampKeyBarState kb; int sends = 0;
        auto frames = [&](int ms, uint32_t held, uint32_t tapFirst) {
            const uint64_t e = MP_NowMs() + ms; bool first = true;
            while(MP_NowMs() < e)
            {
                bool send = false; const uint64_t now = MP_NowMs();
                const int keys = SampKeyBarStep(kb, now, held, first ? tapFirst : 0, send); first = false;
                if(send) { PktSamp k; memset(&k, 0, sizeof(k)); k.type = PKT_SAMP; k.op = SPO_KEYS; k.val = keys; cl.Send(&k, sizeof(k)); ++sends; }
                pump(16, true);
            }
        };
        auto cmdTo = [&](const char* t) { PktSamp k; memset(&k, 0, sizeof(k)); k.type = PKT_SAMP; k.op = SPO_CMD; MP_CopyStr(k.text, t, sizeof(k.text)); cl.Send(&k, sizeof(k)); };
        for(int i = 0; i < SKB_COUNT; ++i)
        {
            if(kSampKeyBar[i].bit <= 0) continue;
            printf("  tap %s (%d)\n", kSampKeyBar[i].label, kSampKeyBar[i].bit);
            frames(16, 1u << i, 1u << i);   // one frame under the finger = a quick tap
            frames(900, 0, 0);
        }
        printf("  hold SPC 2 s\n"); frames(2000, 1u << SKB_SPC, 1u << SKB_SPC); frames(900, 0, 0);
        printf("  hold Y, then N while Y is still down\n"); frames(400, 1u << SKB_Y, 1u << SKB_Y); frames(400, (1u << SKB_Y) | (1u << SKB_N), 1u << SKB_N); frames(900, 0, 0);
        printf("  key sends: %d\n", sends);
        // ESC = CloseDialog(0): response 0, list item -1 (msgbox) / the selected row (list)
        ev.lastDlg = -1; cmdTo("/dlg"); for(int w = 0; w < 30 && ev.lastDlg < 0; ++w) pump(100, true);
        CHECK(ev.lastDlg == 77, "keybar: the PC dialog came (/dlg)");
        { PktDialogResp r; memset(&r, 0, sizeof(r)); r.type = PKT_DIALOG_RESPONSE; r.dialogId = 77; r.response = 0; r.listItem = -1; cl.Send(&r, sizeof(r)); }
        pump(800, true);
        ev.lastDlg = -1; cmdTo("/dlg2"); for(int w = 0; w < 30 && ev.lastDlg < 0; ++w) pump(100, true);
        CHECK(ev.lastDlg == 78, "keybar: the PC list dialog came (/dlg2)");
        { PktDialogResp r; memset(&r, 0, sizeof(r)); r.type = PKT_DIALOG_RESPONSE; r.dialogId = 78; r.response = 0; r.listItem = 1; cl.Send(&r, sizeof(r)); }
        pump(800, true);
        cmdTo("/sel"); pump(800, true);
        { PktSamp k; memset(&k, 0, sizeof(k)); k.type = PKT_SAMP; k.op = SPO_TDCLICK; k.val = -1; cl.Send(&k, sizeof(k)); }
        pump(1000, true);
    }
    if(getenv("MP_TEST_HOLD")) pump(atoi(getenv("MP_TEST_HOLD")), true);   // 5.02.13: two testers - stay to hear the other one's chat
    for(auto& kv : ev.newOps) printf("  new op %d x%d\n", kv.first, kv.second);
    printf("  messages %d, dialogs %d, textdraws %d, samp ops %d, cg acts %d, joins %d\n", ev.msgs, ev.dialogs, ev.tds, ev.samp, ev.cg, ev.joins);
    CHECK(!gate.Ended(), "still connected after chat + /command + movement");
    cl.Disconnect(); pump(300, false);
    gate.Stop(); proxy.Stop();

    if(getenv("MP_TEST_REMOTE_ONLY")) { printf(fails ? "\n%d FAILED\n" : "\nall ok\n", fails); return fails ? 1 : 0; }   // 5.02.10: sweeps skip the host part

    // ---- SERVER role ----
    MPServer srv; srv.SetPersistent(true);
    if(!srv.Start(19792, "Gate Host", [](const char*){}, 10)) { printf("server failed\n"); return 1; }
    SampGate g2; CHECK(g2.StartServer(&srv, 7791, "Gate Host", 10), "SA-MP port 7791 open");
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    SampQueryInfo q2; CHECK(SampGate::Query("127.0.0.1", 7791, q2, 1500) && q2.hostname == "Gate Host", "the MP server answers the SA-MP browser query");
    {   // 5.02.11: a phone on the MP server, moving; MP_TEST_HOST_HOLD=<ms> keeps it up so a second process (a PC-side
        // sampgate_test 127.0.0.1 7791) can check it sees the phone's character (stream in + movement)
        MPClient ph; Ev pe; ph.Connect("127.0.0.1", 19792, "Phone Host", 106);
        const int hold = getenv("MP_TEST_HOST_HOLD") ? atoi(getenv("MP_TEST_HOST_HOLD")) : 3000;
        uint64_t e = MP_NowMs() + hold; uint32_t sq = 1;
        while(MP_NowMs() < e)
        {
            ph.Poll(&pe);
            if(ph.GetState() == NET_CONNECTED && sq % 4 == 0)
            {
                PktSync p; memset(&p, 0, sizeof(p)); p.type = PKT_SYNC; p.seq = sq; p.d.pos[0] = 1958.4f + (float)(sq % 40) * 0.05f; p.d.pos[1] = 1343.2f; p.d.pos[2] = 15.4f;
                p.d.heading = 90; p.d.health = 100; p.d.skin = 106; p.d.speed = 3.0f; ph.Send(&p, sizeof(p));
            }
            ++sq; std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        SampQueryInfo q3; CHECK(SampGate::Query("127.0.0.1", 7791, q3, 1500) && q3.players >= 1, "the launcher query lists the phone player");
        printf("  query: %d players\n", q3.players);
        ph.Disconnect();
    }
    g2.Stop(); srv.Stop();

    printf(fails ? "\n%d FAILED\n" : "\nall ok\n", fails);
    return fails ? 1 : 0;
}
