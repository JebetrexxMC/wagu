// 5.02: open.mp / SA-MP 0.3.7 crossplay inside libMP and mp_server - see sampgate.h
#include "sampgate.h"
#include "server.h"
#include "samp.h"
#include "cgrp.h"
#include "dz_map.h"
#include "online.h"
#include <atomic>
#include <memory>

#include "RakNetworkFactory.h"
#include "RakClientInterface.h"
#include "RakServerInterface.h"
#include "PacketEnumerations.h"
#include "BitStream.h"
#include "StringCompressor.h"
#include "SAMPRakNet.hpp"

#include <math.h>
#include <cmath>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <chrono>
#include <map>
#include <unordered_map>
#include <deque>
#include <array>
#include <algorithm>
#include "sampcp.h"
#include <random>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <poll.h>
#endif

using namespace RakNet;

// 5.02.12: crossplay lines also go to the app log (mp_log.txt) - before, phone logs had no [crossplay] lines at all
static void (*g_GateLog)(const char*) = nullptr;
static int GatePrintf(const char* fmt, ...)
{
    char b[1024]; va_list ap; va_start(ap, fmt); int n = vsnprintf(b, sizeof(b), fmt, ap); va_end(ap);
    fputs(b, stdout);
    if(g_GateLog) { size_t l = strlen(b); while(l && (b[l-1] == '\n' || b[l-1] == '\r')) b[--l] = 0; if(l) g_GateLog(b); }
    return n;
}
#define printf GatePrintf
void SampGate::SetLogHook(void (*fn)(const char*)) { g_GateLog = fn; }

namespace {

const uint32_t kSampVersion = 4057;          // SA-MP 0.3.7-R2/R3 netgame version
const uint32_t kSampVersionDL = 4062;        // 5.02.08: SA-MP 0.3.DL-R1 (custom models)
const uint16_t kInvalid = 0xFFFF;
const uint8_t  kPlayerSync = 207, kVehicleSync = 200;   // SA-MP 0.3.7 sync packet ids
const float    kPi = 3.14159265358979f;
const int      kTdSlots = 200;            // MP client textdraw slots a SA-MP server may use (SAMP_TD_BASE 0..199)
const int      kLabelBase = 0;

uint64_t NowMs() { return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

// ---- small bitstream helpers ------------------------------------------------------------------
std::string RdStr8(BitStream& bs) { uint8_t n = 0; if(!bs.Read(n)) return ""; std::string s(n, 0); if(n && !bs.Read(&s[0], n)) return ""; return s; }
std::string RdStr16(BitStream& bs) { uint16_t n = 0; if(!bs.Read(n)) return ""; std::string s(n, 0); if(n && !bs.Read(&s[0], n)) return ""; return s; }
std::string RdStr32(BitStream& bs) { uint32_t n = 0; if(!bs.Read(n) || n > 8192) return ""; std::string s(n, 0); if(n && !bs.Read(&s[0], (int)n)) return ""; return s; }
std::string RdCompressed(BitStream& bs) { static char b[4097]; b[0] = 0; stringCompressor->DecodeString(b, 4096, &bs); b[4096] = 0; return b; }
void WrStr8(BitStream& bs, const std::string& s) { uint8_t n = (uint8_t)std::min<size_t>(s.size(), 255); bs.Write(n); if(n) bs.Write(s.data(), n); }
void WrStr32(BitStream& bs, const std::string& s) { uint32_t n = (uint32_t)s.size(); bs.Write(n); if(n) bs.Write(s.data(), (int)n); }
bool RdVec(BitStream& bs, float* v) { return bs.Read(v[0]) && bs.Read(v[1]) && bs.Read(v[2]); }
void WrVec(BitStream& bs, const float* v) { bs.Write(v[0]); bs.Write(v[1]); bs.Write(v[2]); }
void WrCompVec(BitStream& bs, float x, float y, float z)
{
    float m = sqrtf(x * x + y * y + z * z); bs.Write(m);
    if(m > 0.00001f) { bs.WriteCompressed(x / m); bs.WriteCompressed(y / m); bs.WriteCompressed(z / m); }
}
bool RdCompVec(BitStream& bs, float* v)
{
    float m = 0; if(!bs.Read(m)) return false; v[0] = v[1] = v[2] = 0;
    if(m > 0.00001f) { float a, b, c; if(!bs.ReadCompressed(a) || !bs.ReadCompressed(b) || !bs.ReadCompressed(c)) return false; v[0] = a * m; v[1] = b * m; v[2] = c * m; }
    return true;
}
uint32_t Bswap(uint32_t v) { return (v >> 24) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (v << 24); }   // ABGR <-> RGBA

// heading (deg, GTA: 0 = north, counter-clockwise) <-> SA-MP quaternion (w x y z)
void HeadingQuat(float h, float* q) { float a = h * kPi / 180.0f; q[0] = cosf(a * 0.5f); q[1] = 0; q[2] = 0; q[3] = -sinf(a * 0.5f); }
float QuatHeading(float w, float x, float y, float z)
{
    float rz = -atan2f(x * y + z * w, 0.5f - x * x - z * z) * 180.0f / kPi;
    while(rz < 0) rz += 360.0f; while(rz >= 360.0f) rz -= 360.0f; return rz;
}
uint8_t PackHA(int h, int a) { auto c = [](int v) -> uint8_t { return v >= 100 ? 0x0F : (uint8_t)((std::max(v, 0) + 6) / 7); }; return (uint8_t)(c(h) << 4 | c(a)); }
void UnpackHA(uint8_t ha, int& h, int& a) { int hi = ha >> 4, lo = ha & 15; h = hi == 15 ? 100 : hi * 7; a = lo == 15 ? 100 : lo * 7; }

// SA-MP keys from MP sync flags
uint16_t KeysFromFlags(uint8_t fl) { uint16_t k = 0; if(fl & SYNC_SHOOTING) k |= 4 | 128; if(fl & SYNC_DUCKING) k |= 2; return k; }

// one SA-MP / open.mp gpci: a random number * 1001 in upper-case hex (open.mp checks % 1001)
std::string Gpci()
{
    // 5.02.05: rand() was never seeded, so every MP phone sent the SAME serial (one gpci ban on a PC server
    // = every MP player banned). Seed once per run from the OS.
    static std::mt19937 rng{ std::random_device{}() ^ (unsigned)NowMs() };
    uint8_t n[24] = { 0 }; for(int i = 0; i < 16; ++i) n[i] = (uint8_t)(rng() & 255);
    unsigned c = 0; for(int i = 0; i < 24; ++i) { unsigned v = n[i] * 1001u + c; n[i] = (uint8_t)(v & 255); c = v >> 8; }
    std::string out; bool lead = true;
    for(int i = 23; i >= 0; --i) for(int h = 1; h >= 0; --h) { int d = (n[i] >> (h * 4)) & 15; if(lead && !d) continue; lead = false; out += "0123456789ABCDEF"[d]; }
    return out.empty() ? "0" : out;
}

SyncData FootToSync(const float* pos, float heading, const float* vel, int health, int armour, uint8_t weapon, uint16_t keys, uint16_t skin)
{
    SyncData d; memset(&d, 0, sizeof(d));
    memcpy(d.pos, pos, sizeof(d.pos)); d.heading = heading;
    float a = heading * kPi / 180.0f; d.aim[0] = pos[0] - sinf(a) * 10.0f; d.aim[1] = pos[1] + cosf(a) * 10.0f; d.aim[2] = pos[2];
    d.speed = sqrtf(vel[0] * vel[0] + vel[1] * vel[1] + vel[2] * vel[2]) * 50.0f;
    d.health = (uint8_t)std::max(0, std::min(health, 255)); d.armour = (uint8_t)std::max(0, std::min(armour, 255));
    d.weapon = weapon & 63; d.skin = skin;
    if(keys & 4) d.flags |= SYNC_SHOOTING;
    if(keys & 2) d.flags |= SYNC_DUCKING;
    if(health <= 0) d.flags |= SYNC_DEAD;
    return d;
}

struct RpcIn { int id; std::vector<uint8_t> data; unsigned bits; PlayerID from; PlayerIndex idx; };
struct HookEv { int kind; int slot; std::string text; uint8_t op = 0, a = 0, b = 0; int32_t val = 0; uint16_t dialog = 0; uint8_t resp = 0; int16_t list = 0; };
enum { HK_CHAT = 1, HK_SAMP, HK_DIALOG };

SampGate* g_QueryGate = nullptr;
int QueryThunk(const char* in, int inLen, char* out, int outCap) { return g_QueryGate ? g_QueryGate->AnswerQuery(in, inLen, out, outCap) : 0; }
void LogThunk(const char* t) { printf("[open.mp net] %s\n", t); }

} // namespace

struct SampGate::Impl
{
    // shared
    std::mutex qm;                       // RPC + hook queues
    std::deque<RpcIn> rpcs;
    std::deque<HookEv> hooks;
    std::string hostname = "MP Server";
    std::string status;
    std::mutex sm;                        // status / query snapshot
    std::string qInfo;                    // cached query answers (built by the gate thread)
    std::vector<std::pair<std::string, int32_t>> qPlayers;
    int qCount = 0, qMax = 0;

    // ---- server role ----
    RakServerInterface* S = nullptr;
    struct Pc { bool on = false, joined = false, spawned = false, dead = false; PlayerID pid; int slot = -1; std::string name; uint16_t skin = 0; uint64_t lastSync = 0; };
    std::vector<Pc> pcs;                                   // by PlayerIndex
    std::map<int, uint64_t> phoneSeen;                     // phone slot -> last relayed (0 = joined, not streamed)
    std::map<int, std::string> phoneNames;
    uint64_t lastRelay = 0, lastPeers = 0;
    bool announce = false; uint64_t annAt = 0; std::shared_ptr<std::atomic<bool>> annBusy = std::make_shared<std::atomic<bool>>(false);   // 5.02.11
    int maxPc = 50;

    // ---- remote role ----
    RakClientInterface* C = nullptr;
    std::string rHost, rPass, rName, rServerName;
    int cp = 1252;   // 5.02.05: the PC server's text codepage (SampCp::ForLanguage), chat / dialogs are converted to UTF-8
    uint16_t rPort = 0;
    int phase = 0;                // 0 wait phone, 1 connecting, 2 accepted, 3 in game (class), 4 spawned, 9 ended
    uint64_t phaseAt = 0;
    int tries = 0;                // 5.02.06: connect attempts (a busy server that drops the first one gets 2 more)
    // 5.02.08: the PC client the phone shows itself as: SA-MP 0.3.7-R5 (4057) or 0.3.DL-R1 (4062, picked when the
    // server's "version" rule says DL; MP_GATE_CLIENT=r2 / r5 / dl forces one). A server that kicks one right at
    // the join gets the other one once.
    uint32_t cliVer = 4057; std::string cliVerStr = "0.3.7-R5"; bool verSwapped = false;
    int rejectN = 0; uint64_t rejoinAt = 0;   // 5.02.12: ConnectionRejected (RPC 130) retries
    int lostN = 0; uint64_t gameAt = 0;   // gameAt: InitGame time (a kick in the first minute is the script, not a restart)
    std::string forceName;                    // 5.02.12: the name for PC servers (empty = the MP name without rank tags)
    std::unordered_map<int32_t, int32_t> modelBase;   // 5.02.08: 0.3.DL custom model id -> its base model (RPC 179)
    int phoneSlot = -1;
    int myId = -1;
    struct Spawn { uint32_t skin = 0; float pos[3] = { 0, 0, 3 }; float rot = 0; uint32_t w[3] = { 0, 0, 0 }, ammo[3] = { 0, 0, 0 }; bool have = false; } spawn;
    struct Rp { std::string name; uint32_t color = 0xFFFFFFFF; uint16_t skin = 0; int slot = -1; SyncData last; bool hasPos = false; int vehicle = -1; };
    std::map<int, Rp> rp;                                  // remote player id -> ghost
    std::map<int, std::pair<int32_t, uint32_t>> pcScore;  // 5.02.12: id -> score, ping (RPC 155) for the phone's player list
    uint64_t scoreAskAt = 0;
    std::map<int, uint16_t> vehModel;                      // remote vehicle id -> model
    std::map<int, int> tdSlot;                             // wire textdraw id -> MP textdraw slot
    int tdOwner[kTdSlots];                       // MP slot -> wire id (-1)
    std::map<int, PktTextDraw> tdDef;
    // 5.02.15: 3D text labels attached to a player / vehicle - the gate sends them as world labels that follow it
    struct ALabel { uint16_t ap = 0xFFFF, avh = 0xFFFF; float off[3] = { 0, 0, 0 }; float dist = 20; uint32_t color = 0xFFFFFFFF; std::string text; bool shown = false; float at[3] = { 0, 0, 0 }; };
    std::map<int, ALabel> alabels;
    std::map<int, std::array<float, 3>> vehPos;            // remote vehicle id -> last known spot (stream in / driver sync)
    uint64_t labelTickAt = 0;
    struct Obj { uint32_t row; uint16_t model; float p[3], r[3]; };
    std::map<int, Obj> objs;
    struct Pick { Obj o; bool inside = false; };   // 5.02.11: pickups (CreatePickup) - shown as objects, walking in sends OnPlayerPickUpPickup
    std::map<int, Pick> picks;
    std::map<int, std::array<float, 4>> zones;
    uint64_t lastFoot = 0, keysUntil = 0, retrySpawnAt = 0, keysHintAt = 0;
    uint32_t heldKeys = 0;
    bool wasDead = false;
    bool wantClass = false;
    bool dlgOpen = false, tdSel = false, spect = false, noAuto = false;   // noAuto: the server sent us back to class selection - wait for the player    // 5.02.10: no auto-spawn while a login dialog / clickable-textdraw login is up (a PC player can't press Spawn then)                 // 5.02.09: ForceClassSelection waits for the next respawn (like the PC client)
    uint64_t spawnAnsAt = 0; int spawnSilent = 0; bool directSpawn = false;   // 5.02.20: Spawn never answered -> spawn here
    uint64_t spawnAskAt = 0, refuseMsgAt = 0, classAt = 0;   // 5.02.17: one RequestSpawn in flight (taps flooded: kicked), refusal note, class selection since
    bool finDl = false;   // 5.02.17: FinishDownload (184) sent - scripts that open their login in OnPlayerFinishedDownloading
    int tdShown = 0;      // 5.02.17: textdraws the server showed (log)
    int dlgId = -1; uint8_t dlgStyle = 0;   // 5.02.17: the open dialog (its answer must match it)
    uint64_t autoSpawnWin = 0; int autoSpawnN = 0;   // 5.02.09: cap auto RequestSpawn so a login screen can't loop
    int lastHealth = 100;
    // 5.02.15: what a PC client reports back - anti-cheats (Nex-AC...) and FPS / AFK scripts read these
    int lastArmour = 0;                   // SetPlayerArmour (RPC 66), 0 at spawn
    int32_t money = 0;                    // GivePlayerMoney / ResetPlayerMoney - sent back in the stats packet (205)
    int32_t drunk = 0;                    // SetPlayerDrunkLevel (RPC 35) - the PC client lowers it 1 per frame
    uint64_t statsAt = 0, drunkAt = 0;
    bool gunOk[64] = {};                  // weapons the server gave (GivePlayerWeapon / spawn), the rest is reported as fists
    // 5.02.13
    bool resetPending = false;            // lost connection: keep ghosts / list on screen until the new InitGame
    uint64_t listFullAt = 0;              // last full player-list resend to the phone
    uint64_t stableSince = 0;             // in game since (lostN goes back to 0 after 3 stable minutes)
    float holdPos[3] = { 0, 0, 0 }; uint64_t holdUntil = 0;   // spawn / SetPlayerPos: report this spot until the phone is there
    int classIdx = 0;                     // class selection: the class the phone looks at (< >)
    bool classShown = false;              // the phone shows the PC class selection (< Spawn >)

    std::atomic<bool>* joinedFlag = nullptr;
    Impl() { for(int& t : tdOwner) t = -1; }
};

typedef SampGate::Impl Impl;

// ================================================================================================
// common
// ================================================================================================
SampGate::SampGate() : m_p(new Impl) {}
SampGate::~SampGate() { Stop(); delete m_p; }

static void OnRpc(RPCParameters* p, void* extra)
{   // RakNet calls this from its network thread: queue, the gate thread handles it
    Impl* I = (Impl*)((void**)extra)[0]; int id = (int)(intptr_t)((void**)extra)[1];
    RpcIn r; r.id = id; r.bits = p->numberOfBitsOfData; r.from = p->sender; r.idx = p->senderIndex;
    if(p->input && r.bits) r.data.assign(p->input, p->input + (r.bits + 7) / 8);
    std::lock_guard<std::mutex> lk(I->qm);
    // 5.02.13: was 4096 - a busy server (300+ players) sends one ServerJoin (137) per player at the join plus the
    // scoreboard, objects and textdraws; RPCs past the cap were dropped, so players were missing from the list and
    // their chat lines were thrown away ("can't see PC chat")
    if(I->rpcs.size() < 65536) I->rpcs.push_back(std::move(r));
}
static void* s_RpcExtra[256][2];

template<class T> static void RegisterAll(T* peer, Impl* I)
{
    for(int i = 0; i < 256; ++i) { s_RpcExtra[i][0] = I; s_RpcExtra[i][1] = (void*)(intptr_t)i; peer->RegisterAsRemoteProcedureCall((RPCID)i, OnRpc, s_RpcExtra[i]); }
}

std::string SampGate::SampName(const char* mpName)
{
    std::string o;
    const char* src = mpName ? mpName : "";
    // 5.02.12: MP rank tags ("[MOD] ", "[VIP] ", "[ADMIN] ") are not part of the name - PC servers got "[MOD]_moquil"
    // and refused or kicked it at once (name rules, RP Firstname_Lastname checks), so every server dropped mod players
    for(;;)
    {
        while(*src == ' ') ++src;
        if(*src != '[') break;
        const char* e = strchr(src, ']'); if(!e || e - src > 10 || !e[1]) break;
        src = e + 1;
    }
    for(const char* p = src; *p && o.size() < 20; ++p)
    {
        char c = *p;
        if(isalnum((unsigned char)c) || strchr("_[]()$@.=", c)) o += c;
        else if(c == ' ' && !o.empty() && o.back() != '_') o += '_';
    }
    while(!o.empty() && o.back() == '_') o.pop_back();
    while(o.size() < 3) o += '_';
    return o;
}

bool SampGate::ParseAddr(const char* text, char* host, int hostCap, uint16_t* port)
{
    if(!text) return false;
    while(*text == ' ') ++text;
    if(!strncmp(text, "samp://", 7)) text += 7;
    std::string s = text; while(!s.empty() && (s.back() == ' ' || s.back() == '/')) s.pop_back();
    if(s.empty()) return false;
    size_t c = s.rfind(':'); int p = 7777;
    if(c != std::string::npos) { p = atoi(s.c_str() + c + 1); s = s.substr(0, c); }
    if(s.empty() || p <= 0 || p > 65535 || (int)s.size() >= hostCap) return false;
    MP_CopyStr(host, s.c_str(), hostCap); *port = (uint16_t)p; return true;
}

static bool Resolve(const char* host, uint32_t* addr)
{
    in_addr a; if(inet_pton(AF_INET, host, &a) == 1) { *addr = a.s_addr; return true; }
    addrinfo hints; memset(&hints, 0, sizeof(hints)); hints.ai_family = AF_INET; addrinfo* res = nullptr;
    if(getaddrinfo(host, nullptr, &hints, &res) || !res) return false;
    *addr = ((sockaddr_in*)res->ai_addr)->sin_addr.s_addr; freeaddrinfo(res); return true;
}

bool SampGate::Query(const char* host, uint16_t port, SampQueryInfo& out, int timeoutMs, bool wantNames)
{
    out = SampQueryInfo();
    uint32_t ip; if(!Resolve(host, &ip)) return false;
    int s = (int)socket(AF_INET, SOCK_DGRAM, 0); if(s < 0) return false;
    sockaddr_in sa; memset(&sa, 0, sizeof(sa)); sa.sin_family = AF_INET; sa.sin_port = htons(port); sa.sin_addr.s_addr = ip;
    auto ask = [&](char op, std::vector<uint8_t>& ans, int* ms) -> bool {
        uint8_t q[11] = { 'S', 'A', 'M', 'P' }; memcpy(q + 4, &ip, 4); q[8] = (uint8_t)(port & 255); q[9] = (uint8_t)(port >> 8); q[10] = (uint8_t)op;
        uint64_t t0 = NowMs();
        if(sendto(s, (const char*)q, 11, 0, (sockaddr*)&sa, sizeof(sa)) != 11) return false;
        while((int)(NowMs() - t0) < timeoutMs)
        {
#ifdef _WIN32
            fd_set f; FD_ZERO(&f); FD_SET(s, &f); timeval tv = { 0, 100000 }; if(select(s + 1, &f, 0, 0, &tv) <= 0) continue;
#else
            pollfd pf = { s, POLLIN, 0 }; if(poll(&pf, 1, 100) <= 0) continue;
#endif
            uint8_t b[4096]; int n = (int)recv(s, (char*)b, sizeof(b), 0);
            if(n >= 11 && !memcmp(b, "SAMP", 4) && b[10] == (uint8_t)op) { ans.assign(b + 11, b + n); if(ms) *ms = (int)(NowMs() - t0); return true; }
        }
        return false;
    };
    std::vector<uint8_t> a; int ms = 0;
    bool ok = ask('i', a, &ms);
    if(ok && a.size() >= 5)
    {
        size_t at = 0; auto u8 = [&]() -> uint32_t { return at < a.size() ? a[at++] : 0; };
        auto u16 = [&]() -> uint32_t { uint32_t v = u8(); return v | u8() << 8; };
        auto u32 = [&]() -> uint32_t { uint32_t v = u16(); return v | u16() << 16; };
        auto str = [&]() -> std::string { uint32_t n = u32(); if(n > a.size() - std::min(at, a.size())) n = (uint32_t)(a.size() - std::min(at, a.size())); std::string r((const char*)a.data() + at, n); at += n; return r; };
        out.password = u8() != 0; out.players = (int)u16(); out.maxPlayers = (int)u16();
        out.hostname = str(); out.gamemode = str(); out.language = str();
        out.ping = ms; out.ok = true;
        std::vector<uint8_t> r;
        if(ask('r', r, nullptr) && r.size() >= 2)
        {
            size_t k = 2; int cnt = r[0] | r[1] << 8;
            for(int i = 0; i < cnt && k < r.size(); ++i)
            {
                int ln = r[k++]; std::string nm((const char*)r.data() + k, std::min<size_t>(ln, r.size() - k)); k += ln; if(k >= r.size()) break;
                int lv = r[k++]; std::string vl((const char*)r.data() + k, std::min<size_t>(lv, r.size() - k)); k += lv;
                if(nm == "version") out.version = vl;
            }
        }
        if(wantNames && out.players > 100) out.namesHidden = true;
        else if(wantNames && out.players > 0)
        {   // 5.02.12: 'c' = u16 count, then u8 length + name + int32 score
            std::vector<uint8_t> c;
            if(ask('c', c, nullptr) && c.size() >= 2)
            {
                size_t k = 2; const int cnt = c[0] | c[1] << 8;
                for(int i = 0; i < cnt && k < c.size(); ++i)
                {
                    const int ln = c[k++]; if(k + ln > c.size()) break;
                    out.names.emplace_back((const char*)c.data() + k, ln); k += ln + 4;
                }
            }
        }
    }
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
    return out.ok;
}

int SampGate::PcPlayers()
{
    if(!m_bRemote) { int n = 0; for(auto& p : m_p->pcs) if(p.joined) ++n; return n; }
    return (int)m_p->rp.size();
}
std::string SampGate::Status() { std::lock_guard<std::mutex> lk(m_p->sm); return m_p->status; }
std::string SampGate::RemoteName() { std::lock_guard<std::mutex> lk(m_p->sm); return m_p->rServerName; }
static void SetStatus(Impl& I, const char* fmt, ...)
{
    char b[256]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof(b), fmt, ap); va_end(ap);
    bool changed; { std::lock_guard<std::mutex> lk(I.sm); changed = I.status != b; I.status = b; }
    if(changed && strncmp(b, "PC players: ", 12) && strncmp(b, "In game", 7)) printf("[crossplay] status: %s\n", b);   // 5.02.12: the join steps in mp_log.txt
}

// ---- hooks (MPServer mutex held: queue only) ----------------------------------------------------
bool SampGate::OnChat(int slot, const char* text)
{
    if(!m_bRun || !m_bRemote || !text) return false;
    HookEv e; e.kind = HK_CHAT; e.slot = slot; e.text = text;
    std::lock_guard<std::mutex> lk(m_p->qm); m_p->hooks.push_back(e); return true;
}
bool SampGate::OnSamp(int slot, const uint8_t* data, int len)
{
    if(!m_bRun || !m_bRemote || len < (int)offsetof(PktSamp, text)) return false;
    PktSamp p; memset(&p, 0, sizeof(p)); memcpy(&p, data, std::min<int>(len, (int)sizeof(p))); p.text[sizeof(p.text) - 1] = 0;
    HookEv e; e.kind = HK_SAMP; e.slot = slot; e.op = p.op; e.a = p.a; e.b = p.b; e.val = p.val; e.text = p.text;
    std::lock_guard<std::mutex> lk(m_p->qm); m_p->hooks.push_back(e); return true;
}
bool SampGate::OnDialog(int slot, uint16_t dialogId, uint8_t response, int16_t listItem, const char* input)
{
    if(!m_bRun || !m_bRemote) return false;
    HookEv e; e.kind = HK_DIALOG; e.slot = slot; e.dialog = dialogId; e.resp = response; e.list = listItem; e.text = input ? input : "";
    std::lock_guard<std::mutex> lk(m_p->qm); m_p->hooks.push_back(e); return true;
}

// ================================================================================================
// SERVER role: PC players join this MP server
// ================================================================================================
static const uint32_t kColors[] = { 0xFF8C13FF, 0xC715FFFF, 0x20B2AAFF, 0xDC143CFF, 0x6495EDFF, 0xF0E68CFF, 0x778899FF, 0xFF1493FF, 0xF4A460FF, 0xEE82EEFF, 0xFFD720FF, 0x8B4513FF, 0x4949A0FF, 0x148B8BFF, 0x14FF7FFF, 0x556B2FFF };

static void SrvRpc(Impl& I, const Impl::Pc& p, int id, BitStream& bs)
{
    I.S->RPC((RPCID)id, &bs, HIGH_PRIORITY, RELIABLE_ORDERED, 2, p.pid, false, false, UNASSIGNED_NETWORK_ID, nullptr);
}
static void SrvRpcAll(Impl& I, int id, BitStream& bs, int exceptIdx = -1)
{
    for(size_t i = 0; i < I.pcs.size(); ++i) if(I.pcs[i].joined && (int)i != exceptIdx) SrvRpc(I, I.pcs[i], id, bs);
}
static void SrvMsg(Impl& I, const Impl::Pc* p, uint32_t col, const std::string& t)
{
    BitStream bs; bs.Write(col); WrStr32(bs, t);
    if(p) SrvRpc(I, *p, 93, bs); else SrvRpcAll(I, 93, bs);
}
static void SrvJoinMsg(Impl& I, const Impl::Pc& to, int id, const std::string& name, uint32_t col)
{
    BitStream bs; bs.Write((uint16_t)id); bs.Write(col); bs.Write((uint8_t)0); WrStr8(bs, name); SrvRpc(I, to, 137, bs);
}
static void SrvStreamIn(Impl& I, const Impl::Pc& to, int id, uint16_t skin, const float* pos, float ang, uint32_t col)
{
    BitStream bs; bs.Write((uint16_t)id); bs.Write((uint8_t)255); bs.Write((uint32_t)skin); WrVec(bs, pos); bs.Write(ang); bs.Write(col);
    bs.Write((uint8_t)4); for(int i = 0; i < 11; ++i) bs.Write((uint16_t)999);
    SrvRpc(I, to, 32, bs);
}
static void SrvQuit(Impl& I, int id, int exceptIdx = -1) { BitStream bs; bs.Write((uint16_t)id); bs.Write((uint8_t)1); SrvRpcAll(I, 138, bs, exceptIdx); }

// server-format on-foot sync (what a SA-MP client gets about another player)
static void WriteFootServer(BitStream& bs, int id, const SyncData& d, uint16_t extraKeys)
{
    const bool car = (d.flags & SYNC_IN_CAR) != 0;
    const float* pos = car ? d.carPos : d.pos; float h = car ? d.carHeading : d.heading;
    float q[4]; HeadingQuat(h, q);
    float a = h * kPi / 180.0f, sp = car ? 0.0f : d.speed / 50.0f;
    bs.Write((uint8_t)kPlayerSync); bs.Write((uint16_t)id);
    bs.Write(false);                                                   // left / right
    uint16_t ud = (!car && d.speed > 0.3f) ? 0xFF80 : 0; bs.Write(ud != 0); if(ud) bs.Write(ud);
    bs.Write((uint16_t)(KeysFromFlags(d.flags) | extraKeys));
    WrVec(bs, pos); bs.WriteNormQuat(q[0], q[1], q[2], q[3]);
    bs.Write(PackHA((d.flags & SYNC_DEAD) ? 0 : d.health, d.armour));
    bs.Write((uint8_t)(d.weapon & 63));
    bs.Write((uint8_t)((d.flags & SYNC_DUCKING) ? 1 : 0));
    WrCompVec(bs, -sinf(a) * sp, cosf(a) * sp, 0.0f);
    bs.Write(false); bs.Write(false);                                  // surfing, animation
}

static void SrvInitGame(Impl& I, MPServer* srv, const Impl::Pc& p)
{
    BitStream bs;
    bs.Write(false); bs.Write(true); bs.Write(true); bs.Write(false); bs.Write(200.0f); bs.Write(true); bs.Write(70.0f);
    bs.Write(false); bs.Write(true); bs.Write(false); bs.Write((uint32_t)1); bs.Write((uint16_t)p.slot); bs.Write(true); bs.Write((uint32_t)1);
    bs.Write((uint8_t)srv->GateWorldHour()); bs.Write((uint8_t)1); bs.Write(0.008f); bs.Write(false); bs.Write((uint32_t)0); bs.Write(false);
    bs.Write((uint32_t)40); bs.Write((uint32_t)40); bs.Write((uint32_t)40); bs.Write((uint32_t)10); bs.Write((uint32_t)1);
    WrStr8(bs, I.hostname);
    uint8_t models[212] = { 0 }; bs.Write((const char*)models, 212);
    bs.Write((uint32_t)0);
    SrvRpc(I, p, 139, bs);
}

static void SrvSpawnPoint(MPServer* srv, float* pos, float* ang)
{   // near the first phone player, else Pershing Square (Los Santos)
    std::vector<MPServer::BridgePeer> peers; srv->BridgePeers(peers);
    for(auto& b : peers) if(b.hasSync && !(b.d.flags & SYNC_DEAD))
    {
        const float* q = (b.d.flags & SYNC_IN_CAR) ? b.d.carPos : b.d.pos;
        pos[0] = q[0] + 2.0f; pos[1] = q[1] + 2.0f; pos[2] = q[2] + 0.5f; *ang = b.d.heading; return;
    }
    pos[0] = 1479.6f; pos[1] = -1675.6f; pos[2] = 14.05f; *ang = 180.0f;
}

static void SrvHandleRpc(SampGate* g, Impl& I, MPServer* srv, RpcIn& r)
{
    if(r.idx >= I.pcs.size()) return;
    Impl::Pc& p = I.pcs[r.idx];
    BitStream bs(r.data.data(), (unsigned)r.data.size(), false);
    switch(r.id)
    {
        case 25:   // ClientJoin
        {
            if(p.joined) break;
            uint32_t ver = 0, chal = 0; uint8_t mod = 0; bs.Read(ver); bs.Read(mod);
            std::string name = SampGate::SampName(RdStr8(bs).c_str()); bs.Read(chal); std::string key = RdStr8(bs); std::string vs = RdStr8(bs);
            p.pid = r.from; p.on = true; p.name = name;
            if(ver != kSampVersion && ver != kSampVersionDL) { Impl::Pc t = p; t.pid = r.from; SrvMsg(I, &t, 0xFF4040FF, "This MP server takes SA-MP 0.3.7 / 0.3.DL / open.mp clients only."); I.S->Kick(r.from); break; }
            int slot = srv->BridgeAdd(name.c_str(), kColors[r.idx % 16], 0);
            if(slot < 0) { Impl::Pc t = p; SrvMsg(I, &t, 0xFF4040FF, "The server is full."); I.S->Kick(r.from); break; }
            p.slot = slot; p.joined = true; p.spawned = false; p.skin = 0; p.lastSync = 0;
            printf("[crossplay] PC player %s joined (SA-MP %s) as MP id %d\n", name.c_str(), vs.c_str(), slot);
            SrvInitGame(I, srv, p);
            // the players already here
            std::vector<MPServer::BridgePeer> peers; srv->BridgePeers(peers);
            for(auto& b : peers) SrvJoinMsg(I, p, b.slot, SampGate::SampName(b.name), 0xFFFFFFFF);
            for(size_t i = 0; i < I.pcs.size(); ++i) if(I.pcs[i].joined && i != r.idx) SrvJoinMsg(I, p, I.pcs[i].slot, I.pcs[i].name, kColors[i % 16]);
            for(size_t i = 0; i < I.pcs.size(); ++i) if(I.pcs[i].joined && i != r.idx) SrvJoinMsg(I, I.pcs[i], slot, name, kColors[r.idx % 16]);
            char w[200]; snprintf(w, sizeof(w), "Welcome to %s - you play with GTA SA phone players (MP crossplay).", I.hostname.c_str());
            SrvMsg(I, &p, 0x88AA62FF, w);
            break;
        }
        case 128:  // RequestClass -> one class
        {
            float pos[3], ang; SrvSpawnPoint(srv, pos, &ang);
            BitStream o; o.Write((uint8_t)1); o.Write((uint8_t)255); o.Write((uint32_t)p.skin); o.Write((uint8_t)0); WrVec(o, pos); o.Write(ang);
            for(int i = 0; i < 6; ++i) o.Write((uint32_t)0);
            SrvRpc(I, p, 128, o);
            break;
        }
        case 129: { BitStream o; o.Write((uint32_t)1); SrvRpc(I, p, 129, o); break; }   // RequestSpawn -> allowed
        case 52:   // Spawn
        {
            if(!p.joined) break;
            p.spawned = true; p.dead = false;
            float pos[3], ang; SrvSpawnPoint(srv, pos, &ang);
            std::vector<MPServer::BridgePeer> peers; srv->BridgePeers(peers);
            for(auto& b : peers) { const float* q = b.hasSync ? ((b.d.flags & SYNC_IN_CAR) ? b.d.carPos : b.d.pos) : pos; SrvStreamIn(I, p, b.slot, b.skin, q, b.d.heading, 0xFFFFFFFF); }
            for(size_t i = 0; i < I.pcs.size(); ++i)
            {
                if(!I.pcs[i].joined || i == r.idx) continue;
                if(I.pcs[i].spawned) SrvStreamIn(I, p, I.pcs[i].slot, I.pcs[i].skin, pos, 0, kColors[i % 16]);
                SrvStreamIn(I, I.pcs[i], p.slot, p.skin, pos, ang, kColors[r.idx % 16]);
            }
            break;
        }
        case 53:   // death
        {
            uint8_t reason = 0; uint16_t killer = kInvalid; bs.Read(reason); bs.Read(killer);
            p.dead = true;
            BitStream o; o.Write(killer); o.Write((uint16_t)p.slot); o.Write(reason); SrvRpcAll(I, 55, o);
            break;
        }
        case 101:  // chat
        {
            std::string t = RdStr8(bs); if(t.empty() || !p.joined) break;
            if(t.size() > 127) t.resize(127);
            srv->BridgeChat(p.slot, t.c_str());
            BitStream o; o.Write((uint16_t)p.slot); WrStr8(o, t); SrvRpcAll(I, 101, o);
            break;
        }
        case 50:   // /command
        {
            std::string t = RdStr32(bs);
            if(t == "/players" || t == "/online")
            {
                std::vector<MPServer::BridgePeer> peers; srv->BridgePeers(peers);
                std::string l = "Phone players: "; for(auto& b : peers) { l += b.name; l += "  "; }
                SrvMsg(I, &p, 0xFFFF00FF, peers.empty() ? "No phone players online." : l);
            }
            else SrvMsg(I, &p, 0xFFFFFFFF, "SERVER: Unknown command. (MP crossplay server: /players)");
            break;
        }
        case 115:  // damage given / taken
        {
            bool take = false; uint16_t who = kInvalid; float dmg = 0; uint32_t weapon = 0, part = 0;
            bs.Read(take); bs.Read(who); bs.Read(dmg); bs.Read(weapon); bs.Read(part);
            if(!take && who < MP_MAX_PLAYERS && dmg > 0 && dmg < 300)
            {   // a PC player hit a phone player: the phone loses health
                float cur[3], h; int hp = 100;
                if(srv->GatePos(who, cur, &h, &hp)) srv->GateAct(who, CGA_HEALTH, 0, 0, std::max(0, hp - (int)ceilf(dmg)));
            }
            break;
        }
        case 155:  // scoreboard
        {
            BitStream o;
            std::vector<MPServer::BridgePeer> peers; srv->BridgePeers(peers);
            for(auto& b : peers) { o.Write((uint16_t)b.slot); o.Write((int32_t)0); o.Write((uint32_t)60); }
            for(auto& q : I.pcs) if(q.joined) { o.Write((uint16_t)q.slot); o.Write((int32_t)0); o.Write((uint32_t)std::max(0, I.S->GetLastPing(q.pid))); }
            SrvRpc(I, p, 155, o);
            break;
        }
        default: break;
    }
    (void)g;
}

static void SrvDrop(Impl& I, MPServer* srv, int idx)
{
    Impl::Pc& p = I.pcs[idx];
    if(p.joined) { printf("[crossplay] PC player %s left\n", p.name.c_str()); srv->BridgeRemove(p.slot); SrvQuit(I, p.slot, idx); }
    p = Impl::Pc();
}

static void SrvFootIn(Impl& I, MPServer* srv, Packet* pk)
{
    if(pk->playerIndex >= I.pcs.size()) return;
    Impl::Pc& p = I.pcs[pk->playerIndex]; if(!p.joined || !p.spawned) return;
    BitStream bs(pk->data, pk->length, false); bs.IgnoreBits(8);
    uint16_t lr, ud, keys; float pos[3], q[4], vel[3], surf[3]; uint8_t hp, ar, wk, sa; uint16_t sid, anim, af;
    if(!bs.Read(lr) || !bs.Read(ud) || !bs.Read(keys) || !RdVec(bs, pos) || !bs.Read(q[0]) || !bs.Read(q[1]) || !bs.Read(q[2]) || !bs.Read(q[3])) return;
    bs.Read(hp); bs.Read(ar); bs.Read(wk); bs.Read(sa); RdVec(bs, vel); RdVec(bs, surf); bs.Read(sid); bs.Read(anim); bs.Read(af);
    if(!std::isfinite(pos[0]) || !std::isfinite(pos[1]) || !std::isfinite(pos[2]) || fabsf(pos[0]) > 20000 || fabsf(pos[1]) > 20000) return;
    float h = QuatHeading(q[0], q[1], q[2], q[3]);
    SyncData d = FootToSync(pos, h, vel, p.dead ? 0 : hp, ar, wk, keys, p.skin);
    if(sa == 1) d.flags |= SYNC_DUCKING;
    srv->BridgeSync(p.slot, d);
    p.lastSync = NowMs();
    // relay to the other PC players
    BitStream o; o.Write((uint8_t)kPlayerSync); o.Write((uint16_t)p.slot);
    o.Write(lr != 0); if(lr) o.Write(lr); o.Write(ud != 0); if(ud) o.Write(ud); o.Write(keys);
    WrVec(o, pos); o.WriteNormQuat(q[0], q[1], q[2], q[3]); o.Write(PackHA(hp, ar)); o.Write(wk); o.Write(sa); WrCompVec(o, vel[0], vel[1], vel[2]);
    o.Write(false); o.Write(false);
    for(size_t i = 0; i < I.pcs.size(); ++i) if(I.pcs[i].spawned && i != pk->playerIndex) I.S->Send(&o, HIGH_PRIORITY, UNRELIABLE_SEQUENCED, 1, I.pcs[i].pid, false);
}

static void SrvTick(SampGate* g, Impl& I, MPServer* srv)
{
    // packets
    for(Packet* pk = I.S->Receive(); pk; I.S->DeallocatePacket(pk), pk = I.S->Receive())
    {
        if(!pk->length) continue;
        switch(pk->data[0])
        {
            case ID_NEW_INCOMING_CONNECTION:
                if(pk->playerIndex < I.pcs.size()) { I.pcs[pk->playerIndex] = Impl::Pc(); I.pcs[pk->playerIndex].on = true; I.pcs[pk->playerIndex].pid = pk->playerId; }
                break;
            case ID_DISCONNECTION_NOTIFICATION: case ID_CONNECTION_LOST:
                if(pk->playerIndex < I.pcs.size()) SrvDrop(I, srv, pk->playerIndex);
                break;
            case kPlayerSync: SrvFootIn(I, srv, pk); break;
            default: break;
        }
    }
    // RPCs
    std::deque<RpcIn> q; { std::lock_guard<std::mutex> lk(I.qm); q.swap(I.rpcs); I.hooks.clear(); }
    for(auto& r : q) SrvHandleRpc(g, I, srv, r);

    const uint64_t now = NowMs();
    // phones -> PC players (join / leave / movement, 20 Hz)
    if(now - I.lastRelay >= 50)
    {
        I.lastRelay = now;
        std::vector<MPServer::BridgePeer> peers; srv->BridgePeers(peers);
        std::map<int, bool> live;
        for(auto& b : peers)
        {
            live[b.slot] = true;
            if(!I.phoneNames.count(b.slot))
            {
                I.phoneNames[b.slot] = b.name;
                std::string nm = SampGate::SampName(b.name);
                for(auto& p : I.pcs) if(p.joined)
                {
                    SrvJoinMsg(I, p, b.slot, nm, 0xFFFFFFFF);
                    if(p.spawned) { float z[3] = { 0, 0, 0 }; SrvStreamIn(I, p, b.slot, b.skin, b.hasSync ? b.d.pos : z, b.d.heading, 0xFFFFFFFF); }
                }
            }
            if(!b.hasSync) continue;
            BitStream o; WriteFootServer(o, b.slot, b.d, 0);
            for(auto& p : I.pcs) if(p.spawned) I.S->Send(&o, HIGH_PRIORITY, UNRELIABLE_SEQUENCED, 1, p.pid, false);
        }
        for(auto it = I.phoneNames.begin(); it != I.phoneNames.end();)
        {
            if(live.count(it->first)) { ++it; continue; }
            SrvQuit(I, it->first); it = I.phoneNames.erase(it);
        }
        // phone chat -> PC players
        std::vector<std::pair<int, std::string>> chat; srv->BridgeTakeChat(chat);
        for(auto& c : chat) { BitStream o; o.Write((uint16_t)c.first); WrStr8(o, c.second); SrvRpcAll(I, 101, o); }
        // 5.02.11: the open.mp internet list (what an open.mp server does at start: GET api.open.mp/0.3.7/announce/<port>;
        // the list then queries this port, so the SA-MP / open.mp launchers show the server and its players - phones too).
        // Again every 10 minutes so a list restart finds it again.
        if(I.announce && (I.annAt == 0 || now - I.annAt >= 600000) && !I.annBusy->load())
        {
            I.annAt = now; I.annBusy->store(true);
            const std::string url = "https://api.open.mp/0.3.7/announce/" + std::to_string(g->Port());
            auto busy = I.annBusy;
            std::thread([url, busy] {
                std::string body, why;
                const bool ok = Online::HttpsFetch(url, body, 15000, why, 4096);
                printf("[crossplay] open.mp list announce %s%s%s\n", ok ? "sent" : "failed: ", ok ? "" : why.c_str(), ok && !body.empty() ? (" - " + body.substr(0, 120)).c_str() : "");
                busy->store(false);
            }).detach();
        }
        // query snapshot
        std::lock_guard<std::mutex> lk(I.sm);
        I.qPlayers.clear();
        for(auto& b : peers) I.qPlayers.emplace_back(SampGate::SampName(b.name), 0);
        for(auto& p : I.pcs) if(p.joined) I.qPlayers.emplace_back(p.name, 0);
        I.qCount = (int)I.qPlayers.size();
        I.status = "PC players: " + std::to_string(g->PcPlayers()) + " (SA-MP port " + std::to_string(g->Port()) + ")";
    }
}

int SampGate::AnswerQuery(const char* in, int inLen, char* out, int outCap)
{
    if(inLen < 11 || outCap < 64) return 0;
    std::lock_guard<std::mutex> lk(m_p->sm);
    std::string o(in, 11);
    auto u16 = [&](uint32_t v) { o += (char)(v & 255); o += (char)(v >> 8); };
    auto u32 = [&](uint32_t v) { u16(v & 0xFFFF); u16(v >> 16); };
    auto s32 = [&](const std::string& s) { u32((uint32_t)s.size()); o += s; };
    auto s8 = [&](const std::string& s) { std::string t = s.substr(0, 255); o += (char)t.size(); o += t; };
    switch(in[10])
    {
        case 'i': o += (char)0; u16((uint32_t)m_p->qCount); u16((uint32_t)m_p->qMax); s32(m_p->hostname); s32("MP crossplay"); s32("English"); break;
        case 'r': u16(4); s8("lagcomp"); s8("On"); s8("mapname"); s8("San Andreas"); s8("version"); s8("omp 1.6.7 (MP)"); s8("weburl"); s8("github.com/JebetrexxMC/wagu"); break;
        case 'c': { int n = std::min<int>((int)m_p->qPlayers.size(), 100); u16((uint32_t)n); for(int i = 0; i < n; ++i) { s8(m_p->qPlayers[i].first); u32((uint32_t)m_p->qPlayers[i].second); } break; }
        case 'd': { int n = std::min<int>((int)m_p->qPlayers.size(), 100); u16((uint32_t)n); for(int i = 0; i < n; ++i) { o += (char)i; s8(m_p->qPlayers[i].first); u32((uint32_t)m_p->qPlayers[i].second); u32(60); } break; }
        case 'p': if(inLen >= 15) o.append(in + 11, 4); break;
        default: return 0;
    }
    if((int)o.size() > outCap) return 0;
    memcpy(out, o.data(), o.size()); return (int)o.size();
}

bool SampGate::StartServer(MPServer* srv, uint16_t port, const char* hostname, int maxPc)
{
    Stop();
    m_pSrv = srv; m_nPort = port; m_bRemote = false; m_bJoined = false;
    Impl& I = *m_p; I.hostname = hostname && *hostname ? hostname : "MP Server"; I.maxPc = std::max(1, std::min(maxPc, 200));
    I.qMax = std::min(1000, srv->GetMaxPlayers() + I.maxPc);
    I.pcs.assign((size_t)I.maxPc + 2, Impl::Pc()); I.phoneNames.clear();
    static ICore core; core.fn = LogThunk;
    SAMPRakNet::Init(&core);
    SAMPRakNet::SeedToken();
    I.S = RakNetworkFactory::GetRakServerInterface();
    RegisterAll(I.S, &I);
    if(!I.S->Start((unsigned short)(I.maxPc + 2), 0, 5, port))
    {
        printf("[crossplay] SA-MP port %u is taken - PC crossplay is off\n", port);
        RakNetworkFactory::DestroyRakServerInterface(I.S); I.S = nullptr; return false;
    }
    I.S->StartOccasionalPing();
    SAMPRakNet::SetPort(port);
    SAMPRakNet::SeedCookie();
    g_QueryGate = this; SAMPRakNet::SetQuery(QueryThunk);
    srv->BridgeEnable(true);
    srv->SetGate(this);
    SetStatus(I, "PC players: 0 (SA-MP port %u)", port);
    printf("[crossplay] PC players (SA-MP 0.3.7 / open.mp launcher) can join on UDP port %u\n", port);
    m_bRun = true;
    m_Thread = std::thread([this] { Loop(); });
    return true;
}

// ================================================================================================
// REMOTE role: this MP server joins a PC open.mp / SA-MP server for its phone player
// ================================================================================================
static void CliRpc(Impl& I, int id, BitStream& bs) { if(I.C) I.C->RPC((RPCID)id, &bs, HIGH_PRIORITY, RELIABLE_ORDERED, 2, false, UNASSIGNED_NETWORK_ID, nullptr); }

static void RemClassSel(Impl& I, MPServer* srv, bool show, bool spawned = false);   // 5.02.13

static void RemReset(Impl& I, MPServer* srv)
{   // ghosts, objects and textdraws of the PC server go away
    for(auto& kv : I.rp) if(kv.second.slot >= 0) srv->BridgeRemove(kv.second.slot);
    I.rp.clear(); I.vehModel.clear(); I.pcScore.clear(); I.scoreAskAt = 0; I.listFullAt = 0; I.resetPending = false; I.holdUntil = 0;
    RemClassSel(I, srv, false);
    if(I.phoneSlot >= 0) srv->GateOp(I.phoneSlot, SPO_PCLIST, 0, 0, 0);
    for(auto& kv : I.objs) { float z[3] = { 0, 0, 0 }; srv->GateObject(kv.second.row, kv.second.model, z, z, false); }
    I.objs.clear();
    for(auto& kv : I.picks) { float z[3] = { 0, 0, 0 }; srv->GateObject(kv.second.o.row, kv.second.o.model, z, z, false); }
    I.picks.clear();
    for(int i = 0; i < kTdSlots; ++i) if(I.tdOwner[i] >= 0) { PktTextDraw t; memset(&t, 0, sizeof(t)); t.type = PKT_TEXTDRAW; t.tdId = (uint16_t)i; t.show = 0; srv->GateTextDraw(I.phoneSlot, t); I.tdOwner[i] = -1; }
    I.tdSlot.clear(); I.tdDef.clear();
    for(auto& kv : I.alabels) if(kv.second.shown && I.phoneSlot >= 0) srv->GateLabel(I.phoneSlot, kv.first, false, nullptr, 0, 0, "");   // 5.02.15
    I.alabels.clear(); I.vehPos.clear();
    I.money = 0; I.drunk = 0; I.statsAt = I.drunkAt = 0; memset(I.gunOk, 0, sizeof(I.gunOk)); I.lastArmour = 0;   // 5.02.15
    for(auto& kv : I.zones) { float f[4]; memcpy(f, kv.second.data(), sizeof(f)); char m[2] = { 4, 0 }; srv->GateOp(I.phoneSlot, SPO_ZONE, (uint8_t)(kv.first & 255), (uint8_t)(kv.first >> 8), 0, f, m); }
    I.zones.clear();
}

static void RemEnd(Impl& I, MPServer* srv, const char* why)
{
    if(I.phase == 9) return;
    char b[200]; snprintf(b, sizeof(b), "[open.mp] %s", why);
    if(I.phoneSlot >= 0) srv->GateMsg(I.phoneSlot, 0xFF6060FF, b);
    RemReset(I, srv);
    if(I.C) I.C->Disconnect(100);
    I.phase = 9; I.phaseAt = NowMs();
    SetStatus(I, "%s", why);
}

static void RemSetClient(Impl& I, bool dl)
{
    I.cliVer = dl ? kSampVersionDL : kSampVersion; I.cliVerStr = dl ? "0.3.DL-R1" : "0.3.7-R5";
}
static int32_t RemModel(Impl& I, int32_t m)
{   // 5.02.08: a 0.3.DL custom model (skin 20001+, object -1000..-30000) shows as its base model on the phone
    auto it = I.modelBase.find(m); return it != I.modelBase.end() ? it->second : m;
}
static Impl::Rp* RemPlayer(Impl& I, int id) { auto it = I.rp.find(id); return it == I.rp.end() ? nullptr : &it->second; }

static void RemGhostIn(Impl& I, MPServer* srv, int id, const float* pos, float ang)
{
    Impl::Rp* p = RemPlayer(I, id); if(!p) return;
    if(p->slot < 0) p->slot = srv->BridgeAdd(p->name.c_str(), p->color, p->skin);
    if(p->slot < 0) return;
    if(pos) { SyncData d; memset(&d, 0, sizeof(d)); memcpy(d.pos, pos, sizeof(d.pos)); d.heading = ang; d.health = 100; d.skin = p->skin; memcpy(d.aim, pos, sizeof(d.aim)); p->last = d; p->hasPos = true; srv->BridgeSync(p->slot, d); }
}

static int RemTdSlot(Impl& I, int wire, bool alloc)
{
    auto it = I.tdSlot.find(wire); if(it != I.tdSlot.end()) return it->second;
    if(!alloc) return -1;
    for(int i = 0; i < kTdSlots; ++i) if(I.tdOwner[i] < 0) { I.tdOwner[i] = wire; I.tdSlot[wire] = i; return i; }
    return -1;
}

// 5.02.13: the phone shows the PC server's class selection (< Spawn > like samp.exe) instead of MP's own spawn picker.
// Before, the MP picker spawned the phone at an MP spot while the PC server still had it in class selection / login,
// and the server's login camera pulled it straight back to the connection view.
static void RemClassSel(Impl& I, MPServer* srv, bool show, bool spawned)
{   // b = 1: hidden because the phone spawned (the phone's join screen ends then)
    if(I.phoneSlot < 0 || (I.classShown == show && !spawned)) return;
    I.classShown = show;
    srv->GateOp(I.phoneSlot, SPO_CLASSSEL, show ? 1 : 0, spawned ? 1 : 0, I.classIdx);
}

static void RemHold(Impl& I, const float* pos, uint64_t ms)
{   // 5.02.13: the PC server put us somewhere (spawn, SetPlayerPos). Until the phone's ped is really there, the sync
    // reports that spot - the old position for a few frames looked like a teleport hack (nex-ac & co kicked at spawn)
    memcpy(I.holdPos, pos, sizeof(I.holdPos)); I.holdUntil = NowMs() + ms;
}

static void RemSpawned(Impl& I, MPServer* srv)
{
    I.phase = 4; I.wasDead = false;
    const int s = I.phoneSlot;
    I.lastHealth = 100; I.lastArmour = 0;   // 5.02.15: what SA-MP's spawn gives
    memset(I.gunOk, 0, sizeof(I.gunOk));
    for(int i = 0; i < 3; ++i) if(I.spawn.have && I.spawn.w[i] < 64) I.gunOk[I.spawn.w[i]] = true;
    I.stableSince = NowMs();
    if(I.spawn.have)
    {
        RemHold(I, I.spawn.pos, 3000);
        srv->GateAct(s, CGA_SKIN, 0, 0, (int32_t)RemModel(I, (int32_t)I.spawn.skin));
        srv->GateAct(s, CGA_TELE, 255, 0, 0, I.spawn.pos, I.spawn.rot);
        srv->GateAct(s, CGA_DISARM);
        for(int i = 0; i < 3; ++i) if(I.spawn.w[i] > 0 && I.spawn.w[i] < 47) srv->GateAct(s, CGA_GUN, (int)I.spawn.w[i], 0, (int32_t)std::max<uint32_t>(I.spawn.ammo[i], 1));
    }
    RemClassSel(I, srv, false, true);   // 5.02.13: after the teleport - the phone's join screen lets go here
    BitStream o; CliRpc(I, 52, o);
}

static bool RdSpawn(BitStream& bs, Impl::Spawn& sp)
{
    uint8_t team, unk; if(!bs.Read(team) || !bs.Read(sp.skin) || !bs.Read(unk) || !RdVec(bs, sp.pos) || !bs.Read(sp.rot)) return false;
    for(int i = 0; i < 3; ++i) bs.Read(sp.w[i]); for(int i = 0; i < 3; ++i) bs.Read(sp.ammo[i]);
    sp.have = true; return true;
}

static void RemHandleRpc(SampGate* g, Impl& I, MPServer* srv, RpcIn& r)
{
    BitStream bs(r.data.data(), (unsigned)r.data.size(), false);
    const int s = I.phoneSlot;
    float v[3], w3[3]; uint8_t u8a = 0, u8b = 0; uint16_t u16a = 0; uint32_t u32a = 0, u32b = 0; int32_t i32 = 0; float fa = 0;
    static const bool dbg = getenv("MP_GATE_DEBUG") != nullptr;   // 5.02.05: host test, print every RPC from the PC server
    if(dbg) printf("[crossplay rpc] %d (%u bytes)\n", r.id, (unsigned)r.data.size());
    switch(r.id)
    {
        case 103:  // 5.02.05: ClientCheck (SendClientCheck). The PC 0.3.7 client always answers; scripts that get
        {          // no answer to 0x48 treat the player as an Android client / bot and many PC servers kick them.
            uint8_t type = 0; uint32_t addr = 0; uint16_t off = 0, cnt = 0;
            if(!bs.Read(type) || !bs.Read(addr)) break; bs.Read(off); bs.Read(cnt);
            if(dbg) printf("[crossplay] ClientCheck type 0x%02X addr 0x%08X off %u count %u\n", type, addr, off, cnt);
            BitStream o; o.Write(type); o.Write(addr); o.Write((uint8_t)0); CliRpc(I, 103, o);
            break;
        }
        case 179:  // 5.02.08: 0.3.DL ModelRequest - remember which base model each custom one replaces
        {
            uint32_t pool = 0, count = 0; uint8_t type = 0; int32_t vw = 0, base = 0, nid = 0;
            bs.Read(pool); bs.Read(count); bs.Read(type); bs.Read(vw); bs.Read(base); bs.Read(nid);
            if(nid) I.modelBase[nid] = base;
            if(dbg) printf("[crossplay] custom model %d -> base %d (type %u)\n", nid, base, type);
            break;
        }
        case 139:  // InitGame
        {
            if(I.resetPending) RemReset(I, srv);   // 5.02.13: reconnected - now the old ghosts / list go (the server sends them again)
            // 5.02.13: a new game (first join / reconnect): the old session's login dialog / textdraw login / spectate are gone.
            // They stayed set after a reconnect = no < Spawn > and no auto-spawn (the phone sat in class selection)
            if(I.dlgOpen) srv->GateDialog(s, -1, 0, "", "", "", "");
            if(I.tdSel) srv->GateOp(s, SPO_TDSELECT, 0, 0, 0);
            I.dlgOpen = false; I.tdSel = false; I.spect = false; I.noAuto = false; I.wantClass = false; I.autoSpawnN = 0; I.retrySpawnAt = 0;
            I.spawnAskAt = 0; I.finDl = false; I.tdShown = 0; I.classAt = NowMs();
            I.spawnAnsAt = 0; I.spawnSilent = 0; I.directSpawn = false;
            bool b; float f;
            for(int i = 0; i < 4; ++i) bs.Read(b); bs.Read(f); bs.Read(b); bs.Read(f); bs.Read(b); bs.Read(b); bs.Read(b);
            bs.Read(u32a); uint16_t id = 0; bs.Read(id); I.myId = id; I.gameAt = NowMs(); { float f4[4] = { 0, 0, 0, 0 }; srv->GateOp(s, SPO_PCLIST, 1, 1, (int32_t)id, f4, I.rName.c_str()); }   // 5.02.12: me in the player list
            bs.Read(b); bs.Read(u32a); uint8_t hour = 12, weather = 1; bs.Read(hour); bs.Read(weather);
            bs.Read(f); bs.Read(b); bs.Read(u32a); bs.Read(b); for(int i = 0; i < 5; ++i) bs.Read(u32a);
            std::string name = RdStr8(bs);
            {   // 5.02.09: some hostnames pad with spaces / 0xA0 / 0xF0 bytes (Horizon RP) - trim, then the code page -> UTF-8
                auto pad = [](unsigned char c) { return c == ' ' || c == 0xA0 || c == 0xF0 || c == '\t'; };
                size_t a = 0, b = name.size(); while(a < b && pad((unsigned char)name[a])) ++a; while(b > a && pad((unsigned char)name[b - 1])) --b;
                name = SampCp::ToUtf8(name.substr(a, b - a), I.cp);
            }
            { std::lock_guard<std::mutex> lk(I.sm); I.rServerName = name; }
            if(I.joinedFlag) *I.joinedFlag = true;
            I.phase = 3; I.phaseAt = NowMs();
            srv->GateOp(s, SPO_HELLO, 0, 0, 0, nullptr, name.c_str());
            srv->GateOp(s, SPO_TIME, hour, 0, weather);
            srv->GateAct(s, CGA_WEATHER, 0, 0, weather);
            char m[200]; snprintf(m, sizeof(m), "[open.mp] Connected to %s as %s (id %d).", name.c_str(), I.rName.c_str(), (int)id);
            srv->GateMsg(s, 0x88AA62FF, m);
            SetStatus(I, "On %s (%s:%u)", name.c_str(), I.rHost.c_str(), I.rPort);
            I.classIdx = 0; I.stableSince = NowMs();
            BitStream o; o.Write((uint16_t)0); CliRpc(I, 128, o);
            RemClassSel(I, srv, true);   // 5.02.13: < Spawn > on the phone
            break;
        }
        case 128:  // class answer
        {
            uint8_t sel = 0; bs.Read(sel); Impl::Spawn sp; if(RdSpawn(bs, sp)) I.spawn = sp;
            {   // 5.02.20: in mp_log.txt once per join - a server with no classes leaves Spawn unanswered
                static uint64_t said = 0; if(said != I.classAt) { said = I.classAt;
                    printf("[crossplay] class answer: selectable %u, skin %u, spawn %.0f %.0f %.0f\n", (unsigned)sel, (unsigned)I.spawn.skin, I.spawn.pos[0], I.spawn.pos[1], I.spawn.pos[2]); }
            }
            if(I.phase < 4)
            {
                const uint64_t now = NowMs();
                if(now - I.autoSpawnWin > 30000) { I.autoSpawnWin = now; I.autoSpawnN = 0; }
                (void)now; if(!I.spect && !I.dlgOpen && !I.tdSel && !I.noAuto) I.retrySpawnAt = NowMs() + 5000;   // 5 s: account loading, then the login dialog   // 5.02.10: wait - a login dialog usually comes right after the class answer
            }
            break;
        }
        case 129:  // spawn answer / SpawnPlayer: 0 = no, 1 = yes, 2 = SpawnPlayer()
            // 5.02.10: SA-MP 0.3.7 servers send 1 byte, open.mp 4 - reading 4 from a 1-byte answer kept an old
            // value, so a "not logged in yet" refusal was taken as a yes (spawn -> the server's guard -> loop)
            if(bs.GetNumberOfUnreadBits() >= 32) bs.Read(u32a); else { uint8_t b8 = 0; bs.Read(b8); u32a = b8; }
            I.spawnAnsAt = NowMs(); I.spawnSilent = 0;   // 5.02.20: the server answers Spawn
            printf("[crossplay] spawn answer %u (%s)\n", u32a, u32a == 2 ? "SpawnPlayer by the script" : u32a ? "allowed" : "refused by OnPlayerRequestSpawn");   // 5.02.17: always in mp_log.txt
            if(u32a) RemSpawned(I, srv);
            else
            {
                if(!I.dlgOpen && !I.tdSel && !I.spect) I.retrySpawnAt = NowMs() + 4000;
                const uint64_t n = NowMs();   // 5.02.17: say why nothing happened (the Spawn button looked dead)
                if(n - I.refuseMsgAt > 10000)
                {
                    I.refuseMsgAt = n;
                    srv->GateMsg(s, 0xA9C4E4FF, I.dlgOpen ? "[open.mp] The server refused Spawn - answer its window first."
                                                          : "[open.mp] The server refused Spawn (it wants a login first). Use its window / textdraws, or type its command in chat (/login password, /register...).");
                }
            }
            break;
        case 68: { Impl::Spawn sp; if(RdSpawn(bs, sp)) I.spawn = sp; break; }
        case 74:   // ForceClassSelection
            if(I.phase >= 4) I.wantClass = true;   // 5.02.09: the PC client only goes back on the next respawn / spectate off
            break;
        case 124:  // TogglePlayerSpectating
            bs.Read(u32a);
            printf("[crossplay] TogglePlayerSpectating %u\n", u32a);   // 5.02.17
            I.spect = u32a != 0;   // 5.02.10: spectating (account loading / login camera) = no Spawn button for a PC player either
            if(I.phase == 3) I.retrySpawnAt = I.spect ? 0 : NowMs() + 1500;
            if(!u32a && I.wantClass && I.phase >= 4) { I.wantClass = false; I.phase = 3; I.noAuto = true;
                srv->GateMsg(I.phoneSlot, 0xA9C4E4FF, "[open.mp] The server sent you back to class selection - log in (dialog or its /command) and you spawn after that."); BitStream o; o.Write((uint16_t)I.classIdx); CliRpc(I, 128, o); RemClassSel(I, srv, true); }
            break;
        case 137:  // player joined
        {
            bs.Read(u16a); bs.Read(u32a); bs.Read(u8a); std::string nm = RdStr8(bs);
            if(u16a == I.myId) break;
            Impl::Rp& p = I.rp[u16a]; p.name = nm; p.color = u32a;
            float f4[4] = { 0, 0, 0, 0 }; srv->GateOp(s, SPO_PCLIST, 1, 0, (int32_t)u16a, f4, nm.c_str());   // 5.02.12: the full player list
            break;
        }
        case 138: bs.Read(u16a); if(Impl::Rp* p = RemPlayer(I, u16a)) { if(p->slot >= 0) srv->BridgeRemove(p->slot); I.rp.erase(u16a); } I.pcScore.erase(u16a); srv->GateOp(s, SPO_PCLIST, 2, 0, (int32_t)u16a); break;
        case 155:  // 5.02.12: scores + pings (the answer to our RPC 155, like the PC client's Tab list)
        {
            uint16_t id; int32_t sc; uint32_t pg;
            while(bs.GetNumberOfUnreadBits() >= 80 && bs.Read(id) && bs.Read(sc) && bs.Read(pg))
            {
                auto& old = I.pcScore[id]; if(old.first == sc && old.second == pg && id != I.myId) continue;
                old = { sc, pg };
                const std::string nm = id == I.myId ? I.rName : (RemPlayer(I, id) ? RemPlayer(I, id)->name : std::string());
                if(nm.empty()) continue;
                float f4[4] = { (float)sc, (float)pg, 0, 0 }; srv->GateOp(s, SPO_PCLIST, 1, id == I.myId ? 1 : 0, (int32_t)id, f4, nm.c_str());
            }
            break;
        }
        case 32:   // stream in
        {
            bs.Read(u16a); bs.Read(u8a); bs.Read(u32a); RdVec(bs, v); bs.Read(fa); bs.Read(u32b);
            if(Impl::Rp* p = RemPlayer(I, u16a))
            {
                static const bool dbg = getenv("MP_GATE_DEBUG") != nullptr;
                if(dbg) printf("[crossplay] stream in player %d %s skin %u at %.1f %.1f %.1f\n", (int)u16a, p->name.c_str(), (unsigned)u32a, v[0], v[1], v[2]);
                p->skin = (uint16_t)RemModel(I, (int32_t)u32a); if(u32b) p->color = u32b; RemGhostIn(I, srv, u16a, v, fa);
            }
            break;
        }
        case 163: bs.Read(u16a); if(Impl::Rp* p = RemPlayer(I, u16a)) { if(p->slot >= 0) srv->BridgeRemove(p->slot); p->slot = -1; } break;
        case 153:  // skin
            bs.Read(u32a); bs.Read(u32b);
            u32b = (uint32_t)RemModel(I, (int32_t)u32b);
            if((int)u32a == I.myId) { srv->GateAct(s, CGA_SKIN, 0, 0, (int32_t)u32b); I.spawn.skin = u32b; }   // 5.02.20: kept for a direct spawn
            else if(Impl::Rp* p = RemPlayer(I, (int)u32a)) p->skin = (uint16_t)u32b;
            break;
        case 72: bs.Read(u16a); bs.Read(u32a); if(Impl::Rp* p = RemPlayer(I, u16a)) p->color = u32a; break;
        case 164:  // vehicle stream in (model for the drivers)
            bs.Read(u16a); bs.Read(u32a); I.vehModel[u16a] = (uint16_t)u32a;
            if(RdVec(bs, v)) { std::array<float, 3> vp = { v[0], v[1], v[2] }; I.vehPos[u16a] = vp; }   // 5.02.15: labels on parked cars
            break;
        case 165: bs.Read(u16a); I.vehModel.erase(u16a); I.vehPos.erase(u16a); break;
        case 93: { bs.Read(u32a); std::string t = SampCp::ToUtf8(RdStr32(bs), I.cp); srv->GateMsg(s, u32a, t.c_str()); break; }
        case 101:
        {
            bs.Read(u16a); std::string t = SampCp::ToUtf8(RdStr8(bs), I.cp);
            if(u16a == I.myId) { srv->GateChat(s, t.c_str()); break; }
            // 5.02.13: every PC chat line is a chat line on the phone. Before, a streamed-in player's line went out as
            // that ghost's own chat packet - dropped on the phone whenever the ghost was not (or no longer) in its player
            // list (stream in / out, a just-joined player), and a player missing from our list was dropped completely.
            static const uint32_t kPal[8] = { 0xFF8C13FF, 0xC715FFFF, 0x20B2AAFF, 0xDC143CFF, 0x6495EDFF, 0xF0E68CFF, 0x778899FF, 0xFF1493FF };
            Impl::Rp* p = RemPlayer(I, u16a);
            uint32_t c = p ? p->color : 0; if((c >> 8) == 0) c = kPal[u16a % 8];   // 5.02.11: colour 0 = SA-MP's palette
            std::string nm = p && !p->name.empty() ? p->name : ("Player " + std::to_string(u16a));
            std::string l = nm + ": {FFFFFF}" + t;   // like the PC chat: the name in the player's colour, the text white
            srv->GateMsg(s, c, l.c_str());
            if(p && p->slot >= 0) { float f[4] = { 30.0f, 8000.0f, 0, 0 }; srv->GateOp(-1, SPO_BUBBLE, 1, 0, p->slot, f, ("{FFFFFF}" + t).c_str()); }   // over the head
            break;
        }
        case 43:   // 5.02.11: RemoveBuildingForPlayer (map mods) -> the phone removes the same buildings
        {
            uint32_t model = 0; float r = 0; bs.Read(model); RdVec(bs, v); bs.Read(r);
            float f4[4] = { v[0], v[1], v[2], r }; srv->GateOp(s, SPO_REMOVEBLD, 0, 0, (int32_t)model, f4); break;
        }
        case 113:  // 5.02.11: SetPlayerAttachedObject (hats, bags, weapons on the back) - my own
        {
            bs.Read(u16a); uint32_t idx = 0; bool create = false; bs.Read(idx); bs.Read(create);
            if((int)u16a != I.myId || idx >= 10) break;
            if(!create) { srv->GateOp(s, SPO_ATTACH, (uint8_t)idx, 0, -1); break; }
            uint32_t model = 0, bone = 0; float off[3], rot[3], sc[3]; bs.Read(model); bs.Read(bone); RdVec(bs, off); RdVec(bs, rot); RdVec(bs, sc);
            char t[96]; snprintf(t, sizeof(t), "%.3f %.3f %.3f %.3f %.3f %.3f", rot[0], rot[1], rot[2], sc[0], sc[1], sc[2]);
            float f4[4] = { off[0], off[1], off[2], 0 };
            srv->GateOp(s, SPO_ATTACH, (uint8_t)idx, (uint8_t)bone, (int32_t)RemModel(I, (int32_t)model), f4, t); break;
        }
        case 86:   // 5.02.11: ApplyAnimation on me (sit, lean, handsup, job animations)
        {
            bs.Read(u16a); std::string lib = RdStr8(bs), an = RdStr8(bs);
            if((int)u16a == I.myId && !lib.empty() && !an.empty()) { std::string t = lib + " " + an; srv->GateAct(s, CGA_ANIM, 0, 0, 0, nullptr, 0, t.c_str()); }
            break;
        }
        case 95:   // 5.02.11: CreatePickup -> an object the phone sees; OnPlayerPickUpPickup when the phone walks in
        {
            int32_t id = 0, model = 0, type = 0; bs.Read(id); bs.Read(model); bs.Read(type); RdVec(bs, v);
            model = RemModel(I, model); if(model <= 0 || model > 65535) break;
            auto old = I.picks.find(id); if(old != I.picks.end()) { srv->GateObject(old->second.o.row, old->second.o.model, old->second.o.p, old->second.o.r, false); I.picks.erase(old); }
            Impl::Pick k; k.o.row = 0x5B000000u | ((uint32_t)id & 0xFFFF); k.o.model = (uint16_t)model; memcpy(k.o.p, v, sizeof(v)); k.o.r[0] = k.o.r[1] = k.o.r[2] = 0;
            I.picks[id] = k; srv->GateObject(k.o.row, k.o.model, k.o.p, k.o.r, true); break;
        }
        case 63:   // DestroyPickup
        {
            int32_t id = 0; bs.Read(id); auto it = I.picks.find(id); if(it == I.picks.end()) break;
            srv->GateObject(it->second.o.row, it->second.o.model, it->second.o.p, it->second.o.r, false); I.picks.erase(it); break;
        }
        case 11:   // 5.02.11: SetPlayerName (the name after login, e.g. Phone_Tester -> John_Doe) - chat lines use it
        {
            bs.Read(u16a); std::string nm = RdStr8(bs);
            if((int)u16a == I.myId) { if(!nm.empty()) I.rName = nm; }
            else if(Impl::Rp* p = RemPlayer(I, u16a)) p->name = nm;
            { auto it = I.pcScore.find(u16a); float f4[4] = { it != I.pcScore.end() ? (float)it->second.first : 0.f, it != I.pcScore.end() ? (float)it->second.second : 0.f, 0, 0 };
              srv->GateOp(s, SPO_PCLIST, 1, (int)u16a == I.myId ? 1 : 0, (int32_t)u16a, f4, nm.c_str()); }
            break;
        }
        case 12: case 13:
        {
            RdVec(bs, v); float cur[3], h = 0; int hp; srv->GatePos(s, cur, &h, &hp); RemHold(I, v, 2500);
            srv->GateAct(s, CGA_TELE, 255, 0, 0, v, h); break;
        }
        case 19: { bs.Read(fa); float cur[3], h = 0; int hp; if(srv->GatePos(s, cur, &h, &hp)) srv->GateAct(s, CGA_TELE, 255, 0, 0, cur, fa); break; }
        case 14: bs.Read(fa); srv->GateAct(s, CGA_HEALTH, 0, 0, (int32_t)fa); I.lastHealth = (int)fa; break;
        case 66: bs.Read(fa); srv->GateAct(s, CGA_ARMOUR, 0, 0, (int32_t)fa); I.lastArmour = (int)fa; break;
        case 22: bs.Read(u32a); bs.Read(u32b); srv->GateAct(s, CGA_GUN, (int)u32a, 0, (int32_t)u32b); if(u32a < 64) I.gunOk[u32a] = true; break;
        case 21: srv->GateAct(s, CGA_DISARM); memset(I.gunOk, 0, sizeof(I.gunOk)); break;
        case 35: bs.Read(i32); I.drunk = std::max<int32_t>(0, i32); I.drunkAt = NowMs(); I.statsAt = 0; break;   // 5.02.15: SetPlayerDrunkLevel
        case 67: bs.Read(u32a); srv->GateOp(s, SPO_WEAPON, 0, 0, (int32_t)u32a); break;
        case 145: bs.Read(u8a); bs.Read(u16a); srv->GateOp(s, SPO_WEAPON, 1, u8a, u16a); break;
        case 18: bs.Read(i32); srv->GateAct(s, CGA_MONEY, 0, 0, i32); I.money += i32; break;
        case 20: srv->GateAct(s, CGA_SETMONEY, 0, 0, 0); I.money = 0; break;
        case 133: bs.Read(u8a); srv->GateAct(s, CGA_WANTED, 0, 0, u8a); break;
        case 15: bs.Read(u8a); srv->GateOp(s, SPO_CONTROL, u8a ? 1 : 0, 0, 0); break;
        case 29: bs.Read(u8a); bs.Read(u8b); srv->GateOp(s, SPO_TIME, u8a, u8b, -1); break;
        case 152: bs.Read(u8a); srv->GateAct(s, CGA_WEATHER, 0, 0, u8a); break;
        case 156: bs.Read(u8a); srv->GateOp(s, SPO_WORLD, u8a, 0, 0); break;
        case 73:   // game text
        {
            bs.Read(u32a); bs.Read(u32b); std::string t = RdStr32(bs);
            srv->GateAct(s, CGA_GAMETEXT, (u32a == 1 || u32a == 2 || u32a == 0) ? 0 : 1, 0, (int32_t)std::min<uint32_t>(u32b, 20000), nullptr, 0, t.c_str());
            break;
        }
        case 61:   // dialog
        {
            int16_t id = -1; bs.Read(id); bs.Read(u8a); std::string cap = SampCp::ToUtf8(RdStr8(bs), I.cp), b1 = SampCp::ToUtf8(RdStr8(bs), I.cp), b2 = SampCp::ToUtf8(RdStr8(bs), I.cp), body = SampCp::ToUtf8(RdCompressed(bs), I.cp);
            I.dlgId = id; I.dlgStyle = u8a;
            I.dlgOpen = id >= 0; if(I.phase == 3) I.retrySpawnAt = I.dlgOpen ? 0 : NowMs() + 1500;   // 5.02.10: login first
            printf("[crossplay] dialog %d style %u \"%.60s\"\n", (int)id, (unsigned)u8a, cap.c_str());   // 5.02.17: always logged
            if(id < 0) srv->GateDialog(s, -1, 0, "", "", "", "");
            else srv->GateDialog(s, id, u8a, cap.c_str(), body.c_str(), b1.c_str(), b2.c_str());
            break;
        }
        case 134:  // show textdraw
        {
            uint16_t wire; uint8_t flags, shadow, outline, style, selectable; float lx, ly, tx, ty, px, py; uint32_t lc, bc, bg;
            bs.Read(wire); bs.Read(flags); bs.Read(lx); bs.Read(ly); bs.Read(lc); bs.Read(tx); bs.Read(ty); bs.Read(bc); bs.Read(shadow); bs.Read(outline); bs.Read(bg); bs.Read(style); bs.Read(selectable); bs.Read(px); bs.Read(py);
            uint16_t model; float rot[3], zoom; int16_t c1, c2; bs.Read(model); RdVec(bs, rot); bs.Read(zoom); bs.Read(c1); bs.Read(c2);
            std::string text = RdStr16(bs);
            int cs = RemTdSlot(I, wire, true); if(cs < 0) break;
            if(++I.tdShown <= 3 || I.tdShown % 50 == 0) printf("[crossplay] textdraw %u shown (%d so far)%s\n", (unsigned)wire, I.tdShown, selectable ? " clickable" : "");   // 5.02.17
            PktTextDraw t; memset(&t, 0, sizeof(t)); t.type = PKT_TEXTDRAW; t.show = 1; t.tdId = (uint16_t)cs;
            t.x = px; t.y = py; t.letterX = lx; t.letterY = ly; t.boxX = tx; t.color = Bswap(lc); t.boxColor = Bswap(bc); t.bgColor = Bswap(bg);
            // 5.02.15: open.mp / SA-MP flags = box | align << 1 | proportional << 4 with align 1 = left, 2 = right, 4 = centre
            // (was read as & 3: right came out centred and centred came out left - "the textdraws are not aligned")
            {
                const int al = (flags >> 1) & 7;
                t.align = (al & 4) ? 2 : (al & 2) ? 3 : 1;
            }
            t.outline = outline; t.shadow = shadow; t.useBox = flags & 1; t.proportional = (flags >> 4) & 1;
            t.font = style;
            // SA-MP: a centred textdraw's box width is TextSize Y (the PC client swaps them); the phone draws align 2 as
            // |boxX - x| wide, so boxX = x + width
            if(t.align == 2) t.boxX = px + fabsf(ty);
            t.pad[0] = selectable ? 1 : 0;
            if(style == 4 || style == 5) MP_CopyStr(t.text, text.c_str(), sizeof(t.text));   // sprite names stay as they are
            else MP_CopyStr(t.text, SampCp::ToUtf8(text, I.cp).c_str(), sizeof(t.text));   // 5.02.15: the server's codepage
            I.tdDef[wire] = t; srv->GateTextDraw(s, t);
            break;
        }
        case 135:
        {
            uint16_t wire; bs.Read(wire); int cs = RemTdSlot(I, wire, false); if(cs < 0) break;
            PktTextDraw t; memset(&t, 0, sizeof(t)); t.type = PKT_TEXTDRAW; t.tdId = (uint16_t)cs; t.show = 0; srv->GateTextDraw(s, t);
            I.tdOwner[cs] = -1; I.tdSlot.erase(wire); I.tdDef.erase(wire);
            break;
        }
        case 105:
        {
            uint16_t wire; bs.Read(wire); std::string text = RdStr16(bs);
            auto it = I.tdDef.find(wire); if(it == I.tdDef.end()) break;
            const bool spr = it->second.font == 4 || it->second.font == 5;
            MP_CopyStr(it->second.text, spr ? text.c_str() : SampCp::ToUtf8(text, I.cp).c_str(), sizeof(it->second.text)); srv->GateTextDraw(s, it->second);
            break;
        }
        case 83: { bool on = false; bs.Read(on); bs.Read(u32a); I.tdSel = on; printf("[crossplay] SelectTextDraw %s (%d textdraws shown)\n", on ? "on" : "off", I.tdShown); if(I.phase == 3) I.retrySpawnAt = on ? 0 : NowMs() + 1500; srv->GateOp(s, SPO_TDSELECT, on ? 1 : 0, 0, (int32_t)u32a); break; }
        case 107: { RdVec(bs, v); bs.Read(fa); float f[4] = { v[0], v[1], v[2], fa }; srv->GateOp(s, SPO_CHECKPOINT, 1, 0, 0, f); srv->GateAct(s, CGA_GPS, 0, 0, 0, v); break; }
        case 37: { float z[4] = { 0, 0, 0, 0 }; srv->GateOp(s, SPO_CHECKPOINT, 0, 0, 0, z); srv->GateAct(s, CGA_GPS, 0, 0, 0, z); break; }
        case 38: { bs.Read(u8a); RdVec(bs, v); RdVec(bs, w3); bs.Read(fa); float f[4] = { v[0], v[1], v[2], fa }; srv->GateOp(s, SPO_CHECKPOINT, 1, 1, 0, f); break; }
        case 39: { float z[4] = { 0, 0, 0, 0 }; srv->GateOp(s, SPO_CHECKPOINT, 0, 1, 0, z); break; }
        case 56:   // map icon
        {
            uint8_t id, type, style; bs.Read(id); RdVec(bs, v); bs.Read(type); bs.Read(u32a); bs.Read(style);
            if(id < 100 && type) { float f[4] = { v[0], v[1], v[2], 0 }; srv->GateOp(s, SPO_MAPICON, id, type, (int32_t)u32a, f); }
            break;
        }
        case 144: bs.Read(u8a); if(u8a < 100) srv->GateOp(s, SPO_MAPICON, u8a, 0, 0); break;
        case 108:  // gang zone
        {
            int16_t id; float f[4]; bs.Read(id); bs.Read(f[0]); bs.Read(f[1]); bs.Read(f[2]); bs.Read(f[3]); bs.Read(u32a);
            if(id < 0) break; I.zones[id] = { f[0], f[1], f[2], f[3] };
            char m[2] = { 1, 0 }; srv->GateOp(s, SPO_ZONE, (uint8_t)(id & 255), (uint8_t)(id >> 8), (int32_t)Bswap(u32a), f, m);
            break;
        }
        case 120: case 121: case 85:
        {
            int16_t id; bs.Read(id); if(r.id == 121) bs.Read(u32a);
            auto it = I.zones.find(id); if(it == I.zones.end()) break;
            float f[4]; memcpy(f, it->second.data(), sizeof(f));
            char m[2] = { (char)(r.id == 120 ? 4 : r.id == 121 ? 2 : 3), 0 };
            srv->GateOp(s, SPO_ZONE, (uint8_t)(id & 255), (uint8_t)(id >> 8), r.id == 121 ? (int32_t)Bswap(u32a) : 0, f, m);
            if(r.id == 120) I.zones.erase(it);
            break;
        }
        case 44:   // create object
        {
            uint16_t id, av = kInvalid, ao = kInvalid; int32_t model; float dd; uint8_t nocol;
            bs.Read(id); bs.Read(model); RdVec(bs, v); RdVec(bs, w3); bs.Read(dd); bs.Read(nocol); bs.Read(av); bs.Read(ao);
            model = RemModel(I, model);
            if(av != kInvalid || ao != kInvalid || model <= 0 || model > 65535) break;
            Impl::Obj o; o.row = 0x5A000000u | id; o.model = (uint16_t)model; memcpy(o.p, v, sizeof(v)); memcpy(o.r, w3, sizeof(w3));
            I.objs[id] = o; srv->GateObject(o.row, o.model, o.p, o.r, true);
            break;
        }
        case 47: { bs.Read(u16a); auto it = I.objs.find(u16a); if(it == I.objs.end()) break; srv->GateObject(it->second.row, it->second.model, it->second.p, it->second.r, false); I.objs.erase(it); break; }
        case 45: case 99:
        {
            bs.Read(u16a); RdVec(bs, v); auto it = I.objs.find(u16a); if(it == I.objs.end()) break;
            if(r.id == 99) { float to[3], sp, tr[3]; RdVec(bs, to); bs.Read(sp); if(RdVec(bs, tr) && tr[0] > -999.0f) memcpy(it->second.r, tr, sizeof(tr)); memcpy(v, to, sizeof(v)); }
            memcpy(it->second.p, v, sizeof(v)); srv->GateObject(it->second.row, it->second.model, it->second.p, it->second.r, true);
            break;
        }
        case 36:   // 3D text label
        {
            uint16_t id, ap, avh; uint8_t los; bs.Read(id); bs.Read(u32a); RdVec(bs, v); bs.Read(fa); bs.Read(los); bs.Read(ap); bs.Read(avh);
            std::string t = SampCp::ToUtf8(RdCompressed(bs), I.cp);   // 5.02.15: the server's codepage (was raw bytes)
            I.alabels.erase(id);
            if(ap != kInvalid || avh != kInvalid)
            {   // 5.02.15: labels on a player / vehicle ("[AFK]", "For sale", taxi signs): RemTick moves them with it
                Impl::ALabel& a = I.alabels[id]; a.ap = ap; a.avh = avh; memcpy(a.off, v, sizeof(a.off)); a.dist = fa; a.color = u32a; a.text = t;
                break;
            }
            srv->GateLabel(s, id, true, v, fa, u32a, t.c_str());
            break;
        }
        case 58: bs.Read(u16a); I.alabels.erase(u16a); srv->GateLabel(s, u16a, false, nullptr, 0, 0, ""); break;
        case 157: RdVec(bs, v); srv->GateOp(s, SPO_CAMERA, 1, 0, 0, v); break;
        case 158: RdVec(bs, v); srv->GateOp(s, SPO_CAMERA, 2, 0, 0, v); break;
        case 162: srv->GateOp(s, SPO_CAMERA, 0, 0, 0); break;
        case 82: { bool pos = false; bs.Read(pos); RdVec(bs, v); RdVec(bs, w3); srv->GateOp(s, SPO_CAMERA, pos ? 1 : 2, 0, 0, w3); break; }
        case 16: bs.Read(u32a); RdVec(bs, v); srv->GateOp(s, SPO_SOUND, 0, 0, (int32_t)u32a, v); break;
        case 41: { std::string url = RdStr8(bs); RdVec(bs, v); bs.Read(fa); bs.Read(u8a); float f[4] = { v[0], v[1], v[2], fa }; srv->GateOp(s, SPO_AUDIO, 1, u8a ? 1 : 0, 0, f, url.c_str()); break; }
        case 42: srv->GateOp(s, SPO_AUDIO, 0, 0, 0); break;
        case 90: RdVec(bs, v); srv->GateOp(s, SPO_VELOCITY, 0, 0, 0, v); break;
        case 91: bs.Read(u8a); RdVec(bs, v); srv->GateOp(s, SPO_VELOCITY, 0, 1, 0, v); break;
        case 59:   // chat bubble
        {
            bs.Read(u16a); bs.Read(u32a); bs.Read(fa); bs.Read(u32b); std::string t = RdStr8(bs);
            Impl::Rp* p = RemPlayer(I, u16a); int slot = u16a == I.myId ? s : (p ? p->slot : -1); if(slot < 0) break;
            char c[16]; snprintf(c, sizeof(c), "{%06X}", (unsigned)((u32a >> 8) & 0xFFFFFF));
            float f[4] = { fa, (float)u32b, 0, 0 }; srv->GateOp(-1, SPO_BUBBLE, 1, 0, slot, f, (std::string(c) + t).c_str());
            break;
        }
        default: break;
    }
}

static void RemFootIn(Impl& I, MPServer* srv, Packet* pk, bool vehicle)
{
    BitStream bs(pk->data, pk->length, false); bs.IgnoreBits(8);
    uint16_t id; if(!bs.Read(id)) return;
    Impl::Rp* p = RemPlayer(I, id); if(!p) return;
    {   // 5.02.11 test: MP_GATE_DEBUG prints the first movement of each PC player (two gates on one server = "PC sees phone")
        static const bool dbg = getenv("MP_GATE_DEBUG") != nullptr; static std::map<int, int> seen;
        if(dbg && seen[id]++ == 0) printf("[crossplay] movement from player %d %s\n", (int)id, p->name.c_str());
    }
    if(p->slot < 0) RemGhostIn(I, srv, id, nullptr, 0);
    if(p->slot < 0) return;
    SyncData d;
    if(!vehicle)
    {
        bool has; uint16_t lr = 0, ud = 0, keys = 0; float pos[3], q[4], vel[3]; uint8_t ha = 0, wp = 0, sa = 0;
        bs.Read(has); if(has) bs.Read(lr); bs.Read(has); if(has) bs.Read(ud);
        if(!bs.Read(keys) || !RdVec(bs, pos) || !bs.ReadNormQuat(q[0], q[1], q[2], q[3])) return;
        bs.Read(ha); bs.Read(wp); bs.Read(sa); RdCompVec(bs, vel);
        int h, a; UnpackHA(ha, h, a);
        d = FootToSync(pos, QuatHeading(q[0], q[1], q[2], q[3]), vel, h, a, wp, keys, p->skin);
        if(sa == 1) d.flags |= SYNC_DUCKING;
        if(ud || lr) d.speed = std::max(d.speed, 3.0f);
        p->vehicle = -1;
    }
    else
    {
        uint16_t vid, lr, ud, keys, vhp; float q[4], pos[3], vel[3]; uint8_t ha, wp;
        if(!bs.Read(vid) || !bs.Read(lr) || !bs.Read(ud) || !bs.Read(keys) || !bs.ReadNormQuat(q[0], q[1], q[2], q[3]) || !RdVec(bs, pos)) return;
        RdCompVec(bs, vel); bs.Read(vhp); bs.Read(ha); bs.Read(wp);
        int h, a; UnpackHA(ha, h, a);
        float hd = QuatHeading(q[0], q[1], q[2], q[3]);
        d = FootToSync(pos, hd, vel, h, a, wp, 0, p->skin);
        auto it = I.vehModel.find(vid);
        d.carModel = it != I.vehModel.end() ? it->second : 0;
        if(d.carModel) { d.flags |= SYNC_IN_CAR | SYNC_DRIVER; memcpy(d.carPos, pos, sizeof(d.carPos)); d.carHeading = hd; }
        { std::array<float, 3> vp = { pos[0], pos[1], pos[2] }; I.vehPos[vid] = vp; }
        p->vehicle = vid;
    }
    p->last = d; p->hasPos = true;
    srv->BridgeSync(p->slot, d);
}

static void RemSendFoot(Impl& I, const SyncData& d)
{   // the phone's movement -> PC server (client format on-foot sync; in a car = on foot at the car)
    const bool car = (d.flags & SYNC_IN_CAR) != 0;
    const float* pos = car ? d.carPos : d.pos; float h = car ? d.carHeading : d.heading;
    if(I.holdUntil)
    {   // 5.02.13: spawn / SetPlayerPos - report the server's spot until the phone's ped got there (no teleport-hack kick)
        const float dx = pos[0] - I.holdPos[0], dy = pos[1] - I.holdPos[1], dz = pos[2] - I.holdPos[2];
        if(dx * dx + dy * dy + dz * dz < 9.0f || NowMs() > I.holdUntil) I.holdUntil = 0; else pos = I.holdPos;
    }
    float q[4]; HeadingQuat(h, q);
    float a = h * kPi / 180.0f, sp = car ? 0.0f : d.speed / 50.0f;
    uint32_t extra = NowMs() < I.keysUntil ? I.heldKeys : 0;
    BitStream o; o.Write((uint8_t)kPlayerSync);
    o.Write((uint16_t)0); o.Write((uint16_t)((!car && d.speed > 0.3f) ? 0xFF80 : 0));
    o.Write((uint16_t)(KeysFromFlags(d.flags) | (extra & 0xFFFF)));
    WrVec(o, pos); o.Write(q[0]); o.Write(q[1]); o.Write(q[2]); o.Write(q[3]);
    // 5.02.15: report like the PC client after the server's own SetPlayerHealth / Armour / GivePlayerWeapon: health
    // and armour never above what the server gave (MP / CLEO regeneration = Nex-AC health hack kick), a weapon the
    // server didn't give (the phone's own) as fists (weapon hack kick)
    const int hp = std::max(0, std::min<int>(d.health, std::max(1, I.lastHealth)));
    const int ar = std::max(0, std::min<int>(d.armour, I.lastArmour));
    if(hp < I.lastHealth) I.lastHealth = hp;
    if(ar < I.lastArmour) I.lastArmour = ar;
    const int wp = (d.weapon < 64 && (d.weapon == 0 || I.gunOk[d.weapon])) ? d.weapon : 0;
    o.Write((uint8_t)std::min<int>(hp, 255)); o.Write((uint8_t)std::min<int>(ar, 255));
    // 5.02.15: the 2 high bits are a value, not bits (open.mp player_pool.hpp: 1 = KEY_YES, 2 = KEY_NO, 3 = KEY_CTRL_BACK).
    // Before, H (262144) came out as 0 and Y + N together as CTRL_BACK.
    const uint8_t addKey = (extra & 262144) ? 3 : (extra & 131072) ? 2 : (extra & 65536) ? 1 : 0;
    o.Write((uint8_t)((wp & 63) | (addKey << 6)));
    o.Write((uint8_t)((d.flags & SYNC_DUCKING) ? 1 : 0));
    float vel[3] = { -sinf(a) * sp, cosf(a) * sp, 0 }; WrVec(o, vel);
    float z[3] = { 0, 0, 0 }; WrVec(o, z); o.Write((uint16_t)0); o.Write((uint16_t)0); o.Write((uint16_t)0);
    I.C->Send(&o, HIGH_PRIORITY, UNRELIABLE_SEQUENCED, 1);
}

// 5.02.06: "The PC server did not answer" came up for busy servers that just drop the first connection request (their
// flood guard) - the client now tries 3 times (a new socket each time) before the red message
// 5.02.12: connect the way samp.exe / the open.mp launcher do: the browser query first ('i', 'r', 'c', 'p'), then the
// RakNet connect. DDoS filters on SA-MP hosts (OVH game, path.net, ServerProject Umbrella...) only let in an IP that
// queried the server first; the retries used to connect without it, so their second try was always dropped.
static bool RemConnect(Impl& I)
{
    SampQueryInfo qi; SampGate::Query(I.rHost.c_str(), I.rPort, qi, 700, true);
    {   // 'p' ping (4 random bytes, echoed back) - the launcher sends it every few seconds
        uint32_t ip = 0;
        if(Resolve(I.rHost.c_str(), &ip))
        {
            int sk = (int)socket(AF_INET, SOCK_DGRAM, 0);
            if(sk >= 0)
            {
                sockaddr_in sa; memset(&sa, 0, sizeof(sa)); sa.sin_family = AF_INET; sa.sin_port = htons(I.rPort); sa.sin_addr.s_addr = ip;
                uint8_t q[15] = { 'S', 'A', 'M', 'P' }; memcpy(q + 4, &ip, 4); q[8] = (uint8_t)(I.rPort & 255); q[9] = (uint8_t)(I.rPort >> 8); q[10] = 'p';
                const uint32_t r = (uint32_t)NowMs() * 2654435761u; memcpy(q + 11, &r, 4);
                sendto(sk, (const char*)q, 15, 0, (sockaddr*)&sa, sizeof(sa));
#ifdef _WIN32
                closesocket(sk);
#else
                close(sk);
#endif
            }
        }
    }
    I.C->SetPassword(I.rPass.c_str());
    return SAMPRakNet::ClientOmpReset(), I.C->Connect(I.rHost.c_str(), I.rPort, 0, 0, 5);
}

static bool RemRetry(Impl& I, MPServer* srv, uint64_t now)
{
    // 5.02.15: 5 tries, 2 / 4 / 6 / 8 s apart (was 3 tries straight after each other). Busy servers behind a flood
    // filter (Brasil Roleplay: 600+ players) ignore connects that come too fast; each try asks the server info
    // again first (RemConnect), like the launchers' server list does before a join.
    if(I.tries >= 4) return false;
    ++I.tries;
    printf("[crossplay] no answer yet - connect try %d of 5\n", I.tries + 1);
    char m[160]; snprintf(m, sizeof(m), "[open.mp] No answer yet (busy server), trying again in %.1f s (%d of 5)...", 1.5 * I.tries, I.tries + 1);
    srv->GateMsg(I.phoneSlot, 0xA9C4E4FF, m);
    I.C->Disconnect(100);
    I.phase = 8; I.rejoinAt = now + 1500ull * (uint64_t)I.tries;   // 5.02.17: 1.5 / 3 / 4.5 / 6 s (flood filters still see a normal client)   // phase 8 connects again (RemConnect) when the time is up
    return true;
}

// 5.02.15: labels attached to a player / vehicle follow it (sent again when it moved 0.5 m, hidden when it is gone)
static void RemLabelTick(Impl& I, MPServer* srv, uint64_t now)
{
    if(I.alabels.empty() || now < I.labelTickAt || I.phoneSlot < 0) return;
    I.labelTickAt = now + 150;
    for(auto& kv : I.alabels)
    {
        Impl::ALabel& a = kv.second;
        bool have = false; float at[3] = { 0, 0, 0 };
        if(a.ap != 0xFFFF && (int)a.ap != I.myId)
        {
            Impl::Rp* p = RemPlayer(I, a.ap);
            if(p && p->hasPos) { have = true; for(int k = 0; k < 3; ++k) at[k] = p->last.pos[k] + a.off[k]; }
        }
        else if(a.avh != 0xFFFF)
        {
            auto it = I.vehPos.find(a.avh);
            if(it != I.vehPos.end()) { have = true; for(int k = 0; k < 3; ++k) at[k] = it->second[k] + a.off[k]; }
        }
        if(!have) { if(a.shown) { srv->GateLabel(I.phoneSlot, kv.first, false, nullptr, 0, 0, ""); a.shown = false; } continue; }
        const float dx = at[0] - a.at[0], dy = at[1] - a.at[1], dz = at[2] - a.at[2];
        if(a.shown && dx * dx + dy * dy + dz * dz < 0.25f) continue;
        memcpy(a.at, at, sizeof(a.at)); a.shown = true;
        srv->GateLabel(I.phoneSlot, kv.first, true, at, a.dist, a.color, a.text.c_str());
    }
}

static void RemTick(SampGate* g, Impl& I, MPServer* srv)
{
    const uint64_t now = NowMs();
    std::vector<MPServer::BridgePeer> peers;
    if(I.phase == 0)
    {
        srv->BridgePeers(peers);
        if(peers.empty()) { if(now - I.phaseAt > 60000) { I.phase = 9; SetStatus(I, "No phone player joined"); } return; }
        I.phoneSlot = peers[0].slot; I.rName = SampGate::SampName(I.forceName.empty() ? peers[0].name : I.forceName.c_str());   // 5.02.12: Settings > Name on PC servers
        SetStatus(I, "Connecting to %s:%u as %s...", I.rHost.c_str(), I.rPort, I.rName.c_str());
        char m[200]; snprintf(m, sizeof(m), "[open.mp] Connecting to %s:%u as %s...", I.rHost.c_str(), I.rPort, I.rName.c_str());
        srv->GateMsg(I.phoneSlot, 0xA9C4E4FF, m);
        {   // 5.02.05: the server's language picks the codepage its chat / dialogs are written in (MP_GATE_CP=1251 forces one)
            SampQueryInfo qi; I.cp = 1252;
            if(SampGate::Query(I.rHost.c_str(), I.rPort, qi, 1200)) I.cp = SampCp::ForLanguage(qi.language, qi.hostname);
            const bool dl = qi.version.find("DL") != std::string::npos || qi.version.find("dl") != std::string::npos;
            RemSetClient(I, dl);
            if(const char* c = getenv("MP_GATE_CLIENT")) { if(!strcmp(c, "dl")) RemSetClient(I, true); else if(!strcmp(c, "r5")) RemSetClient(I, false); else if(!strcmp(c, "r2")) { RemSetClient(I, false); I.cliVerStr = "0.3.7-R2"; } }
            if(const char* f = getenv("MP_GATE_CP")) if(atoi(f) > 0) I.cp = atoi(f);
            printf("[crossplay] text codepage %d (language \"%s\")\n", I.cp, qi.language.c_str());
        }
        if(!RemConnect(I)) { RemEnd(I, srv, "Could not start the connection."); return; }
        I.phase = 1; I.phaseAt = now;
        return;
    }
    if(I.phase == 9) return;
    if(I.phase == 8)
    {   // 5.02.12: waiting to join again after a ConnectionRejected (name in use / no slot)
        if(now < I.rejoinAt) return;
        if(!RemConnect(I)) { RemEnd(I, srv, "Could not start the connection."); return; }
        I.phase = 1; I.phaseAt = now; return;
    }
    if(I.phase == 1 && now - I.phaseAt > 7000 && RemRetry(I, srv, now)) return;   // 5.02.17: 7 s per try (was 12): a full 5-try join takes ~40 s, not ~75
    if(I.phase == 2 && now - I.phaseAt > 60000 && !getenv("MP_GATE_NOJOINTIMEOUT"))
    {   // 5.02.12: connected but the game never starts (no InitGame) - the server waits for its own client's handshake
        // (Rio Rise and other "own app" servers: they list as omp but only let their app in) or a login queue
        RemEnd(I, srv, "The PC server connected but never started the game (1 min). It only lets in its own app / launcher (for example Rio Rise), or it has a join queue."); return;
    }
    if(I.phase == 1 && now - I.phaseAt > 15000) { RemEnd(I, srv, "The PC server did not answer (closed, wrong port or not SA-MP 0.3.7 / open.mp)."); return; }
    if(I.phase >= 3) RemLabelTick(I, srv, now);   // 5.02.15

    for(Packet* pk = I.C->Receive(); pk; I.C->DeallocatePacket(pk), pk = I.C->Receive())
    {
        if(!pk->length) continue;
        switch(pk->data[0])
        {
            case ID_AUTH_KEY:
            {
                char ch[64] = { 0 }; int n = pk->length > 1 ? std::min<int>(pk->data[1], std::min<int>(63, (int)pk->length - 2)) : 0; if(n > 0) memcpy(ch, pk->data + 2, (size_t)n);
                const char* resp = SAMPRakNet::AuthResponse(ch);
                BitStream o; o.Write((uint8_t)ID_AUTH_KEY); uint8_t l = resp ? (uint8_t)strlen(resp) : 0; o.Write(l); if(l) o.Write(resp, l);
                I.C->Send(&o, SYSTEM_PRIORITY, RELIABLE, 0);
                break;
            }
            case ID_CONNECTION_REQUEST_ACCEPTED:
            {
                BitStream in(pk->data, pk->length, false); in.IgnoreBits(8 + 32 + 16 + 16); uint32_t chal = 0; in.Read(chal);
                BitStream o; o.Write(I.cliVer); o.Write((uint8_t)1); WrStr8(o, I.rName); o.Write((uint32_t)(chal ^ I.cliVer));
                WrStr8(o, Gpci()); WrStr8(o, I.cliVerStr.c_str());
                printf("[crossplay] joining as SA-MP %s (%u)%s\n", I.cliVerStr.c_str(), I.cliVer, SAMPRakNet::ClientOmp() ? " + open.mp launcher" : "");
                // 5.02.07: what the open.mp launcher's client adds: its omp version (IsPlayerUsingOfficialClient = true
                // on open.mp; SA-MP 0.3.7 servers ignore the extra bytes). Off: MP_GATE_OMP=0
                if(SAMPRakNet::ClientOmp()) o.Write((uint32_t)SAMPRakNet::CURRENT_OMP_CLIENT_MOD_VERSION);
                if(!getenv("MP_GATE_NOSV"))
                {   // 5.02.11: the SampVoice client plugin's handshake, so "voice plugin not detected" servers let phones in
                    // (SvGetVersion / SvHasMicro). SampVoice 3.1 looks for EF BE AD DE + version 11 + micro anywhere in
                    // ClientJoin; 4.x for DE AD C0 DE + length + version (big endian 4.2.0) + micro, searching from the end.
                    static const uint8_t kSv[] = { 0xEF, 0xBE, 0xAD, 0xDE, 11, 1,   0xDE, 0xAD, 0xC0, 0xDE, 5, 4, 2, 0, 0, 1 };
                    o.Write((const char*)kSv, sizeof(kSv));
                }
                I.C->RPC((RPCID)25, &o, HIGH_PRIORITY, RELIABLE, 0, false, UNASSIGNED_NETWORK_ID, nullptr);
                // 5.02.13: 10 s without an ack = "lost connection" was too short for phones on mobile data (cell handover,
                // the screen turning off, a busy Wi-Fi); the PC server keeps the player longer than that anyway
                I.C->SetTimeoutTime(25000);
                I.phase = 2; I.phaseAt = now;
                break;
            }
            case ID_CONNECTION_ATTEMPT_FAILED: if(!RemRetry(I, srv, now)) RemEnd(I, srv, "The PC server did not answer."); break;
            case ID_NO_FREE_INCOMING_CONNECTIONS: RemEnd(I, srv, "The PC server is full."); break;
            case ID_CONNECTION_BANNED: RemEnd(I, srv, "You are banned from this PC server."); break;
            case ID_INVALID_PASSWORD: RemEnd(I, srv, "Wrong server password."); break;
            case ID_DISCONNECTION_NOTIFICATION:
            {
                // 5.02.12: RPC 130 ConnectionRejected comes right before - 1 version, 2 name (in use / not allowed),
                // 3 mod, 4 no player slot. It was not read, so every one of them looked like "kicked / server restart".
                int why = -1;
                { std::lock_guard<std::mutex> lk(I.qm); for(auto& q : I.rpcs) if(q.id == 130 && !q.data.empty()) why = q.data[0]; }
                if(why >= 0) printf("[crossplay] the PC server rejected the join (reason %d)\n", why);
                if(I.phase == 2 && (why == 2 || why == 4) && I.rejectN < 2)
                {   // the name is still taken by our last connection (a phone that lost the network comes back before the
                    // server dropped the old one, or two joins from one IP) / no slot: wait and join again
                    ++I.rejectN; const int sec = why == 2 ? 15 : 10;
                    char m[220];
                    if(why == 2) snprintf(m, sizeof(m), "[open.mp] The server says the name %s is in use (your last connection may still be on it). Joining again in %d s...", I.rName.c_str(), sec);
                    else snprintf(m, sizeof(m), "[open.mp] The server had no free player slot. Joining again in %d s...", sec);
                    srv->GateMsg(I.phoneSlot, 0xA9C4E4FF, m); printf("%s\n", m);
                    { std::lock_guard<std::mutex> lk(I.qm); I.rpcs.clear(); }
                    I.C->Disconnect(100); I.phase = 8; I.rejoinAt = now + (uint64_t)sec * 1000; break;
                }
                if(I.phase == 2 && why == 2)
                {
                    char m[220]; snprintf(m, sizeof(m), "The server refused the name %s (in use, or not allowed - RP servers want Firstname_Lastname). Change it in Settings > Name.", I.rName.c_str());
                    RemEnd(I, srv, m); break;
                }
                if(I.phase == 2 && why == 4) { RemEnd(I, srv, "The PC server has no free player slot right now."); break; }
                if(I.phase == 2 && why < 0 && I.rejectN == 0)
                {   // dropped at the join without a reason: most often our last connection from this IP is still on the server
                    // (Horizon RP and others drop a second join from one IP) - wait for it to time out, same version first
                    ++I.rejectN;
                    srv->GateMsg(I.phoneSlot, 0xA9C4E4FF, "[open.mp] The server dropped the join (your last connection may still be on it). Joining again in 12 s...");
                    printf("[crossplay] dropped at the join - again in 12 s\n");
                    { std::lock_guard<std::mutex> lk(I.qm); I.rpcs.clear(); }
                    I.C->Disconnect(100); I.phase = 8; I.rejoinAt = now + 12000; break;
                }
                if(I.phase >= 3)
                {   // 5.02.12: in the game this is a kick (a restart drops the link = ID_CONNECTION_LOST, which reconnects);
                    // the PC client does not rejoin either. The gamemode's last chat line says why.
                    RemEnd(I, srv, (I.gameAt && now - I.gameAt < 60000) ? "The PC server kicked you soon after the join (its own check: account, name, anti-cheat...). See its last chat message."
                                                                        : "The PC server closed the connection (Server closed the connection / kicked)."); break;
                }
                if(I.phase == 2 && !I.verSwapped && !getenv("MP_GATE_CLIENT"))
                {   // 5.02.08: kicked at the join (before the game start): try the other SA-MP client version once
                    I.verSwapped = true; RemSetClient(I, I.cliVer != kSampVersionDL);
                    printf("[crossplay] kicked at the join - trying again as SA-MP %s\n", I.cliVerStr.c_str());
                    char m[160]; snprintf(m, sizeof(m), "[open.mp] Trying again as SA-MP %s...", I.cliVerStr.c_str()); srv->GateMsg(I.phoneSlot, 0xA9C4E4FF, m);
                    I.C->Disconnect(100);
                    if(RemConnect(I)) { I.phase = 1; I.phaseAt = now; break; }
                }
                RemEnd(I, srv, "The PC server closed the connection (kicked / server restart)."); break;
            }
            case ID_CONNECTION_LOST:
                if(I.phase >= 2 && I.lostN < 10 && !getenv("MP_GATE_NORECONNECT"))
                {   // 5.02.12: like the PC client ("Lost connection to the server. Reconnecting..."): a phone on mobile data
                    // loses packets for a few seconds all the time
                    // 5.02.13: 10 tries (3 before; the count goes back to 0 after 3 stable minutes), 3 s / 6 s / ... max 30 s
                    // apart, and the PC players / list stay on screen until the server sends them again (they blinked away)
                    ++I.lostN; I.resetPending = true; RemClassSel(I, srv, false);
                    char m[200]; snprintf(m, sizeof(m), "[open.mp] Lost connection to the server. Reconnecting (%d of 10)...", I.lostN);
                    srv->GateMsg(I.phoneSlot, 0xA9C4E4FF, m); printf("%s\n", m);
                    { std::lock_guard<std::mutex> lk(I.qm); I.rpcs.clear(); }
                    I.C->Disconnect(100); I.phase = 8; I.rejoinAt = now + std::min<uint64_t>(3000ull * (uint64_t)I.lostN, 30000ull); I.rejectN = 0; I.tries = 0; break;
                }
                RemEnd(I, srv, "Lost the connection to the PC server."); break;
            case kPlayerSync: RemFootIn(I, srv, pk, false); break;
            case kVehicleSync: RemFootIn(I, srv, pk, true); break;
            default: break;
        }
        if(I.phase == 9) { I.C->DeallocatePacket(pk); return; }
    }

    std::deque<RpcIn> q; std::deque<HookEv> hk;
    { std::lock_guard<std::mutex> lk(I.qm); q.swap(I.rpcs); hk.swap(I.hooks); }
    for(auto& r : q) RemHandleRpc(g, I, srv, r);

    // the phone -> PC server
    for(auto& e : hk)
    {
        if(I.phase < 3 || e.slot != I.phoneSlot) continue;
        if(e.kind == HK_CHAT || (e.kind == HK_SAMP && e.op == SPO_CMD))
        {
            std::string t = SampCp::FromUtf8(e.text, I.cp); if(t.empty()) continue;
            if(t[0] == '/') { BitStream o; WrStr32(o, t); CliRpc(I, 50, o); if(I.phase == 3) { I.noAuto = false; I.autoSpawnN = 0; I.retrySpawnAt = now + 2500; } }   // 5.02.10: /login etc.
            else { BitStream o; WrStr8(o, t.substr(0, 144)); CliRpc(I, 101, o); }
        }
        else if(e.kind == HK_SAMP && e.op == SPO_TDCLICK)
        {
            uint16_t wire = kInvalid; if(e.val >= 0 && e.val < kTdSlots && I.tdOwner[e.val] >= 0) wire = (uint16_t)I.tdOwner[e.val];
            BitStream o; o.Write(wire); CliRpc(I, 83, o);
            if(I.phase == 3) { I.noAuto = false; I.autoSpawnN = 0; I.retrySpawnAt = now + 1500; }   // 5.02.10: a textdraw login / spawn button
        }
        else if(e.kind == HK_SAMP && e.op == SPO_KEYS)
        {   // 5.02.20: a new key (ALT, F, H, Y, N...) goes out at once in the next on-foot packet, not up to 33 ms later
            if((uint32_t)e.val != I.heldKeys) I.lastFoot = 0;
            I.heldKeys = (uint32_t)e.val; I.keysUntil = now + 400;
            if(e.val && I.phase < 4 && now >= I.keysHintAt)
            {   // the PC server only reads keys from a spawned player's sync - tell the phone why nothing happens yet
                I.keysHintAt = now + 15000;
                srv->GateMsg(I.phoneSlot, 0xA9C4E4FF, "[open.mp] Keys reach the PC server after you spawn (TAB works now: it is the phone's own player list).");
            }
        }
        else if(e.kind == HK_SAMP && e.op == SPO_SPAWNREQ && I.phase == 3)
        {   // 5.02.13: the phone's < Spawn > (a = 0) / < > class (a = 1 previous, 2 next) - RequestSpawn / RequestClass
            if(e.a == 0)
            {   // 5.02.17: one RequestSpawn until the server answers (2 s at most) - 20 taps in 5 s flooded the server (Lost connection)
                if(I.spawnAskAt && now - I.spawnAskAt < 1500) continue;   // also after an answer: a tap every 1.5 s at most
                if(I.dlgOpen) { if(now - I.refuseMsgAt > 5000) { I.refuseMsgAt = now; srv->GateMsg(I.phoneSlot, 0xA9C4E4FF, "[open.mp] Answer the server's window first (login / register) - then Spawn."); } continue; }
                I.spawnAskAt = now;
                BitStream o; CliRpc(I, 129, o); I.noAuto = false; I.autoSpawnN = 0; I.retrySpawnAt = 0;
                printf("[crossplay] spawn button (dialog %d, select %d, spectating %d)\n", (int)I.dlgOpen, (int)I.tdSel, (int)I.spect);
            }
            else { I.classIdx = std::max(0, I.classIdx + (e.a == 1 ? -1 : 1)); BitStream o; o.Write((uint16_t)I.classIdx); CliRpc(I, 128, o); }
        }
        else if(e.kind == HK_DIALOG)
        {
            // 5.02.17: open.mp drops the answer of a msgbox / input / password dialog (styles 0, 1, 3) unless the list item is -1
            // (samp.exe sends -1) - the phone sent 0, so every login password was thrown away: never logged in, Spawn refused forever
            int16_t item = e.list;
            const uint8_t st = (int16_t)e.dialog == I.dlgId ? I.dlgStyle : 255;
            if(st == 0 || st == 1 || st == 3) item = -1;
            else if((st == 2 || st == 4 || st == 5) && item < 0) item = 0;
            const uint8_t btn = e.resp ? 1 : 0;
            BitStream o; o.Write((int16_t)e.dialog); o.Write(btn); o.Write(item); WrStr8(o, SampCp::FromUtf8(e.text, I.cp)); CliRpc(I, 62, o);
            printf("[crossplay] dialog %d answered: button %u, item %d\n", (int)(int16_t)e.dialog, (unsigned)btn, (int)item);   // 5.02.17
            I.dlgOpen = false; I.noAuto = false;
            if(I.phase == 3) { I.retrySpawnAt = now + 1500; I.autoSpawnN = 0; }
        }
    }
    if(I.phase == 3 && !I.finDl && I.classAt && now - I.classAt >= 4000 && !I.dlgOpen && !I.tdSel)
    {   // 5.02.17: 0.3.DL scripts open their login in OnPlayerFinishedDownloading (the DL client sends 184 when its models are
        // done). Nothing came 4 s into class selection: send it once, like a client with nothing to download
        I.finDl = true; BitStream o; CliRpc(I, 184, o); printf("[crossplay] FinishDownload sent (no login window yet)\n");
    }
    if(I.phase == 3 && I.retrySpawnAt && now >= I.retrySpawnAt && !I.dlgOpen && !I.tdSel && !I.spect && !I.noAuto)
    {   // 5.02.10: capped (3 per 30 s, a dialog answer resets it) so a login / tutorial screen can't loop or get "spawn before login" kicks
        I.retrySpawnAt = 0;
        if(now - I.autoSpawnWin > 30000) { I.autoSpawnWin = now; I.autoSpawnN = 0; }
        if(++I.autoSpawnN <= 3) { BitStream o; CliRpc(I, 129, o); I.spawnAskAt = now; }
        else if(I.autoSpawnN == 4) srv->GateMsg(I.phoneSlot, 0xA9C4E4FF, "[open.mp] The server keeps you in class selection - log in / finish its dialog first.");
    }

    if(I.phase == 3 && I.spawnAskAt && I.spawnAnsAt < I.spawnAskAt && now - I.spawnAskAt >= 3500 && now - I.classAt >= 8000
       && !I.dlgOpen && !I.tdSel && !I.spect)
    {   // 5.02.20: Spawn was sent and the server never answered (no 129 at all - not even "refused"). SA-MP 0.3.7 drops
        // RequestSpawn when the player has no spawn info (a gamemode with no AddPlayerClass that spawns its own way),
        // open.mp when it thinks we are already on foot. The PC client would stand in class selection forever, and the
        // PC server streams nobody to a player who is not spawned (everyone looked frozen). First ask the class again +
        // Spawn once more; still nothing -> spawn here: the server's spawn info if it sent one, else Grove Street.
        // Our movement then makes the server count us as on foot, so PC players see us and we see them move.
        if(++I.spawnSilent == 1)
        {
            printf("[crossplay] no answer to Spawn - asking the class again\n");
            { BitStream o; o.Write((uint16_t)I.classIdx); CliRpc(I, 128, o); }
            { BitStream o; CliRpc(I, 129, o); } I.spawnAskAt = now;
        }
        else
        {
            I.spawnAskAt = 0; I.directSpawn = true;
            const bool own = I.spawn.have && (I.spawn.pos[0] != 0 || I.spawn.pos[1] != 0);
            if(!own)
            {
                static const float kGrove[3] = { 2495.08f, -1687.43f, 13.52f };
                I.spawn.have = false; RemHold(I, kGrove, 3000);
                srv->GateAct(I.phoneSlot, CGA_TELE, 255, 0, 0, kGrove, 0.0f);
                // the skin the PC server has for us (class answer / SetPlayerSkin, else 0) - what PC and ReSA players see
                srv->GateAct(I.phoneSlot, CGA_SKIN, 0, 0, (int32_t)RemModel(I, (int32_t)I.spawn.skin));
            }
            printf("[crossplay] the server never answered Spawn - spawned here (%s)\n", own ? "its spawn point" : "Grove Street");
            srv->GateMsg(I.phoneSlot, 0xA9C4E4FF, own ? "[open.mp] This server has no Spawn answer - you spawned at its spawn point."
                                                      : "[open.mp] This server has no Spawn button - you spawned at Grove Street.");
            RemSpawned(I, srv);
        }
    }

    // movement 30 Hz while spawned
    if(I.phase >= 3 && now - I.lastFoot >= 33)   // 5.02.17: 30 Hz like samp.exe (onfoot_rate 30) - was 20 Hz, other players saw the phone stutter
    {
        I.lastFoot = now;
        srv->BridgePeers(peers);
        const MPServer::BridgePeer* me = nullptr; for(auto& b : peers) if(b.slot == I.phoneSlot) me = &b;
        if(!me) { RemEnd(I, srv, "The phone player left."); return; }
        if(me->hasSync && I.phase == 4)
        {
            const bool dead = (me->d.flags & SYNC_DEAD) || me->d.health == 0;
            if(dead && !I.wasDead) { BitStream o; o.Write((uint8_t)255); o.Write(kInvalid); CliRpc(I, 53, o); }
            if(!dead && I.wasDead)
            {
                if(I.wantClass) { I.wantClass = false; I.phase = 3; I.wasDead = false; I.noAuto = true; BitStream o; o.Write((uint16_t)I.classIdx); CliRpc(I, 128, o); RemClassSel(I, srv, true); }
                else RemSpawned(I, srv);
            }
            I.wasDead = dead;
            if(!dead) RemSendFoot(I, me->d);
            if(now - I.statsAt >= 1000)
            {   // 5.02.15: the stats packet (205) of the PC client: money + drunk level, about once a second. It was never
                // sent, so the drunk level stood still - FPS scripts (drunk level drop per second) read 0 FPS = paused /
                // AFK and kicked, and GetPlayerMoney / money anti-cheats saw no client money
                if(I.drunk > 0 && I.drunkAt) { const int32_t drop = (int32_t)((now - I.drunkAt) * 45 / 1000); I.drunk = std::max<int32_t>(0, I.drunk - drop); }
                I.drunkAt = now; I.statsAt = now;
                BitStream o; o.Write((uint8_t)205); o.Write(I.money); o.Write(I.drunk);
                I.C->Send(&o, HIGH_PRIORITY, UNRELIABLE_SEQUENCED, 1);
            }
            if(!dead) for(auto& kv : I.picks)
            {   // 5.02.11: walking into a pickup (houses, jobs, info icons) - like the PC client, once per walk-in
                const float dx = me->d.pos[0] - kv.second.o.p[0], dy = me->d.pos[1] - kv.second.o.p[1], dz = me->d.pos[2] - kv.second.o.p[2];
                const float d2 = dx * dx + dy * dy + dz * dz;
                if(!kv.second.inside && d2 < 1.6f * 1.6f) { kv.second.inside = true; BitStream o; o.Write((int32_t)kv.first); CliRpc(I, 131, o); }
                else if(kv.second.inside && d2 > 3.0f * 3.0f) kv.second.inside = false;
            }
        }
        if(now - I.scoreAskAt >= 5000)
        {   // 5.02.12: scores / pings for the player list. 5.02.13: also in class selection / login (the list was empty until
            // the spawn), and every 30 s the whole list goes to the phone again - only changed rows were sent, so a row the
            // phone had lost (its connection to this MP server blinked) stayed away until that player's score changed
            I.scoreAskAt = now; BitStream o; CliRpc(I, 155, o);
            if(now - I.listFullAt >= 30000)
            {
                I.listFullAt = now; I.pcScore.clear();
                for(auto& kv : I.rp) { float f4[4] = { 0, -1, 0, 0 }; srv->GateOp(I.phoneSlot, SPO_PCLIST, 1, 0, (int32_t)kv.first, f4, kv.second.name.c_str()); }
                if(I.myId >= 0) { float f4[4] = { 0, -1, 0, 0 }; srv->GateOp(I.phoneSlot, SPO_PCLIST, 1, 1, (int32_t)I.myId, f4, I.rName.c_str()); }
            }
        }
        if(I.lostN && I.phase >= 3 && I.stableSince && now - I.stableSince > 180000) I.lostN = 0;
        if(I.phase == 3 && !I.dlgOpen && !I.tdSel && !I.spect) RemClassSel(I, srv, true);
        else if(I.phase == 3 && (I.dlgOpen || I.tdSel)) RemClassSel(I, srv, false);   // login dialog / textdraw login first
        // PC players with no stream-in yet but a position: nothing to do (they come with RPC 32)
        std::lock_guard<std::mutex> lk(I.sm);
        if(I.phase >= 3) I.status = "On " + I.rServerName + " (" + std::to_string(I.rp.size()) + " PC players around)";
    }
}

bool SampGate::StartRemote(MPServer* srv, const char* host, uint16_t port, const char* password)
{
    Stop();
    m_pSrv = srv; m_nPort = port; m_bRemote = true; m_bJoined = false;
    Impl& I = *m_p; I.joinedFlag = &m_bJoined; I.rHost = host ? host : ""; I.rPort = port; I.rPass = password ? password : "";
    I.rejectN = 0; I.lostN = 0; I.verSwapped = false; I.rejoinAt = 0; I.gameAt = 0;
    I.phase = 0; I.phaseAt = NowMs(); I.phoneSlot = -1; I.myId = -1; I.rp.clear(); I.pcScore.clear(); I.scoreAskAt = 0; I.objs.clear(); I.picks.clear(); I.tdSlot.clear(); I.tdDef.clear(); I.zones.clear();
    for(int& t : I.tdOwner) t = -1;
    { std::lock_guard<std::mutex> lk(I.sm); I.rServerName.clear(); }
    static ICore core; core.fn = LogThunk;
    SAMPRakNet::Init(&core);
    if(const char* e = getenv("MP_GATE_OMP")) SAMPRakNet::SetClientOmp(atoi(e) != 0);   // 5.02.07: the launcher identity
    I.C = RakNetworkFactory::GetRakClientInterface();
    RegisterAll(I.C, &I);
    srv->BridgeEnable(true);
    srv->SetGate(this);
    SetStatus(I, "Waiting for the phone player...");
    m_bRun = true;
    m_Thread = std::thread([this] { Loop(); });
    return true;
}

void SampGate::Loop()
{
    while(m_bRun)
    {
        if(m_bRemote) { RemTick(this, *m_p, m_pSrv); if(m_p->phase == 9 && NowMs() - m_p->phaseAt > 500) { /* keep the status; the owner stops us */ } }
        else SrvTick(this, *m_p, m_pSrv);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

bool SampGate::Ended() const { return m_bRemote && m_p->phase == 9; }

void SampGate::SetRemoteName(const char* name) { m_p->forceName = name ? name : ""; }
void SampGate::SetAnnounce(bool on) { m_p->announce = on; m_p->annAt = 0; }

void SampGate::Stop()
{
    if(!m_bRun && !m_Thread.joinable()) return;
    m_bRun = false;
    if(m_Thread.joinable()) m_Thread.join();
    Impl& I = *m_p;
    if(m_pSrv) m_pSrv->SetGate(nullptr);
    if(I.S)
    {
        for(size_t i = 0; i < I.pcs.size(); ++i) if(I.pcs[i].joined && m_pSrv) m_pSrv->BridgeRemove(I.pcs[i].slot);
        I.pcs.clear();
        g_QueryGate = nullptr; SAMPRakNet::SetQuery(nullptr);
        I.S->Disconnect(100); RakNetworkFactory::DestroyRakServerInterface(I.S); I.S = nullptr;
    }
    if(I.C)
    {
        if(m_pSrv && I.phase != 9) RemReset(I, m_pSrv);
        I.C->Disconnect(100); RakNetworkFactory::DestroyRakClientInterface(I.C); I.C = nullptr;
    }
    { std::lock_guard<std::mutex> lk(I.qm); I.rpcs.clear(); I.hooks.clear(); }
    m_bJoined = false;
}
