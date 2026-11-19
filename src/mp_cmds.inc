// 2.19 (included by mp.cpp): chat commands + cheats, scoreboard (player list), Mission sync
// request popup, chat box with /command suggestions, spawn picker buttons, carjacking players.

// ---- commands -----------------------------------------------------------------------------
struct ChatCmd { const char* cmd; const char* args; const char* help; };
static const ChatCmd kCmds[] =
{
    { "/tp",         ":place",   "teleport, e.g. /tp :grove_street" },
    { "/wallhack",   "",         "name tags through walls, any distance (on foot + in a car)" },
    { "/nofall",     "",         "no fall damage, soft landings (on foot + in a car)" },
    { "/invincible", "",         "no damage at all (on foot + in a car)" },
    { "/freeze",     "",         "freeze in place (on foot)" },
    { "/invisible",  "",         "nobody sees you (on foot)" },
    { "/fly",        "",         "fly where the camera looks, joystick = move (on foot + in a car)" },
    { "/flash",      "",         "run super fast (on foot)" },
    { "/behind",     "[id|stop]", "go behind the nearest player / player id after 3 2 1 (30 s cooldown)" },
    { "/gravity",    "[0.1-3]",  "gravity, 1 = normal (on foot + in a car)" },
    { "/pagesize",   "[0-20]",   "chat lines shown (0 = only while typing)" },
    { "/fontsize",   "[50-150]", "chat text size in %" },
    { "/chatscale",  "[50-200]", "scale the whole chat (lines + chat box) in %" },
    { "/chatwidth",  "[20-90]",  "chat width in % of the screen" },
    { "/chatfade",   "[0-300]",  "seconds before chat lines fade (0 = never)" },
    { "/chatbg",     "",         "chat background on / off" },
    { "/clearchat",  "",         "clear your chat" },
    { "/stats",      "",         "your account (user, Discord, gender, age, logins)" },
    { "/char",       "",         "same as /stats" },
    { "/help",       "",         "list the commands" },
};
#define MP_CMD_COUNT ((int)(sizeof(kCmds) / sizeof(kCmds[0])))

#define s_nMyScore s_nMyScoreFwd
static bool AuthCommand(const char* cmd);   // 3.21.6 (mp_auth.inc)
static uint64_t s_nLastInfo = 0;
static uint64_t s_nCheatLast = 0;
static uint64_t s_nCheatRefresh = 0;

static const char* OnOff(bool b) { return b ? "ON" : "OFF"; }

static int NearestPlayer(const Vec3& me)
{
    int best = -1; float bd = 1e12f;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        RemotePlayer& p = players[i];
        if(!p.active || i == myId || !p.hasSync || p.invisible) continue;
        float dx = p.sync.pos[0] - me.x, dy = p.sync.pos[1] - me.y, dz = p.sync.pos[2] - me.z;
        float d = dx * dx + dy * dy + dz * dz;
        if(d < bd) { bd = d; best = i; }
    }
    return best;
}

// ---- 2.28 Go behind: never instant ---------------------------------------------------------
// 2.27 teleported the moment "Go behind" / /behind ran, straight to the other player's last sync
// position: into their car's collision (physics threw you up = "flying"), into another interior's
// coordinates (interiors float high above the map), or to a stale spot of a paused player. Other
// players saw you warp and fly around like a cheater. Now:
//   - tapping a name in the Player list ONLY selects it (nothing else, ever)
//   - Go behind = 3 2 1 countdown on screen, Cancel any time, cancelled if the target becomes invalid
//   - time limit: the target must still be valid when the countdown ends (fresh sync, same interior,
//     not paused, not on a mission / cutscene)
//   - cooldown between uses, so it can't be spammed like a teleport hack
//   - safe landing: ground found, speed zeroed and the ped held still for a moment (no fling)
static int      s_nBehindId = -1;          // countdown target (-1 = none)
static uint64_t s_nBehindStart = 0;        // countdown start (ms)
static uint64_t s_nBehindNextOk = 0;       // cooldown end (ms)
static uint64_t s_nBehindHoldUntil = 0;    // landing hold end (ms)
static const uint64_t kBehindCountMs  = 3000;    // 3 2 1
static const uint64_t kBehindCooldown = 30000;   // time limit between two Go behinds
static const uint64_t kBehindFreshMs  = 3000;    // target's sync must be newer than this
static const uint64_t kBehindHoldMs   = 700;     // landing hold

static bool BehindTargetOk(int id, const char** why)
{
    static const char* dummy; if(!why) why = &dummy;
    *why = nullptr;
    if(id < 0 || id >= MP_MAX_PLAYERS || id == myId || !players[id].active) { *why = "player left"; return false; }
    RemotePlayer& p = players[id];
    if(!p.hasSync) { *why = "no position from them yet"; return false; }
    if(p.paused) { *why = "they are paused"; return false; }
    if(p.status & (STATUS_MISSION | STATUS_CUTSCENE)) { *why = "they are on a mission / in a cutscene"; return false; }
    if(MP_NowMs() - p.lastRecv > kBehindFreshMs) { *why = "their position is too old"; return false; }
    int theirArea = p.hasX2 ? (int)p.interior : 0;
    if(theirArea != Game::GetAreaVisible()) { *why = "they are in another interior"; return false; }
    return true;
}

static void BehindCancel(const char* why)
{
    if(s_nBehindId < 0) return;
    SysMsg(COL_RED, "Go behind cancelled%s%s.", why ? ": " : "", why ? why : "");
    s_nBehindId = -1;
}

static void BehindStart(int id)
{
    uint64_t now = MP_NowMs();
    if(s_nBehindId >= 0) { BehindCancel("tapped again"); return; }
    if(now < s_nBehindNextOk) { SysMsg(COL_RED, "Go behind again in %d s.", (int)((s_nBehindNextOk - now + 999) / 1000)); return; }
    const char* why = nullptr;
    if(!BehindTargetOk(id, &why)) { SysMsg(COL_RED, "Can't go behind %s: %s.", (id >= 0 && id < MP_MAX_PLAYERS) ? players[id].name : "?", why); return; }
    int ped = Game::LocalPed();
    if(Game::IsCharInAnyCar(ped))
    {
        int car = Game::GetCarCharIsIn(ped);
        if(Game::GetDriverOfCar(car) != ped) { SysMsg(COL_RED, "Can't go behind as a passenger."); return; }
    }
    s_nBehindId = id; s_nBehindStart = now;
    SysMsg(COL_SYSTEM, "Going behind %s in 3... (tap Cancel to stop)", players[id].name);
}

static void BehindDo(int id)
{
    int ped = Game::LocalPed();
    RemotePlayer& p = players[id];
    float h = p.sync.heading * 0.0174533f;
    bool theyDrive = (p.sync.flags & SYNC_IN_CAR) != 0;
    bool iDrive = Game::IsCharInAnyCar(ped);
    // far enough to be clear of their ped / car collision (a car is ~5 m long, a bus 12 m)
    float back = theyDrive ? 9.0f : (iDrive ? 7.0f : 2.5f);
    Vec3 at = { p.sync.pos[0] + sinf(h) * back, p.sync.pos[1] - cosf(h) * back, p.sync.pos[2] };
    Game::TeleportGround(ped, at, p.sync.heading);   // loads collision, finds the ground, car or foot
    if(iDrive) { int car = Game::GetCarCharIsIn(ped); if(car > 0) Game::SetMoveSpeed(car, true, { 0, 0, 0 }); }
    else { Game::SetMoveSpeed(ped, false, { 0, 0, 0 }); Game::FreezeChar(ped, true); }
    s_nBehindHoldUntil = MP_NowMs() + kBehindHoldMs;
    s_nBehindNextOk = MP_NowMs() + kBehindCooldown;
    SysMsg(COL_SYSTEM, "Behind %s.", p.name);
}

// every frame (from ProcessCheats)
static void TickBehind(int ped, uint64_t now)
{
    if(s_nBehindHoldUntil)
    {
        if(now >= s_nBehindHoldUntil) { s_nBehindHoldUntil = 0; Game::FreezeChar(ped, false); }
        else if(!Game::IsCharInAnyCar(ped)) Game::SetMoveSpeed(ped, false, { 0, 0, 0 });
    }
    if(s_nBehindId < 0) return;
    const char* why = nullptr;
    if(!IsConnected()) { BehindCancel("disconnected"); return; }
    if(!BehindTargetOk(s_nBehindId, &why)) { BehindCancel(why); return; }
    if(Game::IsCharDead(ped)) { BehindCancel("you died"); return; }
    if(now - s_nBehindStart >= kBehindCountMs) { int id = s_nBehindId; s_nBehindId = -1; BehindDo(id); }
}

// big 3 / 2 / 1 in the middle of the screen + Cancel
static void RenderBehindCountdown()
{
    if(s_nBehindId < 0) return;
    uint64_t age = MP_NowMs() - s_nBehindStart;
    int left = (int)((kBehindCountMs - (age < kBehindCountMs ? age : kBehindCountMs) + 999) / 1000);
    if(left < 1) left = 1;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    char num[4]; snprintf(num, sizeof(num), "%d", left);
    float frac = (float)(age % 1000) / 1000.0f;            // each digit pops in and shrinks
    float scale = 7.0f - 2.0f * frac;
    ImFont* f = ImGui::GetFont();
    float fs = ImGui::GetFontSize() * scale;
    ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0, num);
    ImVec2 c(ds.x * 0.5f, ds.y * 0.40f);
    fg->AddText(f, fs, ImVec2(c.x - ts.x * 0.5f + UI::S(4), c.y - ts.y * 0.5f + UI::S(4)), IM_COL32(0, 0, 0, 200), num);
    fg->AddText(f, fs, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), IM_COL32(255, 210, 60, (int)(255 * (1.0f - 0.4f * frac))), num);
    const float w = UI::S(560);
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f - w * 0.5f, c.y + ts.y * 0.5f + UI::S(10)));
    ImGui::SetNextWindowSize(ImVec2(w, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0.72f));
    if(ImGui::Begin("##MPBehindCd", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        ImGui::Text("Going behind %s", players[s_nBehindId].name);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.25f, 0.25f, 0.95f));
        if(ImGui::Button("Cancel##bcd", ImVec2(-1, UI::S(66)))) BehindCancel(nullptr);
        ImGui::PopStyleColor();
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

static void CmdBehind(const char* arg)
{
    if(!Game::IsReady()) return;
    while(arg && *arg == ' ') ++arg;
    if(arg && (!strcasecmp(arg, "stop") || !strcasecmp(arg, "cancel"))) { if(s_nBehindId >= 0) BehindCancel(nullptr); return; }
    int ped = Game::LocalPed();
    Vec3 me; Game::GetCharPos(ped, me);
    int id = -1;
    if(arg && *arg) { id = atoi(arg); if(id < 0 || id >= MP_MAX_PLAYERS || !players[id].active || id == myId) id = -1; }
    else id = NearestPlayer(me);
    if(id < 0) { SysMsg(COL_RED, "No player to go behind%s.", (arg && *arg) ? " (wrong id)" : ""); return; }
    BehindStart(id);
}

static void CmdTp(const char* arg)
{
    while(arg && *arg == ' ') ++arg;
    if(!arg || !*arg)
    {
        std::string all;
        for(int i = 0; i < MP_SPAWN_COUNT; ++i) { if(i) all += ", "; all += ":"; all += kSpawns[i].tag; }
        SysMsg(COL_SYSTEM, "Usage: /tp :place  -  %s", all.c_str());
        return;
    }
    if(arg[0] != ':' && isdigit((unsigned char)arg[0]))   // /tp <id> = go to that player
    {
        int id = atoi(arg);
        if(id >= 0 && id < MP_MAX_PLAYERS && players[id].active && players[id].hasSync && id != myId)
        {
            Vec3 at = { players[id].sync.pos[0] + 1.5f, players[id].sync.pos[1], players[id].sync.pos[2] };
            Game::Teleport(Game::LocalPed(), at, players[id].sync.heading);
            SysMsg(COL_SYSTEM, "Teleported to %s.", players[id].name);
        }
        else SysMsg(COL_RED, "No player with id %d.", id);
        return;
    }
    int k = FindSpawnTag(arg);
    if(k < 0) { SysMsg(COL_RED, "Unknown place \"%s\". Type /tp to list them.", arg); return; }
    const SpawnSpot& s = kSpawns[k];
    int ped = Game::LocalPed();
    Vec3 at = { s.x, s.y, s.z };
    if(Game::IsCharInAnyCar(ped) && Game::GetDriverOfCar(Game::GetCarCharIsIn(ped)) == ped)
    {   // take the car along
        int car = Game::GetCarCharIsIn(ped);
        Game::PreloadArea(at, true);
        Game::SetCarPos(car, { s.x, s.y, s.z + 1.0f }, 0.0f);
        Game::SetCarHeading(car, s.heading);
        Game::SetMoveSpeed(car, true, { 0, 0, 0 });
    }
    else
    {
        Game::Teleport(ped, at, s.heading);
        if(s.findGround) Game::PlaceChar(ped, { s.x, s.y, -100.0f });
    }
    SysMsg(COL_SYSTEM, "Teleported to %s.", s.name);
}

// true = it was a command (handled here, not sent as chat)
static bool GamesCommand(const char* cmd, const char* arg);   // 2.21 mp_games.inc
static bool LuaCmdFallback(const char* cmd, const char* arg);   // 5.01.07: after namespace ScriptMgr
static void GamesHelp();
static bool HandleCommand(const char* text)
{
    if(!text || text[0] != '/') return false;
    if(SampSrvCommand(text)) return true;   // 4.11: a SA-MP gamemode server has its own commands
    char cmd[32] = ""; const char* arg = text + 1;
    int n = 0; while(*arg && *arg != ' ' && n < 30) cmd[n++] = (char)tolower((unsigned char)*arg++);
    cmd[n] = 0;
    while(*arg == ' ') ++arg;
    if(NotifyCommand(cmd, arg)) return true;  // 5.00.07: /notify (staff)
    if(AdmCommand(cmd, arg)) return true;     // 3.54: admin / moderator commands + /report (before the gamemode blocks)
    if(GatesCommand(cmd, arg)) return true;   // 3.04.2: /gatepos, /gates
    if(TdeCommand(cmd, arg)) return true;     // 3.50: /tde Textdraw Editor
    if(DzCommand(cmd, arg)) return true;      // 3.58: Project Day Z (44+ commands, every other one blocked)
    if(CgCommand(cmd, arg)) return true;      // 4.09: CGRP systems (default server, Mega Vil)
    if(RulesCommand(cmd, arg) || SrvSettingsCommand(cmd, arg) || ProgCommand(cmd, arg) || MvCommand(cmd, arg) || DealerCommand(cmd, arg) || HkCommand(cmd, arg) || HmCommand(cmd, arg) || CoopCommand(cmd, arg) || NeedsCommand(cmd, arg) || Modes2Command(cmd, arg)) return true;   // 3.39
    if(Mw3Command(cmd, arg)) return true;   // 3.23: MW3 commands (others blocked)
    if(RpCommand(cmd, arg)) return true;    // 3.25: Mega Vil Futuristic RP commands (others blocked)
    if(AuthCommand(cmd)) return true;       // 3.21.6: /stats  /char (also on Brief Case War)
    if(BcwBlockCommand(cmd)) return true;   // 3.04: Brief Case War: no gamemodes / cheats / teleports
    if(GamesCommand(cmd, arg)) return true;
    // 2.35: the host disabled cheats
    static const char* const kCheatCmds[] = { "tp", "wallhack", "wh", "nofall", "invincible", "god", "freeze", "invisible", "invis", "fly", "flash", "behind", "gravity" };
    if(MP::CheatsBlocked())
        for(const char* c : kCheatCmds) if(!strcmp(cmd, c)) { SysMsg(COL_RED, "Cheats are disabled on this server by the host."); return true; }
    if(!strcmp(cmd, "tp"))              CmdTp(arg);
    else if(!strcmp(cmd, "wallhack") || !strcmp(cmd, "wh")) { s_Cheat.wallhack = !s_Cheat.wallhack; SysMsg(COL_SYSTEM, "Wall hack: %s", OnOff(s_Cheat.wallhack)); }
    else if(!strcmp(cmd, "nofall"))     { s_Cheat.nofall = !s_Cheat.nofall; s_Cheat.applied = false; SysMsg(COL_SYSTEM, "No fall: %s", OnOff(s_Cheat.nofall)); }
    else if(!strcmp(cmd, "invincible") || !strcmp(cmd, "god")) { s_Cheat.invincible = !s_Cheat.invincible; s_Cheat.applied = false; SysMsg(COL_SYSTEM, "Invincible: %s", OnOff(s_Cheat.invincible)); }
    else if(!strcmp(cmd, "freeze"))     { s_Cheat.freeze = !s_Cheat.freeze; SysMsg(COL_SYSTEM, "Freeze: %s", OnOff(s_Cheat.freeze)); }
    else if(!strcmp(cmd, "invisible") || !strcmp(cmd, "invis")) { s_Cheat.invisible = !s_Cheat.invisible; SysMsg(COL_SYSTEM, "Invisible: %s", OnOff(s_Cheat.invisible)); }
    else if(!strcmp(cmd, "fly"))        { s_Cheat.fly = !s_Cheat.fly; SysMsg(COL_SYSTEM, "Fly hack: %s%s", OnOff(s_Cheat.fly), s_Cheat.fly ? "  (joystick = fly where the camera looks)" : ""); }
    else if(!strcmp(cmd, "flash"))      { s_Cheat.flash = !s_Cheat.flash; SysMsg(COL_SYSTEM, "Flash: %s", OnOff(s_Cheat.flash)); }
    else if(!strcmp(cmd, "behind"))     CmdBehind(arg);
    // 2.19.6: chat layout
    else if(!strcmp(cmd, "pagesize"))  { if(*arg) settings.chatLines = atoi(arg); ClampChatSettings(); SaveConfig(); SysMsg(COL_SYSTEM, "Chat lines: %d", settings.chatLines); }
    else if(!strcmp(cmd, "chatscale"))  { if(*arg) settings.chatScale = (float)atof(arg) / 100.0f; ClampChatSettings(); SaveConfig(); SysMsg(COL_SYSTEM, "Chat scale: %d%%", (int)(settings.chatScale * 100 + 0.5f)); }
    else if(!strcmp(cmd, "fontsize"))  { if(*arg) settings.chatSize = (float)atof(arg) / 100.0f; ClampChatSettings(); SaveConfig(); SysMsg(COL_SYSTEM, "Chat text size: %d%%", (int)(settings.chatSize * 100 + 0.5f)); }
    else if(!strcmp(cmd, "chatwidth")) { if(*arg) settings.chatWidth = (float)atof(arg) / 100.0f; ClampChatSettings(); SaveConfig(); SysMsg(COL_SYSTEM, "Chat width: %d%%", (int)(settings.chatWidth * 100 + 0.5f)); }
    else if(!strcmp(cmd, "chatfade"))
    {   // 3.55: /chatfade on | off | [seconds]
        if(!strcasecmp(arg, "off")) settings.chatFadeOn = false;
        else if(!strcasecmp(arg, "on")) { settings.chatFadeOn = true; if(settings.chatFade <= 0) settings.chatFade = 25; }
        else if(*arg) { settings.chatFade = atoi(arg); settings.chatFadeOn = settings.chatFade > 0; }
        else settings.chatFadeOn = !settings.chatFadeOn;
        ClampChatSettings(); SaveConfig();
        SysMsg(COL_SYSTEM, "Chat fade: %s", (settings.chatFadeOn && settings.chatFade) ? (std::to_string(settings.chatFade) + " s").c_str() : "off (the lines stay)");
    }
    else if(!strcmp(cmd, "chatbg"))    { settings.chatBg = !settings.chatBg; SaveConfig(); SysMsg(COL_SYSTEM, "Chat background: %s", OnOff(settings.chatBg)); }
    else if(!strcmp(cmd, "clearchat")) { chat.clear(); }
    else if(!strcmp(cmd, "gravity"))
    {
        float g = *arg ? (float)atof(arg) : (s_Cheat.gravity != 1.0f ? 1.0f : 0.3f);
        if(g < 0.1f) g = 0.1f; if(g > 3.0f) g = 3.0f;
        s_Cheat.gravity = g;
        SysMsg(COL_SYSTEM, "Gravity: %.2f%s", g, g == 1.0f ? " (normal)" : "");
    }
    else if(!strcmp(cmd, "help") || !strcmp(cmd, "cmds") || !strcmp(cmd, "commands"))
    {
        for(int i = 0; i < MP_CMD_COUNT; ++i) SysMsg(COL_SYSTEM, "%s %s  -  %s", kCmds[i].cmd, kCmds[i].args, kCmds[i].help);
        GamesHelp();
        if(AdmLocalStaff()) SysMsg(0xC080FFFF, "Staff: /acmds lists the admin / moderator commands.");
        else SysMsg(COL_SYSTEM, "/report [id] [what happened]  -  tell the staff");
        SysMsg(COL_SYSTEM, "/pay [id] [amount]  -  give your money to a player");   // 4.12.3
    }
    else if(!LuaCmdFallback(cmd, arg)) SysMsg(COL_RED, "Unknown command /%s. Type /help.", cmd);   // 5.01.07
    return true;
}

// ---- cheats, every game tick --------------------------------------------------------------
static void ProcessCheats(int ped, uint64_t now)
{
    if(MP::CheatsBlocked())
    {   // 2.35: everything off (the code below takes the proofs / freeze / invisible off again)
        s_Cheat.wallhack = s_Cheat.nofall = s_Cheat.invincible = s_Cheat.freeze = s_Cheat.invisible = false;
        s_Cheat.fly = s_Cheat.flash = false; s_Cheat.gravity = 1.0f;
    }
    TickBehind(ped, now);   // 2.28: Go behind countdown / landing hold
    float dt = s_nCheatLast ? (now - s_nCheatLast) / 1000.0f : 0.0f;
    s_nCheatLast = now;
    if(dt > 0.1f) dt = 0.1f;
    if(ped <= 0) return;
    bool inCar = Game::IsCharInAnyCar(ped);
    int car = inCar ? Game::GetCarCharIsIn(ped) : 0;
    bool refresh = now - s_nCheatRefresh > 1000;   // missions / the game reset these sometimes
    if(refresh) s_nCheatRefresh = now;

    // proofs: invincible = everything, nofall = collision / fall damage
    bool want = s_Cheat.invincible || s_Cheat.nofall;
    if(want && (!s_Cheat.applied || refresh))
    {
        bool all = s_Cheat.invincible;
        Game::SetCharProofs(ped, all, all, all, true, all);
        if(car) { Game::SetCarProofs(car, all, all, all, true, all); s_Cheat.provedCar = car; }
        s_Cheat.applied = true;
    }
    else if(!want && s_Cheat.applied)
    {
        Game::SetCharProofs(ped, false, false, false, false, false);
        if(s_Cheat.provedCar) Game::SetCarProofs(s_Cheat.provedCar, false, false, false, false, false);
        s_Cheat.provedCar = 0; s_Cheat.applied = false;
    }
    if(s_Cheat.provedCar && s_Cheat.provedCar != car && Game::CarPtr(s_Cheat.provedCar))
    { Game::SetCarProofs(s_Cheat.provedCar, false, false, false, false, false); s_Cheat.provedCar = car; if(car && want) Game::SetCarProofs(car, s_Cheat.invincible, s_Cheat.invincible, s_Cheat.invincible, true, s_Cheat.invincible); }
    if(s_Cheat.invincible && refresh)
    {
        if(Game::GetCharHealth(ped) < 100) Game::SetCharHealth(ped, 100);
        if(car && Game::GetCarHealth(car) < 1000) Game::SetCarHealth(car, 1000);
    }

    // freeze (on foot)
    bool fz = s_Cheat.freeze && !inCar && !JoinFlowActive();
    if(fz && (s_Cheat.frozenPed != ped || refresh)) { Game::FreezeChar(ped, true); s_Cheat.frozenPed = ped; }
    else if(!fz && s_Cheat.frozenPed && !JoinFlowActive()) { Game::FreezeChar(s_Cheat.frozenPed, false); s_Cheat.frozenPed = 0; }

    // invisible (on foot)
    bool inv = s_Cheat.invisible && !inCar && !JoinFlowActive();
    if(inv && (s_Cheat.hiddenPed != ped || refresh)) { Game::SetCharVisible(ped, false); s_Cheat.hiddenPed = ped; }
    else if(!inv && s_Cheat.hiddenPed && !JoinFlowActive()) { Game::SetCharVisible(s_Cheat.hiddenPed, true); s_Cheat.hiddenPed = 0; }

    if(JoinFlowActive() || dt <= 0) return;
    int h = car ? car : ped; bool isCar = car != 0;
    if(isCar && Game::GetDriverOfCar(car) != ped) return;   // passengers don't steer
    Vec3 v; if(!Game::GetMoveSpeed(h, isCar, v)) return;

    if(s_Cheat.fly)
    {
        Vec3 cp, cl; int sx = 0, sy = 0; Game::GetStick(sx, sy);
        if(Game::GetCameraPos(cp, cl))
        {
            float fx = cl.x - cp.x, fy = cl.y - cp.y, fzz = cl.z - cp.z;
            float l = sqrtf(fx * fx + fy * fy + fzz * fzz); if(l < 0.001f) l = 1;
            fx /= l; fy /= l; fzz /= l;
            float rx = fy, ry = -fx;   // right of the camera (flat)
            float fwd = -sy / 128.0f, side = sx / 128.0f;
            if(isCar && fabsf(fwd) < 0.1f && fabsf(side) < 0.1f)
            {   // cars: keep the car's own speed direction, just no falling
                v.z = 0; v.x *= 0.995f; v.y *= 0.995f;
                if(sqrtf(v.x * v.x + v.y * v.y) > 2.0f) { float sp = sqrtf(v.x * v.x + v.y * v.y); v = { fx * sp, fy * sp, fzz * sp }; }
            }
            else
            {
                float sp = isCar ? 45.0f : 16.0f;
                v = { (fx * fwd + rx * side) * sp, (fy * fwd + ry * side) * sp, fzz * fwd * sp };
            }
            Game::SetMoveSpeed(h, isCar, v);
            if(!isCar && (fabsf(fwd) > 0.1f || fabsf(side) > 0.1f)) Game::SetCharHeading(ped, atan2f(-fx, fy) * 57.2958f);
        }
        return;
    }
    bool changed = false;
    if(s_Cheat.flash && !isCar)
    {
        float hs = sqrtf(v.x * v.x + v.y * v.y);
        if(hs > 1.5f && hs < 30.0f) { float k = 26.0f / hs; v.x *= k; v.y *= k; changed = true; }
    }
    if(s_Cheat.gravity != 1.0f)
    {
        v.z += (1.0f - s_Cheat.gravity) * 20.0f * dt;   // game gravity ~ 20 m/s^2 (0.008 per step)
        changed = true;
    }
    if(s_Cheat.nofall && v.z < -14.0f) { v.z = -14.0f; changed = true; }   // soft landing
    if(changed) Game::SetMoveSpeed(h, isCar, v);
}

// ---- ping + score for the scoreboard ---------------------------------------------------------
static void SendPlayerInfo(uint64_t now)
{
    if(now - s_nLastInfo < 3000) return;
    s_nLastInfo = now;
    PktPlayerInfo pi; pi.type = PKT_PLAYER_INFO; pi.id = myId; pi.ping = (uint16_t)MP::DisplayPing(); pi.score = (int16_t)s_nMyScore;
    client.Send(&pi, sizeof(pi));
    if(myId < MP_MAX_PLAYERS) { players[myId].ping = MP::DisplayPing(); players[myId].score = s_nMyScore; }
}

// name tags: hidden behind walls (unless /wallhack), checked a few times a second
static void UpdateTagOcclusion(int id, uint64_t now)
{
    RemotePlayer& p = players[id];
    if(!p.drawValid || !myDrawValid) { p.tagBlocked = false; return; }
    if(now - p.losAt < 250) return;
    p.losAt = now;
    Vec3 cp, cl;
    Vec3 from = Game::GetCameraPos(cp, cl) ? cp : myDrawPos;
    p.tagBlocked = !Game::LineOfSightClear(from, p.drawPos) || CgMasked(id) || SampSrvTagHidden(id);   // 4.09: CGRP /amask, 4.13.53: SA-MP ShowNameTags
}

// ---- carjacking players ------------------------------------------------------------------
// "enter car" at a player's car: driver door = pull them out (if the server is 2.19), passenger = ride
static uint64_t s_nJackSent = 0;
static void TryJack(int ped, int id, uint64_t now)
{
    if(!(s_nServerPerms & PERM_V219) || now - s_nJackSent < 2500) return;
    s_nJackSent = now;
    PktJack j; j.type = PKT_JACK; j.from = myId; j.victim = (uint8_t)id; j.seat = 0;
    client.Send(&j, sizeof(j));
    players[id].jackUntil = now + 6000;   // 4.13.52: read by LeaveAvatarCar (the car is kept for me)
}

// ---- Mission sync request popup ----------------------------------------------------------
static int      s_nPairPopup = -1;
static uint64_t s_nPairPopupMs = 0;
#define PAIR_POPUP_MS 30000

static void ShowPairPopup(int from) { s_nPairPopup = from; s_nPairPopupMs = MP_NowMs(); }

static void RenderPairPopup()
{
    if(s_nPairPopup < 0) return;
    int id = s_nPairPopup;
    if(id >= MP_MAX_PLAYERS || !players[id].active || !IsConnected() || IsPartner(id)) { s_nPairPopup = -1; return; }
    uint64_t age = MP_NowMs() - s_nPairPopupMs;
    if(age > PAIR_POPUP_MS) { SendPair(PAIR_DECLINE, (uint8_t)id); s_bPairAsked[id] = false; s_nPairPopup = -1; return; }
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float w = UI::S(660);
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f - w * 0.5f, ds.y * 0.17f));
    ImGui::SetNextWindowSize(ImVec2(w, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0.72f));
    if(ImGui::Begin("##MPPairPopup", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        ImGui::TextUnformatted("Mission sync request");
        ImGui::Separator();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(UI::FromRGBA(players[id].color | 0xFF)), "%s", players[id].name);
        ImGui::SameLine(0, UI::S(8));
        ImGui::TextWrapped("wants to Mission sync with you: you see each other on missions and share the mission.");
        ImGui::TextDisabled("Closes in %d s", (int)((PAIR_POPUP_MS - age) / 1000));
        float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if(ImGui::Button("Accept##pp", ImVec2(half, UI::S(70))))
        {
            SendPair(PAIR_ACCEPT, (uint8_t)id);
            s_bPairAsked[id] = false; s_nPairPopup = -1;
            SysMsg(COL_SYSTEM, "*** Mission sync with %s...", players[id].name);
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.25f, 0.25f, 0.95f));
        if(ImGui::Button("Decline##pp", ImVec2(half, UI::S(70))))
        {
            SendPair(PAIR_DECLINE, (uint8_t)id);
            s_bPairAsked[id] = false; s_nPairPopup = -1;
            SysMsg(COL_SYSTEM, "*** You declined Mission sync with %s.", players[id].name);
        }
        ImGui::PopStyleColor();
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

// ---- player list (SA-MP scoreboard look) --------------------------------------------------
static int s_nListSel = -1;
static uint64_t s_nListSelMs = 0;   // 2.28: when the row was picked (arms Go behind a moment later)
static void MissionSyncButton(int i, float w)
{
    RemotePlayer& p = players[i];
    const char* lbl; uint8_t act = 0; ImVec4 c(1, 1, 1, 0.94f);
    // 2.31: Mission sync is a group of up to 16. "Unsync" takes only THIS player out of your group.
    bool full = GroupCount() + 1 >= MP_MISSION_GROUP_MAX;
    if(IsPartner(i))             { lbl = "Unsync mission##ms"; c = ImVec4(0.85f, 0.25f, 0.25f, 0.95f); act = PAIR_END; }
    else if(s_bPairAsked[i])     { lbl = "Accept Mission sync##ms"; act = PAIR_ACCEPT; }
    else if(full)                { lbl = "Group full (16)##ms"; c = ImVec4(0.5f, 0.5f, 0.5f, 0.9f); }
    else if(i == s_nPairAskedTo) { lbl = "Asked... (waiting)##ms"; c = ImVec4(0.95f, 0.80f, 0.35f, 0.95f); }
    else                         { lbl = "Mission sync##ms"; act = PAIR_REQUEST; }
    ImGui::PushStyleColor(ImGuiCol_Button, c);
    if(ImGui::Button(lbl, ImVec2(w, UI::S(68))) && act)
    {
        SendPair(act, (uint8_t)i);
        if(act == PAIR_REQUEST) { s_nPairAskedTo = (uint8_t)i; SysMsg(COL_SYSTEM, "*** Asked %s for Mission sync. Waiting for them to accept...", p.name); }
        if(act == PAIR_ACCEPT) { s_bPairAsked[i] = false; if(s_nPairPopup == i) s_nPairPopup = -1; }
        if(act == PAIR_END)
        {
            if(GroupCount() <= 1) { memset(s_bGroup, 0, sizeof(s_bGroup)); }   // the last other one: the group ends
            else s_bGroup[i] = false;
            GroupFixFirst();
            SysMsg(COL_SYSTEM, "*** %s is no longer in your Mission sync.", p.name);
        }
    }
    ImGui::PopStyleColor();
}

static void PagePlayers(void* openPtr)
{
    bool* open = (bool*)openPtr;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, ds.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(ds.x * 0.50f, ds.y * 0.72f), ImGuiCond_FirstUseEver);
    if(!ImGui::Begin("Player list##MPPlayers", open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) { ImGui::End(); return; }
    // header: server name ........ Players: N
    {
        const char* sn = IsConnected() ? (client.GetServerName()[0] ? client.GetServerName() : s_szCurServer) : "Not connected";
        ImGui::TextUnformatted(sn);
        char pl[48]; snprintf(pl, sizeof(pl), "Players: %d", IsConnected() ? MP::OnlineCount() : 0);
        ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(pl).x);
        ImGui::TextUnformatted(pl);
    }
    ImGui::Separator();
    float bottom = UI::S(236);
    ImGui::BeginChild("##sb", ImVec2(0, -bottom), false);
    const float rowH = UI::S(54);
    float W = ImGui::GetContentRegionAvail().x;
    float cId = 0, cName = W * 0.12f, cScore = W * 0.66f, cPing = W * 0.83f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    {   // column titles
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImU32 tc = IM_COL32(200, 200, 200, 255);
        dl->AddText(ImVec2(p.x + cId + UI::S(10), p.y), tc, "id");
        dl->AddText(ImVec2(p.x + cName, p.y), tc, "name");
        dl->AddText(ImVec2(p.x + cScore, p.y), tc, "score");
        dl->AddText(ImVec2(p.x + cPing, p.y), tc, "ping");
        ImGui::Dummy(ImVec2(W, ImGui::GetTextLineHeight() + UI::S(6)));
    }
    int n = 0;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        RemotePlayer& p = players[i];
        if(!p.active) continue;
        ++n;
        ImGui::PushID(i);
        ImVec2 a = ImGui::GetCursorScreenPos();
        if(ImGui::InvisibleButton("##row", ImVec2(W, rowH))) { s_nListSel = (s_nListSel == i) ? -1 : i; s_nListSelMs = MP_NowMs(); }   // 2.28: select only
        bool sel = s_nListSel == i;
        if(sel) dl->AddRectFilled(a, ImVec2(a.x + W, a.y + rowH), IM_COL32(190, 25, 25, 215), UI::S(8));
        else if(n % 2 == 0) dl->AddRectFilled(a, ImVec2(a.x + W, a.y + rowH), IM_COL32(255, 255, 255, 12), UI::S(8));
        float ty = a.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f;
        char buf[64];
        snprintf(buf, sizeof(buf), "%d", i);
        dl->AddText(ImVec2(a.x + cId + UI::S(10), ty), IM_COL32_WHITE, buf);
        std::string nm = p.name; if(i == myId) nm += " (you)"; if(i == MP_BOT_ID && !strcmp(p.name, MP_BOT_NAME)) nm += "  (host, 24/7 bot)"; if(IsPartner(i)) nm += "  [sync]"; if(p.paused) nm += "  (paused)"; else if(p.afk) nm += "  (AFK)";   // 3.34
        if(FeatOn(PERM_SHOW_IDS)) { char ib[12]; snprintf(ib, sizeof(ib), " (%d)", i); nm.insert(strlen(p.name), ib); }   // 3.34: name (id)
        ImU32 nc = UI::FromRGBA((i == myId ? myColor : p.color) | 0xFF);
        if(Mw3OnServer())
        {   // 3.23.3: MW3 in this same list: "[EU] name" in the team colour
            const char* tag = nullptr; uint32_t tc = 0;
            if(Mw3ListTeam(i, &tag, &tc)) { nm = std::string(tag) + " " + nm; nc = UI::FromRGBA(tc); }
        }
        if(sel) nc = IM_COL32_WHITE;
        UI::NameText(dl, ImGui::GetFontSize(), ImVec2(a.x + cName, ty), nc, nm.c_str(), 1.0f, false);   // 2.37: [ADMIN] SENJI animated
        {   // 3.11: voice icon left of the score: speaker = talking now, speaker + red X = muted by you
            ImVec2 vc(a.x + cScore - UI::S(34), a.y + rowH * 0.5f);
            if(i != myId && MP::VoiceMuted(i)) UI::DrawSpeaker(dl, vc, UI::S(13), IM_COL32(230, 230, 230, 255), 0, true);
            else if(i == myId ? MP::MeTalking() : MP::VoiceSpeaking(i)) UI::DrawSpeaker(dl, vc, UI::S(13), IM_COL32_WHITE, 2, false);
        }
        snprintf(buf, sizeof(buf), "%d", Mw3OnServer() ? Mw3ScoreOf(i, i == myId ? s_nMyScore : p.score) : (i == myId ? s_nMyScore : p.score));
        dl->AddText(ImVec2(a.x + cScore, ty), IM_COL32_WHITE, buf);
        int ping = i == myId ? MP::DisplayPing() : p.ping;
        if(ping >= 0) snprintf(buf, sizeof(buf), "%d", ping); else snprintf(buf, sizeof(buf), "-");
        dl->AddText(ImVec2(a.x + cPing, ty), IM_COL32_WHITE, buf);
        ImGui::PopID();
    }
    if(!n) ImGui::TextDisabled("Not connected. Server > Connect, or join a custom server.");
    ImGui::EndChild();
    ImGui::Separator();
    // selected player: status + Mission sync / behind
    int i = s_nListSel;
    if(i >= 0 && i < MP_MAX_PLAYERS && players[i].active && i != myId)
    {
        RemotePlayer& p = players[i];
        if(p.status & STATUS_MISSION) ImGui::TextColored(kYellowTxt, "%s: on mission %s", p.name, MP::MissionTitle(p.mission));
        else if(p.status & STATUS_CUTSCENE) ImGui::TextColored(kYellowTxt, "%s: in a cutscene", p.name);
        else if(p.hasSync) ImGui::Text("%s  |  HP %d  |  armour %d", p.name, p.sync.health, p.sync.armour);
        else ImGui::Text("%s", p.name);
        float w3 = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if((s_nServerPerms & PERM_MISSION_PAIR) && IsConnected()) MissionSyncButton(i, w3);
        else { ImGui::BeginDisabled(); ImGui::Button("Mission sync is off on this server##ms0", ImVec2(w3, UI::S(68))); ImGui::EndDisabled(); }
        ImGui::SameLine();
        // 2.28: the button only works a moment after the row was picked (a tap on a name can never
        // land on it), starts the 3 2 1 countdown instead of teleporting, and shows the cooldown.
        uint64_t nowMs = MP_NowMs();
        bool armed = nowMs - s_nListSelMs > 600;
        char bl[48];
        if(s_nBehindId == i) snprintf(bl, sizeof(bl), "Cancel##bh");
        else if(nowMs < s_nBehindNextOk) snprintf(bl, sizeof(bl), "Go behind (%d s)##bh", (int)((s_nBehindNextOk - nowMs + 999) / 1000));
        else snprintf(bl, sizeof(bl), "Go behind (3 2 1)##bh");
        if(!armed) ImGui::BeginDisabled();
        if(ImGui::Button(bl, ImVec2(w3, UI::S(68))) && armed) { if(s_nBehindId == i) BehindCancel(nullptr); else BehindStart(i); }
        if(!armed) ImGui::EndDisabled();
        if((s_nServerPerms & PERM_MISSION_PAIR) && IsConnected() && client.GetServerBuild() >= 41315) CoopButton(i, -1);   // 4.13.15: Co-op story (2 players)
        {   // 3.11: mute / unmute this player's voice (only for you), red X icon while muted
            bool muted = MP::VoiceMuted(i);
            ImGui::PushStyleColor(ImGuiCol_Button, muted ? ImVec4(0.70f, 0.12f, 0.12f, 0.95f) : ImVec4(0.18f, 0.18f, 0.18f, 0.95f));
            ImVec2 bp = ImGui::GetCursorScreenPos();
            float bw = ImGui::GetContentRegionAvail().x, bh = UI::S(64);
            if(ImGui::Button(muted ? "      Unmute voice##vm" : "      Mute voice##vm", ImVec2(bw, bh))) VoiceToggleMute(i);
            ImGui::PopStyleColor();
            ImVec2 tsz = ImGui::CalcTextSize(muted ? "      Unmute voice" : "      Mute voice");
            ImVec2 ic(bp.x + (bw - tsz.x) * 0.5f + UI::S(20), bp.y + bh * 0.5f);
            UI::DrawSpeaker(ImGui::GetWindowDrawList(), ic, UI::S(15), IM_COL32_WHITE, muted ? 0 : 2, muted);
        }
    }
    else if(IsConnected())
    {
        if(AnyGroupActive())
        {   // 2.31: the whole group + leave it
            std::string g;
            for(int k = 0; k < MP_MAX_PLAYERS; ++k) if(IsPartner(k) && players[k].active) { if(!g.empty()) g += ", "; g += players[k].name; }
            ImGui::TextColored(kGreenTxt, "Mission sync (%d/%d): you, %s", GroupCount() + 1, MP_MISSION_GROUP_MAX, g.c_str());
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.25f, 0.25f, 0.95f));
            if(ImGui::Button("Leave Mission sync##lg", ImVec2(-1, UI::S(60))))
            {
                SendPair(PAIR_END, myId);
                ResetPairs();
                SysMsg(COL_SYSTEM, "*** You left Mission sync.");
            }
            ImGui::PopStyleColor();
        }
        else ImGui::TextDisabled("Tap a player, then Mission sync (up to 16 together). They get Accept / Decline.");
    }
    if(ImGui::Button("Settings##plset", ImVec2(-1, UI::S(60)))) PSettingsOpen();   // 3.35: SA-MP style player settings
    PSettingsPopup();
    ImGui::End();
}

static void PageHelp(void* openPtr)
{
    bool* open = (bool*)openPtr;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, ds.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(ds.x * 0.50f, ds.y * 0.72f), ImGuiCond_FirstUseEver);
    if(!ImGui::Begin("Help##MPHelp", open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) { ImGui::End(); return; }
    ImGui::TextUnformatted("MP 2.37 - multiplayer for GTA SA");
    ImGui::Separator();
    ImGui::TextWrapped("Top bar: the dots open Help, Server and Player list. Chat opens the chat box (type / for the commands). Drag the bar to move it.");
    ImGui::TextWrapped("Server: connect to the default server, join or add custom servers (with an optional password, max 16 characters), host your own.");
    ImGui::TextWrapped("Player list: tap a player, then Mission sync. They get a popup with Accept / Decline. Synced players see each other on every mission (also in mission scenes) and share markers.");
    ImGui::Separator();
    ImGui::TextUnformatted("Chat commands");
    for(int k = 0; k < MP_CMD_COUNT; ++k)
    {
        ImGui::TextColored(ImVec4(1, 0.85f, 0.4f, 1), "%s %s", kCmds[k].cmd, kCmds[k].args);
        ImGui::SameLine(0, UI::S(12));
        ImGui::TextWrapped("%s", kCmds[k].help);
    }
    ImGui::Separator();
    ImGui::TextUnformatted("Games (2.21)");
    ImGui::TextWrapped("/spawn [id|name] (or /v) any vehicle 400-611, /w [id|name|all] weapons, /fix, /flip, /heal, /armour");
    ImGui::TextWrapped("/race 1-8 races with checkpoints and times, /job trucking|quarry|pizza|burger|cluckin|taxi|medic|firetruck, /dm war|grove|airport|minigun|sniper, /leave, /scores");
    ImGui::TextWrapped("Teleports everybody sees: /ls /sf /lv /chiliad /lsairport /sfairport /lvairport /desert /beach /strip /dam /area51 /pizza /burger /cluckin /barber /tattoo /gym /gymsf /gymlv /clothes /prolaps /victim /ammu /spray /transfender /locolow /waa /school /flightschool /boatschool /truck /quarry /emmet /home /gf /casino");
    ImGui::TextWrapped("Open world: /unlock (all cities, shops, food, gyms, barbers, tattoo, clothes, Ammu-Nation, Pay 'n' Spray, mod garages, schools, safe houses), /girlfriends");
    ImGui::Separator();
    ImGui::TextUnformatted("Places for /tp :place");
    std::string all;
    for(int k = 0; k < MP_SPAWN_COUNT; ++k) { if(k) all += "  "; all += ":"; all += kSpawns[k].tag; }
    ImGui::TextWrapped("%s", all.c_str());
    ImGui::End();
}

// ---- 5.02: MonetLoader pill (top bar, moon icon) -> Script Manager for MonetLoader ---------------------------
// Scripts / Log / Last crashes / Shell / Settings, in MP's own window style. Every script of moonloader/,
// moonloader/mods/ and moonloader/lua_scripts/ (lua_scripts/.run/index.txt, written by lua_scripts_loader.lua)
// with an On/Off switch that is saved in moonloader/config/mp_scripts_off.txt: a script switched off stays off
// after a restart or a rejoin (MonetLoader, mods_loader.lua and lua_scripts_loader.lua all read that file).
namespace MonetEmbed { void* Handle(); }
static bool s_bMonetPage = false;
static void BubbleMonet(void*)
{
    if(FloatMenu::IsPageOpen(FloatMenu::PAGE_MONET)) FloatMenu::ClosePage(FloatMenu::PAGE_MONET);
    else FloatMenu::OpenPage(FloatMenu::PAGE_MONET);
    s_bMonetPage = FloatMenu::IsPageOpen(FloatMenu::PAGE_MONET);
}

namespace ScriptMgr
{
struct Item
{
    std::string rel;       // path inside moonloader/ ("foo.lua", "mods/x.lua", "lua_scripts/Hasslehud/HassleHUD Android.luac")
    std::string runPath;   // the file MonetLoader runs (a lua_scripts runner for mod folders / PC-path scripts)
    int group = 0;         // 0 moonloader/, 1 mods/, 2 lua_scripts/, 3 started some other way
    bool cheat = false, folder = false, system = false;
    int id = 0;            // running script id (0 = not loaded)
    bool dead = false;
    bool waiting = false;  // 5.01.06: found on disk, not (yet) in lua_scripts_loader.lua's index
    std::string name, version, authors, desc;
    std::string crash;     // 5.01.06: its last error (Last crashes), shown in the detail pane
};
static std::vector<Item> s_items;
static std::set<std::string> s_off;          // lower-case rel paths
static int s_tab = 0, s_sel = -1;
static uint64_t s_listAt = 0;
static std::string s_find;
static std::string s_selRel;
static std::string s_shellOut;
static int s_shellId = 0;
static bool s_loaderRun = false, s_loaderFile = false;   // 5.01.06: lua_scripts_loader.lua running / on disk
static bool s_hhHere = false;                            // 5.01.06: lua_scripts/Hasslehud is on the phone

// 5.01.06: HassleHUD download (raw.githubusercontent.com, no redirect) -> moonloader/lua_scripts/Hasslehud/
static const char* const kHassleUrls[] = {
    "https://raw.githubusercontent.com/ThirtyFoxMCx00E1/wagu/main/Host/Mods/Hasslehud.zip" };
static std::atomic<int> s_hhDl{ 0 };          // 0 idle, 1 downloading, 2 unpacking, 3 done, 4 failed
static std::atomic<size_t> s_hhGot{ 0 };
static std::mutex s_hhMx;
static std::string s_hhMsg;

typedef int  (*ListFn)(char*, int);
typedef void (*PathFn)(const char*);
typedef void (*IdFn)(int);
typedef void (*VoidFn)();
typedef int  (*ExecFn)(int, const char*, char*, int);
typedef const char* (*VerFn)();
struct Api { void* h = nullptr; ListFn list = nullptr, crashes = nullptr; PathFn load = nullptr; IdFn unload = nullptr, reload = nullptr;
             VoidFn reloadAll = nullptr, clearCrashes = nullptr; ExecFn exec = nullptr; VerFn ver = nullptr; };
static Api Get()
{
    Api a; a.h = MonetEmbed::Handle();
    if(!a.h) return a;
    a.list = (ListFn)dlsym(a.h, "MonetMP_ScriptsEx");
    a.crashes = (ListFn)dlsym(a.h, "MonetMP_Crashes");
    a.load = (PathFn)dlsym(a.h, "MonetMP_LoadPath");
    a.unload = (IdFn)dlsym(a.h, "MonetMP_Unload");
    a.reload = (IdFn)dlsym(a.h, "MonetMP_Reload");
    a.reloadAll = (VoidFn)dlsym(a.h, "MonetMP_ReloadAll");
    a.clearCrashes = (VoidFn)dlsym(a.h, "MonetMP_ClearCrashes");
    a.exec = (ExecFn)dlsym(a.h, "MonetMP_Exec");
    a.ver = (VerFn)dlsym(a.h, "MonetMP_Version");
    return a;
}
static std::string Root() { return std::string(MP::DataDir()) + "/moonloader"; }
static std::string Lower(std::string v) { for(auto& c : v) { if(c == '\\') c = '/'; else c = (char)tolower((unsigned char)c); } return v; }
static std::string Norm(std::string v) { for(auto& c : v) if(c == '\\') c = '/'; std::string o; for(char c : v) { if(c == '/' && !o.empty() && o.back() == '/') continue; o += c; } return o; }
static bool IsScriptFile(const char* n)
{
    size_t l = strlen(n);
    return (l > 4 && !strcasecmp(n + l - 4, ".lua")) || (l > 5 && !strcasecmp(n + l - 5, ".luac"));
}
static void Split(const std::string& line, std::vector<std::string>& f)
{
    f.clear(); size_t a = 0;
    while(true) { size_t t = line.find('\t', a); f.push_back(line.substr(a, t == std::string::npos ? std::string::npos : t - a)); if(t == std::string::npos) break; a = t + 1; }
}
static void ReadOff()
{
    s_off.clear();
    FILE* f = fopen((Root() + "/config/mp_scripts_off.txt").c_str(), "rb");
    if(!f) return;
    char ln[1024];
    while(fgets(ln, sizeof(ln), f))
    {
        std::string s = ln;
        while(!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
        size_t b = s.find_first_not_of(" \t");
        if(b == std::string::npos || s[b] == '#') continue;
        s_off.insert(Lower(s.substr(b)));
    }
    fclose(f);
}
static void WriteOff()
{
    std::string dir = Root() + "/config";
    mkdir(dir.c_str(), 0777);
    std::string p = dir + "/mp_scripts_off.txt", tmp = p + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if(!f) { SysMsg(COL_YELLOW, "Script Manager: can't write %s", p.c_str()); return; }
    fputs("# Scripts switched off in MP's Script Manager (moon icon). One path per line, inside moonloader/.\n", f);
    for(auto& s : s_off) { fputs(s.c_str(), f); fputc('\n', f); }
    fclose(f);
    rename(tmp.c_str(), p.c_str());
}
static bool IsOff(const Item& it) { return s_off.count(Lower(it.rel)) != 0; }
static bool Running(const Item& it) { return it.id && !it.dead; }

static void AddDir(const std::string& dir, const std::string& prefix, int group, bool luaOnly)
{
    DIR* d = opendir(dir.c_str());
    if(!d) return;
    while(dirent* e = readdir(d))
    {
        if(e->d_name[0] == '.' || !IsScriptFile(e->d_name)) continue;
        size_t l = strlen(e->d_name);
        if(luaOnly && !(l > 4 && !strcasecmp(e->d_name + l - 4, ".lua"))) continue;
        std::string full = dir + "/" + e->d_name;
        struct stat st; if(stat(full.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
        Item it; it.rel = prefix + e->d_name; it.runPath = full; it.group = group;
        it.system = (group == 0 && (!strcasecmp(e->d_name, "lua_scripts_loader.lua") || !strcasecmp(e->d_name, "mods_loader.lua")));
        s_items.push_back(it);
    }
    closedir(d);
}
// 5.01.06: lua_scripts/ on disk. Scripts inside a mod folder (a folder with its own lib/, like Hasslehud/) are always
// listed, so they show even before lua_scripts_loader.lua wrote its index; without an index every script is listed.
static bool IsDir(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode); }
static void ScanLua(const std::string& dir, const std::string& rel, int depth, bool pkg, bool all, std::vector<Item>& out)
{
    if(depth > 8 || out.size() > 600) return;
    DIR* d = opendir(dir.c_str());
    if(!d) return;
    std::vector<std::string> files, dirs;
    while(dirent* e = readdir(d))
    {
        if(e->d_name[0] == '.') continue;   // . .. .run .unpacked
        const std::string full = dir + "/" + e->d_name;
        struct stat st; if(stat(full.c_str(), &st) != 0) continue;
        if(S_ISDIR(st.st_mode)) dirs.push_back(e->d_name);
        else if(S_ISREG(st.st_mode) && IsScriptFile(e->d_name)) files.push_back(e->d_name);
    }
    closedir(d);
    if(!pkg && depth > 0 && IsDir(dir + "/lib")) pkg = true;
    if(pkg || all)
        for(auto& f : files)
        {
            Item it; it.rel = "lua_scripts/" + (rel.empty() ? f : rel + "/" + f); it.runPath = dir + "/" + f;
            it.group = 2; it.folder = pkg; it.waiting = true;
            out.push_back(it);
        }
    for(auto& sd : dirs)
    {
        const std::string l = Lower(sd);
        if(l == "lib" || l == "config" || l == "resource" || l == "resources" || l == "luajit" || l == "luarocks" || l == "libstd" || l == "__macosx") continue;
        ScanLua(dir + "/" + sd, rel.empty() ? sd : rel + "/" + sd, depth + 1, pkg, all, out);
    }
}
static void Refresh(const Api& a)
{
    s_items.clear();
    ReadOff();
    const std::string root = Root();
    AddDir(root, "", 0, false);
    AddDir(root + "/mods", "mods/", 1, true);
    bool haveIndex = false;
    if(FILE* f = fopen((root + "/lua_scripts/.run/index.txt").c_str(), "rb"))
    {
        haveIndex = true;
        char ln[2048]; std::vector<std::string> fl;
        while(fgets(ln, sizeof(ln), f))
        {
            std::string s = ln; while(!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
            if(s.empty() || s[0] == '#') continue;
            Split(s, fl); if(fl.size() < 2) continue;
            Item it; it.rel = "lua_scripts/" + fl[0]; it.runPath = fl[1]; it.group = 2;
            if(fl.size() > 2) { it.cheat = fl[2].find("cheat") != std::string::npos; it.folder = fl[2].find("folder") != std::string::npos; }
            s_items.push_back(it);
        }
        fclose(f);
    }
    {   // 5.01.06: what is on disk but not in the index (mod folders always; everything when there is no index)
        static std::vector<Item> s_disk; static uint64_t s_diskAt = 0; static bool s_diskAll = false;
        const uint64_t now = MP_NowMs();
        if(now - s_diskAt > 5000 || s_diskAt == 0 || s_diskAll != !haveIndex)
        {
            s_disk.clear(); s_diskAll = !haveIndex; s_diskAt = now;
            ScanLua(root + "/lua_scripts", "", 0, false, !haveIndex, s_disk);
        }
        for(const Item& d : s_disk)
        {
            bool dup = false;
            for(const Item& it : s_items) if(Lower(it.rel) == Lower(d.rel)) { dup = true; break; }
            if(!dup) s_items.push_back(d);
        }
        s_hhHere = IsDir(root + "/lua_scripts/Hasslehud") || IsDir(root + "/lua_scripts/HassleHUD");
        struct stat st; s_loaderFile = stat((root + "/lua_scripts_loader.lua").c_str(), &st) == 0;
    }
    static char buf[32768];
    buf[0] = 0;
    if(a.list) a.list(buf, sizeof(buf));
    std::vector<std::string> fl;
    for(char* line = buf; line && *line;)
    {
        char* nl = strchr(line, '\n'); if(nl) *nl = 0;
        Split(line, fl);
        if(fl.size() >= 7)
        {
            const std::string path = Norm(fl[3]);
            Item* hit = nullptr;
            for(auto& it : s_items) if(Lower(Norm(it.runPath)) == Lower(path)) { hit = &it; break; }
            if(!hit)
            {
                Item it; it.group = 3; it.runPath = path;
                const std::string r = Norm(root) + "/";
                it.rel = (Lower(path).compare(0, r.size(), Lower(r)) == 0) ? path.substr(r.size()) : path;
                s_items.push_back(it); hit = &s_items.back();
            }
            if(!hit->id || hit->dead)   // a running copy wins over a dead one
            {
                hit->id = atoi(fl[0].c_str()); hit->name = fl[1]; hit->version = fl[4]; hit->authors = fl[5];
                hit->dead = fl[6] == "dead"; hit->desc = fl.size() > 7 ? fl[7] : "";
            }
        }
        line = nl ? nl + 1 : nullptr;
    }
    s_loaderRun = false;
    for(auto& it : s_items) if(it.group == 0 && Lower(it.rel) == "lua_scripts_loader.lua" && Running(it)) s_loaderRun = true;
    if(a.crashes)
    {   // 5.01.06: "unixtime<TAB>name<TAB>path<TAB>error", newest first: the newest error of each script that is not running
        static char cb[16384]; cb[0] = 0; a.crashes(cb, sizeof(cb));
        std::vector<std::string> cf;
        for(char* line = cb; line && *line;)
        {
            char* nl = strchr(line, '\n'); if(nl) *nl = 0;
            Split(line, cf);
            if(cf.size() >= 4)
                for(auto& it : s_items)
                    if(it.crash.empty() && !Running(it) && Lower(Norm(it.runPath)) == Lower(Norm(cf[2]))) it.crash = cf[3];
            line = nl ? nl + 1 : nullptr;
        }
    }
    std::stable_sort(s_items.begin(), s_items.end(), [](const Item& x, const Item& y) {
        if(x.group != y.group) return x.group < y.group;
        return Lower(x.rel) < Lower(y.rel);
    });
    s_sel = -1;
    for(int i = 0; i < (int)s_items.size(); ++i) if(s_items[i].rel == s_selRel) s_sel = i;
}
static void SetOn(const Api& a, Item& it, bool on)
{
    const std::string key = Lower(it.rel);
    if(on) s_off.erase(key); else s_off.insert(key);
    WriteOff();
    if(it.group == 2) return;   // lua_scripts_loader.lua starts / stops it within a second (it watches the file)
    if(!on && Running(it) && a.unload) a.unload(it.id);
    if(on && !Running(it) && a.load) a.load(it.runPath.c_str());
    if(it.system && !on && a.unload)   // the lua_scripts / mods loader: its scripts stop with it
        for(auto& o : s_items) if(Running(o) && o.group == ((Lower(it.rel) == "lua_scripts_loader.lua") ? 2 : 1)) a.unload(o.id);
    s_listAt = 0;
}

static void OnFind(const char* t, void*) { s_find = t ? t : ""; }
static void OnShell(const char* t, void*)
{
    Api a = Get();
    if(!t || !*t) return;
    static char out[8192];
    int ok = a.exec ? a.exec(s_shellId, t, out, sizeof(out)) : 0;
    if(!a.exec) snprintf(out, sizeof(out), "this MonetLoader build has no shell");
    s_shellOut += std::string("> ") + t + "\n" + (ok ? "" : "error: ") + out + "\n";
    if(s_shellOut.size() > 12000) s_shellOut.erase(0, s_shellOut.size() - 12000);
}

static bool TabButton(const char* label, int idx, float w)
{
    const bool on = s_tab == idx;
    ImGui::PushStyleColor(ImGuiCol_Button, on ? ImVec4(0.26f, 0.52f, 0.95f, 0.95f) : ImVec4(0.18f, 0.18f, 0.18f, 0.95f));
    bool hit = ImGui::Button(label, ImVec2(w, UI::S(56)));
    ImGui::PopStyleColor();
    if(hit) s_tab = idx;
    return hit;
}
static void Dot(bool green, bool red)
{
    ImVec2 p = ImGui::GetCursorScreenPos(); float r = UI::S(9), h = ImGui::GetTextLineHeight();
    ImU32 c = green ? IM_COL32(90, 230, 110, 255) : red ? IM_COL32(255, 95, 85, 255) : IM_COL32(120, 120, 120, 255);
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + r, p.y + h * 0.5f), r, c);
    ImGui::Dummy(ImVec2(r * 2 + UI::S(6), h)); ImGui::SameLine(0, 0);
}
static const char* GroupName(int g) { return g == 0 ? "moonloader/" : g == 1 ? "moonloader/mods/" : g == 2 ? "moonloader/lua_scripts/" : "started by a script"; }

static void HassleDownload()
{
    int idle = s_hhDl.load();
    if(idle == 1 || idle == 2) return;
    s_hhDl = 1; s_hhGot = 0;
    std::thread([]{
        const std::string dir = Root() + "/lua_scripts"; mkdir(Root().c_str(), 0777); mkdir(dir.c_str(), 0777);
        const std::string zip = dir + "/.Hasslehud_download.zip";
        std::string body, why;
        for(const char* u : kHassleUrls)
        {
            body.clear(); s_hhGot = 0;
            if(Online::HttpsFetch(u, body, 600000, why, 64u * 1024 * 1024, &s_hhGot) && body.size() > 1024 && body[0] == 'P' && body[1] == 'K') break;
            logger->Error("5.01.06: HassleHUD download %s: %s", u, why.c_str()); body.clear();
        }
        if(body.empty()) { std::lock_guard<std::mutex> l(s_hhMx); s_hhMsg = "Download failed: " + (why.empty() ? std::string("no answer") : why); s_hhDl = 4; return; }
        FILE* f = fopen(zip.c_str(), "wb");
        if(!f || fwrite(body.data(), 1, body.size(), f) != body.size()) { if(f) fclose(f); std::lock_guard<std::mutex> l(s_hhMx); s_hhMsg = "Can't write " + zip; s_hhDl = 4; return; }
        fclose(f); body.clear(); body.shrink_to_fit();
        s_hhDl = 2;
        std::string msg;
        bool ok = MP_UnZip(zip, dir, msg);   // the zip holds Hasslehud/...
        unlink(zip.c_str());
        if(ok && !IsDir(dir + "/Hasslehud")) { ok = false; msg = "no Hasslehud folder in the zip"; }
        { std::lock_guard<std::mutex> l(s_hhMx); s_hhMsg = ok ? "HassleHUD saved in moonloader/lua_scripts/Hasslehud - it starts in a few seconds, /hasslehud opens it." : ("Unpacking failed: " + msg); }
        logger->Info("5.01.06: HassleHUD download %s: %s", ok ? "done" : "failed", msg.c_str());
        s_hhDl = ok ? 3 : 4;
    }).detach();
}
// 5.01.06: notes above the list: lua_scripts_loader.lua not running, HassleHUD not on this phone (download)
static void Notices(const Api& a)
{
    if(!s_loaderRun)
    {
        Item* ld = nullptr;
        for(auto& it : s_items) if(it.group == 0 && Lower(it.rel) == "lua_scripts_loader.lua") ld = &it;
        if(!s_loaderFile) ImGui::TextColored(kYellowTxt, "lua_scripts_loader.lua is missing in moonloader/ - restart the game once (MP puts it back), or copy it from MP's zip.");
        else if(ld && IsOff(*ld)) ImGui::TextColored(kYellowTxt, "lua_scripts_loader.lua is switched off: nothing in lua_scripts/ starts. Switch it on (Settings tab).");
        else
        {
            ImGui::TextColored(kYellowTxt, "lua_scripts_loader.lua is not running: nothing in lua_scripts/ starts.");
            if(ld && ld->crash.size()) ImGui::TextColored(kRedTxt, "Its error: %s", ld->crash.c_str());
            if(ld && a.load && ImGui::Button("Start lua_scripts_loader.lua##smstl", ImVec2(-1, UI::S(52)))) { a.load(ld->runPath.c_str()); s_listAt = 0; }
        }
    }
    const int st = s_hhDl.load();
    if(!s_hhHere || st != 0)
    {
        if(st == 1) ImGui::ProgressBar(fminf(1.0f, (float)(s_hhGot / 1048576.0) / 20.0f), ImVec2(-1, UI::S(26)), (std::string("HassleHUD ") + std::to_string((int)(s_hhGot / 1048576)) + " / 20 MB").c_str());
        else if(st == 2) ImGui::TextUnformatted("HassleHUD: unpacking...");
        else if(st == 3 || st == 4) { std::lock_guard<std::mutex> l(s_hhMx); ImGui::TextColored(st == 3 ? kGreenTxt : kRedTxt, "%s", s_hhMsg.c_str()); }
        if(!s_hhHere && st != 1 && st != 2)
        {
            ImGui::TextColored(kYellowTxt, "HassleHUD Android is not on this phone (moonloader/lua_scripts/Hasslehud).");
            if(ImGui::Button("Download HassleHUD (20 MB)##smhh", ImVec2(-1, UI::S(52)))) HassleDownload();
        }
    }
}

static void TabScripts(const Api& a)
{
    const float avail = ImGui::GetContentRegionAvail().x;
    char fl[300]; snprintf(fl, sizeof(fl), "%s##smfind", s_find.empty() ? "Find..." : s_find.c_str());
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.12f, 0.12f, 0.95f));
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.f, 0.5f));
    if(ImGui::Button(fl, ImVec2(avail * 0.46f, UI::S(56)))) VKeyboard::Open("Find script", s_find.c_str(), 64, OnFind);
    ImGui::PopStyleVar(); ImGui::PopStyleColor();
    ImGui::SameLine();
    if(!s_find.empty()) { if(ImGui::Button("X##smfx", ImVec2(UI::S(56), UI::S(56)))) s_find.clear(); ImGui::SameLine(); }
    const float bw = (ImGui::GetContentRegionAvail().x - UI::S(12)) * 0.5f;
    if(!a.reloadAll) ImGui::BeginDisabled();
    if(ImGui::Button("Reload all##smra", ImVec2(bw, UI::S(56))) && a.reloadAll) { a.reloadAll(); s_listAt = 0; SysMsg(COL_YELLOW, "MonetLoader: reloading every script"); }
    if(!a.reloadAll) ImGui::EndDisabled();
    ImGui::SameLine();
    if(ImGui::Button("Refresh list##smrl", ImVec2(bw, UI::S(56)))) s_listAt = 0;
    Notices(a);

    const float h = ImGui::GetContentRegionAvail().y;
    ImGui::BeginChild("##smlist", ImVec2(avail * 0.46f, h), true);
    const std::string q = Lower(s_find);
    int lastGroup = -1;
    for(int i = 0; i < (int)s_items.size(); ++i)
    {
        Item& it = s_items[i];
        if(!q.empty() && Lower(it.rel).find(q) == std::string::npos && Lower(it.name).find(q) == std::string::npos) continue;
        if(it.group != lastGroup) { lastGroup = it.group; ImGui::TextDisabled("%s", GroupName(it.group)); }
        ImGui::PushID(i);
        bool on = !IsOff(it);
        if(it.group != 3) { if(ImGui::Checkbox("##on", &on)) SetOn(a, it, on); ImGui::SameLine(); }
        Dot(Running(it), it.dead);
        const char* shown = strrchr(it.rel.c_str(), '/'); shown = shown ? shown + 1 : it.rel.c_str();
        if(!on) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.55f, 1));
        if(ImGui::Selectable(shown, s_sel == i)) { s_sel = i; s_selRel = it.rel; }
        if(!on) ImGui::PopStyleColor();
        ImGui::PopID();
    }
    if(s_items.empty()) ImGui::TextDisabled("No scripts");
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##smdetail", ImVec2(0, h), true);
    if(s_sel < 0 || s_sel >= (int)s_items.size()) ImGui::TextWrapped("Pick a script. The switch next to a name turns it on or off - it stays that way after a restart or a rejoin.");
    else
    {
        Item& it = s_items[s_sel];
        const bool off = IsOff(it), run = Running(it);
        const char* fn = strrchr(it.rel.c_str(), '/'); fn = fn ? fn + 1 : it.rel.c_str();
        ImGui::TextWrapped("File: %s", fn);
        ImGui::TextWrapped("Path: %s", (Root() + "/" + it.rel).c_str());
        if(it.runPath != Root() + "/" + it.rel && it.group != 3) ImGui::TextDisabled("Runs through: %s", it.runPath.c_str());
        ImGui::Text("Status:"); ImGui::SameLine();
        if(run) ImGui::TextColored(kGreenTxt, "Running");
        else if(it.dead) ImGui::TextColored(kRedTxt, "Stopped (error - see Last crashes)");
        else if(!it.crash.empty()) ImGui::TextColored(kRedTxt, "Crashed");
        else if(it.waiting && it.group == 2) ImGui::TextColored(kGreyTxt, s_loaderRun ? "Not loaded (lua_scripts_loader.lua starts it within 5 s)" : "Not loaded (lua_scripts_loader.lua is not running)");
        else ImGui::TextColored(kGreyTxt, "Not loaded");
        if(off) { ImGui::SameLine(); ImGui::TextColored(kYellowTxt, " - switched off"); }
        if(!it.name.empty() && it.name != fn) ImGui::TextWrapped("Name: %s", it.name.c_str());
        if(!it.version.empty()) ImGui::TextWrapped("Version: %s", it.version.c_str());
        if(!it.authors.empty()) ImGui::TextWrapped("Authors: %s", it.authors.c_str());
        if(!it.desc.empty()) ImGui::TextWrapped("%s", it.desc.c_str());
        if(!it.crash.empty() && !run) ImGui::TextColored(kRedTxt, "Error: %s", it.crash.c_str());
        if(it.folder) ImGui::TextDisabled("Mod folder: its lib/, config/ and resource/ stay inside its own folder.");
        if(it.cheat) ImGui::TextColored(kYellowTxt, "Cheat script: runs only where the server allows MonetLoader cheats.");
        if(it.system) ImGui::TextDisabled("%s", Lower(it.rel) == "lua_scripts_loader.lua" ? "Starts every script in lua_scripts/. Off = none of them run." : "Starts every script in mods/. Off = none of them run.");
        ImGui::Spacing();
        const float w2 = (ImGui::GetContentRegionAvail().x - UI::S(12)) * 0.5f;
        if(it.group != 3)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, off ? ImVec4(0.22f, 0.55f, 0.28f, 0.95f) : ImVec4(0.70f, 0.20f, 0.20f, 0.95f));
            if(ImGui::Button(off ? "Switch on##smon" : "Switch off##smoff", ImVec2(-1, UI::S(60)))) SetOn(a, it, off);
            ImGui::PopStyleColor();
        }
        const bool canLoad = !run && !off && a.load && !(it.waiting && it.folder);   // a mod folder runs through the loader's runner
        if(!run)
        {
            if(!canLoad) ImGui::BeginDisabled();
            if(ImGui::Button("Load##smld", ImVec2(w2, UI::S(60))) && canLoad) { a.load(it.runPath.c_str()); s_listAt = 0; }
            if(!canLoad) ImGui::EndDisabled();
        }
        else
        {
            if(!a.unload) ImGui::BeginDisabled();
            if(ImGui::Button("Unload##smul", ImVec2(w2, UI::S(60))) && a.unload) { a.unload(it.id); s_listAt = 0; }
            if(!a.unload) ImGui::EndDisabled();
        }
        ImGui::SameLine();
        const bool canRe = (run && a.reload) || (it.group == 2 && (it.dead || !it.crash.empty()) && !off && a.load && !(it.waiting && it.folder));
        if(!canRe) ImGui::BeginDisabled();
        if(ImGui::Button("Reload##smre", ImVec2(w2, UI::S(60))) && canRe)
        {
            if(run) a.reload(it.id); else a.load(it.runPath.c_str());
            s_listAt = 0;
        }
        if(!canRe) ImGui::EndDisabled();
        if(run && ImGui::Button("Open in Shell##smsh", ImVec2(-1, UI::S(56)))) { s_shellId = it.id; s_tab = 3; }
    }
    ImGui::EndChild();
}

static void TabLog()
{
    static std::string s_log; static uint64_t s_logAt = 0;
    const uint64_t now = MP_NowMs();
    if(now - s_logAt > 1500)
    {
        s_logAt = now; s_log.clear();
        if(FILE* f = fopen((Root() + "/moonloader.log").c_str(), "rb"))
        {
            fseek(f, 0, SEEK_END); long sz = ftell(f); long from = sz > 12000 ? sz - 12000 : 0;
            fseek(f, from, SEEK_SET); s_log.resize((size_t)(sz - from));
            size_t got = fread(&s_log[0], 1, s_log.size(), f); s_log.resize(got); fclose(f);
            if(from > 0) { size_t p = s_log.find('\n'); if(p != std::string::npos) s_log.erase(0, p + 1); }
        }
        else s_log = "(no moonloader.log yet)";
    }
    ImGui::BeginChild("##smlog", ImVec2(0, 0), true);
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(s_log.c_str());
    ImGui::PopTextWrapPos();
    static size_t s_lastLen = 0;
    if(s_log.size() != s_lastLen) { s_lastLen = s_log.size(); ImGui::SetScrollHereY(1.0f); }
    ImGui::EndChild();
}

static void TabCrashes(const Api& a)
{
    if(!a.crashes) { ImGui::TextWrapped("This MonetLoader build keeps no crash list (a separate libmonetloader.so in the mods folder?). See the Log tab."); return; }
    static char buf[16384];
    buf[0] = 0; int n = a.crashes(buf, sizeof(buf));
    if(a.clearCrashes && n > 0 && ImGui::Button("Clear##smcc", ImVec2(UI::S(200), UI::S(56)))) { a.clearCrashes(); return; }
    ImGui::BeginChild("##smcr", ImVec2(0, 0), true);
    if(n == 0) ImGui::TextDisabled("No script crashed since the game started.");
    std::vector<std::string> fl;
    for(char* line = buf; line && *line;)
    {
        char* nl = strchr(line, '\n'); if(nl) *nl = 0;
        Split(line, fl);
        if(fl.size() >= 4)
        {
            time_t t = (time_t)atoll(fl[0].c_str()); struct tm lt; localtime_r(&t, &lt);
            ImGui::TextColored(kRedTxt, "%02d:%02d:%02d  %s", lt.tm_hour, lt.tm_min, lt.tm_sec, fl[1].c_str());
            ImGui::TextDisabled("%s", fl[2].c_str());
            ImGui::PushTextWrapPos(0); ImGui::TextUnformatted(fl[3].empty() ? "(no error text)" : fl[3].c_str()); ImGui::PopTextWrapPos();
            ImGui::Separator();
        }
        line = nl ? nl + 1 : nullptr;
    }
    ImGui::EndChild();
}

static void TabShell(const Api& a)
{
    if(!a.exec) { ImGui::TextWrapped("This MonetLoader build has no shell."); return; }
    const Item* target = nullptr;
    for(auto& it : s_items) if(Running(it) && it.id == s_shellId) target = &it;
    if(!target) for(auto& it : s_items) if(Running(it)) { target = &it; s_shellId = it.id; break; }
    ImGui::TextWrapped("Runs Lua inside: %s", target ? (target->name.empty() ? target->rel.c_str() : target->name.c_str()) : "(no running script)");
    if(ImGui::Button("Other script##smsn", ImVec2(UI::S(240), UI::S(56))))
    {
        int first = 0, next = 0; bool pass = false;
        for(auto& it : s_items) if(Running(it)) { if(!first) first = it.id; if(pass && !next) next = it.id; if(it.id == s_shellId) pass = true; }
        s_shellId = next ? next : first;
    }
    ImGui::SameLine();
    if(!target) ImGui::BeginDisabled();
    if(ImGui::Button("Type Lua...##smsi", ImVec2(UI::S(260), UI::S(56))) && target) VKeyboard::Open("Lua (return x shows x)", "", 400, OnShell);
    if(!target) ImGui::EndDisabled();
    ImGui::SameLine();
    if(ImGui::Button("Clear##smsc", ImVec2(UI::S(160), UI::S(56)))) s_shellOut.clear();
    ImGui::BeginChild("##smshell", ImVec2(0, 0), true);
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(s_shellOut.empty() ? "Example: return thisScript().name   or   sampAddChatMessage('hi', -1)" : s_shellOut.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
}

static void TabSettings(const Api& a)
{
    ImGui::TextWrapped("Switches are saved in moonloader/config/mp_scripts_off.txt and stay after a restart or a rejoin.");
    ImGui::Spacing();
    for(int pass = 0; pass < 2; ++pass)
    {
        const char* file = pass == 0 ? "lua_scripts_loader.lua" : "mods_loader.lua";
        Item* it = nullptr;
        for(auto& o : s_items) if(o.group == 0 && Lower(o.rel) == file) it = &o;
        if(!it) continue;
        bool on = !IsOff(*it);
        if(ImGui::Checkbox(pass == 0 ? "Lua scripts folder (moonloader/lua_scripts/)##smls" : "Mods folder (moonloader/mods/)##smmd", &on)) SetOn(a, *it, on);
    }
    int offN = (int)s_off.size();
    ImGui::Spacing();
    ImGui::Text("%d script(s) switched off", offN);
    if(offN > 0 && ImGui::Button("Switch every script on##smallon", ImVec2(-1, UI::S(60))))
    {
        std::vector<Item*> was;
        for(auto& it : s_items) if(IsOff(it)) was.push_back(&it);
        s_off.clear(); WriteOff();
        for(Item* it : was) if(it->group != 2 && !Running(*it) && a.load) a.load(it->runPath.c_str());
        s_listAt = 0;
    }
    ImGui::Spacing();
    ImGui::TextDisabled("lua_scripts/: drop .lua / .luac / .zip there. A folder with its own lib/ (like lua_scripts/Hasslehud) keeps all its files inside itself.");
}
} // namespace ScriptMgr

// 5.01.07: "/hasslehud" said "Unknown command": MonetLoader had no such command, so MP's own handler got it. A command
// of a lua_scripts/ mod is now checked here before that message: why the mod is not running (folder missing -> it is
// downloaded, switched off -> switched on, lua_scripts_loader.lua missing / off / stopped -> started, the mod crashed
// -> its error + it is loaded again, not started yet -> it is loaded now), then the command is sent again to MonetLoader
// for up to 20 s, so it opens by itself as soon as the mod registered it.
// 5.01.08: how a runner loads the script: loadfile, then a plain read + load (a BOM / #! line / a file loadfile can't
// open by name), and the real reason when both fail
static const char* const kLuaRunnerLoad = R"LUA(local __mp_f, __mp_e = loadfile(__mp_src)
if not __mp_f then
  local h, he = io.open(__mp_src, 'rb')
  if h then
    local d = h:read('*a'); h:close()
    if d then
      d = d:gsub('^\239\187\191', ''):gsub('^#[^\n]*', '')
      local f2, e2 = load(d, '@' .. __mp_src)
      if f2 then __mp_f = f2 else __mp_e = e2 or __mp_e end
    end
  else
    __mp_e = tostring(__mp_e) .. ' (' .. tostring(he) .. ')'
  end
end
if not __mp_f then error('[LuaScripts] cannot load ' .. __mp_src .. ': ' .. tostring(__mp_e), 0) end
__mp_f(...)
)LUA";
struct LuaModCmd { const char* cmd; const char* folder; const char* script; const char* title; };
static const LuaModCmd kLuaModCmds[] = {
    { "hasslehud", "Hasslehud", "HassleHUD Android.luac", "HassleHUD" } };
static std::string s_pendCmd; static uint64_t s_pendUntil = 0, s_pendNext = 0; static bool s_pendQuiet = false;
static const LuaModCmd* s_pendMod = nullptr; static std::string s_pendCrash;   // 5.01.08: the mod being waited for / its error seen
static bool s_pendDirect = false;                                              // 5.01.08: MP started it itself already
// 5.01.08: the mod's script wherever the loader found it (lua_scripts/Hasslehud/, lua_scripts/HassleHUD-1/,
// a Hasslehud.zip unpacked into lua_scripts/.unpacked/...), not only the one exact path
static ScriptMgr::Item* LuaModItem(const LuaModCmd* m)
{
    using namespace ScriptMgr;
    const std::string tail = "/" + Lower(m->script), exact = Lower(std::string("lua_scripts/") + m->folder + "/" + m->script);
    Item* best = nullptr;
    for(auto& i : s_items)
    {
        const std::string l = Lower(i.rel);
        if(l != exact && !(l.size() > tail.size() && l.compare(l.size() - tail.size(), tail.size(), tail) == 0)) continue;
        if(!best || (best->waiting && !i.waiting) || (!Running(*best) && Running(i)) || (l == exact && Lower(best->rel) != exact && Running(i) == Running(*best))) best = &i;
    }
    return best;
}
// 5.01.08: the mod's script on disk when the loader has not indexed it yet (moonloader/lua_scripts/<folder>/<script>,
// any case, or one folder deeper: lua_scripts/Hasslehud/Hasslehud/...)
static std::string LuaModFind(const LuaModCmd* m)
{
    using namespace ScriptMgr;
    const std::string base = Root() + "/lua_scripts";
    std::vector<std::string> dirs;
    if(DIR* d = opendir(base.c_str()))
    {
        while(dirent* e = readdir(d)) if(e->d_name[0] != '.' && Lower(e->d_name).find(Lower(m->folder)) != std::string::npos) dirs.push_back(base + "/" + e->d_name);
        closedir(d);
    }
    for(size_t k = 0; k < dirs.size() && k < 16; ++k)
    {
        if(DIR* d = opendir(dirs[k].c_str()))
        {
            std::string hit;
            while(dirent* e = readdir(d))
            {
                if(e->d_name[0] == '.') continue;
                const std::string full = dirs[k] + "/" + e->d_name;
                if(Lower(e->d_name) == Lower(m->script)) hit = full;
                else if(IsDir(full) && dirs.size() < 16) dirs.push_back(full);
            }
            closedir(d);
            if(!hit.empty()) return hit;
        }
    }
    return std::string();
}
static std::string LuaQ(const std::string& v)   // Lua %q
{
    std::string o = "\"";
    for(char c : v) { if(c == '"' || c == '\\') { o += '\\'; o += c; } else if(c == '\n') o += "\\n"; else if(c == '\r') o += "\\r"; else if(c == 0) o += "\\0"; else o += c; }
    return o + "\"";
}
// 5.01.08: the same runner lua_scripts_loader.lua writes (runnerFor) - MP writes it and starts the mod itself, so
// /hasslehud no longer waits on the loader's next scan
static std::string LuaModRunner(const std::string& src)
{
    using namespace ScriptMgr;
    const std::string luaDir = Norm(Root() + "/lua_scripts");
    std::string rel = Norm(src);
    if(rel.compare(0, luaDir.size() + 1, luaDir + "/") == 0) rel = rel.substr(luaDir.size() + 1);
    std::string id = rel;
    { size_t sl = id.rfind('/'), dt = id.rfind('.'); if(dt != std::string::npos && (sl == std::string::npos || dt > sl)) id.resize(dt); }
    for(auto& c : id) if(!isalnum((unsigned char)c) && c != '-' && c != '_' && c != '.') c = '_';
    mkdir((luaDir + "/.run").c_str(), 0777);
    const std::string path = luaDir + "/.run/" + id + ".lua";
    std::string pkg = Norm(src); { size_t sl = pkg.rfind('/'); if(sl != std::string::npos) pkg.resize(sl); }
    std::string body = "-- made by lua_scripts_loader.lua (MP): runs lua_scripts/" + rel + ". Do not edit.\nlocal __mp_src = " + LuaQ(Norm(src)) + "\n";
    body += "local __mp_pkg = " + LuaQ(pkg) + "\n";
    body += R"LUA(do
  local function n(p) return (p:gsub('\\', '/'):gsub('/+', '/')):lower() end
  local real = getWorkingDirectory
  local rootLib = n(real() .. '/lib/')
  -- moonloader/lib/ code (mimgui, MoonMonet...) keeps moonloader/, everything else (the mod and its own lib/)
  -- sees the mod folder, so its config/, resource/, fonts and .so files are found there
  getWorkingDirectory = function()
    local i = debug.getinfo(2, 'S')
    local s = i and i.source or ''
    if s:sub(1, 1) == '@' and n(s:sub(2)):sub(1, #rootLib) == rootLib then return real() end
    return __mp_pkg
  end
  local L = __mp_pkg .. '/lib/'
  package.path = package.path .. ';' .. L .. '?.lua;' .. L .. '?/init.lua;' .. L .. '?.luac;' .. L .. '?/init.luac;' ..
    __mp_pkg .. '/?.lua;' .. __mp_pkg .. '/?/init.lua'
  package.cpath = package.cpath .. ';' .. L .. '?.so'
  -- a .so of the mod that can't load here (HassleHUD's libGIF.so is 32-bit only: on a 64-bit game) no longer kills
  -- the whole mod: its functions return 0 and the rest of the mod runs
  local okf, ffi = pcall(require, 'ffi')
  if okf and type(ffi) == 'table' and ffi.load then
    local load, pkgn = ffi.load, n(__mp_pkg .. '/')
    ffi.load = function(name, ...)
      local ok, lib = pcall(load, name, ...)
      if ok then return lib end
      if type(name) == 'string' and n(name):sub(1, #pkgn) == pkgn then
        print('[LuaScripts] ' .. name .. ' does not load on this game (' .. tostring(lib) .. ') - its functions return 0')
        return setmetatable({}, { __index = function() return function() return 0 end end })
      end
      error(lib, 2)
    end
  end
end
)LUA";
    body += kLuaRunnerLoad;
    std::string cur;
    if(FILE* f = fopen(path.c_str(), "rb")) { char b[4096]; size_t r; while((r = fread(b, 1, sizeof b, f)) > 0) cur.append(b, r); fclose(f); }
    if(cur != body) if(FILE* f = fopen(path.c_str(), "wb")) { fwrite(body.data(), 1, body.size(), f); fclose(f); }
    logger->Info("5.01.08: %s runner written: %s", rel.c_str(), path.c_str());
    return path;
}
// 5.01.08: the last moonloader.log line about the mod (its real error when it stops before Last crashes has one)
static std::string LuaModLogLine(const LuaModCmd* m)
{
    std::string t;
    if(FILE* f = fopen((ScriptMgr::Root() + "/moonloader.log").c_str(), "rb"))
    {
        fseek(f, 0, SEEK_END); long sz = ftell(f); long from = sz > 24576 ? sz - 24576 : 0; fseek(f, from, SEEK_SET);
        t.resize((size_t)(sz - from)); t.resize(fread(&t[0], 1, t.size(), f)); fclose(f);
    }
    const std::string key = ScriptMgr::Lower(m->title);
    std::string last; size_t p = 0;
    while(p < t.size())
    {
        size_t e = t.find('\n', p); if(e == std::string::npos) e = t.size();
        std::string ln = t.substr(p, e - p); p = e + 1;
        const std::string l = ScriptMgr::Lower(ln);
        if(l.find(key) != std::string::npos && (l.find("error") != std::string::npos || l.find("cannot") != std::string::npos || l.find("fail") != std::string::npos
            || l.find("died") != std::string::npos || l.find("terminat") != std::string::npos || l.find("attempt") != std::string::npos)) last = ln;
    }
    while(!last.empty() && (last.back() == '\r' || last.back() == ' ')) last.pop_back();
    if(last.size() > 200) last = last.substr(0, 200) + "...";
    return last;
}
static unsigned s_pendSent = 0, s_pendSeen = 0;   // retry number sent / last one MP got back (= not taken by a script)
static bool LuaCmdFallback(const char* cmd, const char* arg)
{
    using namespace ScriptMgr;
    const LuaModCmd* m = nullptr;
    for(const auto& c : kLuaModCmds) if(!strcmp(cmd, c.cmd)) m = &c;
    if(!m) return false;
    if(s_pendQuiet) { s_pendSeen = s_pendSent; return true; }   // a retry below: no script has the command yet
    const uint64_t now = MP_NowMs();
    auto Retry = [&](const char* why) {
        s_pendCmd = std::string("/") + cmd + (arg && *arg ? std::string(" ") + arg : std::string());
        s_pendUntil = now + 20000; s_pendNext = now + 1500; s_pendSent = s_pendSeen = 0;
        SysMsg(COL_YELLOW, "%s: %s - /%s opens it as soon as it is running.", m->title, why, cmd);
    };
    Api a = Get();
    if(!a.h)
    {
        SysMsg(COL_RED, "%s needs MonetLoader for MP: it is not running ([MP] MonetLoader = 0 in TYFMC.MP.ini, or GTA Vice City - MonetLoader and %s are San Andreas only).", m->title, m->title);
        return true;
    }
    Refresh(a); s_listAt = 0;
    const std::string root = Root();
    const std::string dir = root + "/lua_scripts/" + m->folder;
    Item* it = LuaModItem(m); Item* ld = nullptr;
    for(auto& i : s_items) if(i.group == 0 && Lower(i.rel) == "lua_scripts_loader.lua") ld = &i;
    const std::string onDisk = it ? std::string() : LuaModFind(m);
    if(!it && onDisk.empty())
    {
        if(s_hhDl.load() == 1 || s_hhDl.load() == 2) { Retry("still downloading (moon icon shows the progress)"); return true; }
        if(IsDir(dir) || IsDir(root + "/lua_scripts/" + Lower(m->folder)))
            SysMsg(COL_YELLOW, "%s: moonloader/lua_scripts/%s has no \"%s\" - downloading it again.", m->title, m->folder, m->script);
        HassleDownload();
        Retry("not on this phone (moonloader/lua_scripts/Hasslehud) - downloading it now (20 MB)");
        s_pendUntil = now + 240000;
        return true;
    }
    s_pendMod = m; s_pendCrash.clear(); s_pendDirect = false;
    if(it && IsOff(*it)) { SetOn(a, *it, true); Retry("was switched off in the Script Manager - switched on"); return true; }
    if(it && Running(*it)) { Retry("is still starting (it waits for the game)"); return true; }
    if(it && it->crash.size())
    {
        SysMsg(COL_RED, "%s stopped with an error: %s", m->title, it->crash.c_str());
        s_pendCrash = it->crash;
    }
    // 5.01.08: MP starts the mod itself - it no longer waits on lua_scripts_loader.lua's next scan ("starts it within 5 s")
    const std::string src = it ? Norm(root + "/" + it->rel) : onDisk;
    std::string run = (it && !it->waiting && !it->runPath.empty()) ? it->runPath : std::string();
    if(run.empty()) run = LuaModRunner(src);
    if(!a.load) { SysMsg(COL_RED, "%s: this MonetLoader can't start scripts from MP - update MP.", m->title); return true; }
    if(ld && !s_loaderRun && !IsOff(*ld)) a.load(ld->runPath.c_str());   // the loader too, for the other mods
    a.load(run.c_str());
    s_pendDirect = true;
    logger->Info("5.01.08: /%s -> started %s", cmd, run.c_str());
    Retry(s_pendCrash.empty() ? "starting it now" : "loading it again");
    return true;
}
static void LuaCmdTick(uint64_t now)
{
    if(s_pendCmd.empty() || now < s_pendNext) return;
    if(now > s_pendUntil)
    {
        std::string why = s_pendMod ? LuaModLogLine(s_pendMod) : std::string();
        if(why.empty()) why = s_pendCrash;
        if(why.empty()) SysMsg(COL_RED, "%s did not open: the mod is running but has not added the command yet - moon icon > Log shows what it is doing.", s_pendCmd.c_str());
        else SysMsg(COL_RED, "%s did not start: %s", s_pendCmd.c_str(), why.c_str());
        s_pendCmd.clear(); s_pendMod = nullptr; return;
    }
    s_pendNext = now + 1000;
    if(ScriptMgr::s_hhDl.load() == 1 || ScriptMgr::s_hhDl.load() == 2) return;   // still downloading
    if(s_pendSent && s_pendSeen != s_pendSent) { s_pendCmd.clear(); s_pendMod = nullptr; return; }   // the last retry was not handed back: the mod ran it
    // 5.01.08: the mod stopped while MP waited -> its real error now, not after 20 s
    if(s_pendMod && s_pendDirect && s_pendSent >= 2)
    {
        ScriptMgr::Api a = ScriptMgr::Get();
        if(a.h)
        {
            ScriptMgr::Refresh(a); ScriptMgr::s_listAt = 0;
            ScriptMgr::Item* it = LuaModItem(s_pendMod);
            if(it && !ScriptMgr::Running(*it) && it->crash.size() && it->crash != s_pendCrash)
            {
                SysMsg(COL_RED, "%s stopped: %s", s_pendMod->title, it->crash.c_str());
                SysMsg(COL_YELLOW, "Moon icon > Last crashes has the full error.");
                s_pendCmd.clear(); s_pendMod = nullptr; return;
            }
        }
    }
    const std::string c = s_pendCmd;
    ++s_pendSent;
    s_pendQuiet = true;
    if(!LuaOutChat(c.c_str())) s_pendSeen = s_pendSent;   // no MonetLoader bridge: keep trying until the time is up
    s_pendQuiet = false;
}

static void PageMonet(void* openPtr)
{
    using namespace ScriptMgr;
    bool* open = (bool*)openPtr;
    s_bMonetPage = *open;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, ds.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(ds.x * 0.62f, ds.y * 0.78f), ImGuiCond_FirstUseEver);
    Api a = Get();
    char title[96];
    snprintf(title, sizeof(title), "Script Manager for MonetLoader %s##MPMonet", a.ver ? a.ver() : "");
    if(!ImGui::Begin(title, open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) { ImGui::End(); return; }
    if(!a.h) ImGui::TextColored(kRedTxt, "MonetLoader is not running ([MP] MonetLoader = 0 in TYFMC.MP.ini?) - switches still save.");
    else if(!a.list) ImGui::TextColored(kYellowTxt, "Older MonetLoader build: load / unload / shell need MonetLoader for MP 5.02 (switches still save).");
    const uint64_t now = MP_NowMs();
    if(now - s_listAt > 1000) { Refresh(a); s_listAt = now; }
    const float tw = (ImGui::GetContentRegionAvail().x - UI::S(12) * 4) / 5.f;
    TabButton("Scripts##smt0", 0, tw); ImGui::SameLine();
    TabButton("Log##smt1", 1, tw); ImGui::SameLine();
    TabButton("Last crashes##smt2", 2, tw); ImGui::SameLine();
    TabButton("Shell##smt3", 3, tw); ImGui::SameLine();
    TabButton("Settings##smt4", 4, tw);
    switch(s_tab)
    {
        case 0: TabScripts(a); break;
        case 1: TabLog(); break;
        case 2: TabCrashes(a); break;
        case 3: TabShell(a); break;
        default: TabSettings(a); break;
    }
    ImGui::End();
    s_bMonetPage = *open;
}

// ---- chat box (only while the Chat pill is on) ------------------------------------------------
static bool s_bChatBox = false;
bool  MP::IsChatBoxOpen() { return s_bChatBox; }
float MP::chatDrawnH = 0;
void MP::ClampChatSettings()
{
    if(settings.chatLines < 0) settings.chatLines = 0;
    if(settings.chatLines > 20) settings.chatLines = 20;
    if(!(settings.chatSize >= 0.5f && settings.chatSize <= 1.5f)) settings.chatSize = 0.80f;
    if(!(settings.chatScale >= 0.5f && settings.chatScale <= 2.0f)) settings.chatScale = 1.00f;
    if(!(settings.chatWidth >= 0.2f && settings.chatWidth <= 0.9f)) settings.chatWidth = 0.42f;
    if(settings.chatFade < 0) settings.chatFade = 0;
    if(settings.chatFade > 300) settings.chatFade = 300;
}
static void OnChatBoxSubmit(const char* t, void*) { MP::SendChat(t); }
static void OpenChatKeyboard(const char* initial)
{
    VKeyboard::Open("Say", initial ? initial : "", MP_CHAT_LEN, OnChatBoxSubmit);
    VKeyboard::SetChat(true);
}

struct Suggest { std::string text, hint; };
static void GamesSuggest(const char* t, std::vector<Suggest>& out);   // 2.21
static bool NotifySuggest(const char* t, std::vector<Suggest>& out);   // 5.00.07 (mp_notify.inc)
static void BuildSuggestions(const char* t, std::vector<Suggest>& out)
{
    out.clear();
    if(!t || t[0] != '/') return;
    if(NotifySuggest(t, out)) return;   // 5.00.07: /notify embed: logo: message: timer: start: end:
    const char* sp = strchr(t, ' ');
    if(!sp)
    {   // command names
        size_t n = strlen(t);
        for(int k = 0; k < MP_CMD_COUNT; ++k)
            if(!strncasecmp(kCmds[k].cmd, t, n)) out.push_back({ std::string(kCmds[k].cmd) + (kCmds[k].args[0] ? " " : ""), kCmds[k].args[0] ? std::string(kCmds[k].args) + "  -  " + kCmds[k].help : kCmds[k].help });
        GamesSuggest(t, out);
        return;
    }
    if(!strncasecmp(t, "/tp ", 4))
    {   // places
        const char* a = t + 4; while(*a == ' ') ++a;
        if(*a == ':') ++a;
        size_t n = strlen(a);
        for(int k = 0; k < MP_SPAWN_COUNT && out.size() < 12; ++k)
            if(!strncasecmp(kSpawns[k].tag, a, n)) out.push_back({ std::string("/tp :") + kSpawns[k].tag, kSpawns[k].name });
    }
    else if(!strncasecmp(t, "/spawn ", 7) || !strncasecmp(t, "/v ", 3) || !strncasecmp(t, "/w ", 3) || !strncasecmp(t, "/race ", 6) || !strncasecmp(t, "/dm ", 4) || !strncasecmp(t, "/job ", 5) || !strncasecmp(t, "/goto ", 6))
        GamesSuggest(t, out);
    else if(!strncasecmp(t, "/behind ", 8))
    {
        for(int i = 0; i < MP_MAX_PLAYERS && out.size() < 12; ++i)
            if(players[i].active && i != myId) out.push_back({ "/behind " + std::to_string(i), players[i].name });
    }
    else if(!strncasecmp(t, "/gravity ", 9))
    {
        static const char* g[] = { "0.1", "0.3", "0.5", "1", "2", "3" };
        for(auto v : g) out.push_back({ std::string("/gravity ") + v, !strcmp(v, "1") ? "normal" : "" });
    }
}

static void RenderChatBox()
{
    if(!s_bChatBox) return;
    if(FloatMenu::IsModal()) return;   // 2.26: hidden while the launcher is open
    if(!VKeyboard::IsOpen()) VKeyboard::SetChat(false);
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float cs = settings.chatScale;   // 3.04.5: chat scale
    auto CS = [cs](float v) { return UI::S(v) * cs; };
    const float fs = CS(27.0f);
    float x = ds.x * settings.chatX + CS(4);
    float y = ds.y * settings.chatY + chatDrawnH + CS(10);   // 2.19.6: under the lines really drawn
    if(settings.chatBoxX >= 0 && settings.chatBoxY >= 0)          // 2.22: own position (Server > Display)
    {
        x = ds.x * ImClamp(settings.chatBoxX, 0.0f, 0.9f) + CS(4);
        y = ds.y * ImClamp(settings.chatBoxY, 0.0f, 0.9f);
    }
    float w = ImMin(ImMax(ds.x * settings.chatWidth, ds.x * 0.34f), ds.x - x - CS(10));
    float kb = VKeyboard::Height();
    const bool typing = VKeyboard::IsOpen() && VKeyboard::IsChat();
    const char* txt = VKeyboard::IsChat() ? VKeyboard::GetText() : "";
    // 2.22: box hidden (no background + no label) and not typing -> nothing to draw or tap
    if(!typing && !settings.chatBoxBg && !settings.chatBoxLabel) return;
    // 2.22: "Tap the chat box to type" off -> the box never takes a touch until the Chat pill opens the keyboard
    const bool tappable = typing || settings.chatBoxTap;
    std::vector<Suggest> sug; BuildSuggestions(txt, sug);
    const float boxH = CS(60), rowH = CS(50);
    int maxRows = (int)((ds.y - kb - y - boxH - CS(20)) / rowH);
    if(maxRows < 0) maxRows = 0; if(maxRows > 6) maxRows = 6;
    int shown = (int)sug.size() < maxRows ? (int)sug.size() : maxRows;
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, boxH + (shown ? shown * rowH + CS(12) : 0) + CS(8)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(CS(4), CS(4)));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGuiWindowFlags wf = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar;
    if(!tappable) wf |= ImGuiWindowFlags_NoInputs;
    if(ImGui::Begin("##MPChatBox", nullptr, wf))
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        float bw = ImGui::GetContentRegionAvail().x;
        if(tappable)
        {
            // 2.22: a real tap only - pressed AND released on the box, short, not a swipe / joystick drag
            static double s_PressT = 0;
            bool hit = ImGui::InvisibleButton("##cbin", ImVec2(bw, boxH));
            if(ImGui::IsItemActivated()) s_PressT = ImGui::GetTime();
            const ImGuiIO& io = ImGui::GetIO();
            bool shortTap = ImGui::GetTime() - s_PressT < 0.8 && io.MouseDragMaxDistanceSqr[0] < CS(28) * CS(28);
            if(hit && shortTap && !VKeyboard::IsOpen()) OpenChatKeyboard("");
        }
        else ImGui::Dummy(ImVec2(bw, boxH));
        if(settings.chatBoxBg || typing)
        {
            dl->AddRectFilled(p, ImVec2(p.x + bw, p.y + boxH), IM_COL32(0, 0, 0, 150), CS(10));
            dl->AddRect(p, ImVec2(p.x + bw, p.y + boxH), IM_COL32(255, 255, 255, 235), CS(10), 0, CS(2.5f));
        }
        // ➤ arrow
        float ax = p.x + CS(16), ay = p.y + boxH * 0.5f, as = CS(11);
        dl->AddTriangleFilled(ImVec2(ax, ay - as), ImVec2(ax + as * 1.5f, ay), ImVec2(ax, ay + as), IM_COL32_WHITE);
        std::string shownTxt = UI::EmojiToPUA(txt);
        bool caret = VKeyboard::IsChat() && ((int)(ImGui::GetTime() * 2.0)) % 2 == 0;
        if(caret) shownTxt += "_";
        ImU32 tc = (txt[0] || caret) ? IM_COL32_WHITE : IM_COL32(200, 200, 200, 200);
        dl->PushClipRect(ImVec2(p.x + CS(44), p.y), ImVec2(p.x + bw - CS(8), p.y + boxH), true);
        const char* hint = settings.chatBoxTap ? "Tap to chat  -  type / for commands" : "Chat button to type  -  / for commands";
        if(txt[0] || caret || settings.chatBoxLabel)   // 2.22: label can be hidden
            dl->AddText(UI::Font(), fs * 1.05f, ImVec2(p.x + CS(46), p.y + (boxH - fs * 1.05f) * 0.5f), tc,
                        (txt[0] || caret) ? shownTxt.c_str() : hint);
        dl->PopClipRect();
        if(shown)
        {
            ImGui::Dummy(ImVec2(1, CS(4)));
            ImVec2 q = ImGui::GetCursorScreenPos();
            dl->AddRectFilled(q, ImVec2(q.x + bw, q.y + shown * rowH + CS(8)), IM_COL32(0, 0, 0, 165), CS(10));
            ImGui::Dummy(ImVec2(1, CS(4)));
            for(int k = 0; k < shown; ++k)
            {
                ImGui::PushID(k);
                ImVec2 r = ImGui::GetCursorScreenPos();
                bool hit = ImGui::InvisibleButton("##sg", ImVec2(bw, rowH));
                bool top = k == 0;   // best match: SA-MP style red highlight
                if(top) dl->AddRectFilled(ImVec2(r.x + CS(4), r.y), ImVec2(r.x + bw - CS(4), r.y + rowH), IM_COL32(190, 25, 25, 220), CS(8));
                float ty = r.y + (rowH - fs) * 0.5f;
                dl->AddText(UI::Font(), fs, ImVec2(r.x + CS(16), ty), IM_COL32_WHITE, sug[k].text.c_str());
                if(!sug[k].hint.empty())
                {
                    float tw = UI::Font()->CalcTextSizeA(fs, FLT_MAX, 0, sug[k].text.c_str()).x;
                    dl->PushClipRect(r, ImVec2(r.x + bw - CS(8), r.y + rowH), true);
                    dl->AddText(UI::Font(), fs * 0.85f, ImVec2(r.x + CS(28) + tw, ty + fs * 0.08f), top ? IM_COL32(255, 220, 220, 230) : IM_COL32(190, 190, 190, 220), sug[k].hint.c_str());
                    dl->PopClipRect();
                }
                if(hit)
                {
                    std::string t = sug[k].text;
                    if(!strcmp(t.c_str(), "/tp ")) t = "/tp :";
                    if(VKeyboard::IsChat()) VKeyboard::SetText(t.c_str()); else OpenChatKeyboard(t.c_str());
                }
                ImGui::PopID();
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

// ---- spawn picker: ( <  Spawn  > ) ----------------------------------------------------------
static void RenderSpawnPicker()
{
    if(!MP::SpawnPickerShown()) return;
    float a = MP::SpawnPickerAlpha();
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float bh = UI::S(84), side = UI::S(110), mid = UI::S(300), gap = UI::S(18);
    float total = side * 2 + mid + gap * 2;
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f - total * 0.5f - UI::S(10), ds.y * 0.80f - UI::S(60)));
    ImGui::SetNextWindowSize(ImVec2(total + UI::S(20), bh + UI::S(80)));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, a);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(UI::S(10), UI::S(6)));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    if(ImGui::Begin("##MPSpawn", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar))
    {
        int sel = MP::SpawnSelected();
        char name[96]; snprintf(name, sizeof(name), "%s   (%d/%d)", MP::SpawnSpotName(sel), sel + 1, MP::SpawnSpotCount());
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 wp = ImGui::GetCursorScreenPos();
        float fs = UI::S(32);
        ImVec2 ts = UI::Font()->CalcTextSizeA(fs, FLT_MAX, 0, name);
        float cx = wp.x + total * 0.5f;
        dl->AddRectFilled(ImVec2(cx - ts.x * 0.5f - UI::S(18), wp.y), ImVec2(cx + ts.x * 0.5f + UI::S(18), wp.y + fs + UI::S(16)), IM_COL32(40, 45, 52, 220));   // 4.04: square CS:GO box
        dl->AddRect(ImVec2(cx - ts.x * 0.5f - UI::S(18), wp.y), ImVec2(cx + ts.x * 0.5f + UI::S(18), wp.y + fs + UI::S(16)), IM_COL32(255, 255, 255, 30));
        UI::OutlinedText(dl, fs, ImVec2(cx - ts.x * 0.5f, wp.y + UI::S(8)), IM_COL32_WHITE, name);
        ImGui::Dummy(ImVec2(total, fs + UI::S(26)));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.204f, 0.227f, 0.259f, 0.92f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.27f, 0.30f, 0.34f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.49f, 0.70f, 0.89f, 0.45f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);   // 4.04: square
        ImGui::SetWindowFontScale(1.3f);
        if(ImGui::Button("<##sp", ImVec2(side, bh))) MP::SpawnStep(-1);
        ImGui::SameLine(0, gap);
        ImGui::PopStyleColor(3);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.298f, 0.627f, 0.188f, 1.0f));   // 4.04: green like CONNECT
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.34f, 0.69f, 0.22f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.17f, 0.43f, 0.10f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
        if(ImGui::Button("SPAWN##sp", ImVec2(mid, bh))) MP::SpawnConfirm();
        ImGui::PopStyleColor(4);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.204f, 0.227f, 0.259f, 0.92f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.27f, 0.30f, 0.34f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.49f, 0.70f, 0.89f, 0.45f));
        ImGui::SameLine(0, gap);
        if(ImGui::Button(">##sp", ImVec2(side, bh))) MP::SpawnStep(1);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

// ---- 2.19.1: Passenger button (the mobile game has no "enter as passenger" key) ---------------
static int      s_nPassCand = 0, s_nPassSeat = -1;
static uint64_t s_nPassScan = 0;
static void UpdatePassengerCandidate(int ped, uint64_t now)
{
    if(now - s_nPassScan < 250) return;
    s_nPassScan = now;
    s_nPassCand = 0; s_nPassSeat = -1;
    if(ped <= 0 || JoinFlowActive() || Game::IsCharInAnyCar(ped) || Game::IsCharGettingInToACar(ped) || !Game::IsReady()) return;
    if(Game::GetCharHealth(ped) <= 0) return;
    Vec3 me; Game::GetCharPos(ped, me);
    int car = 0; float best = 6.5f * 6.5f;
    // 2.19.4: only a car another player is DRIVING (they took it first). Empty cars, parked
    // cars and traffic: no icon (just get in normally as the driver).
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        RemotePlayer& p = players[i];
        if(i == myId || !p.active || !p.car || !p.ped || !Game::PedPtr(p.ped)) continue;
        if(!(p.sync.flags & SYNC_IN_CAR) || (p.sync.flags & SYNC_DEAD)) continue;
        if(Game::GetDriverOfCar(p.car) != p.ped) continue;   // they must be in the driver seat
        Vec3 cp; if(!Game::GetCarPos(p.car, cp)) continue;
        float dx = cp.x - me.x, dy = cp.y - me.y, dz = cp.z - me.z, d = dx * dx + dy * dy + dz * dz;
        if(d < best) { best = d; car = p.car; }
    }
    if(!car) return;
    Vec3 cp; if(!Game::GetCarPos(car, cp) || fabsf(cp.z - me.z) > 3.0f) return;
    int seat = Game::FreePassengerSeat(car, 0);
    if(seat < 0) return;
    s_nPassCand = car; s_nPassSeat = seat;
}

// car seen from above with its right door open: white with a dark outline (readable on any
// background), no button background
static void DrawCarOpenIcon(ImDrawList* dl, ImVec2 o, float S, float alpha)
{
    auto P = [&](float x, float y) { return ImVec2(o.x + x * S, o.y + y * S); };
    const ImU32 edge = IM_COL32(0, 0, 0, (int)(200 * alpha));
    const ImU32 white = IM_COL32(255, 255, 255, (int)(255 * alpha));
    const ImU32 dark = IM_COL32(25, 25, 25, (int)(255 * alpha));
    const float hx = 0.70f, hy = 0.36f, L = 0.26f, ang = 0.96f;   // door hinge, length, 55 deg
    const float ex = hx + L * sinf(ang), ey = hy + L * cosf(ang);
    for(int pass = 0; pass < 2; ++pass)
    {
        float g = pass == 0 ? 0.022f : 0.0f;
        ImU32 c = pass == 0 ? edge : white;
        dl->AddLine(P(hx, hy), P(ex, ey), c, (0.07f + 2 * g) * S);
        dl->AddCircleFilled(P(ex, ey), (0.035f + g) * S, c, 16);
        dl->AddRectFilled(P(0.30f - g, 0.06f - g), P(0.70f + g, 0.94f + g), c, (0.13f + g) * S);
    }
    dl->AddQuadFilled(P(0.38f, 0.26f), P(0.62f, 0.26f), P(0.66f, 0.36f), P(0.34f, 0.36f), dark);   // windscreen
    dl->AddQuadFilled(P(0.36f, 0.72f), P(0.64f, 0.72f), P(0.61f, 0.80f), P(0.39f, 0.80f), dark);   // rear window
    dl->AddQuadFilled(P(0.66f, 0.40f), P(0.66f, 0.70f), P(0.63f, 0.68f), P(0.63f, 0.42f), dark);   // side windows
    dl->AddQuadFilled(P(0.34f, 0.40f), P(0.37f, 0.42f), P(0.37f, 0.68f), P(0.34f, 0.70f), dark);
}

static void RenderPassengerButton()
{
    if(!s_nPassCand || JoinFlowActive() || VKeyboard::IsOpen() || Game::IsPaused()) return;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float sz = UI::S(120);
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.79f - sz * 0.5f, ds.y * 0.38f));
    ImGui::SetNextWindowSize(ImVec2(sz, sz));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    if(ImGui::Begin("##MPPassenger", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground))
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        bool tap = ImGui::InvisibleButton("##pb", ImVec2(sz, sz));   // "get in as passenger"
        bool held = ImGui::IsItemActive();
        float k = held ? 0.90f : 1.0f;   // a little smaller while pressed
        float s2 = sz * k;
        DrawCarOpenIcon(ImGui::GetWindowDrawList(), ImVec2(p.x + (sz - s2) * 0.5f, p.y + (sz - s2) * 0.5f), s2, held ? 0.75f : 0.95f);
        if(tap)
        {
            int ped = Game::LocalPed();
            int seat = Game::FreePassengerSeat(s_nPassCand, 0);
            if(Game::GetDriverOfCar(s_nPassCand) <= 0) seat = -2;   // the driver just got out
            if(seat >= 0)
            {
                Game::ClearTasksNow(ped);
                Game::TaskEnterCarAsPassenger(ped, s_nPassCand, seat);
                s_nPassBtnCar = s_nPassCand; s_nPassBtnT = MP_NowMs();
            }
            else if(seat == -1) SysMsg(COL_YELLOW, "That car is full.");
            s_nPassCand = 0;
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

// all 2.19 windows, every frame (FloatMenu floating window)
static void RenderGames();
static void RenderAuth();   // 3.21.6 (mp_auth.inc)
static void RenderStats();
static void RenderSamp();   // 3.01 (mp_world.inc): 3D labels, textdraws, class selection, dialogs
bool MP::DzConnScreenShown() { return DzConnScreen(); }
static void Render219(void*)
{
    // 4.02: the Project Day Z connection screen (cabin) is a join screen too: no top bar, no chat box
    const bool joinScreen = JoinScreenUp() || DzConnScreen();   // 4.13.2: not while still connecting (no black yet)
    FloatMenu::SetHidden(joinScreen);   // join screen: no top bar
    RenderPairPopup();
    RenderBehindCountdown();   // 2.28
    if(!joinScreen || (MP::SpawnPickerShown() && !DzConnScreen())) RenderChatBox();
    RenderSpawnPicker();
    RenderPassengerButton();   // 2.19.1
    RenderGames();             // 2.21: score bar + race / DM / job HUD
    RenderSamp();              // 3.01
    RenderBcw();               // 3.04: Brief Case War
    RenderMw3();               // 3.23: Modern Warfare 3
    RenderRp();                // 3.25: Mega Vil Futuristic
    Game::HideCornerText(DzHideCorner());   // 3.60: DynamicSeason text off the GPS
    DzModelsTick();            // 3.61: MTA DayZ models from GitHub
    SocialFrame();             // 3.63: MP social (friends, chats, party, communities, inbox)
    RenderDz();                // 3.58: Project Day Z Legacy
    RenderLg();                // 4.13.4: Rocket League (was San Andreas Online League)
    CgRender();                // 4.09: CGRP labels, ENTER / EXIT, /editor
    HkRender(); HmRender(); CoopRender(); NeedsRender(); Modes2Render(); DealerRender(); NotifyRender();   // 3.39 / 3.55
    HudRenderExtras();   // 4.07: HUD Player + Ammo Info
    RenderHotelPc();           // 3.36: hotel room computer (every server)
    RenderSafeZones();         // 3.21: hospital safe zones
    RenderMarkers();           // 3.21: red checkpoint beams
    RenderGates();             // 3.04.2: gate fade (over everything)
    RenderStats();             // 3.21.6: /stats  /char
    TdeRender();               // 3.50: /tde Textdraw Editor
    RenderAuth();              // 3.21.6: login screens (over the join view)
    RenderLoadWindow();        // 3.40: Please Wait, Loading Server Data...
}

bool MP::JoinScreenActive() { return JoinScreenUp(); }
