// 5.02: open.mp / SA-MP 0.3.7 crossplay, built into libMP and mp_server (no open.mp component, no plugin).
//
// The SA-MP network (RakNet 2.52 + SA-MP 0.3.7 encryption / auth / cookies) is open.mp's own RakNet
// (third_party/omp-raknet, https://github.com/openmultiplayer/RakNet), with a client side added.
// The packet layouts follow open.mp's NetCode (https://github.com/openmultiplayer/open.mp, Shared/NetCode).
//
// Two roles, both talk to one MPServer through its bridge API (ghost slots) + the Gate* helpers:
//
//   SERVER  (StartServer): PC players with the SA-MP 0.3.7 / open.mp launcher join THIS MP server on a
//           UDP port (default 7777). Each PC player is a ghost slot on the MP server (phones see him),
//           and every phone player is a normal SA-MP player for the PC players. The SA-MP server
//           browser query (info / rules / players / ping) is answered too.
//
//   REMOTE  (StartRemote): this MP server (a private one on 127.0.0.1 inside libMP) joins a PC
//           open.mp / SA-MP server as a SA-MP client, for the phone player that is on it. The PC
//           server's players become ghost slots, its RPCs (messages, dialogs, textdraws, checkpoints,
//           objects, teleports, health, weapons, money, time, weather...) go to the phone, and the
//           phone's movement / chat / commands / dialog answers / textdraw clicks go to the PC server.
#pragma once
#include <stdint.h>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include "protocol.h"

class MPServer;

// SA-MP server browser answer ('i' + 'r' query)
struct SampQueryInfo
{
    bool ok = false;
    bool password = false;
    int players = 0, maxPlayers = 0, ping = -1;
    std::string hostname, gamemode, language, version;
    std::vector<std::string> names;   // 5.02.12: Query(..., wantNames): the 'c' list (servers send it only up to 100 players)
    bool namesHidden = false;         // more than 100 players: SA-MP / open.mp servers never send the names (same on the PC launchers)
};

class SampGate
{
public:
    SampGate();
    ~SampGate();

    // PC players join `srv` on UDP `port` (SA-MP 0.3.7 / open.mp launcher). false = port taken.
    bool StartServer(MPServer* srv, uint16_t port, const char* hostname, int maxPc = 50);
    // `srv` (normally a private MP server on 127.0.0.1) joins the PC server host:port for its phone player.
    bool StartRemote(MPServer* srv, const char* host, uint16_t port, const char* password = nullptr);
    void Stop();
    // 5.02.11 server role: list it on the open.mp internet list (SA-MP / open.mp launchers), like open.mp's announce=true
    void SetAnnounce(bool on);
    // 5.02.12 remote role: the name on the PC server ("Firstname_Lastname" for RP); empty = the MP name without [MOD] / [VIP] tags
    void SetRemoteName(const char* name);
    // 5.02.29 remote role: SampVoice voice chat on / off (Settings > Voice). Can change while connected
    void SetVoice(bool on);

    bool Running() const { return m_bRun; }
    bool IsRemote() const { return m_bRemote; }
    bool RemoteJoined() const { return m_bJoined; }   // remote: InitGame received
    bool Ended() const;                                // remote: the connection is over (Status() says why)
    uint16_t Port() const { return m_nPort; }
    int  PcPlayers();                                  // server: PC players on it, remote: players on the PC server
    std::string Status();                              // one line for the UI
    std::string RemoteName();                          // remote: the PC server's name (InitGame)

    // ---- hooks, called by MPServer while it holds its mutex: they only queue ----
    bool OnChat(int slot, const char* text);           // true = the MP server must not handle it
    bool OnSamp(int slot, const uint8_t* data, int len);
    bool OnDialog(int slot, uint16_t dialogId, uint8_t response, int16_t listItem, const char* input);
    // 5.02.25: a copy of the phone's damage / shot / car state packets (the MP server still handles them itself)
    void OnPhonePkt(int slot, const uint8_t* data, int len);

    // SA-MP browser query of any SA-MP / open.mp server (blocking, timeoutMs)
    static bool Query(const char* host, uint16_t port, SampQueryInfo& out, int timeoutMs = 1500, bool wantNames = false);
    // "1.2.3.4:7777" / "host" (port 7777) -> host + port
    static bool ParseAddr(const char* text, char* host, int hostCap, uint16_t* port);
    // MP name -> a SA-MP name (3..20 of A-Z a-z 0-9 _ [ ] ( ) $ @ . =)
    static std::string SampName(const char* mpName);
    static void SetLogHook(void (*fn)(const char*));   // 5.02.12: [crossplay] lines into mp_log.txt

    // answer of the gate's SA-MP query responder (server role), called from the RakNet socket thread
    int  AnswerQuery(const char* in, int inLen, char* out, int outCap);

    struct Impl;
private:
    void Loop();
    MPServer*           m_pSrv = nullptr;
    std::thread         m_Thread;
    std::atomic<bool>   m_bRun{false};
    std::atomic<bool>   m_bRemote{false};
    std::atomic<bool>   m_bJoined{false};
    uint16_t            m_nPort = 0;
    Impl*               m_p = nullptr;
};
