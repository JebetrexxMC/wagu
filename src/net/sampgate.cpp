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
#include <fcntl.h>
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
#include "net/samp_anims.h"   // 5.02.27
void SampGate::SetLogHook(void (*fn)(const char*)) { g_GateLog = fn; }

namespace {

const uint32_t kSampVersion = 4057;          // SA-MP 0.3.7-R2/R3 netgame version
const uint32_t kSampVersionDL = 4062;        // 5.02.08: SA-MP 0.3.DL-R1 (custom models)
const uint16_t kInvalid = 0xFFFF;
const uint8_t  kPlayerSync = 207, kVehicleSync = 200;   // SA-MP 0.3.7 sync packet ids
const float    kPi = 3.14159265358979f;
const int      kTdSlots = 200;            // MP client textdraw slots a SA-MP server may use (SAMP_TD_BASE 0..199)
const int      kLabelBase = 0;

// 5.02.27: SA-MP animation index <-> GTA anim (IFP block + CKeyGen::GetUppercaseKey hash of the name)
uint32_t AnimKey(const char* s, size_t n)
{   // CRC-32 table, upper case, no final xor (= the game's CKeyGen::GetUppercaseKey)
    uint32_t k = 0xFFFFFFFFu;
    for(size_t i = 0; i < n && s[i]; ++i)
    {
        uint8_t c = (uint8_t)s[i]; if(c >= 'a' && c <= 'z') c -= 32;
        uint32_t r = (k ^ c) & 0xFF;
        for(int b = 0; b < 8; ++b) r = (r & 1) ? (r >> 1) ^ 0xEDB88320u : (r >> 1);
        k = r ^ (k >> 8);
    }
    return k;
}
const std::multimap<uint32_t, int>& AnimTable()
{
    static std::multimap<uint32_t, int> t;
    if(t.empty()) for(int i = 1; i < kSampAnimCount; ++i) { const char* c = strchr(kSampAnimNames[i], ':'); if(c) t.emplace(AnimKey(c + 1, 64), i); }
    return t;
}
int AnimIndex(uint32_t hash, const char* block)
{   // 0 = not a SA-MP animation
    const auto& t = AnimTable(); auto r = t.equal_range(hash); int any = 0;
    for(auto it = r.first; it != r.second; ++it)
    {
        const char* n = kSampAnimNames[it->second]; const char* c = strchr(n, ':');
        if(!any) any = it->second;
        if(block && *block && (size_t)(c - n) == strlen(block) && !strncasecmp(n, block, (size_t)(c - n))) return it->second;
    }
    return any;
}
bool AnimName(int idx, std::string& lib, std::string& name)
{
    if(idx <= 0 || idx >= kSampAnimCount) return false;
    const char* n = kSampAnimNames[idx]; const char* c = strchr(n, ':'); if(!c) return false;
    lib.assign(n, c - n); name = c + 1; return true;
}
// walking / running / idle / aiming: the ghost's own movement plays them - only the rest is copied onto the ghost
bool AnimIsMove(const std::string& lib, const std::string& name)
{
    static const char* const kLibs[] = { "COLT45", "PYTHON", "SILENCED", "SHOTGUN", "BUDDY", "UZI", "RIFLE", "SNIPER", "FLAME", "TEC", "GRENADE", "SWIM", "PARACHUTE" };
    for(const char* l : kLibs) if(!strcasecmp(lib.c_str(), l)) return true;
    if(strcasecmp(lib.c_str(), "PED")) return false;
    static const char* const kPre[] = { "WALK_", "RUN_", "SPRINT_", "IDLE_", "JOG_", "WOMAN_", "TURN_", "GUN_", "FALL_", "JUMP_", "CLIMB_", "WEAPON_", "GUNCROUCH", "GUNMOVE", "CAR_", "ENTER_", "EXIT_", "GETUP", "KO_", "HIT_", "FIGHT", "ABSEIL", "XPRESS", "CROUCH", "COWER", "HANDSCOWER", "STEP_", "BIKE_", "RUN", "WALK" };
    for(const char* p : kPre) if(!strncasecmp(name.c_str(), p, strlen(p))) return true;
    return false;
}

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
struct HookEv { int kind; int slot; std::string text; uint8_t op = 0, a = 0, b = 0; int32_t val = 0; float f[4] = { 0, 0, 0, 0 }; uint16_t dialog = 0; uint8_t resp = 0; int16_t list = 0; };
enum { HK_CHAT = 1, HK_SAMP, HK_DIALOG, HK_RAW };   // HK_RAW: 5.02.25 phone packet copy (text = bytes)

SampGate* g_QueryGate = nullptr;
int QueryThunk(const char* in, int inLen, char* out, int outCap) { return g_QueryGate ? g_QueryGate->AnswerQuery(in, inLen, out, outCap) : 0; }
void LogThunk(const char* t) { printf("[open.mp net] %s\n", t); }

} // namespace

struct SvState;   // 5.02.29: SampVoice (sampgate_voice.inc)
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
    int lostN = 0; uint64_t gameAt = 0; uint64_t lastRecvAt = 0;   // 5.02.29 lastRecvAt: the PC server's last packet (lost-connection log)   // gameAt: InitGame time (a kick in the first minute is the script, not a restart)
    std::string forceName;                    // 5.02.12: the name for PC servers (empty = the MP name without rank tags)
    std::unordered_map<int32_t, int32_t> modelBase;   // 5.02.08: 0.3.DL custom model id -> its base model (RPC 179)
    int phoneSlot = -1;
    int myId = -1;
    struct Spawn { uint32_t skin = 0; uint8_t team = 255; float pos[3] = { 0, 0, 3 }; float rot = 0; uint32_t w[3] = { 0, 0, 0 }, ammo[3] = { 0, 0, 0 }; bool have = false; } spawn;
    // 5.02.23: OnPlayerRequestClass can return 0 (class locked: admins only, not logged in yet). The PC client then never
    // sends RequestSpawn for that class - we ask the class again first and only spawn once the server says it is selectable
    bool classOk = true, spawnAfterClass = false;
    // 5.02.23: chat to the PC server - 128 bytes per line (SA-MP / open.mp drop longer ones), long lines split at a
    // space and sent 1 s apart (script anti-spam kicks), and a check that the server showed our line back
    std::deque<std::string> chatOut; uint64_t chatOutAt = 0;
    std::string echoWant; uint64_t echoAt = 0; uint32_t chatSentN = 0, echoMissN = 0; bool echoHint = false;
    struct Rp { std::string name; uint32_t color = 0xFFFFFFFF; uint16_t skin = 0; int slot = -1; SyncData last; bool hasPos = false; int vehicle = -1;
                uint16_t animId = 0; bool animLoop = false; uint64_t animAt = 0, animSeen = 0; std::string animLib, animName;
                float aimPt[3] = { 0, 0, 0 }; uint64_t aimMs = 0; };   // 5.02.27: his SA-MP anim
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
    bool loginUi = false, loginWaitSaid = false;   // 5.02.28: the server showed a dialog / clickable textdraws this session (its script spawns us, like on PC)
    bool dlgOpen = false, tdSel = false, spect = false, noAuto = false;   // noAuto: the server sent us back to class selection - wait for the player    // 5.02.10: no auto-spawn while a login dialog / clickable-textdraw login is up (a PC player can't press Spawn then)                 // 5.02.09: ForceClassSelection waits for the next respawn (like the PC client)
    uint64_t spawnAnsAt = 0; int spawnSilent = 0; bool directSpawn = false;   // 5.02.20: Spawn never answered -> spawn here
    uint64_t blankAt = 0, chatSumAt = 0; uint32_t blankN = 0, pcChatN = 0, srvMsgN = 0, nameMsgN = 0, tot93 = 0, tot101 = 0;   // 5.02.21: chat clears / chat seen (mp_log.txt)
    uint64_t spawnAskAt = 0, refuseMsgAt = 0, classAt = 0;   // 5.02.17: one RequestSpawn in flight (taps flooded: kicked), refusal note, class selection since
    bool finDl = false;   // 5.02.17: FinishDownload (184) sent - scripts that open their login in OnPlayerFinishedDownloading
    int tdShown = 0;      // 5.02.17: textdraws the server showed (log)
    int dlgId = -1; uint8_t dlgStyle = 0;   // 5.02.17: the open dialog (its answer must match it)
    uint64_t autoSpawnWin = 0; int autoSpawnN = 0;   // 5.02.09: cap auto RequestSpawn so a login screen can't loop
    int lastHealth = 100;
    // 5.02.15: what a PC client reports back - anti-cheats (Nex-AC...) and FPS / AFK scripts read these
    int lastArmour = 0;                   // SetPlayerArmour (RPC 66), 0 at spawn
    // 5.02.25: the callbacks samp.exe fires. The PC server's cars are made on the phone (SPO_VEHICLE); sitting in one
    // sends OnPlayerEnterVehicle + driver / passenger sync instead of "on foot at the car"
    struct Veh { uint16_t model = 0; uint8_t c1 = 0, c2 = 0; float p[3] = { 0, 0, 0 }; float a = 0; bool taken = false; bool locked = false;
                 uint8_t interior = 0; int paintjob = -1; std::vector<uint16_t> mods; uint64_t movedAt = 0; };   // 5.02.26: interior / paintjob / parts too, 5.02.27 movedAt
    std::map<int, Veh> vehs;                                // server vehicle id -> what the phone was told
    int myVeh = -1, mySeat = 0; uint64_t myVehAt = 0;       // the server car the phone sits in
    bool vsHave = false; PktVehState vsLast{};              // the phone's last car state of it (mods / paintjob / colours / damage)
    float myVehHp = 1000.0f; bool mySiren = false;
    int modShop = 0;                                        // 1 Transfender, 2 Loco Low Co, 3 Wheel Arch Angels (OnEnterExitModShop)
    std::map<int, uint8_t> pcTeam;                          // SetPlayerTeam of the others (same team = no damage, like samp.exe)
    uint64_t hitAt = 0; int lastHp = -1, lastAr = -1;       // last PC bullet that hit me / health seen (OnPlayerTakeDamage)
    struct Menu { std::string title; bool two = false; std::vector<std::string> c1, c2; std::string h1, h2; };
    std::map<int, Menu> menus; int menuOpen = -1;           // GTA menus (CreateMenu / ShowMenuForPlayer) as a list window
    struct Act { uint32_t skin = 0; float p[3] = { 0, 0, 0 }; float a = 0; bool invul = true; float hp = 100.0f; };   // 5.02.62: + health
    std::map<int, Act> actors;                              // CreateActor (shown with the phone's actor peds)
    // 5.02.62: the rest of the open.mp client RPCs
    std::map<int, bool> tagOff;                             // ShowPlayerNameTagForPlayer: PC id -> hidden (applied when his ghost appears)
    std::map<int, std::string> vehPlate;                    // SetVehicleNumberPlate (kept; the phone has no custom plates)
    std::map<int, int> trailer;                             // AttachTrailerToVehicle: vehicle -> trailer
    std::map<int, int> objAtt;                              // AttachObjectToPlayer on me: object id -> attachment slot
    int camObj = -1;                                        // AttachCameraToObject
    bool camTarget = false; int camLast[4] = { 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF };   // EnablePlayerCameraTarget / last OnPlayerCameraTarget
    float gravity = 0.008f;                                 // SetGravity / SetPlayerGravity
    // 5.02.26: SetPlayerAttachedObject kept (EditAttachedObject answers with it), EditObject / SelectObject state
    struct Att { bool on = false; uint32_t model = 0, bone = 0, c1 = 0, c2 = 0; float off[3] = { 0, 0, 0 }, rot[3] = { 0, 0, 0 }, sc[3] = { 1, 1, 1 }; };
    Att att[10];
    bool specOn = false;                  // PlayerSpectatePlayer / Vehicle camera is on
    // 5.02.27: the rest of samp.exe's sync - special action echo, aim (203), spectator (212), weapons (204), markers (208)
    uint8_t special = 0;                  // SetPlayerSpecialAction (handsup, cuffed, phone, dance...) reported back in on-foot sync
    uint64_t aimAt = 0, specSyncAt = 0, weapAt = 0;
    uint32_t targetSent = 0xFFFFFFFF;     // last target player << 16 | actor in the weapons update
    struct Marker { int16_t p[3] = { 0, 0, 0 }; bool on = false; uint64_t at = 0; };
    std::map<int, Marker> markers;        // PC player id -> radar marker the phone shows (far players, ShowPlayerMarkers)
    struct Edit { int mode = 0; bool pobj = false; uint16_t id = 0; uint32_t idx = 0; float p[3] = { 0, 0, 0 }, r[3] = { 0, 0, 0 }, p0[3] = { 0, 0, 0 }, r0[3] = { 0, 0, 0 }; float step = 0.25f; } edit;
    // 5.02.24: samp.exe's own client commands (/save /rs /interior /vw /rcon) - the gate answers them like the PC client
    uint8_t interior = 0; int32_t vworld = 0; SyncData lastMe{}; bool haveMe = false;
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

    SvState* sv = nullptr;                // 5.02.29: SampVoice voice chat on this PC server (null = none seen)
    std::atomic<bool> svOn{true};         // 5.02.29: Settings > Voice > SampVoice on PC servers (MP_GATE_NOVOICE=1 = off)
    std::atomic<bool>* joinedFlag = nullptr;
    Impl() { for(int& t : tdOwner) t = -1; if(getenv("MP_GATE_NOVOICE")) svOn = false; }
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
    HookEv e; e.kind = HK_SAMP; e.slot = slot; e.op = p.op; e.a = p.a; e.b = p.b; e.val = p.val; e.text = p.text; memcpy(e.f, p.f, sizeof(e.f));
    std::lock_guard<std::mutex> lk(m_p->qm); m_p->hooks.push_back(e); return true;
}
void SampGate::OnPhonePkt(int slot, const uint8_t* data, int len)
{
    if(!m_bRun || !m_bRemote || !data || len <= 0 || len > 4096) return;
    HookEv e; e.kind = HK_RAW; e.slot = slot; e.op = data[0]; e.text.assign((const char*)data, (size_t)len);
    std::lock_guard<std::mutex> lk(m_p->qm); m_p->hooks.push_back(e);
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
        case 'r': u16(6); s8("lagcomp"); s8("On"); s8("mapname"); s8("San Andreas"); s8("weather"); s8("10"); s8("worldtime"); s8("12:00"); s8("version"); s8("omp 1.6.7 (MP)"); s8("weburl"); s8("github.com/JebetrexxMC/wagu"); break;
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

// ---- 5.02.25: PC server cars / actors on the phone ------------------------------------------------
static const uint16_t kMenuDialog = 32700;   // the list window a GTA menu (ShowMenuForPlayer) is shown in
static void RemVehShow(Impl& I, MPServer* srv, int id)
{
    auto it = I.vehs.find(id); if(it == I.vehs.end() || I.phoneSlot < 0) return;
    const Impl::Veh& v = it->second; if(v.model < 400 || v.model > 611) return;   // 0.3.DL custom cars: not on the phone
    float f[4] = { v.p[0], v.p[1], v.p[2], v.a }; char t[32]; snprintf(t, sizeof(t), "%u %u", (unsigned)v.model, (unsigned)v.c2);
    srv->GateOp(I.phoneSlot, SPO_VEHICLE, 1, v.c1, id, f, t);
    if(v.locked) srv->GateOp(I.phoneSlot, SPO_VEHICLE, 4, 1, id);
    // 5.02.26: its interior, paintjob and parts (the phone fits them once the car is made)
    if(v.interior) srv->GateOp(I.phoneSlot, SPO_VEHMOD, 4, v.interior, id);
    if(v.paintjob >= 0) srv->GateOp(I.phoneSlot, SPO_VEHMOD, 1, (uint8_t)v.paintjob, id);
    for(uint16_t m : v.mods) { float c[4] = { (float)m, 0, 0, 0 }; srv->GateOp(I.phoneSlot, SPO_VEHMOD, 2, 0, id, c); }
}
static void RemVehFree(Impl& I, MPServer* srv, int id)
{
    auto it = I.vehs.find(id); if(it == I.vehs.end() || !it->second.taken) return;
    it->second.taken = false; RemVehShow(I, srv, id);
}
// 5.02.26: EditObject / EditAttachedObject / SelectObject from the phone's panel (samp.exe drags the gizmo with the mouse)
static int32_t RemModel(Impl& I, int32_t model);
static void RemEdit(Impl& I, MPServer* srv, int btn)
{
    Impl::Edit& E = I.edit; const int s = I.phoneSlot; if(!E.mode || s < 0) return;
    if(E.mode == 3)
    {   // SelectObject: the nearest object to me (OnPlayerSelectObject, type 1 = global object)
        if(btn == 21) { E = Impl::Edit(); srv->GateOp(s, SPO_EDIT, 0, 0, 0); return; }
        if(btn != 30 || !I.haveMe) return;
        int best = -1; float bd = 1e9f; const float* me = I.lastMe.pos;
        for(auto& kv : I.objs) { const float* p = kv.second.p; const float d = (p[0] - me[0]) * (p[0] - me[0]) + (p[1] - me[1]) * (p[1] - me[1]) + (p[2] - me[2]) * (p[2] - me[2]); if(d < bd) { bd = d; best = kv.first; } }
        if(best < 0 || bd > 50.0f * 50.0f) { srv->GateMsg(s, 0xA9C4E4FF, "[open.mp] No object within 50 m to select."); return; }
        const Impl::Obj& o = I.objs[best];
        BitStream b; b.Write((uint32_t)1); b.Write((uint16_t)best); b.Write((uint32_t)o.model); WrVec(b, o.p); CliRpc(I, 27, b);
        printf("[crossplay] selected object %d (model %u, %.0f m)\n", best, (unsigned)o.model, sqrtf(bd));
        return;
    }
    const float st = E.step, rs = E.mode == 2 ? 5.0f : 15.0f;
    switch(btn)
    {
        case 1: E.p[0] -= st; break; case 2: E.p[0] += st; break;
        case 3: E.p[1] -= st; break; case 4: E.p[1] += st; break;
        case 5: E.p[2] -= st; break; case 6: E.p[2] += st; break;
        case 7: E.r[0] = fmodf(E.r[0] + rs, 360.0f); break; case 8: E.r[1] = fmodf(E.r[1] + rs, 360.0f); break;
        case 9: E.r[2] = fmodf(E.r[2] - rs + 360.0f, 360.0f); break; case 10: E.r[2] = fmodf(E.r[2] + rs, 360.0f); break;
        case 11: E.step = E.mode == 2 ? (st >= 0.1f ? 0.01f : st * 2.5f) : (st >= 2.0f ? 0.05f : st * 4.0f); return;
        case 20: case 21: break;
        default: return;
    }
    const bool fin = btn == 20 || btn == 21;
    if(btn == 21) { memcpy(E.p, E.p0, sizeof(E.p)); memcpy(E.r, E.r0, sizeof(E.r)); }
    if(E.mode == 1)
    {
        auto it = I.objs.find(E.id);
        if(it != I.objs.end()) { memcpy(it->second.p, E.p, sizeof(E.p)); memcpy(it->second.r, E.r, sizeof(E.r)); srv->GateObject(it->second.row, it->second.model, E.p, E.r, true); }
        const uint32_t resp = btn == 21 ? 0 : (btn == 20 ? 1 : 2);   // EDIT_RESPONSE_CANCEL / FINAL / UPDATE
        BitStream b; b.Write(E.pobj); b.Write(E.id); b.Write(resp); WrVec(b, E.p); WrVec(b, E.r); CliRpc(I, 117, b);
    }
    else if(E.mode == 2)
    {
        Impl::Att& a = I.att[E.idx];
        memcpy(a.off, E.p, sizeof(a.off)); memcpy(a.rot, E.r, sizeof(a.rot));
        char t[96]; snprintf(t, sizeof(t), "%.3f %.3f %.3f %.3f %.3f %.3f", a.rot[0], a.rot[1], a.rot[2], a.sc[0], a.sc[1], a.sc[2]);
        float f4[4] = { a.off[0], a.off[1], a.off[2], 0 };
        srv->GateOp(s, SPO_ATTACH, (uint8_t)E.idx, (uint8_t)a.bone, RemModel(I, (int32_t)a.model), f4, t);
        if(fin)
        {   // OnPlayerEditAttachedObject (only the end answer, like samp.exe)
            BitStream b; b.Write((uint32_t)(btn == 20 ? 1 : 0)); b.Write(E.idx); b.Write(a.model); b.Write(a.bone);
            WrVec(b, a.off); WrVec(b, a.rot); WrVec(b, a.sc); b.Write(a.c1); b.Write(a.c2); CliRpc(I, 116, b);
        }
    }
    if(fin) { printf("[crossplay] edit %s\n", btn == 20 ? "saved" : "cancelled"); E = Impl::Edit(); srv->GateOp(s, SPO_EDIT, 0, 0, 0); }
}
static void RemActorShow(Impl& I, MPServer* srv, int id)
{
    auto it = I.actors.find(id); if(it == I.actors.end() || I.phoneSlot < 0) return;
    const Impl::Act& a = it->second; float f[4] = { a.p[0], a.p[1], a.p[2], a.a }; char t[16]; snprintf(t, sizeof(t), "%u", (unsigned)a.skin);
    srv->GateOp(I.phoneSlot, SPO_ACTOR, 1, a.invul ? 1 : 0, id, f, t);
}

static void RemReset(Impl& I, MPServer* srv);
static void SvFree(Impl& I, MPServer* srv);   // 5.02.29 (sampgate_voice.inc)
static void RemNewSession(Impl& I, MPServer* srv)
{   // 5.02.13 / 5.02.29: a new game (first join / reconnect): the old session's login dialog / textdraw login / spectate are
    // gone. They stayed set after a reconnect = no < Spawn > and no auto-spawn (the phone sat in class selection)
    const int s = I.phoneSlot;
    if(I.resetPending) RemReset(I, srv);   // reconnected - the old ghosts / list go (the server sends them again)
    if(s >= 0 && I.dlgOpen) srv->GateDialog(s, -1, 0, "", "", "", "");
    if(s >= 0 && I.tdSel) srv->GateOp(s, SPO_TDSELECT, 0, 0, 0);
    I.dlgOpen = false; I.tdSel = false; I.spect = false; I.noAuto = false; I.wantClass = false;
    I.loginUi = false; I.loginWaitSaid = false; I.tdShown = 0;
    SvFree(I, srv);   // 5.02.29: SampVoice starts again with the new connection's serverInfo
}
static void RemReset(Impl& I, MPServer* srv)
{   // ghosts, objects and textdraws of the PC server go away
    for(auto& kv : I.rp) if(kv.second.slot >= 0) srv->BridgeRemove(kv.second.slot);
    I.rp.clear(); I.vehModel.clear(); I.pcScore.clear(); I.scoreAskAt = 0; I.listFullAt = 0; I.resetPending = false; I.holdUntil = 0;
    RemClassSel(I, srv, false);
    if(I.phoneSlot >= 0) srv->GateOp(I.phoneSlot, SPO_PCLIST, 0, 0, 0);
    if(I.phoneSlot >= 0 && !I.markers.empty()) srv->GateOp(I.phoneSlot, SPO_MARKER, 2, 0, 0);   // 5.02.27
    I.markers.clear(); I.special = 0; I.targetSent = 0xFFFFFFFF;
    for(auto& kv : I.objs) { float z[3] = { 0, 0, 0 }; srv->GateObject(kv.second.row, kv.second.model, z, z, false); }
    I.objs.clear();
    if(I.edit.mode && I.phoneSlot >= 0) srv->GateOp(I.phoneSlot, SPO_EDIT, 0, 0, 0);   // 5.02.26
    I.edit = Impl::Edit(); for(auto& a : I.att) a = Impl::Att();
    // 5.02.62
    I.tagOff.clear(); I.vehPlate.clear(); I.trailer.clear(); I.objAtt.clear(); I.camObj = -1;
    I.camTarget = false; for(int& c : I.camLast) c = 0xFFFF;
    if(I.phoneSlot >= 0) { if(I.gravity != 0.008f) { float g[4] = { 1, 0, 0, 0 }; srv->GateOp(I.phoneSlot, SPO_GRAVITY, 0, 0, 0, g); } srv->GateOp(I.phoneSlot, SPO_CAMTARGET, 0, 0, 0); }
    I.gravity = 0.008f;
    if(I.phoneSlot >= 0) { srv->GateOp(I.phoneSlot, SPO_HUD, 0, 0, 0); srv->GateOp(I.phoneSlot, SPO_HUD, 1, 0, 0); srv->GateOp(I.phoneSlot, SPO_HUD, 2, 0, 0); }
    for(auto& kv : I.picks) { float z[3] = { 0, 0, 0 }; srv->GateObject(kv.second.o.row, kv.second.o.model, z, z, false); }
    I.picks.clear();
    for(int i = 0; i < kTdSlots; ++i) if(I.tdOwner[i] >= 0) { PktTextDraw t; memset(&t, 0, sizeof(t)); t.type = PKT_TEXTDRAW; t.tdId = (uint16_t)i; t.show = 0; srv->GateTextDraw(I.phoneSlot, t); I.tdOwner[i] = -1; }
    I.tdSlot.clear(); I.tdDef.clear();
    for(auto& kv : I.alabels) if(kv.second.shown && I.phoneSlot >= 0) srv->GateLabel(I.phoneSlot, kv.first, false, nullptr, 0, 0, "");   // 5.02.15
    I.alabels.clear(); I.vehPos.clear();
    I.money = 0; I.drunk = 0; I.statsAt = I.drunkAt = 0; memset(I.gunOk, 0, sizeof(I.gunOk)); I.lastArmour = 0;   // 5.02.15
    for(auto& kv : I.zones) { float f[4]; memcpy(f, kv.second.data(), sizeof(f)); char m[2] = { 4, 0 }; srv->GateOp(I.phoneSlot, SPO_ZONE, (uint8_t)(kv.first & 255), (uint8_t)(kv.first >> 8), 0, f, m); }
    I.zones.clear();
    for(auto& kv : I.vehs) srv->GateOp(I.phoneSlot, SPO_VEHICLE, 0, 0, kv.first);   // 5.02.25
    for(auto& kv : I.actors) srv->GateOp(I.phoneSlot, SPO_ACTOR, 0, 0, kv.first);
    if(I.menuOpen >= 0) srv->GateDialog(I.phoneSlot, -1, 0, "", "", "", "");
    I.vehs.clear(); I.myVeh = -1; I.vsHave = false; I.modShop = 0; I.pcTeam.clear(); I.menus.clear(); I.menuOpen = -1; I.actors.clear(); I.lastHp = I.lastAr = -1;
}

static void RemEnd(Impl& I, MPServer* srv, const char* why)
{
    if(I.phase == 9) return;
    char b[200]; snprintf(b, sizeof(b), "[open.mp] %s", why);
    if(I.phoneSlot >= 0) srv->GateMsg(I.phoneSlot, 0xFF6060FF, b);
    RemReset(I, srv);
    SvFree(I, srv);
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
// 5.02.21: a chat line with nothing to see: only spaces / {RRGGBB} codes / no-break spaces. Servers send 20-100 of
// them to "clear the chat" (on join, after login, /clearchat) - on the phone they pushed every real line out of the box
// and looked like players chatting with blank messages.
static bool ChatBlank(const std::string& t)
{
    for(size_t i = 0; i < t.size(); )
    {
        const unsigned char c = (unsigned char)t[i];
        if(c == ' ' || c == '\t' || c == '\r' || c == '\n') { ++i; continue; }
        if(c == 0xC2 && i + 1 < t.size() && (unsigned char)t[i + 1] == 0xA0) { i += 2; continue; }   // UTF-8 no-break space
        if(c == '{' && i + 7 < t.size() && t[i + 7] == '}')
        {
            bool hex = true; for(int k = 1; k <= 6; ++k) if(!isxdigit((unsigned char)t[i + k])) hex = false;
            if(hex) { i += 8; continue; }
        }
        return false;
    }
    return true;
}
static void RemChatBlank(Impl& I)
{
    ++I.blankN; const uint64_t n = NowMs();
    if(n - I.blankAt > 5000) { I.blankAt = n; printf("[crossplay] the server cleared the chat - empty lines left out (%u so far)\n", I.blankN); }
}

static void RemChatSum(Impl& I)
{   // 5.02.22: once a minute in mp_log.txt: what came from the PC server
    const uint64_t n = NowMs(); if(!I.chatSumAt) { I.chatSumAt = n; return; }
    if(n - I.chatSumAt < 60000) return; I.chatSumAt = n;
    printf("[crossplay] chat so far: %u server messages, %u player chat lines, %u empty lines left out\n", I.tot93, I.tot101, I.blankN);
}
// 5.02.23: chat to the PC server works like samp.exe's chat box. SA-MP / open.mp read at most 128 bytes (a longer
// line is dropped, nobody sees it), so a long line is split at a space into parts of 128. Parts go 1 s apart - many
// scripts kick for 3 lines in 2 s. open.mp's chat_input_filter turns % and ~k into # and {RRGGBB} into a space.
// What the server does with the line is its OnPlayerText: return 1 = the server sends it to everyone (RPC 101),
// return 0 = the script sends its own line (SendClientMessage(ToAll) / ProxDetector local chat / SendPlayerMessageToAll)
// or nothing (muted, not logged in). We watch for our line coming back to tell the phone player once if it never does.
static void RemChatQueue(Impl& I, const std::string& t)
{
    std::string rest = t;
    while(!rest.empty() && I.chatOut.size() < 8)
    {
        if(rest.size() <= 128) { I.chatOut.push_back(rest); break; }
        size_t cut = rest.rfind(' ', 128); if(cut == std::string::npos || cut < 64) cut = 128;
        I.chatOut.push_back(rest.substr(0, cut));
        rest = rest.substr(cut); while(!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
    }
}
static void RemChatSend(Impl& I, MPServer* srv, uint64_t now)
{
    if(!I.echoWant.empty() && I.echoAt && now - I.echoAt > 5000)
    {   // nothing with our text came back in 5 s
        ++I.echoMissN; printf("[crossplay] your chat line did not come back from the server (%u so far) - its OnPlayerText returned 0 without resending it\n", I.echoMissN);
        I.echoWant.clear();
        if(!I.echoHint && I.echoMissN >= 2)
        {
            I.echoHint = true;
            srv->GateMsg(I.phoneSlot, 0xA9C4E4FF, I.phase < 4 ? "[open.mp] This server does not show chat before you spawn / log in."
                                                             : "[open.mp] This server did not show your chat - it may use local chat (only players near you see it), want a login, or you are muted.");
        }
    }
    if(I.chatOut.empty() || I.phase < 3 || now - I.chatOutAt < 1000) return;
    const std::string t = I.chatOut.front(); I.chatOut.pop_front(); I.chatOutAt = now;
    BitStream o; WrStr8(o, t); CliRpc(I, 101, o);
    if(I.chatSentN < 5) { ++I.chatSentN; printf("[crossplay] chat sent (%u bytes): %.100s\n", (unsigned)t.size(), t.c_str()); }
    // the start of the line as the server shows it (after chat_input_filter), in UTF-8 like the lines that come back
    // (up to the first % ~ { - the filter changes those), lower case: RP scripts often capitalise the first letter
    std::string w = SampCp::ToUtf8(t, I.cp);
    w = w.substr(0, w.find_first_of("%~{"));
    while(!w.empty() && w[0] == ' ') w.erase(0, 1);
    if(w.size() > 20) w.resize(20);
    while(!w.empty() && ((unsigned char)w.back() >= 0x80 || w.back() == ' ')) w.pop_back();   // never end inside a letter
    for(char& c : w) c = (char)tolower((unsigned char)c);
    if(w.size() >= 3) { I.echoWant = w; I.echoAt = now; } else I.echoWant.clear();
}

static Impl::Rp* RemPlayer(Impl& I, int id) { auto it = I.rp.find(id); return it == I.rp.end() ? nullptr : &it->second; }

static void RemGhostIn(Impl& I, MPServer* srv, int id, const float* pos, float ang)
{
    Impl::Rp* p = RemPlayer(I, id); if(!p) return;
    if(p->slot < 0)
    {
        p->slot = srv->BridgeAdd(p->name.c_str(), p->color, p->skin);
        // 5.02.62: ShowPlayerNameTagForPlayer - the phone keeps tags per MP slot, so a reused slot gets its state again
        if(p->slot >= 0 && I.phoneSlot >= 0) { auto t = I.tagOff.find(id); srv->GateOp(I.phoneSlot, SPO_TAGS, 1, (t != I.tagOff.end() && t->second) ? 0 : 1, p->slot); }
    }
    if(p->slot < 0) return;
    if(pos) { SyncData d; memset(&d, 0, sizeof(d)); memcpy(d.pos, pos, sizeof(d.pos)); d.heading = ang; d.health = 100; d.skin = p->skin; memcpy(d.aim, pos, sizeof(d.aim)); p->last = d; p->hasPos = true; srv->BridgeSync(p->slot, d); }
}

#include "sampgate_voice.inc"   // 5.02.29: SampVoice voice chat

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
    I.spect = false; I.special = 0;   // 5.02.27: a spawned player is not spectating any more
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

static bool RdSpawn(BitStream& bs, Impl::Spawn& sp, bool dl)
{   // 5.02.23: 0.3.DL class answer (128) / SetSpawnInfo (68) have a custom skin (uint32) right after the skin. It was read
    // as the spawn position: on DL servers the phone spawned at a wrong / garbage spot with no weapons
    uint8_t team, unk; uint32_t custom = 0;
    if(!bs.Read(team) || !bs.Read(sp.skin) || (dl && !bs.Read(custom)) || !bs.Read(unk) || !RdVec(bs, sp.pos) || !bs.Read(sp.rot)) return false;
    sp.team = team;
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
            // 5.02.23: 0x48 (72) answers with the computer's uptime in the address field (open.mp opcodes page) - samp.exe
            // sends GetTickCount(). 0 looked like no PC client to scripts that check memaddr too
            if(type == 0x48) { static const uint32_t base = 3600000u + (uint32_t)(NowMs() % 7200000u); addr = base + (uint32_t)NowMs(); }
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
            // 5.02.29: the old session's state is cleared when the server accepts the join (RemNewSession), not here:
            // open.mp runs OnPlayerConnect before InitGame reaches us, so its TogglePlayerSpectating / textdraws /
            // SelectTextDraw came first and were wiped here. MP then thought it was not spectating and sent on-foot
            // sync from the login screen - Life of Player's anti-cheat blocked the connection ("Lost connection")
            I.autoSpawnN = 0; I.retrySpawnAt = 0;
            I.spawnAskAt = 0; I.finDl = false; I.classAt = NowMs();
            I.spawnAnsAt = 0; I.spawnSilent = 0; I.directSpawn = false;
            I.classOk = true; I.spawnAfterClass = false; I.chatOut.clear(); I.echoWant.clear(); I.echoAt = 0;   // 5.02.23
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
            BitStream o; o.Write((int32_t)0); CliRpc(I, 128, o);
            RemClassSel(I, srv, true);   // 5.02.13: < Spawn > on the phone
            break;
        }
        case 128:  // class answer
        {
            uint8_t sel = 0; bs.Read(sel); Impl::Spawn sp;
            // 5.02.23: Selectable 0 = OnPlayerRequestClass returned 0 - the rest of the answer is zeros, keep the old spawn
            if(RdSpawn(bs, sp, I.cliVer == kSampVersionDL) && sel) I.spawn = sp;
            const bool wasOk = I.classOk; I.classOk = sel != 0;
            if(wasOk != I.classOk) printf("[crossplay] class %d %s\n", I.classIdx, sel ? "selectable" : "locked by OnPlayerRequestClass (return 0)");
            {   // 5.02.20: in mp_log.txt once per join - a server with no classes leaves Spawn unanswered
                static uint64_t said = 0; if(said != I.classAt) { said = I.classAt;
                    printf("[crossplay] class answer: selectable %u, skin %u, spawn %.0f %.0f %.0f\n", (unsigned)sel, (unsigned)I.spawn.skin, I.spawn.pos[0], I.spawn.pos[1], I.spawn.pos[2]); }
            }
            if(I.phase < 4)
            {
                const uint64_t now = NowMs();
                if(now - I.autoSpawnWin > 30000) { I.autoSpawnWin = now; I.autoSpawnN = 0; }
                if(I.spawnAfterClass)
                {   // the phone pressed Spawn on a locked class: asked again, now spawn (or say why not)
                    I.spawnAfterClass = false;
                    if(sel) { BitStream o; CliRpc(I, 129, o); I.spawnAskAt = now; break; }
                    if(now - I.refuseMsgAt > 8000) { I.refuseMsgAt = now; srv->GateMsg(s, 0xA9C4E4FF, "[open.mp] The server does not let you spawn with this class yet - log in first, or pick another class with < >."); }
                    break;
                }
                if(sel && !I.spect && !I.dlgOpen && !I.tdSel && !I.noAuto) I.retrySpawnAt = NowMs() + 5000;   // 5 s: account loading, then the login dialog   // 5.02.10: wait - a login dialog usually comes right after the class answer
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
        case 68: { Impl::Spawn sp; if(RdSpawn(bs, sp, I.cliVer == kSampVersionDL)) I.spawn = sp; break; }   // SetSpawnInfo
        case 74:   // ForceClassSelection
            if(I.phase >= 4) I.wantClass = true;   // 5.02.09: the PC client only goes back on the next respawn / spectate off
            break;
        case 124:  // TogglePlayerSpectating
            bs.Read(u32a);
            printf("[crossplay] TogglePlayerSpectating %u\n", u32a);   // 5.02.17
            if(!u32a && I.specOn) { I.specOn = false; srv->GateAct(s, CGA_SPEC, 0, 0); srv->GateOp(s, SPO_CAMERA, 0, 0, 0); }   // 5.02.26: spectate ends
            if(!u32a && I.spect && I.phase == 3 && !I.wantClass && I.spawn.have)
            {   // 5.02.28: spectating ends in class selection = the PC client spawns by itself (no Spawn button, no
                // RequestSpawn). Login servers (Life of Player...) spawn you this way after the account step - we sent
                // RequestSpawn instead, the script refused it and the phone hung on the login camera
                printf("[crossplay] spectating ended in class selection - spawned like the PC client\n");
                RemSpawned(I, srv); break;
            }
            I.spect = u32a != 0;   // 5.02.10: spectating (account loading / login camera) = no Spawn button for a PC player either
            if(I.phase == 3) I.retrySpawnAt = I.spect ? 0 : NowMs() + 1500;
            if(!u32a && I.wantClass && I.phase >= 4) { I.wantClass = false; I.phase = 3; I.noAuto = true;
                srv->GateMsg(I.phoneSlot, 0xA9C4E4FF, "[open.mp] The server sent you back to class selection - log in (dialog or its /command) and you spawn after that."); BitStream o; o.Write((int32_t)I.classIdx); CliRpc(I, 128, o); RemClassSel(I, srv, true); }
            break;
        case 137:  // player joined
        {
            bs.Read(u16a); bs.Read(u32a); bs.Read(u8a); std::string nm = RdStr8(bs);
            if(u16a == I.myId) break;
            Impl::Rp& p = I.rp[u16a]; p.name = nm; p.color = u32a;
            float f4[4] = { 0, 0, 0, 0 }; srv->GateOp(s, SPO_PCLIST, 1, 0, (int32_t)u16a, f4, nm.c_str());   // 5.02.12: the full player list
            break;
        }
        case 138: bs.Read(u16a); if(I.markers.count(u16a) && I.markers[u16a].on) srv->GateOp(s, SPO_MARKER, 0, 0, u16a); I.markers.erase(u16a); if(Impl::Rp* p = RemPlayer(I, u16a)) { if(p->slot >= 0) srv->BridgeRemove(p->slot); I.rp.erase(u16a); } I.pcScore.erase(u16a); srv->GateOp(s, SPO_PCLIST, 2, 0, (int32_t)u16a); break;
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
            bs.Read(u16a); bs.Read(u8a); bs.Read(u32a);
            if(I.cliVer == kSampVersionDL) { uint32_t custom = 0; bs.Read(custom); }   // 5.02.23: DL custom skin - was read as the position
            RdVec(bs, v); bs.Read(fa); bs.Read(u32b);
            if(I.markers.count(u16a) && I.markers[u16a].on) { I.markers[u16a].on = false; srv->GateOp(s, SPO_MARKER, 0, 0, u16a); }   // 5.02.27: its ped has the blip now
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
            // 5.02.23: 0.3.DL: uint16 player id + skin + custom skin (0.3.7: uint32 id + skin) - DL skins went to the wrong player
            if(I.cliVer == kSampVersionDL) { bs.Read(u16a); u32a = u16a; bs.Read(u32b); } else { bs.Read(u32a); bs.Read(u32b); }
            u32b = (uint32_t)RemModel(I, (int32_t)u32b);
            if((int)u32a == I.myId) { srv->GateAct(s, CGA_SKIN, 0, 0, (int32_t)u32b); I.spawn.skin = u32b; }   // 5.02.20: kept for a direct spawn
            else if(Impl::Rp* p = RemPlayer(I, (int)u32a)) p->skin = (uint16_t)u32b;
            break;
        case 72: bs.Read(u16a); bs.Read(u32a); if(Impl::Rp* p = RemPlayer(I, u16a)) p->color = u32a; break;
        case 164:  // vehicle stream in (model for the drivers)
            bs.Read(u16a); bs.Read(u32a); I.vehModel[u16a] = (uint16_t)u32a;
            if(RdVec(bs, v))
            {
                std::array<float, 3> vp = { v[0], v[1], v[2] }; I.vehPos[u16a] = vp;   // 5.02.15: labels on parked cars
                // 5.02.25: the car itself on the phone (CreateVehicle / AddStaticVehicle) - before, only a PC player's
                // car was there while he drove it, so the phone could never get in a server car
                Impl::Veh& vh = I.vehs[u16a]; vh.model = (uint16_t)I.vehModel[u16a]; memcpy(vh.p, v, sizeof(vh.p));
                bs.Read(fa); vh.a = fa; bs.Read(u8a); bs.Read(u8b); vh.c1 = u8a; vh.c2 = u8b;
                // 5.02.26: interior (LinkVehicleToInterior), parts (AddVehicleComponent), paintjob (+ 1, 0 = none), damage
                float hp = 1000; uint8_t inter = 0, lights = 0, tyres = 0, siren = 0, mods[14] = {}, pj = 0; uint32_t doors = 0, panels = 0;
                bs.Read(hp); bs.Read(inter); bs.Read(doors); bs.Read(panels); bs.Read(lights); bs.Read(tyres); bs.Read(siren);
                for(auto& m : mods) bs.Read(m);
                const bool full = bs.Read(pj);
                vh.interior = inter; vh.mods.clear(); vh.paintjob = -1;
                if(full) { for(uint8_t m : mods) if(m) vh.mods.push_back((uint16_t)(m + 999)); if(pj >= 1 && pj <= 3) vh.paintjob = pj - 1; }
                if(!vh.taken) RemVehShow(I, srv, u16a);
                if(!vh.taken && I.phoneSlot >= 0 && (doors || panels || lights || tyres))
                {
                    char t[64]; snprintf(t, sizeof(t), "%u %u %u %u", panels, doors, (unsigned)lights, (unsigned)tyres);
                    srv->GateOp(I.phoneSlot, SPO_VEHMOD, 5, 0, u16a, nullptr, t);
                }
            }
            break;
        case 165: bs.Read(u16a); I.vehModel.erase(u16a); I.vehPos.erase(u16a); I.vehs.erase(u16a); srv->GateOp(s, SPO_VEHICLE, 0, 0, u16a); break;
        // ---- 5.02.25: cars, teams, menus, actors --------------------------------------------------------
        case 159: { bs.Read(u16a); if(!RdVec(bs, v)) break; auto it = I.vehs.find(u16a); if(it == I.vehs.end()) break; memcpy(it->second.p, v, sizeof(v)); if(u16a != I.myVeh && !it->second.taken) RemVehShow(I, srv, u16a); break; }   // SetVehiclePos
        case 160: { bs.Read(u16a); bs.Read(fa); auto it = I.vehs.find(u16a); if(it == I.vehs.end()) break; it->second.a = fa; if(u16a != I.myVeh && !it->second.taken) RemVehShow(I, srv, u16a); break; }   // SetVehicleZAngle
        case 70: bs.Read(u16a); bs.Read(u8a); if(I.vehs.count(u16a)) srv->GateOp(s, SPO_VEHICLE, 3, u8a, u16a); break;   // PutPlayerInVehicle
        case 71: srv->GateAct(s, CGA_EJECT); break;   // RemovePlayerFromVehicle
        case 147: bs.Read(u16a); bs.Read(fa); if(u16a == I.myVeh) { I.myVehHp = fa; srv->GateAct(s, CGA_CARHP, 0, 0, (int32_t)fa); } break;   // SetVehicleHealth
        case 24:
        {   // SetVehicleParamsEx: my car -> engine / lights / doors, any car -> doors locked (car-lock scripts)
            uint8_t pr[7] = { 0 }; bs.Read(u16a); for(auto& x : pr) bs.Read(x);
            auto it = I.vehs.find(u16a); if(it == I.vehs.end()) break;
            const bool lock = pr[3] == 1; if(lock != it->second.locked) { it->second.locked = lock; srv->GateOp(s, SPO_VEHICLE, 4, lock ? 1 : 0, u16a); }
            if(u16a == I.myVeh) srv->GateOp(s, SPO_VEHPARAMS, (uint8_t)(pr[0] <= 1 ? pr[0] + 1 : 0), (uint8_t)(pr[3] <= 1 ? pr[3] + 1 : 0), pr[1] <= 1 ? pr[1] : -1);
            break;
        }
        case 69: bs.Read(u16a); bs.Read(u8a); if(u16a == I.myId) I.spawn.team = u8a; else I.pcTeam[u16a] = u8a; break;   // SetPlayerTeam
        case 76:
        {   // InitMenu: u8 id, u32 two columns, char[32] title, vec2, col widths, enabled flags, header + items per column
            Impl::Menu m; uint8_t id = 0; uint32_t two = 0; bs.Read(id); bs.Read(two); m.two = two != 0;
            auto fixed = [&]() { char t[33] = { 0 }; bs.Read(t, 32); t[32] = 0; return SampCp::ToUtf8(t, I.cp); };
            m.title = fixed(); float f2[2]; bs.Read(f2[0]); bs.Read(f2[1]); bs.Read(fa); if(m.two) bs.Read(fa);
            bs.Read(u32a); for(int i = 0; i < 12; ++i) bs.Read(u32a);
            m.h1 = fixed(); uint8_t n = 0; bs.Read(n); for(int i = 0; i < n && i < 12; ++i) m.c1.push_back(fixed());
            if(m.two) { m.h2 = fixed(); n = 0; bs.Read(n); for(int i = 0; i < n && i < 12; ++i) m.c2.push_back(fixed()); }
            I.menus[id] = m; break;
        }
        case 77:
        {   // ShowMenuForPlayer -> a list window (Select = OnPlayerSelectedMenuRow, Close = OnPlayerExitedMenu)
            bs.Read(u8a); auto it = I.menus.find(u8a); if(it == I.menus.end() || it->second.c1.empty()) break;
            const Impl::Menu& m = it->second; std::string body;
            for(size_t i = 0; i < m.c1.size(); ++i) { body += m.c1[i]; if(m.two && i < m.c2.size()) { body += '\t'; body += m.c2[i]; } body += '\n'; }
            if(m.two) body = m.h1 + "\t" + m.h2 + "\n" + body;
            I.menuOpen = u8a; srv->GateDialog(s, kMenuDialog, m.two ? 5 : 2, m.title.c_str(), body.c_str(), "Select", "Close"); break;
        }
        case 78: bs.Read(u8a); if(I.menuOpen == (int)u8a) { I.menuOpen = -1; srv->GateDialog(s, -1, 0, "", "", "", ""); } break;   // HideMenuForPlayer
        case 171:
        {   // ShowActorForPlayer (0.3.DL: + custom skin)
            Impl::Act a; bs.Read(u16a); bs.Read(a.skin); if(I.cliVer == kSampVersionDL) bs.Read(u32a);
            if(!RdVec(bs, a.p)) break; bs.Read(a.a); bs.Read(fa); bs.Read(u8a); a.invul = u8a != 0; a.hp = fa;
            I.actors[u16a] = a; RemActorShow(I, srv, u16a);
            { float h[4] = { fa, 0, 0, 0 }; srv->GateOp(s, SPO_ACTOR, 4, 0, u16a, h); }   // 5.02.62: its health (CreateActor / SetActorHealth)
            break;
        }
        case 172: bs.Read(u16a); I.actors.erase(u16a); srv->GateOp(s, SPO_ACTOR, 0, 0, u16a); break;
        case 173:
        {   // ApplyActorAnimation
            bs.Read(u16a); std::string lib = RdStr8(bs), anim = RdStr8(bs); bs.Read(fa); bool loop = false; bs.Read(loop);
            char t[120]; snprintf(t, sizeof(t), "%s %s %d", lib.c_str(), anim.c_str(), loop ? 1 : 0); srv->GateOp(s, SPO_ACTOR, 2, 0, u16a, nullptr, t); break;
        }
        case 174: bs.Read(u16a); srv->GateOp(s, SPO_ACTOR, 3, 0, u16a); break;
        case 175: { bs.Read(u16a); bs.Read(fa); auto it = I.actors.find(u16a); if(it != I.actors.end()) { it->second.a = fa; RemActorShow(I, srv, u16a); } break; }
        case 176: { bs.Read(u16a); if(!RdVec(bs, v)) break; auto it = I.actors.find(u16a); if(it != I.actors.end()) { memcpy(it->second.p, v, sizeof(v)); RemActorShow(I, srv, u16a); } break; }
        case 93:
        {
            bs.Read(u32a); std::string t = SampCp::ToUtf8(RdStr32(bs), I.cp);
            if(ChatBlank(t)) { RemChatBlank(I); break; }   // 5.02.21
            ++I.tot93;
            if(!I.echoWant.empty())
            {   // 5.02.23: own chat came back as a server line (server chat / ProxDetector local chat)
                std::string lo = t; for(char& c : lo) c = (char)tolower((unsigned char)c);
                if(lo.find(I.echoWant) != std::string::npos) I.echoWant.clear();
            }   // 5.02.23: own chat came back (server chat / ProxDetector)
            if(I.srvMsgN < 10) { ++I.srvMsgN; printf("[crossplay] server message %08X: %.100s\n", u32a, t.c_str()); }
            else if(I.nameMsgN < 15)
            {   // 5.02.22: servers with their own chat (OnPlayerText return 0) send players' chat as server messages
                for(auto& kv : I.rp)
                    if(kv.second.name.size() > 2 && t.find(kv.second.name) != std::string::npos)
                    { ++I.nameMsgN; printf("[crossplay] server message with %s's name (server chat): %.100s\n", kv.second.name.c_str(), t.c_str()); break; }
            }
            RemChatSum(I);
            srv->GateMsg(s, u32a, t.c_str()); break;
        }
        case 101:
        {
            bs.Read(u16a); std::string t = SampCp::ToUtf8(RdStr8(bs), I.cp);
            if(ChatBlank(t)) { RemChatBlank(I); break; }   // 5.02.21
            ++I.tot101; RemChatSum(I);
            if(I.pcChatN < 10) { ++I.pcChatN; printf("[crossplay] player chat from id %u: %.100s\n", (unsigned)u16a, t.c_str()); }   // 5.02.21: shows in mp_log.txt that PC chat arrives
            if(u16a == I.myId) { I.echoWant.clear(); srv->GateChat(s, t.c_str()); break; }
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
            if(!create) { I.att[idx].on = false; srv->GateOp(s, SPO_ATTACH, (uint8_t)idx, 0, -1); break; }
            uint32_t model = 0, bone = 0; float off[3], rot[3], sc[3]; bs.Read(model); bs.Read(bone); RdVec(bs, off); RdVec(bs, rot); RdVec(bs, sc);
            {   // 5.02.26: kept for EditAttachedObject (its answer sends the whole attachment back)
                Impl::Att& at = I.att[idx]; at.on = true; at.model = model; at.bone = bone; memcpy(at.off, off, sizeof(off)); memcpy(at.rot, rot, sizeof(rot)); memcpy(at.sc, sc, sizeof(sc));
                at.c1 = at.c2 = 0; bs.Read(at.c1); bs.Read(at.c2);
            }
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
        case 156: bs.Read(u8a); I.interior = u8a; srv->GateOp(s, SPO_WORLD, u8a, 0, 0); break;
        case 48: if(bs.Read(i32)) I.vworld = i32; break;   // 5.02.24: SetPlayerVirtualWorld (for /vw)
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
            I.dlgOpen = id >= 0; if(I.dlgOpen) I.loginUi = true; if(I.phase == 3) I.retrySpawnAt = I.dlgOpen ? 0 : NowMs() + 1500;   // 5.02.10: login first
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
            // 5.02.26: a sprite / model preview (font 4 / 5) is TextSize X by Y big; the phone's TdDrawOne reads
            // the size from letterX / letterY. They used to get the 0.48 x 1.12 letter size - invisible 1-pixel
            // boxes, so menus like Life of Player's language picker showed only text and could not be tapped
            if(style == 4 || style == 5) { t.letterX = tx; t.letterY = ty; t.boxX = 0; t.useBox = 0; t.align = 1; }
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
        case 83: { bool on = false; bs.Read(on); bs.Read(u32a); I.tdSel = on; if(on) I.loginUi = true; printf("[crossplay] SelectTextDraw %s (%d textdraws shown)\n", on ? "on" : "off", I.tdShown); if(I.phase == 3) I.retrySpawnAt = on ? 0 : NowMs() + 1500; srv->GateOp(s, SPO_TDSELECT, on ? 1 : 0, 0, (int32_t)u32a); break; }
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
        case 47: { bs.Read(u16a);
                   { auto at = I.objAtt.find(u16a); if(at != I.objAtt.end()) { I.att[at->second].on = false; srv->GateOp(s, SPO_ATTACH, (uint8_t)at->second, 0, -1); I.objAtt.erase(at); } }   // 5.02.62
                   if(I.camObj == (int)u16a) I.camObj = -1;
                   auto it = I.objs.find(u16a); if(it == I.objs.end()) break; srv->GateObject(it->second.row, it->second.model, it->second.p, it->second.r, false); I.objs.erase(it); break; }
        case 45: case 99:
        {
            bs.Read(u16a); RdVec(bs, v); auto it = I.objs.find(u16a); if(it == I.objs.end()) break;
            if(r.id == 99) { float to[3], sp, tr[3]; RdVec(bs, to); bs.Read(sp); if(RdVec(bs, tr) && tr[0] > -999.0f) memcpy(it->second.r, tr, sizeof(tr)); memcpy(v, to, sizeof(v)); }
            memcpy(it->second.p, v, sizeof(v)); srv->GateObject(it->second.row, it->second.model, it->second.p, it->second.r, true);
            if(I.camObj == (int)u16a) srv->GateOp(s, SPO_CAMERA, 1, 0, 0, v);   // 5.02.62: AttachCameraToObject on a moving object
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
        case 157: I.camObj = -1; RdVec(bs, v); srv->GateOp(s, SPO_CAMERA, 1, 0, 0, v); break;
        case 158: RdVec(bs, v); srv->GateOp(s, SPO_CAMERA, 2, 0, 0, v); break;
        case 162: I.camObj = -1; srv->GateOp(s, SPO_CAMERA, 0, 0, 0); break;
        case 82: { I.camObj = -1; bool pos = false; bs.Read(pos); RdVec(bs, v); RdVec(bs, w3); srv->GateOp(s, SPO_CAMERA, pos ? 1 : 2, 0, 0, w3); break; }
        case 16: bs.Read(u32a); RdVec(bs, v); srv->GateOp(s, SPO_SOUND, 0, 0, (int32_t)u32a, v); break;
        case 41: { std::string url = RdStr8(bs); RdVec(bs, v); bs.Read(fa); bs.Read(u8a); float f[4] = { v[0], v[1], v[2], fa }; srv->GateOp(s, SPO_AUDIO, 1, u8a ? 1 : 0, 0, f, url.c_str()); break; }
        case 42: srv->GateOp(s, SPO_AUDIO, 0, 0, 0); break;
        case 90: RdVec(bs, v); srv->GateOp(s, SPO_VELOCITY, 0, 0, 0, v); break;
        case 91: bs.Read(u8a); RdVec(bs, v); srv->GateOp(s, SPO_VELOCITY, 0, 1, 0, v); break;
        // ---- 5.02.26: the rest of the open.mp functions that reach the client (https://open.mp/docs/server/omp-functions) ----
        case 111: bs.Read(u8a); srv->GateOp(s, SPO_HUD, 0, u8a ? 1 : 0, 0); break;   // ToggleWidescreen
        case 30: bs.Read(u8a); srv->GateOp(s, SPO_HUD, 1, u8a ? 1 : 0, 0); break;    // TogglePlayerClock
        case 104: bs.Read(u8a); srv->GateOp(s, SPO_HUD, 4, u8a ? 1 : 0, 0); break;   // EnableStuntBonusForPlayer / All
        case 17:   // SetPlayerWorldBounds (max x, min x, max y, min y) - 20000 / -20000 = off
        {
            float f[4] = { 0, 0, 0, 0 }; bs.Read(f[0]); bs.Read(f[1]); bs.Read(f[2]); bs.Read(f[3]);
            const bool off = f[0] >= 19999.0f && f[1] <= -19999.0f && f[2] >= 19999.0f && f[3] <= -19999.0f;
            srv->GateOp(s, SPO_HUD, 2, off ? 0 : 1, 0, f); break;
        }
        case 34: { uint32_t skill = 0; uint16_t lvl = 0; bs.Read(u16a); bs.Read(skill); bs.Read(lvl); if((int)u16a == I.myId && skill <= 10) srv->GateOp(s, SPO_HUD, 3, (uint8_t)skill, lvl); break; }   // SetPlayerSkillLevel
        case 94: bs.Read(u8a); srv->GateOp(s, SPO_TIME, u8a % 24, 0, -1); break;       // SetPlayerTime (hour only: SetWorldTime)
        case 79:   // CreateExplosion / CreateExplosionForPlayer
        {
            RdVec(bs, v); bs.Read(u32a); bs.Read(fa);
            srv->GateAct(s, CGA_EXPLODE, (int)std::min<uint32_t>(u32a, 12), 0, 0, v); break;
        }
        case 55:   // SendDeathMessage / SendDeathMessageToPlayer: the kill feed
        {
            uint16_t killer = kInvalid, victim = kInvalid; bs.Read(killer); bs.Read(victim); bs.Read(u8a);
            auto nm = [&](uint16_t id) -> std::string {
                if(id == kInvalid) return "";
                uint32_t col = 0xFFFFFFFF; std::string n;
                if((int)id == I.myId) { n = I.rName; auto it = I.rp.find(id); if(it != I.rp.end()) col = it->second.color; }
                else { Impl::Rp* p = RemPlayer(I, id); if(p) { n = p->name; col = p->color; } }
                if(n.empty()) { char b[16]; snprintf(b, sizeof(b), "#%u", (unsigned)id); n = b; }
                char c[16]; snprintf(c, sizeof(c), "{%06X}", (unsigned)((col >> 8) & 0xFFFFFF)); return c + n;
            };
            const std::string t = nm(killer) + "\t" + nm(victim);
            srv->GateOp(s, SPO_DEATHMSG, 0, 0, u8a, nullptr, t.c_str()); break;
        }
        case 87: bs.Read(u16a); if((int)u16a == I.myId) srv->GateOp(s, SPO_ACTION, 0, 0, 0); break;   // ClearAnimations
        case 88: bs.Read(u8a); I.special = (u8a == 2 || u8a == 1) ? 0 : u8a; srv->GateOp(s, SPO_ACTION, 1, 0, u8a); break;   // SetPlayerSpecialAction (jetpack / duck come from the ped itself)
        case 89: bs.Read(u16a); bs.Read(u8a); if((int)u16a == I.myId) srv->GateOp(s, SPO_ACTION, 2, 0, u8a); break;   // SetPlayerFightingStyle
        case 126:  // PlayerSpectatePlayer: the camera on that PC player's ghost
        {
            bs.Read(u16a); bs.Read(u8a); Impl::Rp* p = RemPlayer(I, u16a);
            if(p && p->slot >= 0) srv->GateAct(s, CGA_SPEC, p->slot, 1);
            else if(p && p->hasPos) { float c[4] = { p->last.pos[0], p->last.pos[1], p->last.pos[2] + 6.0f, 0 }; srv->GateOp(s, SPO_CAMERA, 1, 0, 0, c); float l[4] = { p->last.pos[0], p->last.pos[1], p->last.pos[2], 0 }; srv->GateOp(s, SPO_CAMERA, 2, 0, 0, l); }
            I.specOn = true; break;
        }
        case 127:  // PlayerSpectateVehicle: its driver's ghost, else a camera over the car
        {
            bs.Read(u16a); bs.Read(u8a); int slot = -1;
            for(auto& kv : I.rp) if(kv.second.vehicle == (int)u16a && kv.second.slot >= 0) { slot = kv.second.slot; break; }
            if(slot >= 0) srv->GateAct(s, CGA_SPEC, slot, 1);
            else { auto it = I.vehPos.find(u16a); if(it != I.vehPos.end()) { float c[4] = { it->second[0], it->second[1] - 8.0f, it->second[2] + 5.0f, 0 }; srv->GateOp(s, SPO_CAMERA, 1, 0, 0, c); float l[4] = { it->second[0], it->second[1], it->second[2], 0 }; srv->GateOp(s, SPO_CAMERA, 2, 0, 0, l); } }
            I.specOn = true; break;
        }
        case 46:   // SetObjectRotation
        {
            bs.Read(u16a); RdVec(bs, v); auto it = I.objs.find(u16a); if(it == I.objs.end()) break;
            memcpy(it->second.r, v, sizeof(v)); srv->GateObject(it->second.row, it->second.model, it->second.p, it->second.r, true); break;
        }
        case 122: break;   // StopObject: the phone puts moved objects at their end spot at once (MoveObject), nothing to stop
        case 96:   // SCMEvent from the server: ChangeVehicleColours / ChangeVehiclePaintjob / AddVehicleComponent on a server car
        {
            uint32_t ev = 0, vid = 0, a1 = 0, a2 = 0; bs.Read(u16a); bs.Read(ev); bs.Read(vid); bs.Read(a1); bs.Read(a2);
            auto it = I.vehs.find((int)vid); if(it == I.vehs.end()) break;
            Impl::Veh& vh = it->second;
            if(ev == 1) { vh.paintjob = a1 <= 2 ? (int)a1 : -1; if(!vh.taken) srv->GateOp(s, SPO_VEHMOD, 1, (uint8_t)(vh.paintjob < 0 ? 255 : vh.paintjob), (int32_t)vid); }
            else if(ev == 2 && a1 >= 1000 && a1 <= 1193)
            {
                if(std::find(vh.mods.begin(), vh.mods.end(), (uint16_t)a1) == vh.mods.end()) vh.mods.push_back((uint16_t)a1);
                float c[4] = { (float)a1, 0, 0, 0 }; if(!vh.taken) srv->GateOp(s, SPO_VEHMOD, 2, 0, (int32_t)vid, c);
            }
            else if(ev == 3) { vh.c1 = (uint8_t)a1; vh.c2 = (uint8_t)a2; float c[4] = { (float)a2, 0, 0, 0 }; if(!vh.taken) srv->GateOp(s, SPO_VEHMOD, 6, vh.c1, (int32_t)vid, c); }
            break;
        }
        case 57:   // RemoveVehicleComponent
        {
            uint16_t comp = 0; bs.Read(u16a); bs.Read(comp); auto it = I.vehs.find(u16a); if(it == I.vehs.end()) break;
            auto& m = it->second.mods; m.erase(std::remove(m.begin(), m.end(), comp), m.end());
            float c[4] = { (float)comp, 0, 0, 0 }; srv->GateOp(s, SPO_VEHMOD, 3, 0, u16a, c); break;
        }
        case 65: bs.Read(u16a); bs.Read(u8a); { auto it = I.vehs.find(u16a); if(it != I.vehs.end()) it->second.interior = u8a; } srv->GateOp(s, SPO_VEHMOD, 4, u8a, u16a); break;   // LinkVehicleToInterior
        case 106:  // UpdateVehicleDamageStatus
        {
            uint32_t panels = 0, doors = 0; uint8_t lights = 0, tyres = 0; bs.Read(u16a); bs.Read(panels); bs.Read(doors); bs.Read(lights); bs.Read(tyres);
            if(!I.vehs.count(u16a)) break;
            char t[64]; snprintf(t, sizeof(t), "%u %u %u %u", panels, doors, (unsigned)lights, (unsigned)tyres);
            srv->GateOp(s, SPO_VEHMOD, 5, 0, u16a, nullptr, t); break;
        }
        case 117:  // EditObject / EditPlayerObject: the phone's move panel (each step = EDIT_RESPONSE_UPDATE, like dragging on PC)
        {
            bool pobj = false; bs.Read(pobj); bs.Read(u16a); auto it = I.objs.find(u16a); if(it == I.objs.end()) break;
            I.edit = Impl::Edit(); I.edit.mode = 1; I.edit.pobj = pobj; I.edit.id = u16a;
            memcpy(I.edit.p, it->second.p, sizeof(I.edit.p)); memcpy(I.edit.r, it->second.r, sizeof(I.edit.r));
            memcpy(I.edit.p0, I.edit.p, sizeof(I.edit.p)); memcpy(I.edit.r0, I.edit.r, sizeof(I.edit.r));
            srv->GateOp(s, SPO_EDIT, 1, 0, u16a, nullptr, "Edit object"); break;
        }
        case 116:  // EditAttachedObject
        {
            bs.Read(u32a); if(u32a >= 10 || !I.att[u32a].on) break;
            I.edit = Impl::Edit(); I.edit.mode = 2; I.edit.idx = u32a;
            memcpy(I.edit.p, I.att[u32a].off, sizeof(I.edit.p)); memcpy(I.edit.r, I.att[u32a].rot, sizeof(I.edit.r));
            memcpy(I.edit.p0, I.edit.p, sizeof(I.edit.p)); memcpy(I.edit.r0, I.edit.r, sizeof(I.edit.r)); I.edit.step = 0.02f;
            srv->GateOp(s, SPO_EDIT, 2, 0, (int32_t)u32a, nullptr, "Edit attached object"); break;
        }
        case 27: I.edit = Impl::Edit(); I.edit.mode = 3; srv->GateOp(s, SPO_EDIT, 3, 0, 0, nullptr, "Select object"); break;   // SelectObject
        case 28: if(I.edit.mode) { I.edit = Impl::Edit(); srv->GateOp(s, SPO_EDIT, 0, 0, 0); } break;                      // CancelEdit
        // ---- 5.02.62: the open.mp client RPCs that were still missing (open.mp docs + Shared/NetCode) ----
        case 80:   // ShowPlayerNameTagForPlayer (playerid, show)
        {
            bs.Read(u16a); bs.Read(u8a); I.tagOff[u16a] = u8a == 0;
            if(Impl::Rp* p = RemPlayer(I, u16a)) if(p->slot >= 0) srv->GateOp(s, SPO_TAGS, 1, u8a ? 1 : 0, p->slot);
            break;
        }
        case 146:  // SetGravity / SetPlayerGravity (0.008 = normal): the phone scales its fall speed like /gravity
        {
            if(!bs.Read(fa) || !std::isfinite(fa)) break;
            I.gravity = fa; float g[4] = { std::max(-5.0f, std::min(5.0f, fa / 0.008f)), 0, 0, 0 };
            srv->GateOp(s, SPO_GRAVITY, 0, 0, 0, g); printf("[crossplay] gravity %.4f (x%.2f)\n", fa, g[0]); break;
        }
        case 81:   // AttachCameraToObject: the camera sits on the object (and follows MoveObject / SetObjectPos)
        {
            bs.Read(u16a); auto it = I.objs.find(u16a); if(it == I.objs.end()) break;
            I.camObj = u16a; srv->GateOp(s, SPO_CAMERA, 1, 0, 0, it->second.p); break;
        }
        case 75:   // AttachObjectToPlayer: a server object on me = a free attachment slot (spine), the world copy goes
        {
            uint16_t pid = kInvalid; float off[3] = { 0, 0, 0 }, rot[3] = { 0, 0, 0 };
            bs.Read(u16a); bs.Read(pid); RdVec(bs, off); RdVec(bs, rot);
            auto it = I.objs.find(u16a); if(it == I.objs.end() || (int)pid != I.myId) break;   // on a PC player: his own client shows it
            int slot = -1; { auto at = I.objAtt.find(u16a); if(at != I.objAtt.end()) slot = at->second; }
            for(int k = 9; k >= 0 && slot < 0; --k) if(!I.att[k].on) slot = k;
            if(slot < 0) break;
            Impl::Att& at = I.att[slot]; at.on = true; at.model = it->second.model; at.bone = 1; memcpy(at.off, off, sizeof(off)); memcpy(at.rot, rot, sizeof(rot)); at.sc[0] = at.sc[1] = at.sc[2] = 1;
            I.objAtt[u16a] = slot;
            srv->GateObject(it->second.row, it->second.model, it->second.p, it->second.r, false);
            char t[96]; snprintf(t, sizeof(t), "%.3f %.3f %.3f 1 1 1", rot[0], rot[1], rot[2]); float f4[4] = { off[0], off[1], off[2], 0 };
            srv->GateOp(s, SPO_ATTACH, (uint8_t)slot, 1, (int32_t)it->second.model, f4, t); break;
        }
        case 178:  // SetActorHealth
        {
            bs.Read(u16a); bs.Read(fa); auto it = I.actors.find(u16a); if(it == I.actors.end()) break;
            it->second.hp = fa; float h[4] = { fa, 0, 0, 0 }; srv->GateOp(s, SPO_ACTOR, 4, 0, u16a, h); break;
        }
        case 170:  // EnablePlayerCameraTarget: the phone reports what its camera looks at (OnPlayerCameraTarget, RPC 168)
        {
            bool on = false; bs.Read(on); I.camTarget = on; for(int& c : I.camLast) c = 0xFFFF;
            srv->GateOp(s, SPO_CAMTARGET, on ? 1 : 0, 0, 0); break;
        }
        case 166:  // PlayerDeath (a PC player died): his ghost falls (the dead flag until his next movement after respawn)
        {
            bs.Read(u16a); Impl::Rp* p = RemPlayer(I, u16a); if(!p || p->slot < 0 || !p->hasPos) break;
            SyncData d = p->last; d.health = 0; d.armour = 0; d.flags |= SYNC_DEAD; p->last = d; srv->BridgeSync(p->slot, d); break;
        }
        case 123:  // SetVehicleNumberPlate: kept for the log (stock GTA plates on the phone)
        {
            bs.Read(u16a); std::string pl = RdStr8(bs); I.vehPlate[u16a] = pl;
            if(dbg) printf("[crossplay] vehicle %d plate \"%s\"\n", (int)u16a, pl.c_str()); break;
        }
        case 148: { uint16_t tr = 0; bs.Read(tr); bs.Read(u16a); I.trailer[u16a] = tr; break; }   // AttachTrailerToVehicle (the trailer sync moves it)
        case 149: bs.Read(u16a); I.trailer.erase(u16a); break;                                    // DetachTrailerFromVehicle
        // Read and left to the sync / not shown on the phone (named here so MP_GATE_DEBUG logs say why):
        case 26:   // EnterVehicle (a PC player gets in): his vehicle sync puts the ghost in the car
        case 154:  // ExitVehicle (a PC player gets out): his foot sync takes the ghost out
        case 33:   // SetPlayerShopName (interior shop scripts of the PC game)
        case 60:   // SendGameTimeUpdate (server uptime for the PC client's timers, not the world clock)
        case 84:   // SetObjectMaterial / SetObjectMaterialText (objects are shown with their stock textures)
        case 112:  // PlayCrimeReport (police scanner voice)
        case 167:  // DisableRemoteVehicleCollisions
        case 183: case 185:   // 0.3.DL ModelUrl / DownloadCompleted: custom models are shown as their base model (RPC 179)
            if(dbg) printf("[crossplay] RPC %d read, nothing to show on the phone\n", r.id);
            break;
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
    SyncData d; float footVel[3] = { 0, 0, 0 };
    if(!vehicle)
    {
        bool has; uint16_t lr = 0, ud = 0, keys = 0; float pos[3], q[4], vel[3]; uint8_t ha = 0, wp = 0, sa = 0;
        bs.Read(has); if(has) bs.Read(lr); bs.Read(has); if(has) bs.Read(ud);
        if(!bs.Read(keys) || !RdVec(bs, pos) || !bs.ReadNormQuat(q[0], q[1], q[2], q[3])) return;
        bs.Read(ha); bs.Read(wp); bs.Read(sa); RdCompVec(bs, vel);
        for(int k = 0; k < 3; ++k) footVel[k] = vel[k] * 50.0f;   // GTA units per frame -> m/s
        {   // 5.02.27: surfing, then the PC player's animation (ApplyAnimation / dances / sitting) - onto his ghost
            bool surf = false, hasAnim = false; uint16_t animId = 0, animFl = 0;
            if(bs.Read(surf) && surf) { uint16_t sid; float so[3]; bs.Read(sid); RdVec(bs, so); }
            if(bs.Read(hasAnim) && hasAnim) { bs.Read(animId); bs.Read(animFl); }
            std::string lib, an;
            const bool show = animId && AnimName(animId, lib, an) && !AnimIsMove(lib, an);
            if(show && animId != p->animId) { p->animId = animId; p->animAt = NowMs(); }
            if(!show) p->animId = 0;
            if(show) { p->animLib = lib; p->animName = an; p->animLoop = (animFl & 0x100) != 0; p->animSeen = NowMs(); }
        }
        int h, a; UnpackHA(ha, h, a);
        d = FootToSync(pos, QuatHeading(q[0], q[1], q[2], q[3]), vel, h, a, wp, keys, p->skin);
        if(NowMs() - p->aimMs < 600) memcpy(d.aim, p->aimPt, sizeof(d.aim));   // 5.02.27: where he really aims (aim sync)
        if(sa == 1) d.flags |= SYNC_DUCKING;
        if(ud || lr) d.speed = std::max(d.speed, 3.0f);
        if(p->vehicle >= 0) RemVehFree(I, srv, p->vehicle);   // 5.02.25: he got out - the parked car is back on the phone
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
        if(p->vehicle != vid)
        {   // 5.02.25: a PC player drives a server car: his ghost car shows it, the parked copy goes away
            if(p->vehicle >= 0) RemVehFree(I, srv, p->vehicle);
            auto vt = I.vehs.find(vid); if(vt != I.vehs.end() && !vt->second.taken) { vt->second.taken = true; srv->GateOp(I.phoneSlot, SPO_VEHICLE, 2, 0, vid); }
        }
        { auto vt = I.vehs.find(vid); if(vt != I.vehs.end()) { memcpy(vt->second.p, pos, sizeof(pos)); vt->second.a = hd; } }
        p->vehicle = vid;
    }
    p->last = d; p->hasPos = true;
    if(!vehicle && (p->animId || NowMs() - p->animSeen < 1500))
    {   // 5.02.27: the anim as a script anim (IFP block + name hash) - an empty list for a moment after it ends fades it out
        SyncExt e; memset(&e, 0, sizeof(e)); e.ver = MP_SYNC_EXT_VER; e.time = (uint32_t)NowMs();
        memcpy(e.vel, footVel, sizeof(footVel));
        if(p->animId)
        {
            AnimSync a; memset(&a, 0, sizeof(a));
            a.group = -1; a.id = -1; a.hash = AnimKey(p->animName.c_str(), 64);
            std::string b = p->animLib; for(auto& c : b) c = (char)tolower((unsigned char)c);
            MP_CopyStr(a.block, b.c_str(), sizeof(a.block));
            a.time = (float)(NowMs() - p->animAt) / 1000.0f; a.blend = 255; a.speed = 50; a.flags = p->animLoop ? 0x2 : 0;
            memcpy(&e.anim[0], &a, sizeof(a)); e.animCount = 1;
        }
        srv->BridgeSync(p->slot, d, &e);
        return;
    }
    srv->BridgeSync(p->slot, d);
}

// 5.02.24: the client commands samp.exe answers itself (open.mp docs "Client Commands"). The server never sees them,
// so sending them as RPC 50 got "Unknown command" (or a script's own /save). /rcon goes as packet 201 like samp.exe.
static void RemSavePos(Impl& I, MPServer* srv, bool raw, const std::string& note)
{
    if(!I.haveMe) { srv->GateMsg(I.phoneSlot, 0xFFFFFFFF, "Spawn first - there is no position to save yet."); return; }
    const SyncData& d = I.lastMe; const bool car = (d.flags & SYNC_IN_CAR) != 0 && d.carModel;
    const float* p = car ? d.carPos : d.pos; const float a = car ? d.carHeading : d.heading;
    char line[256];
    if(raw) snprintf(line, sizeof(line), "%.4f,%.4f,%.4f,%.4f%s%s", p[0], p[1], p[2], a, note.empty() ? "" : " // ", note.c_str());
    else if(car) snprintf(line, sizeof(line), "AddStaticVehicle(%u,%.4f,%.4f,%.4f,%.4f,-1,-1); // %s", (unsigned)d.carModel, p[0], p[1], p[2], a, note.c_str());
    else snprintf(line, sizeof(line), "AddPlayerClass(%u,%.4f,%.4f,%.4f,%.4f,0,0,0,0,0,0); // %s", (unsigned)d.skin, p[0], p[1], p[2], a, note.c_str());
    const char* file = raw ? "rawpositions.txt" : "savedpositions.txt";
    if(FILE* f = fopen(file, "a")) { fprintf(f, "%s\n", line); fclose(f); }
    printf("[crossplay] %s: %s\n", file, line);
    srv->GateMsg(I.phoneSlot, 0x88AA88FF, (std::string(raw ? "-> Raw position saved: " : "-> Saved: ") + line).c_str());
}

static bool RemClientCmd(Impl& I, MPServer* srv, const std::string& t)
{
    size_t sp = t.find(' '); std::string cmd = t.substr(1, sp == std::string::npos ? std::string::npos : sp - 1), arg;
    if(sp != std::string::npos) { arg = t.substr(sp + 1); while(!arg.empty() && arg[0] == ' ') arg.erase(0, 1); }
    for(auto& c : cmd) c = (char)tolower((unsigned char)c);
    char b[128];
    if(cmd == "save") { RemSavePos(I, srv, false, arg); return true; }
    if(cmd == "rs") { RemSavePos(I, srv, true, arg); return true; }
    if(cmd == "interior") { snprintf(b, sizeof(b), "Current Interior: %u", (unsigned)I.interior); srv->GateMsg(I.phoneSlot, 0x88AA88FF, b); return true; }
    if(cmd == "vw") { snprintf(b, sizeof(b), "Current Virtual World: %d", (int)I.vworld); srv->GateMsg(I.phoneSlot, 0x88AA88FF, b); return true; }
    if(cmd == "rcon")
    {   // open.mp reads it as readDynStr32 (64 characters at most): "/rcon login <password>", "/rcon cmdlist" ...
        if(arg.empty()) { srv->GateMsg(I.phoneSlot, 0xFFFFFFFF, "Usage: /rcon login [password]  then  /rcon [command]"); return true; }
        if(arg.size() > 64) arg.resize(64);
        BitStream o; o.Write((uint8_t)201); o.Write((uint32_t)arg.size()); o.Write(arg.data(), (unsigned)arg.size());
        if(I.C) I.C->Send(&o, HIGH_PRIORITY, RELIABLE_ORDERED, 0);
        printf("[crossplay] rcon packet sent (%u characters)\n", (unsigned)arg.size());
        return true;
    }
    return false;
}

static void RemSendFoot(Impl& I, const SyncData& d, uint16_t animIdx = 0, uint16_t animFl = 0)
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
    // 5.02.27: the special action the server set (SPECIAL_ACTION_HANDSUP / CUFFED / USECELLPHONE / DANCE...) goes back like
    // samp.exe does - GetPlayerSpecialAction on the server read 0 for a phone player, so arrest / phone / dance scripts broke
    o.Write((uint8_t)((d.flags & SYNC_DUCKING) ? 1 : (I.special != 1 ? I.special : 0)));
    float vel[3] = { -sinf(a) * sp, cosf(a) * sp, 0 }; WrVec(o, vel);
    // surfing offset + id 0, then AnimationID / flags (5.02.27: the phone's script anim, e.g. ApplyAnimation, dances,
    // sitting - GetPlayerAnimationIndex read 0 and PC players never saw the phone's animations)
    float z[3] = { 0, 0, 0 }; WrVec(o, z); o.Write((uint16_t)0); o.Write(animIdx); o.Write(animIdx ? animFl : (uint16_t)0);
    I.C->Send(&o, HIGH_PRIORITY, UNRELIABLE_SEQUENCED, 1);
}

// ---- 5.02.25: the callbacks samp.exe fires (open.mp docs: scripting/callbacks) --------------------------------------
// SA-MP's default weapon damage (lag compensation on: the victim's client takes it, then sends OnPlayerTakeDamage)
static float WeaponDamage(int w)
{
    switch(w)
    {
        case 0: case 1: case 14: return 1.32f;
        case 9: return 27.06f;
        case 16: case 35: case 36: case 39: return 82.5f;
        case 22: case 29: return 8.25f;
        case 23: return 13.2f;
        case 24: case 38: return 46.2f;
        case 25: case 26: return 30.0f;
        case 27: return 39.6f;
        case 28: case 32: return 6.6f;
        case 30: case 31: return 9.9f;
        case 33: return 24.75f;
        case 34: return 41.25f;
        case 37: return 2.64f;
        case 41: case 42: return 0.33f;
        default: return (w >= 2 && w <= 15) ? 4.62f : 5.0f;
    }
}
static int RemPcIdOfSlot(Impl& I, int slot) { for(auto& kv : I.rp) if(kv.second.slot == slot) return kv.first; return -1; }
static bool RemSameTeam(Impl& I, int pcId)
{   // SetPlayerTeam: players on one team (not NO_TEAM 255) can't hurt each other - samp.exe sends no damage then
    auto it = I.pcTeam.find(pcId); return I.spawn.team != 255 && it != I.pcTeam.end() && it->second == I.spawn.team;
}
static void RemTakeDamage(Impl& I, MPServer* srv, int issuer, float dmg, int weapon, int part, uint64_t now)
{   // my health / armour on the phone + OnPlayerTakeDamage
    if(dmg <= 0 || !I.haveMe) return;
    int hp = std::min<int>(I.lastMe.health, std::max(1, I.lastHealth)), ar = std::min<int>(I.lastMe.armour, I.lastArmour);
    float left = dmg; const int fromAr = std::min<int>(ar, (int)(left + 0.5f)); ar -= fromAr; left -= (float)fromAr;
    hp = std::max(0, hp - (int)(left + 0.5f));
    if(fromAr) { srv->GateAct(I.phoneSlot, CGA_ARMOUR, 0, 0, ar); I.lastArmour = ar; }
    srv->GateAct(I.phoneSlot, CGA_HEALTH, 0, 0, hp); I.lastHealth = std::max(1, hp);
    I.hitAt = now; I.lastHp = hp; I.lastAr = ar;
    BitStream o; o.Write(true); o.Write((uint16_t)issuer); o.Write(dmg); o.Write((uint32_t)weapon); o.Write((uint32_t)part); CliRpc(I, 115, o);
}

static void RemMyCar(Impl& I, MPServer* srv, int vid, int seat, uint64_t now)
{   // the phone got in / out of a server car: OnPlayerEnterVehicle / OnPlayerExitVehicle, then driver / passenger sync
    if(vid >= 0 && !I.vehs.count(vid)) vid = -1;
    if(vid == I.myVeh && (vid < 0 || seat == I.mySeat)) return;
    if(I.myVeh >= 0) { BitStream o; o.Write((uint16_t)I.myVeh); CliRpc(I, 154, o); printf("[crossplay] out of vehicle %d\n", I.myVeh); if(I.modShop) I.modShop = 0; }
    I.myVeh = vid; I.mySeat = seat; I.myVehAt = now; I.vsHave = false; I.myVehHp = 1000.0f; I.mySiren = false;
    if(vid >= 0)
    {
        BitStream o; o.Write((uint16_t)vid); o.Write((uint8_t)(seat ? 1 : 0)); CliRpc(I, 26, o);
        printf("[crossplay] in vehicle %d (%s)\n", vid, seat ? "passenger" : "driver");
    }
}

static void RemPhonePkt(Impl& I, MPServer* srv, const HookEv& e, uint64_t now)
{
    const uint8_t* d = (const uint8_t*)e.text.data(); const int len = (int)e.text.size();
    if(e.op == PKT_DAMAGE && len >= (int)sizeof(PktDamage))
    {   // I hit a PC player: OnPlayerGiveDamage (body part 3 = torso)
        PktDamage p; memcpy(&p, d, sizeof(p));
        const int pc = RemPcIdOfSlot(I, p.target); if(pc < 0 || RemSameTeam(I, pc) || !(p.amount > 0)) return;
        BitStream o; o.Write(false); o.Write((uint16_t)pc); o.Write(p.amount); o.Write((uint32_t)p.weapon); o.Write((uint32_t)3); CliRpc(I, 115, o);
    }
    else if(e.op == PKT_SHOT && len >= MP_PKT_SHOT_HDR + (int)sizeof(ShotEntry))
    {   // every bullet: OnPlayerWeaponShot (bullet sync 206). Hit = the PC player / server car the bullet ended at
        PktShot m; memset(&m, 0, sizeof(m)); memcpy(&m, d, std::min<int>(len, (int)sizeof(m)));
        const int n = std::min<int>(m.count, (len - MP_PKT_SHOT_HDR) / (int)sizeof(ShotEntry));
        for(int i = 0; i < n && i < MP_SHOT_MAX; ++i)
        {
            const ShotEntry& s = m.e[i]; if(s.kind != 0 || s.weapon < 22 || s.weapon > 38 || s.weapon == 35 || s.weapon == 36 || s.weapon == 37) continue;
            uint8_t type = 0; uint16_t hid = kInvalid; float off[3] = { s.b[0], s.b[1], s.b[2] };
            float best = 1.6f * 1.6f;
            for(auto& kv : I.rp)
            {
                if(!kv.second.hasPos) continue; const float* q = kv.second.last.pos;
                const float dx = s.b[0] - q[0], dy = s.b[1] - q[1], dz = s.b[2] - q[2], d2 = dx * dx + dy * dy + dz * dz;
                if(d2 < best) { best = d2; type = 1; hid = (uint16_t)kv.first; off[0] = dx; off[1] = dy; off[2] = dz; }
            }
            if(!type)
            {
                best = 3.0f * 3.0f;
                for(auto& kv : I.vehs)
                {
                    const float dx = s.b[0] - kv.second.p[0], dy = s.b[1] - kv.second.p[1], dz = s.b[2] - kv.second.p[2], d2 = dx * dx + dy * dy + dz * dz;
                    if(d2 < best) { best = d2; type = 2; hid = (uint16_t)kv.first; off[0] = dx; off[1] = dy; off[2] = dz; }
                }
            }
            BitStream o; o.Write((uint8_t)206); o.Write(type); o.Write(hid);
            WrVec(o, s.a); WrVec(o, s.b); WrVec(o, off); o.Write(s.weapon);
            I.C->Send(&o, HIGH_PRIORITY, UNRELIABLE_SEQUENCED, 1);
        }
    }
    else if(e.op == PKT_VEH_STATE && len >= (int)sizeof(PktVehState) && I.myVeh >= 0 && I.mySeat == 0)
    {   // my server car: health + siren for the driver sync, tuning -> OnVehicleMod / Paintjob / Respray, damage -> OnVehicleDamageStatusUpdate
        PktVehState v; memcpy(&v, d, sizeof(v));
        I.myVehHp = (float)v.health; I.mySiren = (v.flags & VEHF_SIREN) != 0;
        if(I.vsHave)
        {
            const PktVehState& o0 = I.vsLast;
            auto scm = [&](uint32_t type, uint32_t a1, uint32_t a2) { BitStream o; o.Write((uint32_t)I.myVeh); o.Write(a1); o.Write(a2); o.Write(type); CliRpc(I, 96, o); };
            if(v.paintjob != o0.paintjob && v.paintjob >= 0 && v.paintjob <= 2) scm(1, (uint32_t)v.paintjob, 0);
            for(int k = 0; k < MP_VEH_MOD_SLOTS; ++k)
            {
                const uint16_t md = v.mods[k]; if(md < 1000 || md > 1193) continue;
                bool had = false; for(int j = 0; j < MP_VEH_MOD_SLOTS; ++j) if(o0.mods[j] == md) had = true;
                if(!had) scm(2, md, 0);
            }
            if(v.col[0] != o0.col[0] || v.col[1] != o0.col[1]) scm(3, v.col[0], v.col[1]);
            if(v.panels != o0.panels || memcmp(v.doors, o0.doors, 4) || v.lights != o0.lights || memcmp(v.wheels, o0.wheels, 4))
            {
                const uint32_t doors = (uint32_t)v.doors[0] | ((uint32_t)v.doors[1] << 8) | ((uint32_t)v.doors[2] << 16) | ((uint32_t)v.doors[3] << 24);
                // tyres: bit 0 rear right, 1 front right, 2 rear left, 3 front left (game wheels: 0 FL, 1 RL, 2 FR, 3 RR)
                const uint8_t tyres = (uint8_t)((v.wheels[3] ? 1 : 0) | (v.wheels[2] ? 2 : 0) | (v.wheels[1] ? 4 : 0) | (v.wheels[0] ? 8 : 0));
                BitStream o; o.Write((uint16_t)I.myVeh); o.Write(v.panels); o.Write(doors); o.Write((uint8_t)v.lights); o.Write(tyres); CliRpc(I, 106, o);
            }
        }
        I.vsLast = v; I.vsHave = true;
    }
}

static void RemModShop(Impl& I, const float* p)
{   // OnEnterExitModShop: driving into / out of Transfender (1), Loco Low Co (2), Wheel Arch Angels (3)
    struct Shop { float x, y, z; int id; };
    static const Shop kShops[] = { { 1041.6f, -1016.0f, 32.1f, 1 }, { -1935.9f, 237.7f, 34.3f, 1 }, { 2386.8f, 1050.5f, 10.8f, 1 },
                                   { 2644.9f, -2044.3f, 13.5f, 2 }, { -2723.7f, 217.2f, 4.4f, 3 } };
    int in = 0;
    if(I.myVeh >= 0 && I.mySeat == 0)
        for(const Shop& s : kShops) { const float dx = p[0] - s.x, dy = p[1] - s.y, dz = p[2] - s.z; if(dx * dx + dy * dy < 7.0f * 7.0f && fabsf(dz) < 4.0f) in = s.id; }
    if(in == I.modShop) return;
    BitStream o; o.Write((uint32_t)I.myVeh); o.Write((uint32_t)(in ? 1 : 0)); o.Write((uint32_t)(in ? in : I.modShop)); o.Write((uint32_t)4); CliRpc(I, 96, o);
    I.modShop = in;
}

// 5.02.27: aim sync (203) like samp.exe: camera mode, look direction, camera spot, zoom / weapon state. Servers read it
// with GetPlayerCameraPos / FrontVector / Mode / Zoom / Aspect Ratio and GetPlayerWeaponState - it was all 0 for a phone
// player (aim-direction scripts, "look at the door" /enter, anti-aimbot checks), and PC players saw the phone never aim
static void RemSendAim(Impl& I, const SyncData& d, bool aiming)
{
    const bool car = (d.flags & SYNC_IN_CAR) != 0;
    const float* p = car ? d.carPos : d.pos; const float h = car ? d.carHeading : d.heading;
    const float a = h * kPi / 180.0f; float fw[3] = { -sinf(a), cosf(a), -0.08f };
    float cam[3] = { p[0] - fw[0] * (car ? 6.0f : 3.2f), p[1] - fw[1] * (car ? 6.0f : 3.2f), p[2] + (car ? 1.6f : 0.75f) };
    const float ax = d.aim[0] - cam[0], ay = d.aim[1] - cam[1], az = d.aim[2] - cam[2], al = sqrtf(ax * ax + ay * ay + az * az);
    if(aiming && al > 0.5f && al < 300.0f)
    {   // over the shoulder, towards the aim point
        cam[0] = p[0] - fw[0] * 1.2f + fw[1] * 0.35f; cam[1] = p[1] - fw[1] * 1.2f - fw[0] * 0.35f; cam[2] = p[2] + 0.6f;
        const float bx = d.aim[0] - cam[0], by = d.aim[1] - cam[1], bz = d.aim[2] - cam[2], bl = sqrtf(bx * bx + by * by + bz * bz);
        if(bl > 0.1f) { fw[0] = bx / bl; fw[1] = by / bl; fw[2] = bz / bl; }
    }
    else { const float l = sqrtf(fw[0] * fw[0] + fw[1] * fw[1] + fw[2] * fw[2]); fw[0] /= l; fw[1] /= l; fw[2] /= l; }
    const int wp = (d.weapon < 64 && (d.weapon == 0 || I.gunOk[d.weapon])) ? d.weapon : 0;
    uint8_t mode = car ? 18 : 4;                                   // 4 = on foot follow, 18 = car follow
    if(aiming && !car) mode = (wp == 34) ? 7 : (wp == 35 || wp == 36) ? 8 : (wp == 43) ? 46 : (wp >= 22) ? 53 : 4;
    if(aiming && car) mode = 55;                                   // drive-by
    // weapon state (2 high bits): 0 no bullets, 1 last bullet, 2 more bullets, 3 reloading - fists / melee = 2 like samp.exe
    const uint8_t zoom = 35 & 63, ws = 2;
    BitStream o; o.Write((uint8_t)203);
    o.Write(mode); WrVec(o, fw); WrVec(o, cam); o.Write(asinf(std::max(-1.0f, std::min(1.0f, fw[2])))); o.Write((uint8_t)(zoom | (ws << 6)));
    o.Write((uint8_t)198);                                         // (aspect ratio - 1) * 255: 16:9 like a PC screen
    I.C->Send(&o, HIGH_PRIORITY, UNRELIABLE_SEQUENCED, 1);
}
// 5.02.27: spectator sync (212): samp.exe sends it all the time while TogglePlayerSpectating is on (login cameras,
// admin spectate). Without it OnPlayerUpdate never ran for a phone on a login screen - AFK / paused checks kicked it
static void RemSendSpec(Impl& I, const SyncData* d)
{
    const uint32_t extra = NowMs() < I.keysUntil ? I.heldKeys : 0;
    float p[3] = { 0, 0, 0 }; if(d) memcpy(p, (d->flags & SYNC_IN_CAR) ? d->carPos : d->pos, sizeof(p));
    BitStream o; o.Write((uint8_t)212); o.Write((uint16_t)0); o.Write((uint16_t)0); o.Write((uint16_t)(extra & 0xFFFF)); WrVec(o, p);
    I.C->Send(&o, HIGH_PRIORITY, UNRELIABLE_SEQUENCED, 1);
}
// 5.02.27: weapons update (204) like samp.exe: the player / actor the phone aims at (GetPlayerTargetPlayer /
// GetPlayerTargetActor). No slot data: open.mp copies it over its own weapon record, and the phone doesn't count ammo
// the way samp.exe does - the server keeps the weapons / ammo it gave (no weapon / ammo hack kick)
static void RemSendWeapons(Impl& I, uint16_t target, uint16_t actor)
{
    BitStream o; o.Write((uint8_t)204); o.Write(target); o.Write(actor);
    I.C->Send(&o, HIGH_PRIORITY, RELIABLE, 1);
}
static uint16_t RemAimTarget(Impl& I, const SyncData& d, bool aiming, bool actor)
{
    if(!aiming) return kInvalid;
    float best = 2.2f * 2.2f; uint16_t id = kInvalid;
    if(!actor) { for(auto& kv : I.rp) if(kv.second.hasPos) { const float* q = kv.second.last.pos; const float dx = q[0] - d.aim[0], dy = q[1] - d.aim[1], dz = q[2] - d.aim[2], e = dx * dx + dy * dy + dz * dz; if(e < best) { best = e; id = (uint16_t)kv.first; } } }
    else { for(auto& kv : I.actors) { const float* q = kv.second.p; const float dx = q[0] - d.aim[0], dy = q[1] - d.aim[1], dz = q[2] - d.aim[2], e = dx * dx + dy * dy + dz * dz; if(e < best) { best = e; id = (uint16_t)kv.first; } } }
    return id;
}

static void RemSendCar(Impl& I, const SyncData& d)
{   // driver (200) / passenger (211) sync of the server car the phone sits in
    const float* pos = d.carPos; const float h = d.carHeading; float q[4]; HeadingQuat(h, q);
    const int hp = std::max(0, std::min<int>(d.health, std::max(1, I.lastHealth))), ar = std::max(0, std::min<int>(d.armour, I.lastArmour));
    const int wp = (d.weapon < 64 && (d.weapon == 0 || I.gunOk[d.weapon])) ? d.weapon : 0;
    uint32_t extra = NowMs() < I.keysUntil ? I.heldKeys : 0;
    const uint8_t addKey = (extra & 262144) ? 3 : (extra & 131072) ? 2 : (extra & 65536) ? 1 : 0;
    BitStream o;
    if(I.mySeat == 0)
    {
        const float a = h * kPi / 180.0f, sp = d.speed / 50.0f; float vel[3] = { -sinf(a) * sp, cosf(a) * sp, 0 };
        o.Write((uint8_t)kVehicleSync); o.Write((uint16_t)I.myVeh);
        o.Write((uint16_t)0); o.Write((uint16_t)(d.speed > 0.5f ? 0xFF80 : 0)); o.Write((uint16_t)((extra & 0xFFFF) | (d.speed > 0.5f ? 8 : 0)));
        o.Write(q[0]); o.Write(q[1]); o.Write(q[2]); o.Write(q[3]); WrVec(o, pos); WrVec(o, vel);
        o.Write(std::max(0.0f, std::min(I.myVehHp, 1000.0f)));
        o.Write((uint8_t)std::min(hp, 255)); o.Write((uint8_t)std::min(ar, 255));
        o.Write((uint8_t)((wp & 63) | (addKey << 6))); o.Write((uint8_t)(I.mySiren ? 1 : 0)); o.Write((uint8_t)0);
        o.Write((uint16_t)0); o.Write((uint32_t)0);
    }
    else
    {   // seat 7 bits + drive-by 1 bit, weapon 6 bits + additional key 2 bits
        const uint16_t sk = (uint16_t)((I.mySeat & 0x7F) | ((wp & 63) << 8) | (addKey << 14));
        o.Write((uint8_t)211); o.Write((uint16_t)I.myVeh); o.Write(sk);
        o.Write((uint8_t)std::min(hp, 255)); o.Write((uint8_t)std::min(ar, 255));
        o.Write((uint16_t)0); o.Write((uint16_t)0); o.Write((uint16_t)(extra & 0xFFFF)); WrVec(o, pos);
    }
    I.C->Send(&o, HIGH_PRIORITY, UNRELIABLE_SEQUENCED, 1);
    if(auto it = I.vehs.find(I.myVeh); it != I.vehs.end()) { memcpy(it->second.p, pos, sizeof(it->second.p)); it->second.a = h; }
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
    // 5.02.29: 576-byte datagrams like samp.exe (rakClient->SetMTUSize(576)). RakNet's default is 1500: on phone data
    // (IPv6 carriers with 464XLAT / CLAT, tunnels, some Wi-Fi) the big ones are dropped silently, their resends too -
    // during a burst (Life of Player's 111-textdraw language menu) nothing got acked and the link was "lost" ~25 s later
    { static const int kMtu = getenv("MP_GATE_MTU") ? atoi(getenv("MP_GATE_MTU")) : 576; I.C->SetMTUSize(kMtu); }
    I.lastRecvAt = NowMs();
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
    if(I.phase >= 2) SvTick(I, srv, now);          // 5.02.29: SampVoice keepAlive / voice in and out

    for(Packet* pk = I.C->Receive(); pk; I.C->DeallocatePacket(pk), pk = I.C->Receive())
    {
        I.lastRecvAt = now;
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
                RemNewSession(I, srv);   // 5.02.29: before the server's first RPCs (they come before InitGame on open.mp)
                printf("[crossplay] joining as SA-MP %s (%u)%s\n", I.cliVerStr.c_str(), I.cliVer, SAMPRakNet::ClientOmp() ? " + open.mp launcher" : "");
                // 5.02.07: what the open.mp launcher's client adds: its omp version (IsPlayerUsingOfficialClient = true
                // on open.mp; SA-MP 0.3.7 servers ignore the extra bytes). Off: MP_GATE_OMP=0
                if(SAMPRakNet::ClientOmp()) o.Write((uint32_t)SAMPRakNet::CURRENT_OMP_CLIENT_MOD_VERSION);
                if(!getenv("MP_GATE_NOSV"))
                {   // 5.02.11: the SampVoice client plugin's handshake, so "voice plugin not detected" servers let phones in
                    // (SvGetVersion / SvHasMicro). SampVoice 3.1 looks for EF BE AD DE + version 11 + micro anywhere in
                    // ClientJoin; 4.x for DE AD C0 DE + length + version (big endian 4.2.0) + micro, searching from the end.
                    uint8_t kSv[] = { 0xEF, 0xBE, 0xAD, 0xDE, 11, 1,   0xDE, 0xAD, 0xC0, 0xDE, 5, 4, 2, 0, 0, 1 };
                    if(!I.svOn) { kSv[5] = 0; kSv[15] = 0; }   // 5.02.29: SampVoice off in Settings: SvHasMicro = no (like a PC without a mic)
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
                    printf("[crossplay] link lost: last packet from the PC server %.1f s ago, phase %d, ping %d ms\n",
                           (double)(now - I.lastRecvAt) / 1000.0, I.phase, I.C->GetAveragePing());   // 5.02.29: why (mp_log.txt)
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
            case 222: SvControl(I, srv, pk->data, (int)pk->length); break;   // 5.02.29: SampVoice control packet
            case kPlayerSync: RemFootIn(I, srv, pk, false); break;
            case kVehicleSync: RemFootIn(I, srv, pk, true); break;
            case 203:
            {   // 5.02.27: a PC player's aim sync - his ghost aims / shoots where he really looks (was always straight ahead)
                BitStream bs(pk->data, pk->length, false); bs.IgnoreBits(8);
                uint16_t pid = 0; uint8_t mode = 0; float fr[3], cp[3];
                if(!bs.Read(pid) || !bs.Read(mode) || !RdVec(bs, fr) || !RdVec(bs, cp)) break;
                Impl::Rp* p = RemPlayer(I, pid); if(!p) break;
                const float l = sqrtf(fr[0] * fr[0] + fr[1] * fr[1] + fr[2] * fr[2]); if(l < 0.1f || l > 10.0f) break;
                for(int k = 0; k < 3; ++k) p->aimPt[k] = cp[k] + fr[k] / l * 30.0f;
                p->aimMs = now; break;
            }
            case 211:
            {   // 5.02.27: a PC player as a passenger: on the phone he sits in the same car (with its PC driver / the phone)
                BitStream bs(pk->data, pk->length, false); bs.IgnoreBits(8);
                uint16_t pid = 0, vid = 0, lr, ud, keys; uint8_t seat = 0, wp = 0, hp = 0, ar = 0; float pos[3];
                if(!bs.Read(pid) || !bs.Read(vid) || !bs.Read(seat) || !bs.Read(wp) || !bs.Read(hp) || !bs.Read(ar) || !bs.Read(lr) || !bs.Read(ud) || !bs.Read(keys) || !RdVec(bs, pos)) break;
                Impl::Rp* p = RemPlayer(I, pid); if(!p) break;
                if(p->slot < 0) RemGhostIn(I, srv, pid, nullptr, 0);
                if(p->slot < 0) break;
                const float zv[3] = { 0, 0, 0 }; const float hd = p->hasPos ? p->last.heading : 0.0f;
                SyncData d = FootToSync(pos, hd, zv, hp, ar, wp & 63, keys, p->skin);
                int owner = -1;
                if(I.myVeh == (int)vid && I.mySeat == 0) owner = I.phoneSlot;   // he rides in the car the phone drives
                else for(auto& kv : I.rp) if(kv.first != (int)pid && kv.second.vehicle == (int)vid && kv.second.slot >= 0) { owner = kv.second.slot; break; }
                auto mt = I.vehModel.find(vid);
                if(owner >= 0 && mt != I.vehModel.end())
                {
                    d.flags |= SYNC_IN_CAR; d.carModel = mt->second; memcpy(d.carPos, pos, sizeof(d.carPos));
                    SyncExt e; memset(&e, 0, sizeof(e)); e.ver = MP_SYNC_EXT_VER; e.time = (uint32_t)now;
                    e.hasVeh = 1; e.vehOwner = (uint8_t)owner; e.vehSeat = (int8_t)std::max(0, (int)(seat & 0x7F) - 1);
                    p->last = d; p->hasPos = true; srv->BridgeSync(p->slot, d, &e);
                }
                else { p->last = d; p->hasPos = true; srv->BridgeSync(p->slot, d); }   // no driver on the phone: he moves with the car
                break;
            }
            case 209: case 210:
            {   // 5.02.27: unoccupied (pushed / hit by a PC player) and trailer sync - the parked car on the phone moves too
                BitStream bs(pk->data, pk->length, false); bs.IgnoreBits(8);
                uint16_t pid = 0, vid = 0; float pos[3], hd = 0;
                if(!bs.Read(pid) || !bs.Read(vid)) break;
                if(pk->data[0] == 209)
                {
                    uint8_t seat; float roll[3], dir[3];
                    if(!bs.Read(seat) || !RdVec(bs, roll) || !RdVec(bs, dir) || !RdVec(bs, pos)) break;
                    hd = atan2f(-dir[0], dir[1]) * 180.0f / kPi; if(hd < 0) hd += 360.0f;
                }
                else
                {
                    float q[4];
                    if(!RdVec(bs, pos) || !bs.Read(q[0]) || !bs.Read(q[1]) || !bs.Read(q[2]) || !bs.Read(q[3])) break;
                    hd = QuatHeading(q[0], q[1], q[2], q[3]);
                }
                if((int)vid == I.myVeh) break;                         // the phone drives / sits in it: the phone is right
                auto vt = I.vehs.find(vid); if(vt == I.vehs.end() || vt->second.taken) break;
                Impl::Veh& v = vt->second;
                const float dx = pos[0] - v.p[0], dy = pos[1] - v.p[1], dz = pos[2] - v.p[2];
                if(dx * dx + dy * dy + dz * dz < 0.25f || now - v.movedAt < 400) break;
                memcpy(v.p, pos, sizeof(v.p)); v.a = hd; v.movedAt = now;
                { std::array<float, 3> vp = { pos[0], pos[1], pos[2] }; I.vehPos[vid] = vp; }
                RemVehShow(I, srv, vid); break;
            }
            case 208:
            {   // 5.02.27: player markers (ShowPlayerMarkers / LimitPlayerMarkerRadius): the PC client shows far players as
                // coloured radar squares - the phone showed only streamed-in players
                BitStream bs(pk->data, pk->length, false); bs.IgnoreBits(8);
                uint32_t n = 0; if(!bs.Read(n) || n > 1000) break;
                for(uint32_t k = 0; k < n; ++k)
                {
                    uint16_t pid = 0; bool on = false; int16_t q[3] = { 0, 0, 0 };
                    if(!bs.Read(pid) || !bs.Read(on)) break;
                    if(on && (!bs.Read(q[0]) || !bs.Read(q[1]) || !bs.Read(q[2]))) break;
                    Impl::Rp* p = (int)pid == I.myId ? nullptr : RemPlayer(I, pid);
                    const bool show = on && p && p->slot < 0;          // streamed in = its ped is on the radar already
                    Impl::Marker& m = I.markers[pid];
                    if(!show) { if(m.on) { m.on = false; srv->GateOp(I.phoneSlot, SPO_MARKER, 0, 0, pid); } continue; }
                    const int dx = q[0] - m.p[0], dy = q[1] - m.p[1];
                    if(m.on && dx * dx + dy * dy < 25 && now - m.at < 5000) continue;
                    if(m.on && now - m.at < 1000) continue;            // at most once a second per player
                    m.on = true; m.at = now; memcpy(m.p, q, sizeof(q));
                    const uint32_t c = p->color; char col[12]; snprintf(col, sizeof(col), "%08X", (unsigned)((c & 0xFF) ? c : (c | 0xFF)));
                    float f[4] = { (float)q[0], (float)q[1], (float)q[2], 0 };
                    srv->GateOp(I.phoneSlot, SPO_MARKER, 1, 0, pid, f, col);
                }
                break;
            }
            case 206:
            {   // 5.02.25: a PC player's bullet hit me -> the damage on the phone + OnPlayerTakeDamage (like samp.exe with lagcomp)
                BitStream bs(pk->data, pk->length, false); bs.IgnoreBits(8);
                uint16_t from = 0, hid = 0; uint8_t type = 0, w = 0; float a[3], b[3], c[3];
                if(!bs.Read(from) || !bs.Read(type) || !bs.Read(hid) || !RdVec(bs, a) || !RdVec(bs, b) || !RdVec(bs, c) || !bs.Read(w)) break;
                if(I.phase != 4 || type != 1 || (int)hid != I.myId || I.wasDead || RemSameTeam(I, from)) break;
                RemTakeDamage(I, srv, from, WeaponDamage(w), w, 3, now);
                break;
            }
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
            if(t[0] == '/' && RemClientCmd(I, srv, t)) continue;   // 5.02.24: samp.exe's own commands
            if(t[0] == '/')
            {   // 5.02.10: /login etc. 5.02.23: 128 bytes at most (SA-MP / open.mp drop a longer command without a word)
                if(t.size() > 128) { printf("[crossplay] command cut to 128 characters (was %u)\n", (unsigned)t.size()); t.resize(128); }
                BitStream o; WrStr32(o, t); CliRpc(I, 50, o); if(I.phase == 3) { I.noAuto = false; I.autoSpawnN = 0; I.retrySpawnAt = now + 2500; }
            }
            else if(!ChatBlank(t)) RemChatQueue(I, t);   // 5.02.23: no blank lines (the PC client never sends them)
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
                I.spawnAskAt = now; I.noAuto = false; I.autoSpawnN = 0; I.retrySpawnAt = 0;
                if(!I.classOk) { I.spawnAfterClass = true; BitStream o; o.Write((int32_t)I.classIdx); CliRpc(I, 128, o); }   // 5.02.23: locked class - ask again first
                else { BitStream o; CliRpc(I, 129, o); }
                printf("[crossplay] spawn button (dialog %d, select %d, spectating %d)\n", (int)I.dlgOpen, (int)I.tdSel, (int)I.spect);
            }
            else { I.classIdx = std::max(0, I.classIdx + (e.a == 1 ? -1 : 1)); BitStream o; o.Write((int32_t)I.classIdx); CliRpc(I, 128, o); }
        }
        else if(e.kind == HK_DIALOG && e.dialog == kMenuDialog && I.menuOpen >= 0)
        {   // 5.02.25: a GTA menu shown as a list: Select = OnPlayerSelectedMenuRow, Close = OnPlayerExitedMenu
            BitStream o; if(e.resp && e.list >= 0) { o.Write((uint8_t)e.list); CliRpc(I, 132, o); } else CliRpc(I, 140, o);
            printf("[crossplay] menu %d: %s %d\n", I.menuOpen, e.resp ? "row" : "exit", (int)e.list); I.menuOpen = -1;
        }
        else if(e.kind == HK_SAMP && e.op == SPO_MYCAR && I.phase == 4) RemMyCar(I, srv, e.val, e.a, now);
        else if(e.kind == HK_SAMP && e.op == SPO_VEHDEATH && I.phase == 4 && I.vehs.count(e.val))
        {   // OnVehicleDeath (the server respawns it: stream out + in at its spawn)
            BitStream o; o.Write((uint16_t)e.val); CliRpc(I, 136, o); printf("[crossplay] vehicle %d destroyed\n", (int)e.val);
        }
        else if(e.kind == HK_SAMP && e.op == SPO_CLICKPLAYER && I.phase >= 3 && e.val >= 0 && e.val < 1000)
        {   // OnPlayerClickPlayer (source 0 = scoreboard)
            BitStream o; o.Write((uint16_t)e.val); o.Write((uint8_t)0); CliRpc(I, 23, o);
        }
        else if(e.kind == HK_SAMP && e.op == SPO_CLICKMAP && I.phase == 4)
        {   // OnPlayerClickMap (samp.exe sends the waypoint once it is set on the pause map)
            BitStream o; WrVec(o, e.f); CliRpc(I, 119, o);
        }
        else if(e.kind == HK_SAMP && e.op == SPO_INTERIOR && I.phase == 4 && e.a != I.interior)
        {   // OnPlayerInteriorChange
            I.interior = e.a; BitStream o; o.Write((uint8_t)e.a); CliRpc(I, 118, o);
        }
        else if(e.kind == HK_SAMP && e.op == SPO_EDIT) RemEdit(I, srv, e.a);   // 5.02.26
        else if(e.kind == HK_SAMP && e.op == SPO_ACTORDMG && I.phase == 4 && I.actors.count(e.val))
        {   // 5.02.62: OnPlayerGiveDamageActor (only vulnerable actors take damage, as on PC)
            BitStream o; o.Write(false); o.Write((uint16_t)e.val); o.Write(e.f[0]); o.Write((uint32_t)e.a); o.Write((uint32_t)(e.b ? e.b : 3)); CliRpc(I, 177, o);
        }
        else if(e.kind == HK_SAMP && e.op == SPO_CAMTARGET && I.phase == 4 && I.camTarget)
        {   // 5.02.62: OnPlayerCameraTarget - object, vehicle, player, actor (65535 = none); sent when it changes
            int t[4] = { 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF };
            if(e.f[0] >= 0 && I.vehs.count((int)e.f[0])) t[1] = (int)e.f[0];
            if(e.f[1] >= 0) for(auto& kv : I.rp) if(kv.second.slot == (int)e.f[1]) { t[2] = kv.first; break; }
            if(e.f[2] >= 0 && I.actors.count((int)e.f[2])) t[3] = (int)e.f[2];
            if(memcmp(t, I.camLast, sizeof(t)))
            {
                memcpy(I.camLast, t, sizeof(t));
                BitStream o; for(int k = 0; k < 4; ++k) o.Write((uint16_t)t[k]); CliRpc(I, 168, o);
            }
        }
        else if(e.kind == HK_RAW && e.op == PKT_VOICE) { if(I.phase >= 3) SvPhoneVoice(I, srv, (const uint8_t*)e.text.data(), (int)e.text.size(), now); }   // 5.02.29: SampVoice
        else if(e.kind == HK_RAW && I.phase == 4) RemPhonePkt(I, srv, e, now);
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
    RemChatSend(I, srv, now);
    if(I.phase == 3 && !I.finDl && I.classAt && now - I.classAt >= 4000 && !I.dlgOpen && !I.tdSel)
    {   // 5.02.17: 0.3.DL scripts open their login in OnPlayerFinishedDownloading (the DL client sends 184 when its models are
        // done). Nothing came 4 s into class selection: send it once, like a client with nothing to download
        I.finDl = true; BitStream o; CliRpc(I, 184, o); printf("[crossplay] FinishDownload sent (no login window yet)\n");
    }
    if(I.phase == 3 && I.retrySpawnAt && now >= I.retrySpawnAt && !I.dlgOpen && !I.tdSel && !I.spect && !I.noAuto)
    {   // 5.02.10: capped (3 per 30 s, a dialog answer resets it) so a login / tutorial screen can't loop or get "spawn before login" kicks
        I.retrySpawnAt = 0;
        if(now - I.autoSpawnWin > 30000) { I.autoSpawnWin = now; I.autoSpawnN = 0; }
        if(++I.autoSpawnN <= 3)
        {   // 5.02.23: a locked class (OnPlayerRequestClass return 0): ask the class again, Spawn follows if it opened up
            if(!I.classOk) { I.spawnAfterClass = true; BitStream o; o.Write((int32_t)I.classIdx); CliRpc(I, 128, o); }
            else { BitStream o; CliRpc(I, 129, o); I.spawnAskAt = now; }
        }
        else if(I.autoSpawnN == 4) srv->GateMsg(I.phoneSlot, 0xA9C4E4FF, "[open.mp] The server keeps you in class selection - log in / finish its dialog first.");
    }

    if(I.phase == 3 && I.spawnAskAt && I.spawnAnsAt < I.spawnAskAt && now - I.spawnAskAt >= 3500 && now - I.classAt >= 8000
       && !I.dlgOpen && !I.tdSel && !I.spect && I.loginUi && !I.loginWaitSaid)
    {   // 5.02.28: a login / tutorial server (it showed a dialog or clickable textdraws): its script spawns us (SpawnPlayer /
        // spectate off) when its steps are done - spawning here ourselves broke the account step (stuck on Life of Player)
        I.loginWaitSaid = true; I.spawnAskAt = 0;
        printf("[crossplay] no answer to Spawn on a login server - waiting for its script to spawn us\n");
        srv->GateMsg(I.phoneSlot, 0xA9C4E4FF, "[open.mp] Finish the server's login / tutorial steps - it spawns you by itself after that.");
    }
    if(I.phase == 3 && I.spawnAskAt && I.spawnAnsAt < I.spawnAskAt && now - I.spawnAskAt >= 3500 && now - I.classAt >= 8000
       && !I.dlgOpen && !I.tdSel && !I.spect && !I.loginUi)
    {   // 5.02.20: Spawn was sent and the server never answered (no 129 at all - not even "refused"). SA-MP 0.3.7 drops
        // RequestSpawn when the player has no spawn info (a gamemode with no AddPlayerClass that spawns its own way),
        // open.mp when it thinks we are already on foot. The PC client would stand in class selection forever, and the
        // PC server streams nobody to a player who is not spawned (everyone looked frozen). First ask the class again +
        // Spawn once more; still nothing -> spawn here: the server's spawn info if it sent one, else Grove Street.
        // Our movement then makes the server count us as on foot, so PC players see us and we see them move.
        if(++I.spawnSilent == 1)
        {
            printf("[crossplay] no answer to Spawn - asking the class again\n");
            { BitStream o; o.Write((int32_t)I.classIdx); CliRpc(I, 128, o); }
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
        if(I.spect && I.phase >= 3 && now - I.specSyncAt >= 100) { I.specSyncAt = now; RemSendSpec(I, me->hasSync ? &me->d : nullptr); }
        if(me->hasSync && I.phase == 4 && !I.spect)
        {
            const bool dead = (me->d.flags & SYNC_DEAD) || me->d.health == 0;
            if(dead && !I.wasDead) { BitStream o; o.Write((uint8_t)255); o.Write(kInvalid); CliRpc(I, 53, o); }
            if(!dead && I.wasDead)
            {
                if(I.wantClass) { I.wantClass = false; I.phase = 3; I.wasDead = false; I.noAuto = true; BitStream o; o.Write((int32_t)I.classIdx); CliRpc(I, 128, o); RemClassSel(I, srv, true); }
                else RemSpawned(I, srv);
            }
            I.wasDead = dead;
            // 5.02.25: in a server car (1 s after OnPlayerEnterVehicle, like the enter animation) = driver / passenger sync
            const bool inCar = (me->d.flags & SYNC_IN_CAR) != 0;
            if(!dead && I.myVeh >= 0 && inCar && now - I.myVehAt >= 800) { RemSendCar(I, me->d); if(I.mySeat == 0) RemModShop(I, me->d.carPos); }
            else if(!dead)
            {   // 5.02.27: SA-MP flags = frame delta (4.1) | loop << 8 | lock x << 9 | lock y << 10 | freeze << 11
                uint16_t ai = 0, af = 0;
                if(me->hasAnim) { ai = (uint16_t)AnimIndex(me->anim.hash, me->anim.block); af = (uint16_t)(4 | ((me->anim.flags & 0x2) ? 0x100 : 0x800) | 0x600); }
                RemSendFoot(I, me->d, ai, af);
            }
            if(!dead)
            {   // 5.02.27: aim sync 10 times a second while aiming / shooting, once a second otherwise (like samp.exe)
                const uint32_t keys = (now < I.keysUntil ? I.heldKeys : 0) | KeysFromFlags(me->d.flags);
                const bool aiming = (keys & 128) || (me->d.flags & SYNC_SHOOTING);
                if(now - I.aimAt >= (aiming ? 100u : 1000u)) { I.aimAt = now; RemSendAim(I, me->d, aiming); }
                const uint16_t tp = RemAimTarget(I, me->d, aiming, false), ta = RemAimTarget(I, me->d, aiming, true);
                const uint32_t tk = ((uint32_t)tp << 16) | ta;
                if(tk != I.targetSent && now - I.weapAt >= 250) { I.weapAt = now; I.targetSent = tk; RemSendWeapons(I, tp, ta); }
            }
            if(dead) { I.lastHp = -1; if(I.myVeh >= 0) RemMyCar(I, srv, -1, 0, now); }
            else
            {   // 5.02.25: health lost without a PC bullet (fall, fire, explosion, car crash) = OnPlayerTakeDamage, issuer INVALID_PLAYER_ID
                const int hp = std::min<int>(me->d.health, std::max(1, I.lastHealth)), ar = std::min<int>(me->d.armour, I.lastArmour);
                if(I.lastHp >= 0 && (hp < I.lastHp || ar < I.lastAr) && now - I.hitAt > 600)
                {
                    const float lost = (float)(std::max(0, I.lastHp - hp) + std::max(0, I.lastAr - ar));
                    BitStream o; o.Write(true); o.Write(kInvalid); o.Write(lost); o.Write((uint32_t)(inCar ? 49 : 54)); o.Write((uint32_t)3); CliRpc(I, 115, o);
                }
                I.lastHp = hp; I.lastAr = ar;
            }
            I.lastMe = me->d; I.haveMe = true;   // 5.02.24: /save /rs
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
void SampGate::SetVoice(bool on) { m_p->svOn = on; }
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
        if(m_pSrv) SvFree(I, m_pSrv);
        I.C->Disconnect(100); RakNetworkFactory::DestroyRakClientInterface(I.C); I.C = nullptr;
    }
    { std::lock_guard<std::mutex> lk(I.qm); I.rpcs.clear(); I.hooks.clear(); }
    m_bJoined = false;
}
