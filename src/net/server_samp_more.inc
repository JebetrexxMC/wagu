// 4.13.53: SA-MP natives that were "ok" stubs before (included in server_samp.inc, inside its namespace).
// Chat bubbles, gang zones, actors, menus (shown as list dialogs), audio streams, player / vehicle
// velocity, armed weapon / ammo, spectating, vehicle colours / repair / params and name tags.
// The phone side is src/mp_sampsrv.inc (PKT_SAMP ops SPO_BUBBLE..SPO_TAGS in net/samp.h).

static void OpAll(uint8_t op, uint8_t a, uint8_t b, int32_t val, const float* f = nullptr, const char* text = nullptr)
{
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(g_SampPl[i].on) S_->SampOp(i, op, a, b, val, f, text);
}
static int DriverOf(cell veh)
{
    if(veh <= 0) return -1;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(g_SampPl[i].on && g_SampPl[i].vehicle == veh && g_SampPl[i].state == 2) return i;
    return -1;
}
static void ColHex(char* out, size_t n, uint32_t rgba) { snprintf(out, n, "{%06X}", (unsigned)((rgba >> 8) & 0xFFFFFF)); }

// ---- chat bubble --------------------------------------------------------------------------------
N(SetPlayerChatBubble)
{   // (playerid, text[], color, Float:drawdistance, expiretime)
    if(!PlOk(params[1])) return 0;
    char c[16]; ColHex(c, sizeof(c), (uint32_t)params[3]);
    std::string t = std::string(c) + Str(amx, params[2]);
    float f[4] = { PF(params, 4), (float)params[5], 0, 0 };
    OpAll(SPO_BUBBLE, 1, 0, params[1], f, t.c_str());
    return 1;
}

// ---- gang zones (radar + map) -----------------------------------------------------------------
static SampZone* ZN(cell id) { return id >= 0 && id < (cell)g_Samp->zones.size() && g_Samp->zones[id].used ? &g_Samp->zones[id] : nullptr; }
static void ZoneSend(int slot, int id, int mode, uint32_t col)
{
    SampZone* z = ZN(id); if(!z) return;
    char m[2] = { (char)mode, 0 };
    if(slot < 0) OpAll(SPO_ZONE, (uint8_t)(id & 0xFF), (uint8_t)(id >> 8), (int32_t)col, z->r, m);
    else S_->SampOp(slot, SPO_ZONE, (uint8_t)(id & 0xFF), (uint8_t)(id >> 8), (int32_t)col, z->r, m);
}
N(GangZoneCreate)
{
    for(size_t i = 0; i < g_Samp->zones.size(); ++i) if(!g_Samp->zones[i].used)
    {
        SampZone& z = g_Samp->zones[i]; z = SampZone(); z.used = true;
        z.r[0] = std::min(PF(params, 1), PF(params, 3)); z.r[1] = std::min(PF(params, 2), PF(params, 4));
        z.r[2] = std::max(PF(params, 1), PF(params, 3)); z.r[3] = std::max(PF(params, 2), PF(params, 4));
        return (cell)i;
    }
    return -1;
}
N(GangZoneDestroy) { SampZone* z = ZN(params[1]); if(!z) return 0; ZoneSend(-1, (int)params[1], 4, 0); z->used = false; return 1; }
N(GangZoneShowForPlayer) { if(!PlOk(params[1]) || !ZN(params[2])) return 0; ZoneSend((int)params[1], (int)params[2], 1, (uint32_t)params[3]); return 1; }
N(GangZoneShowForAll) { SampZone* z = ZN(params[1]); if(!z) return 0; z->allCol = (uint32_t)params[2]; z->allOn = true; ZoneSend(-1, (int)params[1], 1, z->allCol); return 1; }
N(GangZoneHideForPlayer) { if(!PlOk(params[1]) || !ZN(params[2])) return 0; ZoneSend((int)params[1], (int)params[2], 4, 0); return 1; }
N(GangZoneHideForAll) { SampZone* z = ZN(params[1]); if(!z) return 0; z->allOn = false; z->flashCol = 0; ZoneSend(-1, (int)params[1], 4, 0); return 1; }
N(GangZoneFlashForPlayer) { if(!PlOk(params[1]) || !ZN(params[2])) return 0; ZoneSend((int)params[1], (int)params[2], 2, (uint32_t)params[3]); return 1; }
N(GangZoneFlashForAll) { SampZone* z = ZN(params[1]); if(!z) return 0; z->flashCol = (uint32_t)params[2]; ZoneSend(-1, (int)params[1], 2, z->flashCol); return 1; }
N(GangZoneStopFlashForPlayer) { if(!PlOk(params[1]) || !ZN(params[2])) return 0; ZoneSend((int)params[1], (int)params[2], 3, 0); return 1; }
N(GangZoneStopFlashForAll) { SampZone* z = ZN(params[1]); if(!z) return 0; z->flashCol = 0; ZoneSend(-1, (int)params[1], 3, 0); return 1; }

// ---- actors -------------------------------------------------------------------------------------
static SampActor* AC(cell id) { auto it = g_Samp->actors.find((int)id); return it == g_Samp->actors.end() ? nullptr : &it->second; }
static void ActorSend(int slot, int id)
{
    SampActor* a = AC(id);
    char t[200];
    if(!a) { if(slot < 0) OpAll(SPO_ACTOR, 0, 0, id); else S_->SampOp(slot, SPO_ACTOR, 0, 0, id); return; }
    snprintf(t, sizeof(t), "%d", a->model);
    if(slot < 0) OpAll(SPO_ACTOR, 1, a->invul ? 1 : 0, id, a->p, t); else S_->SampOp(slot, SPO_ACTOR, 1, a->invul ? 1 : 0, id, a->p, t);
    if(!a->anim.empty()) { if(slot < 0) OpAll(SPO_ACTOR, 2, 0, id, nullptr, a->anim.c_str()); else S_->SampOp(slot, SPO_ACTOR, 2, 0, id, nullptr, a->anim.c_str()); }
}
static cell ActorNew(int model, float x, float y, float z, float r, bool invul, float hp, int world)
{
    if(g_Samp->actors.size() >= 1000) return 0xFFFF;
    int id = g_Samp->actorNext++;
    SampActor& a = g_Samp->actors[id]; a.model = model; a.p[0] = x; a.p[1] = y; a.p[2] = z; a.p[3] = r; a.invul = invul; a.hp = hp; a.world = world;
    ActorSend(-1, id);
    return id;
}
N(CreateActor) { return ActorNew((int)params[1], PF(params, 2), PF(params, 3), PF(params, 4), PF(params, 5), true, 100.0f, 0); }
N(CreateDynamicActor)
{   // streamer: (modelid, x, y, z, r, invulnerable = 1, health = 100, worldid = -1, ...)
    int n = NArgs(params);
    return ActorNew((int)params[1], PF(params, 2), PF(params, 3), PF(params, 4), PF(params, 5), n >= 6 ? params[6] != 0 : true, n >= 7 ? PF(params, 7) : 100.0f, n >= 8 ? (int)params[8] : -1);
}
N(DestroyActor) { if(!AC(params[1])) return 0; g_Samp->actors.erase((int)params[1]); ActorSend(-1, (int)params[1]); return 1; }
N(IsValidActor) { return AC(params[1]) != nullptr; }
N(SetActorPos) { SampActor* a = AC(params[1]); if(!a) return 0; a->p[0] = PF(params, 2); a->p[1] = PF(params, 3); a->p[2] = PF(params, 4); ActorSend(-1, (int)params[1]); return 1; }
N(GetActorPos) { SampActor* a = AC(params[1]); if(!a) return 0; SetRefF(amx, params[2], a->p[0]); SetRefF(amx, params[3], a->p[1]); SetRefF(amx, params[4], a->p[2]); return 1; }
N(SetActorFacingAngle) { SampActor* a = AC(params[1]); if(!a) return 0; a->p[3] = PF(params, 2); ActorSend(-1, (int)params[1]); return 1; }
N(GetActorFacingAngle) { SampActor* a = AC(params[1]); if(!a) return 0; SetRefF(amx, params[2], a->p[3]); return 1; }
N(SetActorInvulnerable) { SampActor* a = AC(params[1]); if(!a) return 0; a->invul = params[2] != 0; ActorSend(-1, (int)params[1]); return 1; }
N(IsActorInvulnerable) { SampActor* a = AC(params[1]); return a && a->invul; }
N(SetActorHealth) { SampActor* a = AC(params[1]); if(!a) return 0; a->hp = PF(params, 2); return 1; }
N(GetActorHealth) { SampActor* a = AC(params[1]); if(!a) return 0; SetRefF(amx, params[2], a->hp); return 1; }
N(SetActorVirtualWorld) { SampActor* a = AC(params[1]); if(!a) return 0; a->world = (int)params[2]; return 1; }
N(GetActorVirtualWorld) { SampActor* a = AC(params[1]); return a ? a->world : 0; }
N(ApplyActorAnimation)
{   // (actorid, animlib[], animname[], Float:fDelta, loop, lockx, locky, freeze, time)
    SampActor* a = AC(params[1]); if(!a) return 0;
    char t[200]; snprintf(t, sizeof(t), "%s %s %d", Str(amx, params[2]).c_str(), Str(amx, params[3]).c_str(), NArgs(params) >= 5 ? (int)params[5] : 0);
    a->anim = t;
    OpAll(SPO_ACTOR, 2, 0, (int32_t)params[1], nullptr, t); return 1;
}
N(ClearActorAnimations) { SampActor* a = AC(params[1]); if(!a) return 0; a->anim.clear(); OpAll(SPO_ACTOR, 3, 0, (int32_t)params[1]); return 1; }
N(GetActorPoolSize) { UNUSED_AMX; int m = -1; for(auto& kv : g_Samp->actors) m = std::max(m, kv.first); return m; }

// ---- menus: shown as a list dialog (OnPlayerSelectedMenuRow / OnPlayerExitedMenu) ----------------
#define SAMP_MENU_DLG 32700
static SampMenu* MN(cell id) { return id >= 0 && id < (cell)g_Samp->menus.size() && g_Samp->menus[id].used ? &g_Samp->menus[id] : nullptr; }
N(CreateMenu)
{   // (title[], columns, Float:x, Float:y, Float:col1width, Float:col2width)
    for(size_t i = 0; i < g_Samp->menus.size(); ++i) if(!g_Samp->menus[i].used)
    {
        SampMenu& m = g_Samp->menus[i]; m = SampMenu(); m.used = true; m.title = Str(amx, params[1]); m.cols = params[2] >= 2 ? 2 : 1;
        return (cell)i;
    }
    return 0xFF;
}
N(DestroyMenu) { SampMenu* m = MN(params[1]); if(!m) return 0; m->used = false; return 1; }
N(IsValidMenu) { return MN(params[1]) != nullptr; }
N(AddMenuItem)
{   // (menuid, column, menutext[]) -> row
    SampMenu* m = MN(params[1]); if(!m) return -1; int c = params[2] ? 1 : 0;
    if(m->items[c].size() >= 12) return -1;   // SA-MP: 12 rows
    m->items[c].push_back(Str(amx, params[3]));
    return (cell)m->items[c].size() - 1;
}
N(SetMenuColumnHeader) { SampMenu* m = MN(params[1]); if(!m) return 0; m->head[params[2] ? 1 : 0] = Str(amx, params[3]); return 1; }
N(DisableMenu) { SampMenu* m = MN(params[1]); if(!m) return 0; m->off = true; return 1; }
N(DisableMenuRow) { SampMenu* m = MN(params[1]); if(!m || params[2] < 0 || params[2] >= 12) return 0; if((int)m->rowOff.size() <= params[2]) m->rowOff.resize((size_t)params[2] + 1, false); m->rowOff[(size_t)params[2]] = true; return 1; }
N(ShowMenuForPlayer)
{   // (menuid, playerid)
    SampMenu* m = MN(params[1]); if(!m || !PlOk(params[2]) || m->off) return 0;
    SampPl& p = g_SampPl[params[2]];
    std::string body; p.menuRows.clear();
    const bool heads = !m->head[0].empty() || !m->head[1].empty();
    if(heads) body = m->head[0] + (m->cols > 1 ? "\t" + m->head[1] : std::string()) + "\n";
    for(size_t r = 0; r < m->items[0].size(); ++r)
    {
        if(r < m->rowOff.size() && m->rowOff[r]) continue;
        body += m->items[0][r];
        if(m->cols > 1) body += "\t" + (r < m->items[1].size() ? m->items[1][r] : std::string());
        body += "\n";
        p.menuRows.push_back((int)r);
    }
    if(p.menuRows.empty()) return 0;
    if(!body.empty() && body.back() == '\n') body.pop_back();
    p.menu = (int)params[1];
    S_->SampDialog((int)params[2], SAMP_MENU_DLG, heads ? 5 : (m->cols > 1 ? 4 : 2), m->title, body, "Select", "Exit");
    return 1;
}
N(HideMenuForPlayer) { if(!PlOk(params[2])) return 0; SampPl& p = g_SampPl[params[2]]; if(p.menu < 0) return 0; p.menu = -1; S_->SampDialog((int)params[2], -1, 0, "", "", "", ""); return 1; }
N(GetPlayerMenu) { return PlOk(params[1]) && g_SampPl[params[1]].menu >= 0 ? g_SampPl[params[1]].menu : 0xFF; }
// called from SampOnDialog: true = it was a menu
static bool MenuOnDialog(int slot, const PktDialogResp& r)
{
    SampPl& p = g_SampPl[slot];
    if(r.dialogId != SAMP_MENU_DLG || p.menu < 0) return false;
    p.menu = -1;
    if(r.response && r.listItem >= 0 && r.listItem < (int)p.menuRows.size()) CallAll("OnPlayerSelectedMenuRow", { AI(slot), AI(p.menuRows[(size_t)r.listItem]) });
    else CallAll("OnPlayerExitedMenu", { AI(slot) });
    return true;
}

// ---- audio streams ------------------------------------------------------------------------------
N(PlayAudioStreamForPlayer)
{   // (playerid, url[], Float:posX = 0, posY = 0, posZ = 0, Float:distance = 50, usepos = 0)
    if(!PlOk(params[1])) return 0; int n = NArgs(params);
    float f[4] = { n >= 3 ? PF(params, 3) : 0, n >= 4 ? PF(params, 4) : 0, n >= 5 ? PF(params, 5) : 0, n >= 6 ? PF(params, 6) : 50.0f };
    std::string url = Str(amx, params[2]); if(url.empty()) return 0;
    S_->SampOp((int)params[1], SPO_AUDIO, 1, (uint8_t)(n >= 7 && params[7] ? 1 : 0), 0, f, url.c_str()); return 1;
}
N(StopAudioStreamForPlayer) { if(!PlOk(params[1])) return 0; S_->SampOp((int)params[1], SPO_AUDIO, 0, 0, 0); return 1; }

// ---- velocity / weapons / spectating ------------------------------------------------------------
N(SetPlayerVelocity) { if(!PlOk(params[1])) return 0; float f[4] = { PF(params, 2), PF(params, 3), PF(params, 4), 0 }; S_->SampOp((int)params[1], SPO_VELOCITY, 0, 0, 0, f); return 1; }
N(SetVehicleVelocity) { int d = DriverOf(params[1]); if(d < 0) return VH(params[1]) != nullptr; float f[4] = { PF(params, 2), PF(params, 3), PF(params, 4), 0 }; S_->SampOp(d, SPO_VELOCITY, 0, 1, 0, f); return 1; }
N(SetVehicleAngularVelocity) { UNUSED_AMX; return 1; }
N(SetPlayerArmedWeapon) { if(!PlOk(params[1])) return 0; S_->SampOp((int)params[1], SPO_WEAPON, 0, 0, params[2]); return 1; }
N(SetPlayerAmmo) { if(!PlOk(params[1])) return 0; S_->SampOp((int)params[1], SPO_WEAPON, 1, (uint8_t)params[2], params[3]); return 1; }
N(PlayerSpectatePlayer) { if(!PlOk(params[1]) || !PlOk(params[2])) return 0; S_->SampAct((int)params[1], CGA_SPEC, (int)params[2], 1); return 1; }
N(PlayerSpectateVehicle) { if(!PlOk(params[1])) return 0; int d = DriverOf(params[2]); if(d < 0) return 0; S_->SampAct((int)params[1], CGA_SPEC, d, 1); return 1; }

// ---- vehicles (the gamemode's car is the one its driver sits in) -----------------------------------
N(ChangeVehicleColor) { SampVeh* v = VH(params[1]); if(!v) return 0; v->c1 = (int)params[2]; v->c2 = (int)params[3]; int d = DriverOf(params[1]); if(d >= 0) S_->SampAct(d, CGA_CARCOLOR, v->c1, v->c2); return 1; }
N(RepairVehicle) { SampVeh* v = VH(params[1]); if(!v) return 0; v->hp = 1000; int d = DriverOf(params[1]); if(d >= 0) S_->SampAct(d, CGA_FIXCAR); return 1; }
N(SetVehicleHealth) { SampVeh* v = VH(params[1]); if(!v) return 0; v->hp = PF(params, 2); int d = DriverOf(params[1]); if(d >= 0) S_->SampAct(d, CGA_CARHP, 0, 0, (int32_t)v->hp); return 1; }
N(SetVehicleParamsEx)
{   // (vehicleid, engine, lights, alarm, doors, bonnet, boot, objective)
    SampVeh* v = VH(params[1]); if(!v) return 0;
    for(int i = 0; i < 7; ++i) v->par[i] = NArgs(params) >= i + 2 ? (int)params[i + 2] : -1;
    int d = DriverOf(params[1]);
    if(d >= 0) S_->SampOp(d, SPO_VEHPARAMS, (uint8_t)(v->par[0] + 1), (uint8_t)(v->par[3] + 1), v->par[1]);
    return 1;
}
N(GetVehicleParamsEx) { SampVeh* v = VH(params[1]); for(int i = 2; i <= NArgs(params) && i <= 8; ++i) SetRef(amx, params[i], v ? v->par[i - 2] : -1); return v != nullptr; }
N(GetPlayerVelocity) { if(!PlOk(params[1])) return 0; const SampPl& p = g_SampPl[params[1]]; SetRefF(amx, params[2], p.vel[0]); SetRefF(amx, params[3], p.vel[1]); SetRefF(amx, params[4], p.vel[2]); return 1; }
N(GetVehicleVelocity)
{
    if(!VH(params[1])) return 0; int d = -1;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(g_SampPl[i].on && g_SampPl[i].vehicle == params[1]) { d = i; break; }
    for(int i = 0; i < 3; ++i) SetRefF(amx, params[i + 2], d >= 0 ? g_SampPl[d].vel[i] : 0.0f);
    return 1;
}

// ---- name tags ----------------------------------------------------------------------------------
static void TagsSend(int slot) { float f[4] = { g_Samp->tagDist, 0, 0, 0 }; if(slot < 0) OpAll(SPO_TAGS, 0, g_Samp->tags ? 1 : 0, 0, f); else S_->SampOp(slot, SPO_TAGS, 0, g_Samp->tags ? 1 : 0, 0, f); }
N(ShowNameTags) { g_Samp->tags = params[1] != 0; TagsSend(-1); return 1; }
N(SetNameTagDrawDistance) { float d = PF(params, 1); g_Samp->tagDist = d > 1.0f ? d : 70.0f; TagsSend(-1); return 1; }
N(ShowPlayerNameTagForPlayer) { if(!PlOk(params[1]) || !PlOk(params[2])) return 0; S_->SampOp((int)params[1], SPO_TAGS, 1, params[3] ? 1 : 0, params[2]); return 1; }

// everything a player who joins late must get
static void SampMoreJoin(int slot)
{
    if(!g_Samp->tags || g_Samp->tagDist != 70.0f) TagsSend(slot);
    for(size_t i = 0; i < g_Samp->zones.size(); ++i)
    {
        const SampZone& z = g_Samp->zones[i]; if(!z.used) continue;
        if(z.allOn) ZoneSend(slot, (int)i, 1, z.allCol);
        if(z.flashCol) ZoneSend(slot, (int)i, 2, z.flashCol);
    }
    for(auto& kv : g_Samp->actors) ActorSend(slot, kv.first);
}
// GetPlayerVelocity: from the synced positions (SA-MP units: metres per 1/50 s)
static void SampMoreSync(SampPl& p, const float* pos)
{
    const uint64_t now = MP_NowMs();
    if(p.lposAt && now > p.lposAt && now - p.lposAt < 1500)
    {
        const float dt = (float)(now - p.lposAt) / 1000.0f * 50.0f;
        for(int i = 0; i < 3; ++i) p.vel[i] = (pos[i] - p.lpos[i]) / dt;
    }
    else for(float& v : p.vel) v = 0;
    memcpy(p.lpos, pos, sizeof(p.lpos)); p.lposAt = now;
}
