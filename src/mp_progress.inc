// 3.39.1: saved progress on the default server (and custom servers): where I was, money, weapons +
// ammo, health, armour, hunger and energy. The phone sends it every 15 s and when leaving; the server
// keeps it for my account (logged in) and gives it back after the next login - also after a game
// restart, another phone or a server restart. Brief Case War / MW3 / Mega Vil keep their own saves.

static struct { bool got = false, applied = false; PktProgress p = {}; uint64_t gotAt = 0, sendAt = 0, settledAt = 0;
                // 5.01.12: the weapons come back one by one while their models stream in (no LOAD_ALL_MODELS_NOW)
                uint8_t wep[13] = {}; uint16_t ammo[13] = {}; int nWep = 0, given = 0; uint64_t wepUntil = 0; } s_Prog;

static void ProgReset() { s_Prog.got = false; s_Prog.applied = false; s_Prog.gotAt = 0; s_Prog.sendAt = 0; s_Prog.settledAt = 0; s_Prog.nWep = 0; s_Prog.given = 0; s_Prog.wepUntil = 0; }
static bool ProgActive()
{
    if(client.GetState() != NET_CONNECTED || client.GetServerBuild() < 33910) return false;
    if(BcwOnServer() || Mw3OnServer() || RpOnServer() || DzOnServer() || LgOnServer()) return false;   // 5.01.12: + Rocket League (its lobby owns your place)
    if(s_bAuthReq && s_Auth.state != AuthClient::DONE) return false;
    return true;
}
static void ProgOnPacket(const PktProgress& m)
{
    if(m.op == 0) { s_Prog.p = m; s_Prog.got = true; s_Prog.applied = false; s_Prog.gotAt = MP_NowMs(); }
    else if(m.op == 2) { s_Prog.got = false; s_Prog.applied = true; s_Prog.sendAt = MP_NowMs(); }   // nothing saved yet: start saving
}
static void ProgSendNow()
{
    if(!ProgActive() || !s_Prog.applied) return;
    const int ped = Game::LocalPed(); if(ped <= 0 || Game::IsCharDead(ped)) return;
    PktProgress g; memset(&g, 0, sizeof(g));
    g.type = PKT_PROGRESS; g.op = 1; g.ver = 1;
    Vec3 p; if(!Game::GetCharPos(ped, p)) return;
    g.x = p.x; g.y = p.y; g.z = p.z; g.heading = Game::GetCharHeading(ped);
    g.money = Game::GetMoney();
    g.interior = (uint8_t)std::max(0, std::min(255, Game::GetAreaVisible()));
    g.health = (uint8_t)std::max(1, std::min(200, Game::GetCharHealth(ped)));
    g.armour = (uint8_t)std::max(0, std::min(100, Game::GetCharArmour(ped)));
    g.hunger = (uint8_t)s_Need.hunger; g.energy = (uint8_t)s_Need.energy;
    int n = 0;
    for(int slot = 1; slot <= 13 && n < 13; ++slot)
    {
        int w, a; if(!Game::GetWeaponInSlot(ped, slot, w, a)) continue;
        g.wep[n] = (uint8_t)w; g.ammo[n] = (uint16_t)std::max(1, std::min(a, 65000)); ++n;
    }
    client.Send(&g, sizeof(g));
    s_Prog.sendAt = MP_NowMs();
}
static void ProgProcess(int ped, uint64_t now)
{
    if(!ProgActive()) { if(client.GetState() != NET_CONNECTED) ProgReset(); return; }
    if(ped <= 0) return;
    if(s_Prog.got && !s_Prog.applied)
    {
        const bool settled = Game::IsReady() && AuthJoinSettled() && s_nJoinPhase == JP_NONE && !Game::IsCharDead(ped) && !AuthScreenUp();
        if(!settled) { s_Prog.settledAt = 0; return; }
        if(!s_Prog.settledAt) { s_Prog.settledAt = now; return; }
        if(now - s_Prog.settledAt < 1200) return;
        const PktProgress& g = s_Prog.p;
        // 4.12.9: you just picked a place in the < Spawn > picker: you stay there. This was the 2nd teleport that
        // came with the rules window (login done + join screen gone = saved place applied), and its LOAD_SCENE
        // of your last place (often Grove Street) froze the game. Money, weapons, health, hunger still come back.
        const bool keepPlace = s_bJoinSpawnPicked || s_SpawnSettle.until != 0;
        if(!keepPlace)
        {
            if(Game::IsCharInAnyCar(ped)) Game::WarpOutOfCar(ped, { g.x, g.y, g.z });
            if(g.interior) { Game::SetInterior(ped, g.interior); Game::Teleport(ped, { g.x, g.y, g.z - 1.0f }, g.heading); }
            else { Game::SetInterior(ped, 0); Game::TeleportGround(ped, { g.x, g.y, g.z }, g.heading); }
        }
        const int cur = Game::GetMoney(); if(g.money != cur) Game::AddMoney(g.money - cur);
        Game::SetCharHealth(ped, std::max(10, (int)g.health));
        { int ar = Game::GetCharArmour(ped); if(g.armour > ar) Game::AddArmour(ped, g.armour - ar); }
        Game::RemoveAllWeapons(ped);
        int nw = 0;
        s_Prog.nWep = 0; s_Prog.given = 0; s_Prog.wepUntil = now + 12000;
        for(int k = 0; k < 13; ++k) if(g.wep[k]) { s_Prog.wep[s_Prog.nWep] = g.wep[k]; s_Prog.ammo[s_Prog.nWep] = g.ammo[k]; ++s_Prog.nWep; ++nw; }
        Game::SetCurrentWeapon(ped, 0);
        if(g.hunger || g.energy) { s_Need.hunger = g.hunger; s_Need.energy = g.energy; }
        s_Prog.applied = true; s_Prog.sendAt = now;
        SysMsg(COL_GREEN, "Your progress is back: %s$%d, %d weapon%s, health and hunger.", keepPlace ? "" : "your place, ", g.money, nw, nw == 1 ? "" : "s");
        return;
    }
    if(s_Prog.nWep > 0)
    {   // 5.01.12: the saved weapons, as their models arrive (12 s max, then the rest is dropped)
        int left = 0;
        for(int k = 0; k < s_Prog.nWep; ++k)
        {
            if(!s_Prog.wep[k]) continue;
            const int r = Game::GiveWeaponAmmoLight(ped, s_Prog.wep[k], s_Prog.ammo[k]);
            if(r != 0) { if(r > 0) ++s_Prog.given; s_Prog.wep[k] = 0; }
            else ++left;
        }
        if(!left || now > s_Prog.wepUntil) { s_Prog.nWep = 0; Game::SetCurrentWeapon(ped, 0); }
        return;   // no save while the weapons are still coming back (it would save fewer)
    }
    if(s_Prog.applied && now - s_Prog.sendAt > 15000) ProgSendNow();
}
static bool ProgCommand(const char* cmd, const char*)
{
    if(strcmp(cmd, "save") && strcmp(cmd, "saveprogress")) return false;
    if(!ProgActive()) { SysMsg(COL_RED, s_bAuthReq ? "Log in first - your progress is saved to your account." : "This server can't save progress (it needs MP 3.39.1)."); return true; }
    if(!s_Prog.applied) { SysMsg(COL_YELLOW, "Your saved progress is still loading..."); return true; }
    s_Prog.sendAt = 0; ProgSendNow(); SysMsg(COL_GREEN, "Progress saved (it also saves by itself every 15 s and when you leave).");
    return true;
}
