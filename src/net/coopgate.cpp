// 5.02.79: CoopFusion32 crossplay - see coopgate.h
#include "coopgate.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <map>
#include <deque>
#include <chrono>
#include <algorithm>

namespace {
uint64_t NowMs() { return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
std::vector<std::string> Split(const std::string& s)
{
    std::vector<std::string> f; size_t a = 0;
    for(;;) { size_t b = s.find('|', a); f.push_back(s.substr(a, b == std::string::npos ? std::string::npos : b - a)); if(b == std::string::npos) break; a = b + 1; }
    return f;
}
float Fl(const std::vector<std::string>& f, size_t i, float d = 0) { if(i >= f.size()) return d; char* e = nullptr; float v = strtof(f[i].c_str(), &e); return (e && e != f[i].c_str() && isfinite(v)) ? v : d; }
int   N(const std::vector<std::string>& f, size_t i, int d = 0) { if(i >= f.size() || f[i].empty()) return d; return atoi(f[i].c_str()); }
std::string Clean(const char* s, size_t max)
{   // the co-op protocol splits on '|' and lines: no '|', no line breaks, ASCII names
    std::string o;
    for(const char* p = s ? s : ""; *p && o.size() < max; ++p) { unsigned char c = (unsigned char)*p; o += (c == '|' || c < 32 || c > 126) ? '_' : (char)c; }
    if(o.empty()) o = "Player";
    return o;
}
bool Melee(int w) { return w >= 0 && w <= 15; }
}

struct CoopGate::Impl
{
    std::string host, name; uint16_t port = 0;
    int fd = -1; std::string rx;
    uint16_t myId = 0;
    std::mutex sm; std::string status = "connecting";
    struct Pl
    {
        int slot = -1; std::string name = "Player"; int color = 0;
        int model = 0, anim = 1, hp = 100, arm = 0, weapon = 0, veh = 0, seat = 0;
        float pos[3] = {0, 0, 0}, h = 0, v[3] = {0, 0, 0};
        uint64_t seenAt = 0, shotAt = 0, hitByMeAt = 0; int hitByMeWeapon = 0; bool dead = false, have = false;
    };
    std::map<int, Pl> pl;                                    // CoopFusion id -> player
    struct Veh { int model = 0; float p[3] = {0, 0, 0}; float h = 0; };
    std::map<int, Veh> veh;
    // the phone
    int phoneSlot = -1; SyncData phone{}; bool havePhone = false; float lastPos[3] = {0, 0, 0}; uint64_t lastPosAt = 0; float vel[3] = {0, 0, 0};
    int expect = -1; uint64_t expectAt = 0;                  // the health + armour we already brought the phone down to
    int lastAttacker = 0; uint64_t lastAttackAt = 0;         // co-op id that last shot / hit near the phone
    // phone packets (queued under the server's mutex)
    std::mutex qm; std::deque<std::string> q;
    std::atomic<int> count{0};
};

CoopGate::CoopGate() : m_p(new Impl) {}
CoopGate::~CoopGate() { Stop(); delete m_p; }

bool CoopGate::Start(MPServer* srv, const char* host, uint16_t port, const char* name)
{
    Stop();
    if(!srv || !host || !*host || !port) return false;
    delete m_p; m_p = new Impl;
    m_p->host = host; m_p->port = port; m_p->name = Clean(name, 14);
    m_pSrv = srv; m_bEnded = false; m_bJoined = false; m_bRun = true;
    srv->SetGhostFullSync(true);
    srv->BridgeEnable(true);
    srv->SetPhoneHook(this);
    m_Thread = std::thread(&CoopGate::Loop, this);
    return true;
}

void CoopGate::Stop()
{
    if(!m_bRun && !m_Thread.joinable()) return;
    m_bRun = false;
    if(m_Thread.joinable()) m_Thread.join();
    if(m_pSrv) { m_pSrv->SetPhoneHook(nullptr); m_pSrv->SetGhostFullSync(false); }
    m_pSrv = nullptr; m_bJoined = false;
}

int CoopGate::Players() { return m_p ? m_p->count.load() : 0; }
std::string CoopGate::Status() { std::lock_guard<std::mutex> lk(m_p->sm); return m_p->status; }

void CoopGate::OnPhonePkt(int slot, const uint8_t* data, int len)
{
    if(!m_bRun || !data || len <= 0 || len > 1024) return;
    if(data[0] != PKT_DAMAGE && data[0] != PKT_SHOT && data[0] != PKT_DEATH) return;
    std::string s((const char*)data, (size_t)len); s.insert(s.begin(), (char)(slot & 0xFF));
    std::lock_guard<std::mutex> lk(m_p->qm); if(m_p->q.size() < 256) m_p->q.push_back(s);
}

// ---- the loop ----------------------------------------------------------------------------------
static bool SendLine(CoopGate::Impl& I, const std::string& l)
{
    if(I.fd < 0) return false;
    const char* p = l.data(); size_t n = l.size();
    while(n)
    {
        ssize_t w = send(I.fd, p, n, MSG_NOSIGNAL);
        if(w > 0) { p += w; n -= (size_t)w; continue; }
        if(w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) { pollfd pf{I.fd, POLLOUT, 0}; if(poll(&pf, 1, 50) <= 0) return false; continue; }
        return false;
    }
    return true;
}

static int Connect(const std::string& host, uint16_t port, int timeoutMs)
{
    addrinfo hints{}; hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr; char ps[8]; snprintf(ps, sizeof(ps), "%u", port);
    if(getaddrinfo(host.c_str(), ps, &hints, &res) != 0 || !res) return -1;
    int fd = -1;
    for(addrinfo* a = res; a && fd < 0; a = a->ai_next)
    {
        int s = socket(a->ai_family, a->ai_socktype, a->ai_protocol); if(s < 0) continue;
        fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
        int r = connect(s, a->ai_addr, a->ai_addrlen);
        if(r < 0 && errno == EINPROGRESS)
        {
            pollfd pf{s, POLLOUT, 0};
            if(poll(&pf, 1, timeoutMs) == 1) { int err = 0; socklen_t el = sizeof(err); getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &el); r = err ? -1 : 0; }
            else r = -1;
        }
        if(r == 0) { int one = 1; setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)); fd = s; }
        else close(s);
    }
    freeaddrinfo(res);
    return fd;
}

static CoopGate::Impl::Pl& Player(CoopGate::Impl& I, MPServer* srv, int id)
{
    CoopGate::Impl::Pl& p = I.pl[id];
    if(p.slot < 0)
    {
        p.slot = srv->BridgeAdd(p.name.c_str(), 0xFFFFFFFF, (uint16_t)p.model, " (Co-op)");
        I.count = (int)I.pl.size();
    }
    return p;
}

static void SyncGhost(CoopGate::Impl& I, MPServer* srv, CoopGate::Impl::Pl& p, uint64_t now)
{
    if(p.slot < 0) return;
    SyncData d; memset(&d, 0, sizeof(d));
    memcpy(d.pos, p.pos, sizeof(d.pos)); d.heading = p.h;
    const float rad = p.h * 0.0174532925f;
    d.aim[0] = p.pos[0] - sinf(rad) * 10.0f; d.aim[1] = p.pos[1] + cosf(rad) * 10.0f; d.aim[2] = p.pos[2];
    d.speed = sqrtf(p.v[0] * p.v[0] + p.v[1] * p.v[1] + p.v[2] * p.v[2]) * 50.0f;   // GTA units / frame -> m/s
    d.health = (uint8_t)std::max(0, std::min(255, p.hp)); d.armour = (uint8_t)std::max(0, std::min(255, p.arm));
    d.weapon = (uint8_t)std::max(0, std::min(255, p.weapon)); d.skin = (uint16_t)p.model;
    if(p.hp <= 0) d.flags |= SYNC_DEAD;
    if(now - p.shotAt < 300) d.flags |= SYNC_SHOOTING;
    SyncExt e; memset(&e, 0, sizeof(e)); e.ver = MP_SYNC_EXT_VER; e.time = (uint32_t)now;
    e.moveState = (uint8_t)std::max(1, std::min(7, p.anim)); memcpy(e.vel, p.v, sizeof(e.vel));
    bool ext = true;
    auto vt = p.veh ? I.veh.find(p.veh) : I.veh.end();
    if(vt != I.veh.end() && vt->second.model >= 400 && vt->second.model < 612)
    {
        if(p.seat == 0)
        {   // he drives: his ghost car
            d.flags |= SYNC_IN_CAR | SYNC_DRIVER; d.carModel = (uint16_t)vt->second.model;
            memcpy(d.carPos, vt->second.p, sizeof(d.carPos)); d.carHeading = vt->second.h;
        }
        else
        {   // a passenger: in the car of its co-op driver (when we have that driver)
            int owner = -1;
            for(auto& kv : I.pl) if(&kv.second != &p && kv.second.veh == p.veh && kv.second.seat == 0 && kv.second.slot >= 0) { owner = kv.second.slot; break; }
            if(owner >= 0) { d.flags |= SYNC_IN_CAR; d.carModel = (uint16_t)vt->second.model; memcpy(d.carPos, vt->second.p, sizeof(d.carPos)); e.hasVeh = 1; e.vehOwner = (uint8_t)owner; e.vehSeat = (int8_t)std::max(0, p.seat - 1); }
        }
    }
    srv->BridgeSync(p.slot, d, ext ? &e : nullptr);
}

static void Died(CoopGate::Impl& I, MPServer* srv, CoopGate::Impl::Pl& p, uint64_t now)
{   // a co-op player's health reached 0: the kill feed (ours when the phone hurt him in the last 6 s)
    if(p.dead || p.slot < 0) return;
    p.dead = true;
    const bool mine = p.hitByMeAt && now - p.hitByMeAt < 6000;
    srv->GateDeath(p.slot, mine ? I.phoneSlot : -1, (uint8_t)(mine ? p.hitByMeWeapon : 255));
}

static void HurtPhone(CoopGate::Impl& I, MPServer* srv, int attackerId, int weapon, int total, uint64_t now)
{   // bring the phone's health + armour down to `total` (never twice for the same hit: `expect`)
    if(I.phoneSlot < 0 || !I.havePhone) return;
    const int have = (int)I.phone.health + (int)I.phone.armour;
    int ref = have;
    if(I.expect >= 0 && now - I.expectAt < 3000) ref = std::min(ref, I.expect);
    const int dmg = ref - total;
    if(dmg <= 0) return;
    I.expect = total; I.expectAt = now;
    auto it = I.pl.find(attackerId);
    const int from = it != I.pl.end() ? it->second.slot : -1;
    if(weapon == 255 && it != I.pl.end()) weapon = it->second.weapon;   // a health report carries no weapon: use the attacker's gun
    srv->GateDamage(from, I.phoneSlot, (uint8_t)std::max(0, std::min(255, weapon)), (float)dmg);
}

static void Line(CoopGate& G, CoopGate::Impl& I, MPServer* srv, const std::string& line, std::atomic<bool>& joined, uint64_t now)
{
    if(line.empty()) return;
    const std::vector<std::string> f = Split(line);
    const std::string& t = f[0];
    if(t == "WELCOME") { I.myId = (uint16_t)N(f, 1); joined = true; std::lock_guard<std::mutex> lk(I.sm); I.status = "on the co-op server"; return; }
    if(t == "NAME" && f.size() >= 3)
    {
        const int id = N(f, 1); if(id <= 0 || id == I.myId) return;
        CoopGate::Impl::Pl& p = I.pl[id]; p.name = f[2]; p.color = N(f, 3);
        if(p.slot < 0) Player(I, srv, id);
        return;
    }
    if(t == "PLAYER" && f.size() >= 14)
    {
        const int id = N(f, 1); if(id <= 0 || id == I.myId) return;
        auto& raw = I.pl[id];
        if(!f[13].empty()) raw.name = f[13];
        raw.model = N(f, 2); raw.anim = N(f, 3, 1);
        raw.pos[0] = Fl(f, 4); raw.pos[1] = Fl(f, 5); raw.pos[2] = Fl(f, 6); raw.h = Fl(f, 7);
        const int hp = N(f, 8, 100); raw.arm = N(f, 9); raw.weapon = N(f, 10); raw.veh = N(f, 11); raw.seat = N(f, 12);
        raw.v[0] = Fl(f, 18); raw.v[1] = Fl(f, 19); raw.v[2] = Fl(f, 20);
        raw.hp = hp; raw.seenAt = now; raw.have = true;
        CoopGate::Impl::Pl& p = Player(I, srv, id);
        if(hp <= 0) Died(I, srv, p, now); else p.dead = false;
        SyncGhost(I, srv, p, now);
        return;
    }
    if(t == "PLAYER_VITALS" && f.size() >= 4)
    {
        const int id = N(f, 1), hp = N(f, 2), arm = N(f, 3);
        if(id == I.myId) { HurtPhone(I, srv, (now - I.lastAttackAt < 1500) ? I.lastAttacker : 0, 255, hp + arm, now); return; }
        auto it = I.pl.find(id); if(it == I.pl.end()) return;
        it->second.hp = hp; it->second.arm = arm;
        if(hp <= 0) Died(I, srv, it->second, now); else it->second.dead = false;
        SyncGhost(I, srv, it->second, now);
        return;
    }
    if(t == "PLAYER_HIT" && f.size() >= 4)
    {   // a co-op player's melee hit on us (the server sends it to the target only)
        const int from = N(f, 1), w = N(f, 2), dmg = N(f, 3);
        if(dmg <= 0 || !I.havePhone) return;
        I.lastAttacker = from; I.lastAttackAt = now;
        int have = (int)I.phone.health + (int)I.phone.armour;
        if(I.expect >= 0 && now - I.expectAt < 3000) have = std::min(have, I.expect);
        HurtPhone(I, srv, from, w, std::max(0, have - std::min(dmg, 200)), now);
        return;
    }
    if((t == "SHOT" && f.size() >= 9) || (t == "SHOTX" && f.size() >= 12))
    {   // a co-op player fires: his ghost shoots, and he is the one who hurt us when our health drops right after
        const int id = N(f, 1); auto it = I.pl.find(id); if(it == I.pl.end()) return;
        it->second.shotAt = now;
        float a[3], b[3];
        if(t == "SHOT") { for(int k = 0; k < 3; ++k) { a[k] = Fl(f, 3 + k); b[k] = a[k] + Fl(f, 6 + k) * 60.0f; } }
        else { for(int k = 0; k < 3; ++k) { a[k] = Fl(f, 5 + k); b[k] = Fl(f, 8 + k); } }
        if(I.havePhone)
        {   // the bullet passes within 2.5 m of the phone: he is the attacker
            const float* q = (I.phone.flags & SYNC_IN_CAR) ? I.phone.carPos : I.phone.pos;
            float ab[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, aq[3] = { q[0] - a[0], q[1] - a[1], q[2] - a[2] };
            const float l2 = ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2];
            float tt = l2 > 0.0001f ? (aq[0] * ab[0] + aq[1] * ab[1] + aq[2] * ab[2]) / l2 : 0; tt = std::max(0.0f, std::min(1.2f, tt));
            const float dx = a[0] + ab[0] * tt - q[0], dy = a[1] + ab[1] * tt - q[1], dz = a[2] + ab[2] * tt - q[2];
            if(dx * dx + dy * dy + dz * dz < 2.5f * 2.5f) { I.lastAttacker = id; I.lastAttackAt = now; }
        }
        SyncGhost(I, srv, it->second, now);
        return;
    }
    if(t == "VEHICLE" && f.size() >= 9)
    {
        CoopGate::Impl::Veh& v = I.veh[N(f, 1)];
        v.model = N(f, 2); v.p[0] = Fl(f, 5); v.p[1] = Fl(f, 6); v.p[2] = Fl(f, 7); v.h = Fl(f, 8);
        return;
    }
    if(t == "VEHICLE_REMOVE") { I.veh.erase(N(f, 1)); return; }
    if(t == "REMOVE")
    {
        auto it = I.pl.find(N(f, 1)); if(it == I.pl.end()) return;
        if(it->second.slot >= 0) srv->BridgeRemove(it->second.slot);
        I.pl.erase(it); I.count = (int)I.pl.size();
        return;
    }
    (void)G;
}

static void PhonePkt(CoopGate::Impl& I, const std::string& e, uint64_t now)
{
    if(e.size() < 2) return;
    const int slot = (unsigned char)e[0]; const uint8_t* d = (const uint8_t*)e.data() + 1; const int len = (int)e.size() - 1;
    (void)slot;
    auto bySlot = [&](int s) -> std::map<int, CoopGate::Impl::Pl>::iterator { for(auto it = I.pl.begin(); it != I.pl.end(); ++it) if(it->second.slot == s) return it; return I.pl.end(); };
    char b[256];
    if(d[0] == PKT_DAMAGE && len >= (int)sizeof(PktDamage))
    {   // the phone hurt a co-op player: report the health we see (the server keeps the lowest), melee as a hit too
        PktDamage p; memcpy(&p, d, sizeof(p));
        auto it = bySlot(p.target); if(it == I.pl.end() || !(p.amount > 0) || p.amount > 300) return;
        auto& pl = it->second;
        int dmg = (int)ceilf(p.amount);
        int arm = pl.arm, hp = pl.hp;
        const int fromArm = std::min(arm, dmg); arm -= fromArm; hp = std::max(0, hp - (dmg - fromArm));
        pl.arm = arm; pl.hp = hp; pl.hitByMeAt = now; pl.hitByMeWeapon = p.weapon;
        if(Melee(p.weapon)) { snprintf(b, sizeof(b), "PLAYER_HIT|%d|%d|%d|3|0|255|255\n", it->first, (int)p.weapon, std::min(10000, dmg)); SendLine(I, b); }
        snprintf(b, sizeof(b), "PLAYER_HEALTH|%d|%d|%d\n", it->first, std::min(100, hp), std::min(100, arm)); SendLine(I, b);
    }
    else if(d[0] == PKT_SHOT && len >= MP_PKT_SHOT_HDR + (int)sizeof(ShotEntry))
    {   // every bullet of the phone: the co-op players replay it (their game hits with it, like their own shots)
        PktShot m; memset(&m, 0, sizeof(m)); memcpy(&m, d, std::min<int>(len, (int)sizeof(m)));
        const int n = std::min<int>(m.count, (len - MP_PKT_SHOT_HDR) / (int)sizeof(ShotEntry));
        for(int i = 0; i < n && i < MP_SHOT_MAX; ++i)
        {
            const ShotEntry& s = m.e[i];
            if(s.kind != 0 || s.weapon > 0x2E) continue;
            snprintf(b, sizeof(b), "SHOTX|0|%u|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|0|0|0\n", (unsigned)s.weapon, s.a[0], s.a[1], s.a[2], s.b[0], s.b[1], s.b[2]);
            SendLine(I, b);
        }
    }
    else if(d[0] == PKT_DEATH)
    {   // the phone died: the next INPUT says health 0; a new life starts the damage count again
        I.expect = -1;
    }
}

static void SendInput(CoopGate::Impl& I, uint64_t now)
{
    if(!I.havePhone) return;
    const SyncData& d = I.phone;
    const bool car = (d.flags & SYNC_IN_CAR) != 0;
    const float* p = car ? d.carPos : d.pos; const float h = car ? d.carHeading : d.heading;
    if(I.lastPosAt && now > I.lastPosAt)
    {
        const float dt = (now - I.lastPosAt) / 1000.0f;
        if(dt > 0.02f && dt < 1.0f) for(int k = 0; k < 3; ++k) I.vel[k] = (p[k] - I.lastPos[k]) / dt / 50.0f;   // m/s -> GTA units / frame
    }
    memcpy(I.lastPos, p, sizeof(I.lastPos)); I.lastPosAt = now;
    const float sp = car ? 0 : d.speed;
    const int move = sp < 0.3f ? 1 : (sp < 2.6f ? 4 : (sp < 5.5f ? 6 : 7));
    const float rad = h * 0.0174532925f, aimX = -sinf(rad), aimY = cosf(rad);
    int hp = d.health, arm = d.armour;
    if(d.flags & SYNC_DEAD) hp = 0;
    char b[384];
    snprintf(b, sizeof(b), "INPUT|%.3f|%.3f|%.3f|%.3f|%d|0|0|0.000|0.000|%.3f|%.3f|%u|%d|%u|%d|%d|0||0|0|0|0|0|%.4f|%.4f|%.4f\n",
             p[0], p[1], p[2], h, (d.flags & SYNC_SHOOTING) ? 1 : 0, aimX, aimY, (unsigned)d.skin, move, (unsigned)d.weapon,
             std::min(100, hp), std::min(100, arm), I.vel[0], I.vel[1], I.vel[2]);
    SendLine(I, b);
}

void CoopGate::Loop()
{
    Impl& I = *m_p; MPServer* srv = m_pSrv;
    auto setStatus = [&](const std::string& s) { std::lock_guard<std::mutex> lk(I.sm); I.status = s; };
    setStatus("connecting to " + I.host + ":" + std::to_string(I.port));
    I.fd = Connect(I.host, I.port, 5000);
    if(I.fd < 0) { setStatus("no answer from " + I.host + ":" + std::to_string(I.port)); m_bEnded = true; m_bRun = false; return; }
    SendLine(I, "JOIN|" + I.name + "|0\n");
    uint64_t lastIn = 0, lastPeer = 0, lastSweep = 0, start = NowMs();
    char buf[8192];
    while(m_bRun)
    {
        pollfd pf{I.fd, POLLIN, 0};
        const int pr = poll(&pf, 1, 10);
        const uint64_t now = NowMs();
        if(pr > 0 && (pf.revents & (POLLIN | POLLHUP | POLLERR)))
        {
            ssize_t n = recv(I.fd, buf, sizeof(buf), 0);
            if(n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) { setStatus("the co-op server closed the connection"); m_bEnded = true; break; }
            if(n > 0)
            {
                I.rx.append(buf, (size_t)n);
                if(I.rx.size() > 262144) { setStatus("the co-op server sent too much"); m_bEnded = true; break; }
                size_t e;
                while((e = I.rx.find('\n')) != std::string::npos)
                {
                    std::string l = I.rx.substr(0, e); I.rx.erase(0, e + 1);
                    while(!l.empty() && (l.back() == '\r' || l.back() == ' ')) l.pop_back();
                    if(l.size() <= 4096) Line(*this, I, srv, l, m_bJoined, now);
                }
            }
        }
        if(!m_bJoined && now - start > 8000) { setStatus("the co-op server did not answer the join"); m_bEnded = true; break; }
        if(now - lastPeer >= 33)
        {   // the phone on the private server
            lastPeer = now;
            std::vector<MPServer::BridgePeer> peers; srv->BridgePeers(peers);
            I.havePhone = false;
            for(auto& b : peers) if(b.hasSync) { I.phoneSlot = b.slot; I.phone = b.d; I.havePhone = true; break; }
            if(I.havePhone && I.expect >= 0 && (int)I.phone.health + (int)I.phone.armour > I.expect + 5) I.expect = -1;   // healed / respawned
        }
        { std::deque<std::string> q; { std::lock_guard<std::mutex> lk(I.qm); q.swap(I.q); } for(auto& e : q) PhonePkt(I, e, now); }
        { std::vector<std::pair<int, std::string>> chat; srv->BridgeTakeChat(chat); }   // CoopFusion32 has no chat lines: the phone's chat stays local
        if(m_bJoined && now - lastIn >= 50) { lastIn = now; SendInput(I, now); }
        if(now - lastSweep >= 1000)
        {   // a co-op player with no snapshot for 10 s is gone
            lastSweep = now;
            for(auto it = I.pl.begin(); it != I.pl.end();)
                if(it->second.seenAt && now - it->second.seenAt > 10000) { if(it->second.slot >= 0) srv->BridgeRemove(it->second.slot); it = I.pl.erase(it); }
                else ++it;
            I.count = (int)I.pl.size();
        }
    }
    for(auto& kv : I.pl) if(kv.second.slot >= 0) srv->BridgeRemove(kv.second.slot);
    I.pl.clear(); I.count = 0;
    if(I.fd >= 0) { close(I.fd); I.fd = -1; }
    if(!m_bEnded) setStatus("left the co-op server");
    m_bRun = false;
}
