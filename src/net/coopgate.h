// 5.02.79: CoopFusion32 crossplay, built into libMP (the CoopFusion32 mod itself is not changed).
//
// CoopFusion32 (net.coopfusion32.mobile, an AML co-op mod for GTA SA 2.00) talks a line protocol over TCP to its
// server (coopfusion/server/index.mjs, port 6767): JOIN / WELCOME, INPUT (our state) -> PLAYER snapshots, NAME,
// REMOVE, VEHICLE, PLAYER_HIT (melee), PLAYER_HEALTH -> PLAYER_VITALS (each client reports the health it sees,
// the server keeps the lowest), SHOT / SHOTX (bullets the others replay).
//
// Like the open.mp crossplay (SampGate REMOTE role), libMP starts a private MP server on 127.0.0.1 and the phone
// joins it as usual. CoopGate is the co-op client of that server:
//   co-op players  -> ghost slots on the private server ("Name (Co-op)"), so the phone sees and fights them with
//                     MP's own remote players (kill feed, hit effects, nametags)
//   the phone      -> INPUT lines (position, heading, skin, weapon, health, armour, speed)
//   combat sync    -> the phone's hits: PLAYER_HEALTH (+ PLAYER_HIT for melee) and SHOTX for every bullet;
//                     their hits: PLAYER_HIT / PLAYER_VITALS become MP damage on the phone (from the right player,
//                     so the kill is credited); a co-op player's death is a kill feed line.
#pragma once
#include <stdint.h>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include "server.h"

class CoopGate : public MPServer::PhoneHook
{
public:
    CoopGate();
    ~CoopGate();
    // `srv` (a private MP server on 127.0.0.1) joins the CoopFusion32 server host:port as `name` (max 14 letters)
    bool Start(MPServer* srv, const char* host, uint16_t port, const char* name);
    void Stop();
    bool Running() const { return m_bRun; }
    bool Joined() const { return m_bJoined; }   // WELCOME received
    bool Ended() const { return m_bEnded; }     // the connection is over (Status() says why)
    int  Players();                             // co-op players on that server (not us)
    std::string Status();
    // MPServer::PhoneHook (called under the server's mutex: queue only)
    void OnPhonePkt(int slot, const uint8_t* data, int len) override;

    struct Impl;
private:
    void Loop();
    MPServer*         m_pSrv = nullptr;
    std::thread       m_Thread;
    std::atomic<bool> m_bRun{false}, m_bJoined{false}, m_bEnded{false};
    Impl*             m_p = nullptr;
};
