#include "net/samp_keybar.h"   // 5.02.15: the crossplay key bar
// 4.11: the phone side of SA-MP gamemodes running on an MP server (net/server_samp.inc, net/samp.h).
// The server runs the PC gamemode (.amx) and sends PKT_SAMP for the things MP had no packet for yet:
// checkpoints, map icons, camera, freeze, time / weather, interior, attached objects, 3D text labels
// and clickable textdraws (SelectTextDraw). Every /command goes to the gamemode (Pawn.CMD / OnPlayerCommandText),
// and the Y / N / H buttons send SA-MP keys (OnPlayerKeyStateChange).

struct SsLabel { Vec3 p; float dist; uint32_t color; std::string text; };
struct SsAttach { int obj = 0; int model = -1; Vec3 off{ 0, 0, 0 }, rot{ 0, 0, 0 }; uint64_t tryAt = 0; };
struct SsZone { float r[4] = { 0, 0, 0, 0 }; uint32_t col = 0, flash = 0; };   // 4.13.53: gang zones
struct SsVeh { int model = 0, c1 = -1, c2 = -1; Vec3 p{ 0, 0, 0 }; float h = 0; int car = 0; uint64_t tryAt = 0; bool taken = false, locked = false, deadSent = false; };   // 5.02.25
struct SsActor { int model = 0; Vec3 p{ 0, 0, 0 }; float h = 0; bool invul = true; int ped = 0; std::string anim; bool animDone = false; uint64_t tryAt = 0; };
static struct SampSrvState
{
    bool on = false, pawnCmd = false;
    std::string mode;
    bool select = false; uint32_t hover = 0xFFFF00FF;
    bool cp[2] = { false, false }, inCp[2] = { false, false }; Vec3 cpPos[2]; float cpSize[2] = { 0, 0 };
    int icon[100] = { 0 };
    std::unordered_map<int, SsLabel> labels;
    SsAttach att[10];
    bool cam = false; Vec3 camPos{ 0, 0, 0 }, camLook{ 0, 0, 0 }; bool haveLook = false;
    bool frozen = false;
    int keys = 0;
    // 4.13.53
    std::map<int, SsZone> zones;
    std::map<int, SsActor> actors;
    bool audio = false, audioPos = false; Vec3 audioAt{ 0, 0, 0 }; float audioDist = 50.0f;
    bool tags = true; float tagDist = 0.0f; bool tagOff[MP_MAX_PLAYERS] = {};
    // 5.02.13: PC server class selection (SPO_CLASSSEL): < Spawn > like samp.exe, and "spawned" ends the join screen
    bool pcClass = false, pcSpawned = false; int pcClassIdx = 0;
    // 5.02.25: the PC server's cars (CreateVehicle / AddStaticVehicle) + the callbacks samp.exe fires
    std::map<int, SsVeh> vehs;
    int myVeh = -1, mySeat = 0, area = -1, warpVeh = -1, warpSeat = 0; uint64_t warpAt = 0;
    Vec3 wp{ 0, 0, 0 }; bool haveWp = false;
} s_SS;
static bool SampSrvPcSpawned() { return s_SS.pcSpawned; }

static bool SampSrvOn() { return s_SS.on && client.GetState() == NET_CONNECTED; }
static void SampSrvSend(uint8_t op, uint8_t a, uint8_t b, int32_t val, const char* text = nullptr)
{
    if(client.GetState() != NET_CONNECTED) return;
    PktSamp p; memset(&p, 0, sizeof(p)); p.type = PKT_SAMP; p.op = op; p.a = a; p.b = b; p.val = val;
    if(text) MP_CopyStr(p.text, text, sizeof(p.text));
    client.Send(&p, sizeof(p));
}

static void SampSrvClickPlayer(int id) { if(SampSrvOn()) SampSrvSend(SPO_CLICKPLAYER, 0, 0, id); }   // 5.02.25

static void SampSrvReset()
{
    for(auto& b : s_SS.icon) if(b) { Game::RemoveBlip(b); b = 0; }
    for(auto& a : s_SS.att) { if(a.obj > 0) Game::DeleteWorldObject(a.obj); a = SsAttach(); }
    if(s_SS.cam) Game::RestoreCamera();
    if(s_SS.frozen) Game::SetPlayerControl(true);
    for(auto& kv : s_SS.actors) if(kv.second.ped > 0) Game::DeleteChar(kv.second.ped);
    for(auto& kv : s_SS.vehs) if(kv.second.car > 0) Game::DeleteCar(kv.second.car);
    if(s_SS.audio) Sound::StreamStop();
    s_SS = SampSrvState();
    s_PcList.clear();
}

static void SampSrvOnPacket(const uint8_t* d, int len)
{
    if(len < (int)offsetof(PktSamp, text)) return;
    PktSamp p; memset(&p, 0, sizeof(p)); memcpy(&p, d, std::min<int>(len, (int)sizeof(p))); p.text[sizeof(p.text) - 1] = 0;
    const int ped = Game::LocalPed();
    const Vec3 f = { p.f[0], p.f[1], p.f[2] };
    switch(p.op)
    {
        case SPO_HELLO:
            s_SS.on = true; s_SS.pawnCmd = p.a != 0; s_SS.mode = p.text;
            SysMsg(COL_SYSTEM, "This server runs the SA-MP gamemode \"%s\" - every /command goes to it.", p.text);
            break;
        case SPO_TDSELECT: s_SS.select = p.a != 0; if(p.val) s_SS.hover = (uint32_t)p.val; break;
        case SPO_CHECKPOINT:
        {
            int k = p.b ? 1 : 0;
            s_SS.cp[k] = p.a != 0; s_SS.inCp[k] = false; s_SS.cpPos[k] = f; s_SS.cpSize[k] = p.f[3] > 0.5f ? p.f[3] : 3.0f;
            break;
        }
        case SPO_MAPICON:
        {
            if(p.a >= 100) break;
            if(s_SS.icon[p.a]) { Game::RemoveBlip(s_SS.icon[p.a]); s_SS.icon[p.a] = 0; }
            if(p.b) s_SS.icon[p.a] = Game::AddSpriteBlip(f, p.b);
            break;
        }
        case SPO_CAMERA:
            if(p.a == 0) { if(s_SS.cam) Game::RestoreCamera(); s_SS.cam = false; s_SS.haveLook = false; }
            else if(p.a == 1) { s_SS.camPos = f; s_SS.cam = true; if(!s_SS.haveLook) s_SS.camLook = { f.x, f.y + 1.0f, f.z }; }
            else { s_SS.camLook = f; s_SS.haveLook = true; s_SS.cam = true; }
            if(s_SS.cam) Game::SetFixedCamera(s_SS.camPos, s_SS.camLook);
            break;
        case SPO_CONTROL: s_SS.frozen = p.a == 0; Game::SetPlayerControl(p.a != 0); if(ped) Game::FreezeChar(ped, p.a == 0); break;
        case SPO_WORLD: if(ped) Game::SetInterior(ped, p.a); break;
        case SPO_TIME: s_nPcHour = p.a % 24; s_nPcMin = p.b % 60; Game::SetTimeOfDay(p.a, p.b); if(p.val >= 0) Game::ForceWeatherNow(p.val); break;   // 5.02.14: held by ApplyWorld
        case SPO_ATTACH:
        {
            if(p.a >= 10) break;
            SsAttach& a = s_SS.att[p.a];
            if(a.obj > 0) Game::DeleteWorldObject(a.obj);
            a = SsAttach();
            if(p.val >= 0)
            {
                a.model = p.val; a.off = f;
                float r[6] = { 0, 0, 0, 1, 1, 1 }; sscanf(p.text, "%f %f %f %f %f %f", &r[0], &r[1], &r[2], &r[3], &r[4], &r[5]);
                a.rot = { r[0], r[1], r[2] };
            }
            break;
        }
        case SPO_LABEL:
        {
            if(!p.a) { s_SS.labels.erase(p.val); break; }
            SsLabel l; l.p = f; l.dist = p.f[3] > 1.0f ? p.f[3] : 20.0f;
            // the colour is in the last 4 bytes of text (after the 0)
            size_t n = strnlen(p.text, sizeof(p.text) - 5);
            uint32_t c = 0xFFFFFFFF; memcpy(&c, p.text + sizeof(p.text) - 5, 4); if(!c) c = 0xFFFFFFFF;
            l.color = c; l.text.assign(p.text, n);
            s_SS.labels[p.val] = l;
            break;
        }
        // ---- 4.13.53 ----
        case SPO_BUBBLE:
            if(p.val < 0 || p.val >= MP_MAX_PLAYERS) break;
            MP_CopyStr(s_Bubble[p.val].text, p.text, sizeof(s_Bubble[p.val].text));
            s_Bubble[p.val].until = MP_NowMs() + (uint64_t)std::max(500.0f, std::min(p.f[1], 600000.0f));
            break;
        case SPO_ZONE:
        {
            const int id = p.a | (p.b << 8); SsZone& z = s_SS.zones[id];
            memcpy(z.r, p.f, sizeof(z.r));
            switch(p.text[0])
            {
                case 1: z.col = (uint32_t)p.val; break;
                case 2: z.flash = (uint32_t)p.val; break;
                case 3: z.flash = 0; break;
                default: s_SS.zones.erase(id); break;
            }
            break;
        }
        case SPO_ACTOR:
        {
            auto it = s_SS.actors.find(p.val);
            if(p.a == 0) { if(it != s_SS.actors.end()) { if(it->second.ped > 0) Game::DeleteChar(it->second.ped); s_SS.actors.erase(it); } break; }
            SsActor& a = s_SS.actors[p.val];
            if(p.a == 1)
            {
                const int model = atoi(p.text);
                if(a.ped > 0 && model != a.model) { Game::DeleteChar(a.ped); a.ped = 0; }
                a.model = model; a.p = f; a.h = p.f[3]; a.invul = p.b != 0;
                if(a.ped > 0) { Game::SetCharPos(a.ped, a.p, 0.0f); Game::SetCharHeading(a.ped, a.h); Game::SetCharProofs(a.ped, a.invul, a.invul, a.invul, a.invul, a.invul); }
            }
            else if(p.a == 2) { a.anim = p.text; a.animDone = false; }
            else { a.anim.clear(); a.animDone = true; if(a.ped > 0) Game::ClearTasksNow(a.ped); }
            break;
        }
        case SPO_VEHICLE:
        {   // 5.02.25: a PC server car (made near me by SampSrvVehicles)
            auto it = s_SS.vehs.find(p.val);
            if(p.a == 0)
            {
                if(it == s_SS.vehs.end()) break;
                if(it->second.car > 0 && !(ped && Game::IsCharInAnyCar(ped) && Game::GetCarCharIsIn(ped) == it->second.car)) Game::DeleteCar(it->second.car);
                s_SS.vehs.erase(it); break;
            }
            SsVeh& v = s_SS.vehs[p.val];
            if(p.a == 1)
            {
                int model = 0, c2 = -1; sscanf(p.text, "%d %d", &model, &c2);
                if(v.car > 0 && model != v.model) { Game::DeleteCar(v.car); v.car = 0; }
                v.model = model; v.c1 = p.b; v.c2 = c2; v.p = f; v.h = p.f[3]; v.taken = false; v.deadSent = false;
                if(v.car > 0 && s_SS.myVeh != p.val) { Game::SetCarPos(v.car, v.p, 0.0f); Game::SetCarHeading(v.car, v.h); Game::SetCarColours(v.car, v.c1, v.c2); }
            }
            else if(p.a == 2) { v.taken = true; if(v.car > 0 && s_SS.myVeh != p.val) { Game::DeleteCar(v.car); v.car = 0; } }
            else if(p.a == 3) { s_SS.warpVeh = p.val; s_SS.warpSeat = p.b; s_SS.warpAt = 0; }
            else if(p.a == 4) { v.locked = p.b != 0; if(v.car > 0) Game::LockCar(v.car, v.locked); }
            break;
        }
        case SPO_AUDIO:
            if(s_SS.audio) Sound::StreamStop();
            s_SS.audio = false;
            if(p.a && p.text[0])
            {
                s_SS.audioPos = p.b != 0; s_SS.audioAt = f; s_SS.audioDist = p.f[3] > 1.0f ? p.f[3] : 50.0f;
                s_SS.audio = Sound::StreamPlayUrl(p.text, s_SS.audioPos ? 0.0f : 1.0f);
                if(!s_SS.audio) SysMsg(COL_SYSTEM, "Could not play the server's audio stream.");
            }
            break;
        case SPO_VELOCITY:
        {
            if(!ped) break;
            const Vec3 v = { p.f[0] * 50.0f, p.f[1] * 50.0f, p.f[2] * 50.0f };   // SA-MP: metres per 1/50 s
            if(p.b) { const int car = Game::GetCarCharIsIn(ped); if(car > 0) Game::SetMoveSpeed(car, true, v); }
            else Game::SetMoveSpeed(ped, false, v);
            break;
        }
        case SPO_WEAPON:
            if(!ped) break;
            if(p.a == 0) Game::SetCurrentWeapon(ped, p.val);
            else Game::SetAmmo(ped, p.b, p.val);
            break;
        case SPO_TAGS:
            if(p.a == 0) { s_SS.tags = p.b != 0; s_SS.tagDist = p.f[0]; }
            else if(p.val >= 0 && p.val < MP_MAX_PLAYERS) s_SS.tagOff[p.val] = p.b == 0;
            break;
        case SPO_VEHPARAMS:
        {
            const int car = ped ? Game::GetCarCharIsIn(ped) : 0; if(car <= 0) break;
            if(p.a) Game::SetCarEngineOn(car, p.a == 2);
            if(p.b) Game::LockCar(car, p.b == 2);
            break;
        }
        case SPO_CLASSSEL:   // 5.02.13
        {
            const bool was = s_SS.pcClass;
            s_SS.pcClass = p.a != 0; s_SS.pcClassIdx = p.val;
            if(p.a) s_SS.pcSpawned = false; else if(p.b) s_SS.pcSpawned = true;
            // 5.02.15: login servers (SkillArenica, Brasil RP...) hide the class selection themselves and spawn you after
            // their login / register window - there is no Spawn button there on PC either
            if(was && !p.a && !p.b && !s_SS.pcSpawned)
                SysMsg(COL_SYSTEM, "This server spawns you after its login - answer its window (password / register). There is no Spawn button on it, on PC too.");
            break;
        }
        case SPO_PCLIST:   // 5.02.12: the PC server's whole player list (only the nearby ones are in players[])
            if(p.a == 0) s_PcList.clear();
            else if(p.a == 2) s_PcList.erase(p.val);
            else if(p.a == 1 && p.val >= 0 && p.val < 1000)
            {
                PcListEntry& e = s_PcList[p.val];
                if(p.text[0]) e.name = p.text;
                e.score = (int)p.f[0]; e.ping = p.f[1] > 0 ? (int)p.f[1] : (e.ping >= 0 ? e.ping : -1); e.me = p.b != 0;
            }
            break;
        default: break;   // SPO_SOUND / SPO_REMOVEBLD: not on MP yet
    }
}

// 4.13.53: gang zones of the gamemode on the radar + map (mp_world.inc TurfZoneProvider asks)
static int SampSrvZones(Game::RadarZone* out, int maxOut)
{
    if(!SampSrvOn()) return -1;
    const bool flash = (MP_NowMs() / 500) % 2 == 0;
    int n = 0;
    for(auto& kv : s_SS.zones)
    {
        if(n >= maxOut) break;
        const SsZone& z = kv.second;
        uint32_t c = (z.flash && flash) ? z.flash : z.col;
        if(!c) continue;
        out[n++] = { z.r[0], z.r[1], z.r[2], z.r[3], c };
    }
    return n;
}
// 4.13.53: ShowNameTags / SetNameTagDrawDistance / ShowPlayerNameTagForPlayer
static bool SampSrvTagHidden(int id)
{
    if(!SampSrvOn() || id < 0 || id >= MP_MAX_PLAYERS) return false;
    if(!s_SS.tags || s_SS.tagOff[id]) return true;
    if(s_SS.tagDist > 1.0f && players[id].drawValid && myDrawValid)
    {
        const Vec3& a = players[id].drawPos; const float dx = a.x - myDrawPos.x, dy = a.y - myDrawPos.y, dz = a.z - myDrawPos.z;
        if(dx * dx + dy * dy + dz * dz > s_SS.tagDist * s_SS.tagDist) return true;
    }
    return false;
}
// 4.13.53: actors (streamed in near me) and the audio stream volume
static void SampSrvActorsAudio(const Vec3& me, uint64_t now)
{
    int made = 0;   // 5.02.15: a server with many actors made all of them in one frame after the spawn (freeze)
    for(auto& kv : s_SS.actors)
    {
        SsActor& a = kv.second;
        const float dx = a.p.x - me.x, dy = a.p.y - me.y, d2 = dx * dx + dy * dy;
        if(d2 > 200.0f * 200.0f) { if(a.ped > 0) { Game::DeleteChar(a.ped); a.ped = 0; a.animDone = false; } continue; }
        if(a.ped <= 0)
        {
            if(now < a.tryAt || made >= 2) continue;
            a.tryAt = now + 500; ++made;
            if(!Game::IsSafePedModel(a.model) || !Game::ModelReady(a.model)) continue;
            a.ped = Game::CreateNpcPed(a.model, { a.p.x, a.p.y, a.p.z + 1.0f }, a.h);
            if(a.ped <= 0) { a.ped = 0; continue; }
            Game::SetCharProofs(a.ped, a.invul, a.invul, a.invul, a.invul, a.invul);
            Game::FreezeChar(a.ped, true);
            a.animDone = a.anim.empty();
        }
        if(!a.animDone && now >= a.tryAt)
        {
            a.tryAt = now + 300;
            char lib[40] = "", anim[48] = ""; int loop = 0;
            if(sscanf(a.anim.c_str(), "%39s %47s %d", lib, anim, &loop) < 2) { a.animDone = true; continue; }
            a.animDone = loop ? Game::PlayAnimLoop(a.ped, anim, lib) : Game::PlayAnimOnce(a.ped, anim, lib);
        }
    }
    if(s_SS.audio)
    {
        Sound::StreamUpdate();
        if(s_SS.audioPos)
        {
            const float dx = s_SS.audioAt.x - me.x, dy = s_SS.audioAt.y - me.y, dz = s_SS.audioAt.z - me.z;
            const float d = sqrtf(dx * dx + dy * dy + dz * dz);
            Sound::StreamSetVolume(d >= s_SS.audioDist ? 0.0f : 1.0f - d / s_SS.audioDist);
        }
        if(Sound::StreamEnded()) s_SS.audio = false;
    }
}

// 5.02.25: the PC server's cars near me, the car I sit in (OnPlayerEnterVehicle / ExitVehicle), OnVehicleDeath,
// OnPlayerInteriorChange and OnPlayerClickMap (the map waypoint)
static void SampSrvVehicles(int ped, const Vec3& me, uint64_t now)
{
    int made = 0, inCar = 0;
    if(Game::IsCharInAnyCar(ped)) inCar = Game::GetCarCharIsIn(ped);
    int my = -1, seat = 0;
    for(auto& kv : s_SS.vehs)
    {
        SsVeh& v = kv.second;
        if(v.car > 0 && v.car == inCar) { my = kv.first; seat = Game::GetDriverOfCar(inCar) == ped ? 0 : 1; }
        if(v.car > 0 && !v.deadSent && Game::IsCarDead(v.car)) { v.deadSent = true; SampSrvSend(SPO_VEHDEATH, 0, 0, kv.first); }
        const float dx = v.p.x - me.x, dy = v.p.y - me.y, d2 = dx * dx + dy * dy;
        if(v.car > 0 && v.car != inCar)
        {
            Vec3 cp; if(Game::GetCarPos(v.car, cp)) v.p = cp;   // pushed / driven away and left: it stays there
            if(d2 > 300.0f * 300.0f || v.taken) { Game::DeleteCar(v.car); v.car = 0; }
            continue;
        }
        if(v.car > 0 || v.taken || d2 > 250.0f * 250.0f || v.model < 400 || v.model > 611) continue;
        if(now < v.tryAt || made >= 2) continue;
        v.tryAt = now + 400;
        const int c = Game::CreateCar(v.model, v.p);   // 0 while the model streams in
        if(c <= 0) continue;
        ++made; v.car = c;
        Game::SetCarHeading(c, v.h); Game::KeepMissionCar(c);
        if(v.c1 >= 0 || v.c2 >= 0) Game::SetCarColours(c, v.c1 >= 0 ? v.c1 : 1, v.c2 >= 0 ? v.c2 : 1);
        if(v.locked) Game::LockCar(c, true);
    }
    if(my != s_SS.myVeh || (my >= 0 && seat != s_SS.mySeat)) { s_SS.myVeh = my; s_SS.mySeat = seat; SampSrvSend(SPO_MYCAR, (uint8_t)seat, 0, my); }
    if(s_SS.warpVeh >= 0 && now >= s_SS.warpAt)
    {   // PutPlayerInVehicle: the car may still be streaming in
        auto it = s_SS.vehs.find(s_SS.warpVeh); s_SS.warpAt = now + 300;
        if(it == s_SS.vehs.end() || s_SS.myVeh == s_SS.warpVeh) s_SS.warpVeh = -1;
        else if(it->second.car > 0)
        {
            if(s_SS.warpSeat == 0) Game::WarpIntoCar(ped, it->second.car); else Game::WarpIntoCarAsPassenger(ped, it->second.car, s_SS.warpSeat - 1);
            s_SS.warpVeh = -1;
        }
        else if(it->second.model >= 400 && it->second.model <= 611) it->second.tryAt = 0;
        else s_SS.warpVeh = -1;
    }
    const int area = Game::GetCharArea(ped);
    if(area != s_SS.area) { if(s_SS.area >= 0) SampSrvSend(SPO_INTERIOR, (uint8_t)area, 0, 0); s_SS.area = area; }
    Vec3 w; const bool have = Game::GetWaypoint(w);
    if(have && (!s_SS.haveWp || fabsf(w.x - s_SS.wp.x) > 0.5f || fabsf(w.y - s_SS.wp.y) > 0.5f))
    {
        PktSamp q; memset(&q, 0, sizeof(q)); q.type = PKT_SAMP; q.op = SPO_CLICKMAP; q.f[0] = w.x; q.f[1] = w.y; q.f[2] = w.z;
        client.Send(&q, sizeof(q));
    }
    s_SS.haveWp = have; if(have) s_SS.wp = w;
}

// every frame: checkpoints, attached objects, the dynamic map objects, a fixed camera
static void SampSrvProcess(int ped, uint64_t now)
{
    if(!SampSrvOn()) { if(s_SS.on && client.GetState() != NET_CONNECTED) SampSrvReset(); return; }
    if(!ped) return;
    Vec3 me{ 0, 0, 0 }; Game::GetCharPos(ped, me);
    for(int k = 0; k < 2; ++k)
    {
        if(!s_SS.cp[k]) continue;
        const float dx = me.x - s_SS.cpPos[k].x, dy = me.y - s_SS.cpPos[k].y, dz = me.z - s_SS.cpPos[k].z;
        const bool in = dx * dx + dy * dy <= s_SS.cpSize[k] * s_SS.cpSize[k] && fabsf(dz) < 4.0f;
        if(in != s_SS.inCp[k]) { s_SS.inCp[k] = in; SampSrvSend(SPO_CPENTER, in ? 1 : 0, (uint8_t)k, 0); }
    }
    for(auto& a : s_SS.att)
    {
        if(a.model < 0 || a.obj > 0 || now < a.tryAt) continue;
        a.tryAt = now + 500;
        if(!Game::IsKnownModel(a.model)) { a.model = -1; continue; }   // SA-MP-only models (the model pack)
        int o = Game::CreateWorldObject(a.model, me);
        if(o > 0) { a.obj = o; Game::SetObjectCollision(o, false); Game::AttachObjectToCharRot(o, ped, a.off, a.rot); }
    }
    SampSrvActorsAudio(me, now);   // 4.13.53
    SampSrvVehicles(ped, me, now);   // 5.02.25
    if(!DzOnServer()) DzMapTick(me, now);   // CreateObject / CreateDynamicObject of the gamemode (PKT_DZ_MAP)
    if(s_SS.cam && now % 1000 < 40) Game::SetFixedCamera(s_SS.camPos, s_SS.camLook);
}

// /commands: the gamemode has them (a few phone-only ones stay here)
static bool SampSrvCommand(const char* text)
{
    if(!SampSrvOn() || !text || text[0] != '/') return false;
    static const char* const kLocal[] = { "q", "quit", "fps", "bubbles", "icons", "mapicons", "hudmenu", "launcher", "mpsettings",
        // 5.02.24: samp.exe answers these itself (open.mp "Client Commands") - the server never sees them
        "pagesize", "fontsize", "timestamp", "mem", "fpslimit", "headmove", "dl", "nametagstatus", "audiomsg", "hudscalefix", "ctd" };
    char cmd[32] = ""; int n = 0; const char* a = text + 1;
    while(*a && *a != ' ' && n < 30) cmd[n++] = (char)tolower((unsigned char)*a++);
    cmd[n] = 0;
    for(const char* l : kLocal) if(!strcmp(cmd, l)) return false;
    SampSrvSend(SPO_CMD, 0, 0, 0, text);
    return true;
}

// 5.02.15: the crossplay key bar (SA-MP Mobile style). SA-MP key bits: TAB = KEY_ACTION 1, SPC = KEY_SPRINT 8,
// F = KEY_SECONDARY_ATTACK 16, 2 = KEY_SUBMISSION 512, ALT = KEY_WALK 1024, Y = KEY_YES 65536, N = KEY_NO 131072,
// H = KEY_CTRL_BACK 262144. G has no key bit on PC either: samp.exe gets you in the nearest car as a passenger.
static void SampKeyBarPassenger()
{   // 5.02.15: like samp.exe's G - the nearest car within 6 m, a car another player drives first, else any car (the
    // server's parked cars too; before, only a car a player was driving counted, so G on a parked car did nothing)
    const int ped = Game::LocalPed();
    if(ped <= 0 || Game::IsCharInAnyCar(ped) || Game::IsCharDead(ped)) { logger->Info("[keybar] G: not on foot"); return; }
    Vec3 me; if(!Game::GetCharPos(ped, me)) return;
    int best = 0; float bd = 6.0f;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        const RemotePlayer& p = players[i];
        if(i == myId || !p.ped || !p.car) continue;
        Vec3 cp; if(!Game::GetCarPos(p.car, cp) || fabsf(cp.z - me.z) > 3.0f) continue;
        const float d = Dist2D(me, cp); if(d < bd) { bd = d; best = p.car; }
    }
    if(!best) { best = Game::FindNearestCar(me, 6.0f); Vec3 cp; if(best && (!Game::GetCarPos(best, cp) || fabsf(cp.z - me.z) > 3.0f)) best = 0; }
    if(!best) { logger->Info("[keybar] G: no car within 6 m"); SysMsg(COL_YELLOW, "No car next to you to get in as a passenger (G)."); return; }
    const int seat = Game::FreePassengerSeat(best, 0);
    if(seat < 0) { logger->Info("[keybar] G: car %d full", best); SysMsg(COL_YELLOW, "That car is full."); return; }
    Game::ClearTasksNow(ped); Game::TaskEnterCarAsPassenger(ped, best, seat);
    s_nPassBtnCar = best; s_nPassBtnT = MP_NowMs();
    logger->Info("[keybar] G: getting in car %d seat %d (%.1f m)", best, seat, bd);
}
static void SampKeyBar(uint64_t now)
{
    static bool s_bFold = false;
    static SampKeyBarState s_Kb;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float bh = std::max(34.0f, ds.y * 0.062f), bw = bh * 1.18f, gap = bh * 0.14f;
    const int n = s_bFold ? 1 : 1 + SKB_COUNT;
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.05f, ds.y * 0.395f));
    ImGui::SetNextWindowSize(ImVec2(n * (bw + gap) + 12.0f, bh + 12.0f));
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.05f, 0.05f, 0.06f, 0.62f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.05f, 0.05f, 0.06f, 0.62f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.55f, 0.08f, 0.08f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.75f, 0.10f, 0.10f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    ImGui::Begin("##SampKeyBar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoFocusOnAppearing);
    if(ImGui::Button(s_bFold ? ">>##kb" : "<<##kb", ImVec2(bw, bh))) s_bFold = !s_bFold;
    uint32_t held = 0, tapped = 0;
    if(!s_bFold)
        for(int i = 0; i < SKB_COUNT; ++i)
        {
            ImGui::SameLine(0, gap);
            char id[16]; snprintf(id, sizeof(id), "%s##kb%d", kSampKeyBar[i].label, i);
            if(ImGui::Button(id, ImVec2(bw, bh))) tapped |= 1u << i;
            if(ImGui::IsItemActive()) held |= 1u << i;
        }
    ImGui::End();
    ImGui::PopStyleColor(5); ImGui::PopStyleVar(3);
    if(tapped & (1u << SKB_ESC))
    {   // ESC: what samp.exe does - cancel a textdraw menu, else close the dialog (response 0)
        if(s_SS.select) { s_SS.select = false; SampSrvSend(SPO_TDCLICK, 0, 0, -1); }
        else if(s_Dlg.open) CloseDialog(0);
    }
    if(tapped & (1u << SKB_TAB))
    {   // TAB: the PC server's player list too, a toggle like samp.exe's scoreboard
        const bool was = FloatMenu::IsPageOpen(FloatMenu::PAGE_PLAYERS);
        if(was) FloatMenu::ClosePage(FloatMenu::PAGE_PLAYERS); else FloatMenu::OpenPage(FloatMenu::PAGE_PLAYERS);
        logger->Info("[keybar] TAB: player list %s (%d on the PC server)", was ? "closed" : "open", (int)s_PcList.size());
    }
    if(tapped & (1u << SKB_G)) SampKeyBarPassenger();
    bool send = false; const int keys = SampKeyBarStep(s_Kb, now, held, tapped, send);
    if(send) { s_SS.keys = keys; SampSrvSend(SPO_KEYS, 0, 0, keys); }
}

static void SampSrvRender(ImDrawList* dl)
{
    if(!SampSrvOn()) return;
    const int ped = Game::LocalPed();
    Vec3 me{ 0, 0, 0 }; if(ped) Game::GetCharPos(ped, me);
    // checkpoints: the SA-MP red beam
    for(int k = 0; k < 2; ++k) if(s_SS.cp[k]) MkBeam(dl, s_SS.cpPos[k], s_SS.cpSize[k], 6.0f, MK_BEAM_RGBA, s_SS.inCp[k] ? 0.4f : 0.8f);
    // 3D text labels
    ImFont* font = ImGui::GetFont();
    for(auto& kv : s_SS.labels)
    {
        const SsLabel& l = kv.second;
        const float dx = l.p.x - me.x, dy = l.p.y - me.y, dz = l.p.z - me.z, d = sqrtf(dx * dx + dy * dy + dz * dz);
        if(d > l.dist) continue;
        float sx, sy, depth; if(!Game::WorldToScreen(l.p, sx, sy, depth)) continue;
        std::vector<SampSeg> segs; SampSplitColors(l.text.c_str(), TdCol(l.color), segs);
        const float fs = ImGui::GetFontSize() * 0.9f;
        SampDrawSegs(dl, font, fs, ImVec2(sx - SampSegsWidth(font, fs, segs) * 0.5f, sy), segs, std::max(0.35f, 1.0f - d / l.dist));
    }
    // SelectTextDraw: tap a selectable textdraw (Close = Esc)
    if(s_SS.select)
    {
        const ImVec2 ds = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(ds);
        ImGui::Begin("##SampTdSelect", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);
        const ImVec2 m = ImGui::GetIO().MousePos;
        ImDrawList* wl = ImGui::GetWindowDrawList();
        for(int i = 0; i < SAMP_TD_LOCAL; ++i)
        {
            const SampTd& t = s_Td[i];
            if(!t.used || !t.d.pad[0]) continue;
            const ImVec4 r = t.rect;
            if(m.x < r.x || m.y < r.y || m.x > r.z || m.y > r.w) continue;
            wl->AddRectFilled(ImVec2(r.x, r.y), ImVec2(r.z, r.w), (TdCol(s_SS.hover) & 0x00FFFFFF) | 0x50000000);
            if(ImGui::IsMouseClicked(0)) SampSrvSend(SPO_TDCLICK, 0, 0, i);
        }
        ImGui::SetCursorPos(ImVec2(ds.x - 130.0f, 10.0f));
        if(ImGui::Button("Close", ImVec2(120.0f, 44.0f))) { s_SS.select = false; SampSrvSend(SPO_TDCLICK, 0, 0, -1); }
        ImGui::End();
    }
    // 5.02.13: the PC server's class selection - <  Spawn  > (samp.exe's buttons). The MP spawn picker is off on PC servers.
    if(s_SS.pcClass && !s_Dlg.open && !s_SS.select)
    {
        const ImVec2 ds = ImGui::GetIO().DisplaySize;
        const float bh = std::max(56.0f, ds.y * 0.09f), aw = bh * 1.3f, sw = bh * 3.2f, gap = 14.0f, w = aw * 2 + sw + gap * 2 + 16.0f;
        ImGui::SetNextWindowPos(ImVec2((ds.x - w) * 0.5f, ds.y - bh - ds.y * 0.06f)); ImGui::SetNextWindowSize(ImVec2(w, bh + 16.0f));
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::Begin("##SampClassSel", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
        if(ImGui::Button("<##cls", ImVec2(aw, bh))) SampSrvSend(SPO_SPAWNREQ, 1, 0, 0);
        ImGui::SameLine(0, gap);
        if(ImGui::Button("Spawn##cls", ImVec2(sw, bh))) SampSrvSend(SPO_SPAWNREQ, 0, 0, 0);
        ImGui::SameLine(0, gap);
        if(ImGui::Button(">##cls", ImVec2(aw, bh))) SampSrvSend(SPO_SPAWNREQ, 2, 0, 0);
        ImGui::End();
    }
    // 5.02.15: crossplay (PC SA-MP / open.mp server through the gate): the SA-MP Mobile key bar  << ESC TAB ALT SPC F H 2 Y N G.
    // Its own window on the left under the radar, never in the floating menu, only on crossplay servers. PC gamemodes
    // open their menus / doors / engines / jobs with OnPlayerKeyStateChange (Y N H F ALT...), and dialogs close with ESC
    // (open.mp dialog styles: ESC = response 0, like button 2). Native SA-MP gamemodes on MP servers keep the Y / N / H column.
    if(ConnectedToSampProxy()) SampKeyBar(MP_NowMs());
    else if(!s_Dlg.open)
    {   // Y / N / H keys (SA-MP KEY_YES / KEY_NO / KEY_CTRL_BACK)
        const ImVec2 ds = ImGui::GetIO().DisplaySize;
        const float bw = 56.0f;
        ImGui::SetNextWindowPos(ImVec2(ds.x - bw - 12.0f, ds.y * 0.42f)); ImGui::SetNextWindowSize(ImVec2(bw + 8.0f, bw * 3 + 32.0f));
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::Begin("##SampKeys", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
        static const struct { const char* l; int bit; } kKeys[] = { { "Y", 65536 }, { "N", 131072 }, { "H", 262144 } };
        int keys = 0;
        for(auto& k : kKeys) { ImGui::Button(k.l, ImVec2(bw, bw)); if(ImGui::IsItemActive()) keys |= k.bit; }
        ImGui::End();
        if(keys != s_SS.keys) { s_SS.keys = keys; SampSrvSend(SPO_KEYS, 0, 0, keys); }
    }
}
