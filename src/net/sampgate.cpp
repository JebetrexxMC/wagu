// 5.02: open.mp / SA-MP 0.3.7 crossplay inside libMP and mp_server - see sampgate.h
#include "sampgate.h"
#include "server.h"
#include "samp.h"
#include "cgrp.h"
#include "dz_map.h"

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

namespace {

const uint32_t kSampVersion = 4057;          // SA-MP 0.3.7-R2/R3 netgame version
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
    int maxPc = 50;

    // ---- remote role ----
    RakClientInterface* C = nullptr;
    std::string rHost, rPass, rName, rServerName;
    uint16_t rPort = 0;
    int phase = 0;                // 0 wait phone, 1 connecting, 2 accepted, 3 in game (class), 4 spawned, 9 ended
    uint64_t phaseAt = 0;
    int phoneSlot = -1;
    int myId = -1;
    struct Spawn { uint32_t skin = 0; float pos[3] = { 0, 0, 3 }; float rot = 0; uint32_t w[3] = { 0, 0, 0 }, ammo[3] = { 0, 0, 0 }; bool have = false; } spawn;
    struct Rp { std::string name; uint32_t color = 0xFFFFFFFF; uint16_t skin = 0; int slot = -1; SyncData last; bool hasPos = false; int vehicle = -1; };
    std::map<int, Rp> rp;                                  // remote player id -> ghost
    std::map<int, uint16_t> vehModel;                      // remote vehicle id -> model
    std::map<int, int> tdSlot;                             // wire textdraw id -> MP textdraw slot
    int tdOwner[kTdSlots];                       // MP slot -> wire id (-1)
    std::map<int, PktTextDraw> tdDef;
    struct Obj { uint32_t row; uint16_t model; float p[3], r[3]; };
    std::map<int, Obj> objs;
    std::map<int, std::array<float, 4>> zones;
    uint64_t lastFoot = 0, keysUntil = 0, retrySpawnAt = 0;
    uint32_t heldKeys = 0;
    bool wasDead = false;
    int lastHealth = 100;

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
    if(I->rpcs.size() < 4096) I->rpcs.push_back(std::move(r));
}
static void* s_RpcExtra[256][2];

template<class T> static void RegisterAll(T* peer, Impl* I)
{
    for(int i = 0; i < 256; ++i) { s_RpcExtra[i][0] = I; s_RpcExtra[i][1] = (void*)(intptr_t)i; peer->RegisterAsRemoteProcedureCall((RPCID)i, OnRpc, s_RpcExtra[i]); }
}

std::string SampGate::SampName(const char* mpName)
{
    std::string o;
    for(const char* p = mpName ? mpName : ""; *p && o.size() < 20; ++p)
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

bool SampGate::Query(const char* host, uint16_t port, SampQueryInfo& out, int timeoutMs)
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
    std::lock_guard<std::mutex> lk(I.sm); I.status = b;
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
            if(ver != kSampVersion) { Impl::Pc t = p; t.pid = r.from; SrvMsg(I, &t, 0xFF4040FF, "This MP server takes SA-MP 0.3.7 / open.mp clients only."); I.S->Kick(r.from); break; }
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
        case 'r': u16(4); s8("lagcomp"); s8("On"); s8("mapname"); s8("San Andreas"); s8("version"); s8("omp 1.6.7 (MP)"); s8("weburl"); s8("github.com/ThirtyFoxMCx00E1/wagu"); break;
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

static void RemReset(Impl& I, MPServer* srv)
{   // ghosts, objects and textdraws of the PC server go away
    for(auto& kv : I.rp) if(kv.second.slot >= 0) srv->BridgeRemove(kv.second.slot);
    I.rp.clear(); I.vehModel.clear();
    for(auto& kv : I.objs) { float z[3] = { 0, 0, 0 }; srv->GateObject(kv.second.row, kv.second.model, z, z, false); }
    I.objs.clear();
    for(int i = 0; i < kTdSlots; ++i) if(I.tdOwner[i] >= 0) { PktTextDraw t; memset(&t, 0, sizeof(t)); t.type = PKT_TEXTDRAW; t.tdId = (uint16_t)i; t.show = 0; srv->GateTextDraw(I.phoneSlot, t); I.tdOwner[i] = -1; }
    I.tdSlot.clear(); I.tdDef.clear();
    for(auto& kv : I.zones) { float f[4]; memcpy(f, kv.second.data(), sizeof(f)); char m[2] = { 4, 0 }; srv->GateOp(I.phoneSlot, SPO_ZONE, (uint8_t)(kv.first & 255), (uint8_t)(kv.first >> 8), 0, f, m); }
    I.zones.clear();
}

static void RemEnd(Impl& I, MPServer* srv, const char* why)
{
    if(I.phase == 9) return;
    char b[200]; snprintf(b, sizeof(b), "[open.mp] %s", why);
    if(I.phoneSlot >= 0) srv->GateMsg(I.phoneSlot, 0xFF6060FF, b);
    printf("[crossplay] %s\n", why);
    RemReset(I, srv);
    if(I.C) I.C->Disconnect(100);
    I.phase = 9; I.phaseAt = NowMs();
    SetStatus(I, "%s", why);
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

static void RemSpawned(Impl& I, MPServer* srv)
{
    I.phase = 4; I.wasDead = false;
    const int s = I.phoneSlot;
    if(I.spawn.have)
    {
        srv->GateAct(s, CGA_SKIN, 0, 0, (int32_t)I.spawn.skin);
        srv->GateAct(s, CGA_TELE, 255, 0, 0, I.spawn.pos, I.spawn.rot);
        srv->GateAct(s, CGA_DISARM);
        for(int i = 0; i < 3; ++i) if(I.spawn.w[i] > 0 && I.spawn.w[i] < 47) srv->GateAct(s, CGA_GUN, (int)I.spawn.w[i], 0, (int32_t)std::max<uint32_t>(I.spawn.ammo[i], 1));
    }
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
            BitStream o; o.Write(type); o.Write(addr); o.Write((uint8_t)0); CliRpc(I, 103, o);
            break;
        }
        case 139:  // InitGame
        {
            bool b; float f;
            for(int i = 0; i < 4; ++i) bs.Read(b); bs.Read(f); bs.Read(b); bs.Read(f); bs.Read(b); bs.Read(b); bs.Read(b);
            bs.Read(u32a); uint16_t id = 0; bs.Read(id); I.myId = id;
            bs.Read(b); bs.Read(u32a); uint8_t hour = 12, weather = 1; bs.Read(hour); bs.Read(weather);
            bs.Read(f); bs.Read(b); bs.Read(u32a); bs.Read(b); for(int i = 0; i < 5; ++i) bs.Read(u32a);
            std::string name = RdStr8(bs);
            { std::lock_guard<std::mutex> lk(I.sm); I.rServerName = name; }
            if(I.joinedFlag) *I.joinedFlag = true;
            I.phase = 3; I.phaseAt = NowMs();
            srv->GateOp(s, SPO_HELLO, 0, 0, 0, nullptr, name.c_str());
            srv->GateOp(s, SPO_TIME, hour, 0, weather);
            srv->GateAct(s, CGA_WEATHER, 0, 0, weather);
            char m[200]; snprintf(m, sizeof(m), "[open.mp] Connected to %s as %s (id %d).", name.c_str(), I.rName.c_str(), (int)id);
            srv->GateMsg(s, 0x88AA62FF, m);
            SetStatus(I, "On %s (%s:%u)", name.c_str(), I.rHost.c_str(), I.rPort);
            BitStream o; o.Write((uint16_t)0); CliRpc(I, 128, o);
            break;
        }
        case 128:  // class answer
        {
            uint8_t sel = 0; bs.Read(sel); Impl::Spawn sp; if(RdSpawn(bs, sp)) I.spawn = sp;
            if(I.phase < 4) { BitStream o; CliRpc(I, 129, o); }
            break;
        }
        case 129:  // spawn answer / SpawnPlayer
            bs.Read(u32a);
            if(u32a) RemSpawned(I, srv); else I.retrySpawnAt = NowMs() + 4000;
            break;
        case 68: { Impl::Spawn sp; if(RdSpawn(bs, sp)) I.spawn = sp; break; }
        case 74:   // ForceClassSelection
            I.phase = 3; { BitStream o; o.Write((uint16_t)0); CliRpc(I, 128, o); } break;
        case 137:  // player joined
        {
            bs.Read(u16a); bs.Read(u32a); bs.Read(u8a); std::string nm = RdStr8(bs);
            if(u16a == I.myId) break;
            Impl::Rp& p = I.rp[u16a]; p.name = nm; p.color = u32a;
            break;
        }
        case 138: bs.Read(u16a); if(Impl::Rp* p = RemPlayer(I, u16a)) { if(p->slot >= 0) srv->BridgeRemove(p->slot); I.rp.erase(u16a); } break;
        case 32:   // stream in
        {
            bs.Read(u16a); bs.Read(u8a); bs.Read(u32a); RdVec(bs, v); bs.Read(fa); bs.Read(u32b);
            if(Impl::Rp* p = RemPlayer(I, u16a)) { p->skin = (uint16_t)u32a; if(u32b) p->color = u32b; RemGhostIn(I, srv, u16a, v, fa); }
            break;
        }
        case 163: bs.Read(u16a); if(Impl::Rp* p = RemPlayer(I, u16a)) { if(p->slot >= 0) srv->BridgeRemove(p->slot); p->slot = -1; } break;
        case 153:  // skin
            bs.Read(u32a); bs.Read(u32b);
            if((int)u32a == I.myId) srv->GateAct(s, CGA_SKIN, 0, 0, (int32_t)u32b);
            else if(Impl::Rp* p = RemPlayer(I, (int)u32a)) p->skin = (uint16_t)u32b;
            break;
        case 72: bs.Read(u16a); bs.Read(u32a); if(Impl::Rp* p = RemPlayer(I, u16a)) p->color = u32a; break;
        case 164:  // vehicle stream in (model for the drivers)
            bs.Read(u16a); bs.Read(u32a); I.vehModel[u16a] = (uint16_t)u32a; break;
        case 165: bs.Read(u16a); I.vehModel.erase(u16a); break;
        case 93: { bs.Read(u32a); std::string t = RdStr32(bs); srv->GateMsg(s, u32a, t.c_str()); break; }
        case 101:
        {
            bs.Read(u16a); std::string t = RdStr8(bs);
            if(u16a == I.myId) srv->GateChat(s, t.c_str());
            else if(Impl::Rp* p = RemPlayer(I, u16a))
            {
                if(p->slot >= 0) srv->GateChat(p->slot, t.c_str());
                else { std::string l = p->name + ": " + t; srv->GateMsg(s, p->color, l.c_str()); }
            }
            break;
        }
        case 12: case 13:
        {
            RdVec(bs, v); float cur[3], h = 0; int hp; srv->GatePos(s, cur, &h, &hp);
            srv->GateAct(s, CGA_TELE, 255, 0, 0, v, h); break;
        }
        case 19: { bs.Read(fa); float cur[3], h = 0; int hp; if(srv->GatePos(s, cur, &h, &hp)) srv->GateAct(s, CGA_TELE, 255, 0, 0, cur, fa); break; }
        case 14: bs.Read(fa); srv->GateAct(s, CGA_HEALTH, 0, 0, (int32_t)fa); I.lastHealth = (int)fa; break;
        case 66: bs.Read(fa); srv->GateAct(s, CGA_ARMOUR, 0, 0, (int32_t)fa); break;
        case 22: bs.Read(u32a); bs.Read(u32b); srv->GateAct(s, CGA_GUN, (int)u32a, 0, (int32_t)u32b); break;
        case 21: srv->GateAct(s, CGA_DISARM); break;
        case 67: bs.Read(u32a); srv->GateOp(s, SPO_WEAPON, 0, 0, (int32_t)u32a); break;
        case 145: bs.Read(u8a); bs.Read(u16a); srv->GateOp(s, SPO_WEAPON, 1, u8a, u16a); break;
        case 18: bs.Read(i32); srv->GateAct(s, CGA_MONEY, 0, 0, i32); break;
        case 20: srv->GateAct(s, CGA_SETMONEY, 0, 0, 0); break;
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
            int16_t id = -1; bs.Read(id); bs.Read(u8a); std::string cap = RdStr8(bs), b1 = RdStr8(bs), b2 = RdStr8(bs), body = RdCompressed(bs);
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
            PktTextDraw t; memset(&t, 0, sizeof(t)); t.type = PKT_TEXTDRAW; t.show = 1; t.tdId = (uint16_t)cs;
            t.x = px; t.y = py; t.letterX = lx; t.letterY = ly; t.boxX = tx; t.color = Bswap(lc); t.boxColor = Bswap(bc); t.bgColor = Bswap(bg);
            t.font = style; t.align = (uint8_t)((flags >> 1) & 3); if(!t.align) t.align = 1; t.outline = outline; t.shadow = shadow; t.useBox = flags & 1; t.proportional = (flags >> 4) & 1;
            t.pad[0] = selectable ? 1 : 0;
            MP_CopyStr(t.text, text.c_str(), sizeof(t.text));
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
            MP_CopyStr(it->second.text, text.c_str(), sizeof(it->second.text)); srv->GateTextDraw(s, it->second);
            break;
        }
        case 83: { bool on = false; bs.Read(on); bs.Read(u32a); srv->GateOp(s, SPO_TDSELECT, on ? 1 : 0, 0, (int32_t)u32a); break; }
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
            std::string t = RdCompressed(bs);
            if(ap != kInvalid || avh != kInvalid) break;   // attached labels: not yet
            srv->GateLabel(s, id, true, v, fa, u32a, t.c_str());
            break;
        }
        case 58: bs.Read(u16a); srv->GateLabel(s, u16a, false, nullptr, 0, 0, ""); break;
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
    }
    p->last = d; p->hasPos = true;
    srv->BridgeSync(p->slot, d);
}

static void RemSendFoot(Impl& I, const SyncData& d)
{   // the phone's movement -> PC server (client format on-foot sync; in a car = on foot at the car)
    const bool car = (d.flags & SYNC_IN_CAR) != 0;
    const float* pos = car ? d.carPos : d.pos; float h = car ? d.carHeading : d.heading;
    float q[4]; HeadingQuat(h, q);
    float a = h * kPi / 180.0f, sp = car ? 0.0f : d.speed / 50.0f;
    uint32_t extra = NowMs() < I.keysUntil ? I.heldKeys : 0;
    BitStream o; o.Write((uint8_t)kPlayerSync);
    o.Write((uint16_t)0); o.Write((uint16_t)((!car && d.speed > 0.3f) ? 0xFF80 : 0));
    o.Write((uint16_t)(KeysFromFlags(d.flags) | (extra & 0xFFFF)));
    WrVec(o, pos); o.Write(q[0]); o.Write(q[1]); o.Write(q[2]); o.Write(q[3]);
    o.Write((uint8_t)std::min<int>(d.health, 255)); o.Write((uint8_t)std::min<int>(d.armour, 255));
    o.Write((uint8_t)((d.weapon & 63) | (((extra >> 16) & 3) << 6)));
    o.Write((uint8_t)((d.flags & SYNC_DUCKING) ? 1 : 0));
    float vel[3] = { -sinf(a) * sp, cosf(a) * sp, 0 }; WrVec(o, vel);
    float z[3] = { 0, 0, 0 }; WrVec(o, z); o.Write((uint16_t)0); o.Write((uint16_t)0); o.Write((uint16_t)0);
    I.C->Send(&o, HIGH_PRIORITY, UNRELIABLE_SEQUENCED, 1);
}

static void RemTick(SampGate* g, Impl& I, MPServer* srv)
{
    const uint64_t now = NowMs();
    std::vector<MPServer::BridgePeer> peers;
    if(I.phase == 0)
    {
        srv->BridgePeers(peers);
        if(peers.empty()) { if(now - I.phaseAt > 60000) { I.phase = 9; SetStatus(I, "No phone player joined"); } return; }
        I.phoneSlot = peers[0].slot; I.rName = SampGate::SampName(peers[0].name);
        SetStatus(I, "Connecting to %s:%u as %s...", I.rHost.c_str(), I.rPort, I.rName.c_str());
        char m[200]; snprintf(m, sizeof(m), "[open.mp] Connecting to %s:%u as %s...", I.rHost.c_str(), I.rPort, I.rName.c_str());
        srv->GateMsg(I.phoneSlot, 0xA9C4E4FF, m);
        I.C->SetPassword(I.rPass.c_str());
        if(!I.C->Connect(I.rHost.c_str(), I.rPort, 0, 0, 5)) { RemEnd(I, srv, "Could not start the connection."); return; }
        I.phase = 1; I.phaseAt = now;
        return;
    }
    if(I.phase == 9) return;
    if(I.phase == 1 && now - I.phaseAt > 15000) { RemEnd(I, srv, "The PC server did not answer (closed, wrong port or not SA-MP 0.3.7 / open.mp)."); return; }

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
                BitStream o; o.Write(kSampVersion); o.Write((uint8_t)1); WrStr8(o, I.rName); o.Write((uint32_t)(chal ^ kSampVersion));
                WrStr8(o, Gpci()); WrStr8(o, "0.3.7-R2");
                I.C->RPC((RPCID)25, &o, HIGH_PRIORITY, RELIABLE, 0, false, UNASSIGNED_NETWORK_ID, nullptr);
                I.phase = 2; I.phaseAt = now;
                break;
            }
            case ID_CONNECTION_ATTEMPT_FAILED: RemEnd(I, srv, "The PC server did not answer."); break;
            case ID_NO_FREE_INCOMING_CONNECTIONS: RemEnd(I, srv, "The PC server is full."); break;
            case ID_CONNECTION_BANNED: RemEnd(I, srv, "You are banned from this PC server."); break;
            case ID_INVALID_PASSWORD: RemEnd(I, srv, "Wrong server password."); break;
            case ID_DISCONNECTION_NOTIFICATION: RemEnd(I, srv, "The PC server closed the connection (kicked / server restart)."); break;
            case ID_CONNECTION_LOST: RemEnd(I, srv, "Lost the connection to the PC server."); break;
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
            std::string t = e.text; if(t.empty()) continue;
            if(t[0] == '/') { BitStream o; WrStr32(o, t); CliRpc(I, 50, o); }
            else { BitStream o; WrStr8(o, t.substr(0, 144)); CliRpc(I, 101, o); }
        }
        else if(e.kind == HK_SAMP && e.op == SPO_TDCLICK)
        {
            uint16_t wire = kInvalid; if(e.val >= 0 && e.val < kTdSlots && I.tdOwner[e.val] >= 0) wire = (uint16_t)I.tdOwner[e.val];
            BitStream o; o.Write(wire); CliRpc(I, 83, o);
        }
        else if(e.kind == HK_SAMP && e.op == SPO_KEYS) { I.heldKeys = (uint32_t)e.val; I.keysUntil = now + 400; }
        else if(e.kind == HK_DIALOG)
        {
            BitStream o; o.Write((int16_t)e.dialog); o.Write(e.resp); o.Write(e.list); WrStr8(o, e.text); CliRpc(I, 62, o);
            if(I.phase == 3) I.retrySpawnAt = now + 1500;
        }
    }
    if(I.phase == 3 && I.retrySpawnAt && now >= I.retrySpawnAt) { I.retrySpawnAt = 0; BitStream o; CliRpc(I, 129, o); }

    // movement 20 Hz while spawned
    if(I.phase >= 3 && now - I.lastFoot >= 50)
    {
        I.lastFoot = now;
        srv->BridgePeers(peers);
        const MPServer::BridgePeer* me = nullptr; for(auto& b : peers) if(b.slot == I.phoneSlot) me = &b;
        if(!me) { RemEnd(I, srv, "The phone player left."); return; }
        if(me->hasSync && I.phase == 4)
        {
            const bool dead = (me->d.flags & SYNC_DEAD) || me->d.health == 0;
            if(dead && !I.wasDead) { BitStream o; o.Write((uint8_t)255); o.Write(kInvalid); CliRpc(I, 53, o); }
            if(!dead && I.wasDead) RemSpawned(I, srv);
            I.wasDead = dead;
            if(!dead) RemSendFoot(I, me->d);
        }
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
    I.phase = 0; I.phaseAt = NowMs(); I.phoneSlot = -1; I.myId = -1; I.rp.clear(); I.objs.clear(); I.tdSlot.clear(); I.tdDef.clear(); I.zones.clear();
    for(int& t : I.tdOwner) t = -1;
    { std::lock_guard<std::mutex> lk(I.sm); I.rServerName.clear(); }
    static ICore core; core.fn = LogThunk;
    SAMPRakNet::Init(&core);
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
