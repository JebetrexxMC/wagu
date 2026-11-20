// 4.11: the phone side of SA-MP gamemodes running on an MP server (net/server_samp.inc, net/samp.h).
// The server runs the PC gamemode (.amx) and sends PKT_SAMP for the things MP had no packet for yet:
// checkpoints, map icons, camera, freeze, time / weather, interior, attached objects, 3D text labels
// and clickable textdraws (SelectTextDraw). Every /command goes to the gamemode (Pawn.CMD / OnPlayerCommandText),
// and the Y / N / H buttons send SA-MP keys (OnPlayerKeyStateChange).

struct SsLabel { Vec3 p; float dist; uint32_t color; std::string text; };
struct SsAttach { int obj = 0; int model = -1; Vec3 off{ 0, 0, 0 }, rot{ 0, 0, 0 }; uint64_t tryAt = 0; };
struct SsZone { float r[4] = { 0, 0, 0, 0 }; uint32_t col = 0, flash = 0; };   // 4.13.53: gang zones
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
} s_SS;

static bool SampSrvOn() { return s_SS.on && client.GetState() == NET_CONNECTED; }
static void SampSrvSend(uint8_t op, uint8_t a, uint8_t b, int32_t val, const char* text = nullptr)
{
    if(client.GetState() != NET_CONNECTED) return;
    PktSamp p; memset(&p, 0, sizeof(p)); p.type = PKT_SAMP; p.op = op; p.a = a; p.b = b; p.val = val;
    if(text) MP_CopyStr(p.text, text, sizeof(p.text));
    client.Send(&p, sizeof(p));
}

static void SampSrvReset()
{
    for(auto& b : s_SS.icon) if(b) { Game::RemoveBlip(b); b = 0; }
    for(auto& a : s_SS.att) { if(a.obj > 0) Game::DeleteWorldObject(a.obj); a = SsAttach(); }
    if(s_SS.cam) Game::RestoreCamera();
    if(s_SS.frozen) Game::SetPlayerControl(true);
    for(auto& kv : s_SS.actors) if(kv.second.ped > 0) Game::DeleteChar(kv.second.ped);
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
        case SPO_TIME: Game::SetTimeOfDay(p.a, p.b); if(p.val >= 0) Game::ForceWeatherNow(p.val); break;
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
    for(auto& kv : s_SS.actors)
    {
        SsActor& a = kv.second;
        const float dx = a.p.x - me.x, dy = a.p.y - me.y, d2 = dx * dx + dy * dy;
        if(d2 > 200.0f * 200.0f) { if(a.ped > 0) { Game::DeleteChar(a.ped); a.ped = 0; a.animDone = false; } continue; }
        if(a.ped <= 0)
        {
            if(now < a.tryAt) continue;
            a.tryAt = now + 500;
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
    if(!DzOnServer()) DzMapTick(me, now);   // CreateObject / CreateDynamicObject of the gamemode (PKT_DZ_MAP)
    if(s_SS.cam && now % 1000 < 40) Game::SetFixedCamera(s_SS.camPos, s_SS.camLook);
}

// /commands: the gamemode has them (a few phone-only ones stay here)
static bool SampSrvCommand(const char* text)
{
    if(!SampSrvOn() || !text || text[0] != '/') return false;
    static const char* const kLocal[] = { "q", "quit", "fps", "bubbles", "icons", "mapicons", "hudmenu", "launcher", "mpsettings" };
    char cmd[32] = ""; int n = 0; const char* a = text + 1;
    while(*a && *a != ' ' && n < 30) cmd[n++] = (char)tolower((unsigned char)*a++);
    cmd[n] = 0;
    for(const char* l : kLocal) if(!strcmp(cmd, l)) return false;
    SampSrvSend(SPO_CMD, 0, 0, 0, text);
    return true;
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
    // Y / N / H keys (SA-MP KEY_YES / KEY_NO / KEY_CTRL_BACK)
    if(!s_Dlg.open)
    {
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
