// Linux test (4.13.59): phone calls on every server (dial / ring / busy / answer / hang up / decline / leave).
//   g++ -O2 -std=c++17 -pthread -I../src call_test.cpp ../src/net/client.cpp ../src/net/server.cpp -o pair_test
#include <stdio.h>
#include <string.h>
#include <thread>
#include <chrono>
#include "net/client.h"
#include "net/server.h"

struct Ev : INetEvents {
    int id = -1, perms = -1, partner = -1, asked = -1, denied = 0, ended = 0;
    int ring = -1, ringing = -1, linked = -1, cEnded = 0, cDeclined = 0, busy = 0, unavail = 0;
    void OnCall(const PktCall& c) override
    {
        int o = c.other == MP_INVALID_ID ? -1 : c.other;
        switch(c.action) { case CALL_RING: ring = o; break; case CALL_RINGING: ringing = o; break; case CALL_LINKED: linked = o; break;
            case CALL_ENDED: ++cEnded; linked = -1; break; case CALL_DECLINED: ++cDeclined; break; case CALL_BUSY: ++busy; break; case CALL_UNAVAILABLE: ++unavail; break; }
    }
    void OnConnected(const PktWelcome& w) override { id = w.yourId; }
    void OnRejected(uint8_t) override {}
    void OnConnectFailed() override {}
    void OnLostConnection() override {}
    void OnPlayerJoin(const PktPlayerJoin&) override {}
    void OnPlayerLeave(const PktPlayerLeave&) override {}
    void OnSync(const PktSync&, const SyncExt*) override {}
    void OnChat(const PktChat&) override {}
    void OnDeath(const PktDeath&) override {}
    void OnDamage(const PktDamage&) override {}
    void OnServerMessage(const PktServerMsg&) override {}
    void OnSetColor(const PktSetColor&) override {}
    void OnPause(const PktPause&) override {}
    void OnStatus(const PktStatus&) override {}
    void OnWorld(const PktWorld&) override {}
    void OnPerms(const PktPerms& p) override { perms = p.perms; }
    void OnPair(const PktPair& a) override
    {
        if(a.action == PAIR_REQUEST) asked = a.other;
        if(a.action == PAIR_LINKED) partner = a.other;
        if(a.action == PAIR_END) { ++ended; if(partner == a.other) partner = -1; }
        if(a.action == PAIR_DENIED) ++denied;
    }
};
static MPClient cl[3]; static Ev ev[3];
static void Pump(int ms) { for(int t = 0; t < ms / 5; ++t) { for(int i = 0; i < 3; ++i) cl[i].Poll(&ev[i]); std::this_thread::sleep_for(std::chrono::milliseconds(5)); } }
static void Call(int from, uint8_t act, int to) { PktCall q; q.type = PKT_CALL; q.action = act; q.other = (uint8_t)(to >= 0 ? ev[to].id : MP_INVALID_ID); cl[from].Send(&q, sizeof(q)); }
int main()
{
    bool pass = true;
    #define CHECK(c, msg) do { bool _c = (c); printf("%-62s %s\n", msg, _c ? "OK" : "FAIL"); pass &= _c; } while(0)
    MPServer srv; if(!srv.Start(19359, "Call Test", [](const char*){}, 100)) return 1;
    for(int i = 0; i < 3; ++i) { char n[8]; snprintf(n, 8, "P%d", i); cl[i].Connect("127.0.0.1", 19359, n, 106); }
    Pump(800);
    CHECK(ev[0].id >= 0 && ev[1].id >= 0 && ev[2].id >= 0, "3 clients connected");
    Call(0, CALL_DIAL, 1); Pump(200);
    CHECK(ev[1].ring == ev[0].id && ev[0].ringing == ev[1].id && ev[2].ring < 0, "dial: only the callee rings, the caller hears it ring");
    Call(2, CALL_DIAL, 1); Pump(200);
    CHECK(ev[2].busy == 1 && ev[1].ring == ev[0].id, "a second caller gets BUSY");
    Call(1, CALL_ACCEPT, 0); Pump(200);
    CHECK(ev[0].linked == ev[1].id && ev[1].linked == ev[0].id, "answer links both phones");
    Call(2, CALL_DIAL, 0); Pump(200);
    CHECK(ev[2].busy == 2, "calling a player who is on a call = BUSY");
    Call(1, CALL_HANGUP, -1); Pump(200);
    CHECK(ev[0].cEnded == 1 && ev[1].cEnded == 1 && ev[0].linked < 0, "hang up ends it on both phones");
    Call(2, CALL_DIAL, 0); Pump(200);
    CHECK(ev[0].ring == ev[2].id, "after the call the phone can ring again");
    Call(0, CALL_DECLINE, 2); Pump(200);
    CHECK(ev[2].cDeclined == 1 && ev[2].linked < 0, "decline tells the caller");
    Call(1, CALL_ACCEPT, 2); Pump(200);
    CHECK(ev[1].linked < 0 && ev[1].cEnded == 2, "accepting a call nobody made is refused");
    Call(0, CALL_DIAL, 0); Pump(200);
    CHECK(ev[0].unavail == 1, "calling yourself = UNAVAILABLE");
    Call(0, CALL_DIAL, 2); Pump(200); Call(2, CALL_ACCEPT, 0); Pump(200);
    CHECK(ev[0].linked == ev[2].id, "second call links");
    cl[2].Disconnect(); Pump(400);
    CHECK(ev[0].cEnded >= 2 && ev[0].linked < 0, "the partner leaving ends the call");
    srv.Stop();
    printf(pass ? "ALL OK\n" : "SOME FAILED\n");
    return pass ? 0 : 1;
}
