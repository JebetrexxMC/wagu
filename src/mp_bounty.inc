// 5.02.31: Bounty Hunter (Blood Deathmatch) - always on on the default server (Broker MQQ), Add server > Gamemode >
// Bounty Hunter on a custom server (GM2_BOUNTY in PktPerms.pad). Phone side only: every phone sees every PKT_DEATH
// (the server broadcasts them to everyone), so every phone keeps the same kill streaks / bounties.
//   * a bounty on every head: $100 + $100 for every kill in a row. Kill = the bounty (green line next to the radar,
//     "victim - Deagle +130.00"), killed = -$100 (blue line)
//   * over the head: the bounty in green, kills x deaths, and "Death by the Fog!" in red after your hit
//   * multi kills (pricedown banner): kills within 45 s + 20 ticks of each other = one chain:
//     Killing Spree! / Double Kill! / Triple Kill! / Quadruple Kill! / Mania! / Savage! / Rampage! ...
//     a kill after a longer gap (3 min ...) starts a new chain, it is never a double kill
//   * bottom left (under the radar): K / D / Ratio / Duel / Level in red
//   * bottom centre: "victim is now part of the fog" / "victim rests in pieces"
//   * duels (/duel id, /accept, /decline): ">> The duel between A and B has begun!" in the Broker MQQ reds / browns
//   * first time here: ">> name joined the Blood Deathmatch for the first time. Welcome!" for everyone
//   * a bloody welcome logo + your own welcome message (Settings: Add server > Gamemode > Bounty Hunter) + a kill sound
// Duels / first-time greetings ride on PktEvent (kind DM, idx MP_EV_QUEUE, state LOBBY, pad 0xB7): the server relays
// them to everyone as they are, older phones drop them (SampOnEvent: idx QUEUE = a queue packet).

#define MP_BOUNTY_BASE     100
#define MP_BOUNTY_STEP     100
#define MP_BOUNTY_LOSS     100
#define MP_BOUNTY_CHAIN_MS (45000 + 20 * 50)   // 45 s + 20 game ticks (50 ms)
#define MP_BOUNTY_MAGIC    0xB7

const char* const kBountyModeDesc =
    "Bounty Hunter (Blood Deathmatch): a bounty on every head (+$100 per kill in a row), kill rewards, multi kills "
    "(Killing Spree, Double Kill, Triple Kill ... within 45 s), duels (/duel id), K / D / Ratio / Duel / Level under the radar. "
    "Always on on Broker MQQ.";

struct BountyLine { std::string text; uint32_t col; uint64_t at; };
struct BountyState
{
    int  streak[MP_MAX_PLAYERS] = {}, kills[MP_MAX_PLAYERS] = {}, deaths[MP_MAX_PLAYERS] = {};
    uint64_t hitAt[MP_MAX_PLAYERS] = {};      // my last hit on them ("Death by the Fog!")
    // me
    int  myKills = 0, myDeaths = 0, myDuels = 0, myXp = 0;
    int  chain = 0; uint64_t chainAt = 0;      // multi kills
    std::string banner; uint64_t bannerAt = 0;
    std::string fog; uint64_t fogAt = 0;       // bottom centre
    std::vector<BountyLine> lines;             // next to the radar
    int  duelWith = -1, duelInvFrom = -1; uint64_t duelInvAt = 0;
    int  duelA = -1, duelB = -1;               // a duel I can see (anyone's)
    bool statsLoaded = false; char statsFor[64] = "";
    bool welcomePending = false; uint64_t welcomeAt = 0;
};
static BountyState s_Bh;

static bool BountyActive() { return client.GetState() == NET_CONNECTED && ModeOn2(GM2_BOUNTY); }
static int  BountyOf(int id) { return (id >= 0 && id < MP_MAX_PLAYERS) ? MP_BOUNTY_BASE + MP_BOUNTY_STEP * s_Bh.streak[id] : MP_BOUNTY_BASE; }
static const char* BountyName(int id)
{
    if(id == myId) return (myId < MP_MAX_PLAYERS && players[myId].active) ? players[myId].name : settings.name;
    if(id >= 0 && id < MP_MAX_PLAYERS && players[id].active) return players[id].name;
    return "?";
}
static std::string BountyWeapon(int w)
{
    if(w == MP_RAM_WEAPON || w == 49 || w == 50) return "Vehicle";
    if(w == 0) return "Fist";
    if(w == 51) return "Explosion";
    if(w == 53) return "Drowned";
    if(w == 54) return "Fall";
    for(int i = 0; i < MP_WEAP_COUNT; ++i)
        if(kWeaps[i].id == w)
        {
            std::string n = kWeaps[i].name; if(!n.empty()) n[0] = (char)toupper((unsigned char)n[0]);
            if(n == "Ak47") n = "AK-47"; else if(n == "M4") n = "M4"; else if(n == "Mp5") n = "MP5"; else if(n == "Rpg") n = "RPG"; else if(n == "Tec9") n = "Tec-9";
            return n;
        }
    return "Weapon";
}
static int BountyLevel() { return 1 + s_Bh.myXp / 100; }

// ---- my stats, per server, in files/TYFMC/MP/bounty_stats.txt ------------------------------------------------
static void BountyStatsKey(char* out, size_t n) { snprintf(out, n, "%s@%s", settings.name, s_szCurServer[0] ? s_szCurServer : "server"); }
static void BountyStatsSave()
{
    if(!s_Bh.statsLoaded || !s_szDataDir[0]) return;
    char path[512]; snprintf(path, sizeof(path), "%s/bounty_stats.txt", s_szDataDir);
    std::vector<std::string> keep;
    if(FILE* f = fopen(path, "r")) { char ln[256]; while(fgets(ln, sizeof(ln), f)) { char k[128] = ""; if(sscanf(ln, "%127[^|]|", k) == 1 && strcmp(k, s_Bh.statsFor)) keep.push_back(ln); } fclose(f); }
    FILE* f = fopen(path, "w"); if(!f) return;
    for(auto& l : keep) fputs(l.c_str(), f);
    fprintf(f, "%s|%d %d %d %d\n", s_Bh.statsFor, s_Bh.myKills, s_Bh.myDeaths, s_Bh.myDuels, s_Bh.myXp);
    fclose(f);
}
static void BountyStatsLoad()
{
    char key[64]; BountyStatsKey(key, sizeof(key));
    if(s_Bh.statsLoaded && !strcmp(key, s_Bh.statsFor)) return;
    s_Bh.statsLoaded = true; MP_CopyStr(s_Bh.statsFor, key, sizeof(s_Bh.statsFor));
    s_Bh.myKills = s_Bh.myDeaths = s_Bh.myDuels = s_Bh.myXp = 0;
    char path[512]; snprintf(path, sizeof(path), "%s/bounty_stats.txt", s_szDataDir);
    if(FILE* f = fopen(path, "r"))
    {
        char ln[256];
        while(fgets(ln, sizeof(ln), f))
        {
            char k[128] = ""; int a = 0, b = 0, c = 0, d = 0;
            if(sscanf(ln, "%127[^|]|%d %d %d %d", k, &a, &b, &c, &d) == 5 && !strcmp(k, key)) { s_Bh.myKills = a; s_Bh.myDeaths = b; s_Bh.myDuels = c; s_Bh.myXp = d; }
        }
        fclose(f);
    }
}

// ---- events (duels, first-time greeting) -------------------------------------------------------------------------
static void BountySend(int op, int target)
{
    if(client.GetState() != NET_CONNECTED) return;
    PktEvent e; memset(&e, 0, sizeof(e));
    e.type = PKT_EVENT; e.id = myId; e.kind = EVK_DM; e.idx = MP_EV_QUEUE; e.state = EVS_LOBBY;
    e.cp = (uint8_t)op; e.slot = (uint8_t)(target < 0 ? MP_INVALID_ID : target); e.pad = MP_BOUNTY_MAGIC;
    client.Send(&e, sizeof(e));
}
static void BountyDuelBegun(int a, int b)
{
    s_Bh.duelA = a; s_Bh.duelB = b;
    if(a == myId) s_Bh.duelWith = b; else if(b == myId) s_Bh.duelWith = a;
    SysMsg(0xDC2626FF, "{DC2626}>> {C08454}The duel between {FFFFFF}%s {C08454}and {FFFFFF}%s {C08454}has begun!", BountyName(a), BountyName(b));
}
static void BountyFirstTime(int id)
{
    SysMsg(0xDC2626FF, "{DC2626}>> {FFFFFF}%s {C08454}joined the {DC2626}Blood Deathmatch {C08454}for the first time. Welcome!", BountyName(id));
}
static void BountyOnEvent(const PktEvent& e)
{
    if(e.pad != MP_BOUNTY_MAGIC || e.id >= MP_MAX_PLAYERS || e.id == myId || !BountyActive()) return;
    const int from = e.id, to = e.slot;
    switch(e.cp)
    {
        case 1:   // invite
            if(to == myId)
            {
                s_Bh.duelInvFrom = from; s_Bh.duelInvAt = MP_NowMs();
                SysMsg(0xDC2626FF, "{DC2626}>> {FFFFFF}%s {C08454}challenges you to a duel! {FFFFFF}/accept {C08454}or {FFFFFF}/decline", BountyName(from));
            }
            break;
        case 2: if(to < MP_MAX_PLAYERS) BountyDuelBegun(to, from); break;   // to = the one who challenged
        case 3: if(to == myId) SysMsg(0xC08454FF, "{DC2626}>> {FFFFFF}%s {C08454}declined your duel.", BountyName(from)); break;
        case 4: BountyFirstTime(from); break;
    }
}
static void BountyLeave(int id)
{
    if(id < 0 || id >= MP_MAX_PLAYERS) return;
    s_Bh.streak[id] = s_Bh.kills[id] = s_Bh.deaths[id] = 0; s_Bh.hitAt[id] = 0;
    if(s_Bh.duelWith == id) { s_Bh.duelWith = -1; if(BountyActive()) SysMsg(0xC08454FF, "{DC2626}>> {C08454}Your duel is over: the other player left."); }
    if(s_Bh.duelA == id || s_Bh.duelB == id) s_Bh.duelA = s_Bh.duelB = -1;
    if(s_Bh.duelInvFrom == id) s_Bh.duelInvFrom = -1;
}
static void BountyReset()
{
    BountyStatsSave();
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) { s_Bh.streak[i] = s_Bh.kills[i] = s_Bh.deaths[i] = 0; s_Bh.hitAt[i] = 0; }
    s_Bh.chain = 0; s_Bh.chainAt = 0; s_Bh.banner.clear(); s_Bh.fog.clear(); s_Bh.lines.clear();
    s_Bh.duelWith = s_Bh.duelInvFrom = s_Bh.duelA = s_Bh.duelB = -1; s_Bh.welcomePending = false;
    s_Bh.statsLoaded = false;
}

// ---- hits / kills ------------------------------------------------------------------------------------------------
static void BountyOnMyHit(int id, float amount, int weapon)
{
    (void)amount; (void)weapon;
    if(id < 0 || id >= MP_MAX_PLAYERS || !BountyActive()) return;
    s_Bh.hitAt[id] = MP_NowMs();
}
static const char* BountyChainName(int n)
{
    static const char* const k[] = { "Killing Spree!", "Double Kill!", "Triple Kill!", "Quadruple Kill!", "Mania!", "Savage!", "Rampage!", "Unstoppable!", "Godlike!" };
    return n >= 1 && n <= 9 ? k[n - 1] : "Legendary!";
}
static void BountyPushLine(const std::string& t, uint32_t col)
{
    s_Bh.lines.push_back({ t, col, MP_NowMs() });
    if(s_Bh.lines.size() > 5) s_Bh.lines.erase(s_Bh.lines.begin());
}
static void BountyOnDeath(const PktDeath& d)
{
    if(!BountyActive() || d.victim >= MP_MAX_PLAYERS) return;
    BountyStatsLoad();
    const uint64_t now = MP_NowMs();
    const int v = d.victim, k = (d.killer < MP_MAX_PLAYERS && d.killer != d.victim) ? d.killer : -1;
    const int bounty = BountyOf(v);
    ++s_Bh.deaths[v]; s_Bh.streak[v] = 0;
    char buf[160];
    if(k >= 0)
    {
        ++s_Bh.kills[k]; ++s_Bh.streak[k];
        static const char* const kFog[] = { "is now part of the fog", "rests in pieces", "was swallowed by the fog", "bled out" };
        snprintf(buf, sizeof(buf), "%s {DC2626}%s", BountyName(v), kFog[(s_Bh.kills[k] + v) % 4]);
        s_Bh.fog = buf; s_Bh.fogAt = now;
        if(k == myId)
        {
            ++s_Bh.myKills; s_Bh.myXp += 10 + 5 * (s_Bh.streak[myId] > 1 ? s_Bh.streak[myId] - 1 : 0);
            s_Bh.chain = (s_Bh.chainAt && now - s_Bh.chainAt <= MP_BOUNTY_CHAIN_MS) ? s_Bh.chain + 1 : 1;
            s_Bh.chainAt = now;
            s_Bh.banner = BountyChainName(s_Bh.chain); s_Bh.bannerAt = now;
            const int reward = bounty + (s_Bh.chain > 1 ? 50 * (s_Bh.chain - 1) : 0);
            Game::AddMoney(reward);
            snprintf(buf, sizeof(buf), "%s - %s +%d.00", BountyName(v), BountyWeapon(d.weapon).c_str(), reward);
            BountyPushLine(buf, IM_COL32(60, 220, 90, 255));
            if(settings.bountySound) { char p[512]; snprintf(p, sizeof(p), "%s/sound_010.mp3", s_szDataDir); Sound::PlayHit(p, 1.0f); }
            if(s_Bh.chain >= 2) SysMsg(0xDC2626FF, "{DC2626}>> {FFFFFF}%s {C08454}%s", BountyName(myId), s_Bh.banner.c_str());
        }
        else if(s_Bh.streak[k] >= 3 && s_Bh.streak[k] % 3 == 0)
            SysMsg(0xDC2626FF, "{DC2626}>> {FFFFFF}%s {C08454}is on a %d kill streak! Bounty: {33CC55}$%d", BountyName(k), s_Bh.streak[k], BountyOf(k));
        // duels
        if((k == s_Bh.duelA && v == s_Bh.duelB) || (k == s_Bh.duelB && v == s_Bh.duelA))
        {
            SysMsg(0xDC2626FF, "{DC2626}>> {FFFFFF}%s {C08454}won the duel against {FFFFFF}%s{C08454}!", BountyName(k), BountyName(v));
            if(k == myId) { ++s_Bh.myDuels; s_Bh.myXp += 50; }
            s_Bh.duelA = s_Bh.duelB = -1; s_Bh.duelWith = -1;
        }
    }
    if(v == myId)
    {
        ++s_Bh.myDeaths; s_Bh.chain = 0; s_Bh.chainAt = 0;
        int loss = MP_BOUNTY_LOSS; const int have = Game::GetMoney(); if(loss > have) loss = have > 0 ? have : 0;
        if(loss) Game::AddMoney(-loss);
        snprintf(buf, sizeof(buf), "%s - %s -%d.00", k >= 0 ? BountyName(k) : "World", BountyWeapon(d.weapon).c_str(), MP_BOUNTY_LOSS);
        BountyPushLine(buf, IM_COL32(70, 140, 255, 255));
    }
    if(k == myId || v == myId) BountyStatsSave();
}

// ---- join: first time + the bloody welcome ---------------------------------------------------------------------
static void BountyOnJoined()
{
    s_Bh.welcomePending = true; s_Bh.welcomeAt = MP_NowMs();
}
static void BountyWelcome()
{
    s_Bh.welcomePending = false;
    BountyStatsLoad();
    // first time on this server under this name?
    char path[512]; snprintf(path, sizeof(path), "%s/bounty_seen.txt", s_szDataDir);
    char key[64]; BountyStatsKey(key, sizeof(key));
    bool seen = false;
    if(FILE* f = fopen(path, "r")) { char ln[128]; while(fgets(ln, sizeof(ln), f)) { ln[strcspn(ln, "\r\n")] = 0; if(!strcmp(ln, key)) seen = true; } fclose(f); }
    if(!settings.bountyWelcomeOn && seen) return;
    if(settings.bountyWelcomeOn)
    {
        SysMsg(0x8B0000FF, "{5A0A0A}\xE2\x96\x84\xE2\x96\x84\xE2\x96\x84 {8B0000}\xE2\x98\xA0 {DC2626}B L O O D   D E A T H M A T C H {8B0000}\xE2\x98\xA0 {5A0A0A}\xE2\x96\x84\xE2\x96\x84\xE2\x96\x84");
        SysMsg(0xDC2626FF, "{8B0000}\xE2\x89\x88\xE2\x89\x88 {FFFFFF}B O U N T Y   {DC2626}H U N T E R {8B0000}\xE2\x89\x88\xE2\x89\x88");
        SysMsg(0x8B0000FF, "{8B0000}  ' . |  ' ! .  |  ' . !  | . '  ! .");
        SysMsg(0xDC2626FF, "%s", WelcomeFill(settings.bountyWelcome).c_str());
        SysMsg(0xC08454FF, "{C08454}Kills in a row raise your bounty. {FFFFFF}/bounty {C08454}help, {FFFFFF}/duel id {C08454}challenge.");
        if(settings.bountySound) { char p[512]; snprintf(p, sizeof(p), "%s/sound_010.mp3", s_szDataDir); Sound::PlayFile(p, 1.0f); }
    }
    if(!seen)
    {
        if(FILE* f = fopen(path, "a")) { fprintf(f, "%s\n", key); fclose(f); }
        BountyFirstTime(myId); BountySend(4, -1);
    }
}

// ---- Broker MQQ restrictions (no unlimited money / health / cheats) --------------------------------------------
static bool DefaultBlocksCheat(const char* cmd)
{
    if(MP::IsLocalAdmin()) return false;
    static const char* const k[] = { "god", "godmode", "invincible", "invisible", "invis", "wallhack", "wh", "flash", "freeze", "gravity",
                                     "nofall", "money", "givemoney", "setmoney", "health", "sethealth", "hp", "armour", "armor", "setarmour",
                                     "infinite", "unlimited", "infammo", "ammo" };
    for(const char* c : k) if(!strcasecmp(cmd, c)) return true;
    return false;
}
// Broker MQQ rule: no unlimited money. MP's own money changes go through Game::AddMoney (counted: rewards, pay days,
// saved progress, shops ...); anything else that adds $100,000+ at once (a trainer, a money cheat, HESOYAM) or
// goes over $99,999,999 is taken back right away - from the moment you join, every time.
#define MP_MONEY_CAP        99999999
#define MP_MONEY_JUMP_LIMIT 100000
static int MoneyRuleTakeBack(int prev, int cur, long long granted)
{
    if(prev < 0) return 0;   // no baseline yet
    long long legit = (long long)prev + granted;
    if(legit > MP_MONEY_CAP) legit = MP_MONEY_CAP;
    if(legit < 0) legit = 0;
    const long long unexplained = (long long)cur - legit;
    if(cur > MP_MONEY_CAP) return (int)(cur - (legit < cur ? legit : (long long)MP_MONEY_CAP));
    if(unexplained >= MP_MONEY_JUMP_LIMIT) return (int)unexplained;
    return 0;
}
static int  s_nMoneyRuleTaken = 0;   // tests / log: how much the rule took back this session
static void DefaultRestrictTick(int ped, uint64_t now)
{
    static uint64_t s_nNext = 0, s_nWarnAt = 0; static int s_nMoney = -1; static bool s_bWas = false;
    const bool on = MP::IsOnDefault() && client.GetState() == NET_CONNECTED && !MP::IsLocalAdmin();
    if(!on) { s_bWas = false; s_nMoney = -1; return; }
    if(!s_bWas) { s_bWas = true; s_nMoney = -1; Game::TakeMoneyGranted(); }
    if(now < s_nNext) return;
    s_nNext = now + 250;
    if(s_Cheat.invincible || s_Cheat.invisible || s_Cheat.wallhack || s_Cheat.flash || s_Cheat.nofall || s_Cheat.gravity != 1.0f)
    {
        s_Cheat.invincible = s_Cheat.invisible = s_Cheat.wallhack = s_Cheat.flash = s_Cheat.nofall = false; s_Cheat.gravity = 1.0f;
        SysMsg(COL_RED, "{DC2626}Broker MQQ: {FFFFFF}cheats are not allowed on this server.");
    }
    if(ped <= 0 || !Game::IsReady()) return;
    if(Game::GetCharHealth(ped) > 200) { Game::SetCharHealth(ped, 100); SysMsg(COL_RED, "{DC2626}Broker MQQ: {FFFFFF}unlimited health is not allowed."); }
    if(Game::GetCharArmour(ped) > 150) Game::SetCharArmour(ped, 100);
    const int m = Game::GetMoney();
    const long long granted = Game::TakeMoneyGranted();
    const int back = MoneyRuleTakeBack(s_nMoney, m, granted);
    if(back > 0)
    {
        Game::AddMoney(-back); Game::TakeMoneyGranted();   // our own take-back is not a grant
        s_nMoneyRuleTaken += back;
        logger->Info("Broker MQQ money rule: took back $%d (had $%d, before $%d, MP gave %lld)", back, m, s_nMoney, granted);
        if(now - s_nWarnAt > 3000) { s_nWarnAt = now; SysMsg(COL_RED, "{DC2626}Broker MQQ rule: {FFFFFF}unlimited money is blocked here. {FF4444}-$%d", back); }
    }
    s_nMoney = Game::GetMoney();
}

// ---- tick / commands --------------------------------------------------------------------------------------------
static void BountyTick(int ped, uint64_t now)
{
    DefaultRestrictTick(ped, now);   // (5.02.58: SoloTick runs in MP::Process now)
    if(s_Bh.welcomePending && client.GetState() == NET_CONNECTED)
    {
        if(BountyActive() && !JoinFlowActive() && !AuthScreenUp() && now - s_Bh.welcomeAt > 2500) BountyWelcome();
        else if(now - s_Bh.welcomeAt > 15000) s_Bh.welcomePending = false;   // the server doesn't have the gamemode
    }
    if(s_Bh.duelInvFrom >= 0 && now - s_Bh.duelInvAt > 30000) s_Bh.duelInvFrom = -1;
    while(!s_Bh.lines.empty() && now - s_Bh.lines.front().at > 7000) s_Bh.lines.erase(s_Bh.lines.begin());
}
static bool BountyCommand(const char* cmd, const char* arg)
{
    if(!BountyActive()) return false;
    if(!strcasecmp(cmd, "bounty"))
    {
        SysMsg(0xDC2626FF, "{DC2626}>> {FFFFFF}Bounty Hunter {C08454}(Blood Deathmatch)");
        SysMsg(0xC08454FF, "{C08454}Your bounty: {33CC55}$%d {C08454}| streak %d | level %d", BountyOf(myId), myId < MP_MAX_PLAYERS ? s_Bh.streak[myId] : 0, BountyLevel());
        SysMsg(0xC08454FF, "{FFFFFF}/duel id {C08454}challenge  {FFFFFF}/accept  /decline  /bstats  /bwelcome text|on|off");
        int best = -1; for(int i = 0; i < MP_MAX_PLAYERS; ++i) if((players[i].active || i == myId) && (best < 0 || s_Bh.streak[i] > s_Bh.streak[best])) best = i;
        if(best >= 0 && s_Bh.streak[best] > 0) SysMsg(0xC08454FF, "{C08454}Most wanted: {FFFFFF}%s {33CC55}$%d", BountyName(best), BountyOf(best));
        return true;
    }
    if(!strcasecmp(cmd, "duel"))
    {
        int id = arg && *arg ? atoi(arg) : -1;
        if(id < 0 || id >= MP_MAX_PLAYERS || id == myId || !players[id].active) { SysMsg(COL_RED, "Usage: /duel [player id]"); return true; }
        BountySend(1, id);
        SysMsg(0xC08454FF, "{DC2626}>> {C08454}You challenged {FFFFFF}%s {C08454}to a duel.", BountyName(id));
        return true;
    }
    if(!strcasecmp(cmd, "accept") && s_Bh.duelInvFrom >= 0)
    {
        const int from = s_Bh.duelInvFrom; s_Bh.duelInvFrom = -1;
        BountySend(2, from); BountyDuelBegun(from, myId);
        return true;
    }
    if(!strcasecmp(cmd, "decline") && s_Bh.duelInvFrom >= 0)
    {
        BountySend(3, s_Bh.duelInvFrom); s_Bh.duelInvFrom = -1;
        SysMsg(0xC08454FF, "{C08454}Duel declined.");
        return true;
    }
    if(!strcasecmp(cmd, "bstats"))
    {
        BountyStatsLoad();
        SysMsg(0xDC2626FF, "{DC2626}K: %d  D: %d  Ratio: %.2f  Duel: %d  Level: %d (%d xp)", s_Bh.myKills, s_Bh.myDeaths,
               s_Bh.myDeaths ? (float)s_Bh.myKills / s_Bh.myDeaths : (float)s_Bh.myKills, s_Bh.myDuels, BountyLevel(), s_Bh.myXp);
        return true;
    }
    if(!strcasecmp(cmd, "bwelcome"))
    {
        if(!arg || !*arg) { SysMsg(0xC08454FF, "{C08454}Welcome: %s", settings.bountyWelcome); return true; }
        if(!strcasecmp(arg, "on") || !strcasecmp(arg, "off")) settings.bountyWelcomeOn = !strcasecmp(arg, "on");
        else MP_CopyStr(settings.bountyWelcome, arg, sizeof(settings.bountyWelcome));
        SaveConfig(); SysMsg(COL_GREEN, "Bounty Hunter welcome saved.");
        return true;
    }
    return false;
}

#ifndef MP_BOUNTY_TEST   // tools/tests/bounty_test.cpp runs everything above on a PC (no ImGui)
// ---- drawing ---------------------------------------------------------------------------------------------------
static void BountyText(ImDrawList* dl, ImFont* f, float fs, ImVec2 p, ImU32 col, const char* t)
{
    // the same outline as UI::OutlinedText, with any font (pricedown banner)
    const ImU32 o = IM_COL32(0, 0, 0, (col >> IM_COL32_A_SHIFT) & 0xFF);
    const float d = fs > 40 ? 2.0f : 1.0f;
    for(int dx = -1; dx <= 1; ++dx) for(int dy = -1; dy <= 1; ++dy) if(dx || dy) dl->AddText(f, fs, ImVec2(p.x + dx * d, p.y + dy * d), o, t);
    dl->AddText(f, fs, p, col, t);
}
static void BountyColorText(ImDrawList* dl, float fs, ImVec2 p, ImU32 base, const char* t, bool center)
{
    // "{RRGGBB}" runs (white name + red phrase)
    struct Run { std::string s; ImU32 c; }; std::vector<Run> runs; Run cur{ "", base };
    for(const char* c = t; *c; )
    {
        if(c[0] == '{' && strlen(c) >= 8 && c[7] == '}')
        {
            unsigned v; if(sscanf(c + 1, "%6x", &v) == 1) { if(!cur.s.empty()) runs.push_back(cur); cur.s.clear(); cur.c = IM_COL32((v >> 16) & 255, (v >> 8) & 255, v & 255, 255); c += 8; continue; }
        }
        cur.s += *c++;
    }
    if(!cur.s.empty()) runs.push_back(cur);
    float w = 0; for(auto& r : runs) w += UI::Font()->CalcTextSizeA(fs, FLT_MAX, 0, r.s.c_str()).x;
    float x = center ? p.x - w * 0.5f : p.x;
    for(auto& r : runs) { UI::OutlinedText(dl, fs, ImVec2(x, p.y), r.c, r.s.c_str()); x += UI::Font()->CalcTextSizeA(fs, FLT_MAX, 0, r.s.c_str()).x; }
}
static void RenderBounty()
{
    if(!BountyActive() || Game::IsPaused() || !myDrawValid || FloatMenu::IsModal() || JoinFlowActive() || AuthScreenUp()) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const uint64_t now = MP_NowMs();
    // the radar: centre = me, radius = a far point clamped to the disc
    float cx = UI::S(200), cy = ds.y - UI::S(230), r = UI::S(150);
    {
        float x1, y1, x2, y2;
        if(Game::RadarWorldToScreen(MP::myDrawPos.x, MP::myDrawPos.y, x1, y1) &&
           Game::RadarWorldToScreen(MP::myDrawPos.x, MP::myDrawPos.y + 5000.0f, x2, y2, true))
        { cx = x1; cy = y1; float rr = sqrtf((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1)); if(rr > UI::S(40) && rr < ds.y * 0.4f) r = rr; }
    }
    const float fs = UI::S(26);
    // bottom left, under the radar: K / D / Ratio / Duel / Level
    {
        char b[160];
        const float ratio = s_Bh.myDeaths ? (float)s_Bh.myKills / s_Bh.myDeaths : (float)s_Bh.myKills;
        snprintf(b, sizeof(b), "K: %d  D: %d  Ratio: %.2f  Duel: %d  Level: %d", s_Bh.myKills, s_Bh.myDeaths, ratio, s_Bh.myDuels, BountyLevel());
        float y = cy + r + UI::S(10); if(y > ds.y - fs - UI::S(6)) y = ds.y - fs - UI::S(6);
        UI::OutlinedText(dl, fs, ImVec2(cx - r, y), IM_COL32(220, 38, 38, 255), b);
        if(s_Bh.duelWith >= 0) { snprintf(b, sizeof(b), "Duel vs %s", BountyName(s_Bh.duelWith)); UI::OutlinedText(dl, fs * 0.85f, ImVec2(cx - r, y - fs), IM_COL32(196, 132, 84, 255), b); }
    }
    // next to the radar: rewards / losses
    {
        float y = cy - r * 0.5f;
        for(auto& l : s_Bh.lines)
        {
            const float a = now - l.at > 6000 ? 1.0f - (now - l.at - 6000) / 1000.0f : 1.0f;
            UI::OutlinedText(dl, fs, ImVec2(cx + r + UI::S(14), y), (l.col & 0x00FFFFFF) | ((ImU32)(255 * a) << IM_COL32_A_SHIFT), l.text.c_str(), a);
            y += fs * 1.15f;
        }
    }
    // bottom centre: "name is now part of the fog"
    if(!s_Bh.fog.empty() && now - s_Bh.fogAt < 5000)
        BountyColorText(dl, fs * 1.1f, ImVec2(ds.x * 0.5f, ds.y - UI::S(120)), IM_COL32(255, 255, 255, 255), s_Bh.fog.c_str(), true);
    // multi kill banner (pricedown, white with a red bar)
    if(!s_Bh.banner.empty() && now - s_Bh.bannerAt < 3000)
    {
        ImFont* pf = UI::PricedownFont(); if(!pf) pf = UI::Font();
        const float bfs = UI::S(64), e = (now - s_Bh.bannerAt) / 3000.0f, a = e > 0.8f ? (1.0f - e) / 0.2f : 1.0f;
        const ImVec2 ts = pf->CalcTextSizeA(bfs, FLT_MAX, 0, s_Bh.banner.c_str());
        const ImVec2 p(ds.x * 0.5f - ts.x * 0.5f, ds.y * 0.28f);
        dl->AddRectFilled(ImVec2(p.x - UI::S(30), p.y + ts.y * 0.62f), ImVec2(p.x + ts.x + UI::S(30), p.y + ts.y * 0.92f), IM_COL32(170, 20, 20, (int)(200 * a)));
        BountyText(dl, pf, bfs, p, IM_COL32(255, 255, 255, (int)(255 * a)), s_Bh.banner.c_str());
    }
}
// over a player's head (hud.cpp, after the damage indicator): the bounty, kills x deaths, "Death by the Fog!"
void MP::DrawBountyTag(ImDrawList* dl, int id, const Vec3& head)
{
    if(id < 0 || id >= MP_MAX_PLAYERS || !BountyActive()) return;
    Vec3 top = head; top.z += 0.30f;
    float sx, sy, depth;
    if(!Game::WorldToScreen(top, sx, sy, depth)) return;
    const float k = settings.nametagSize, fs = UI::S(26.0f) * k;
    char a[32], b[32]; snprintf(a, sizeof(a), "$%d", BountyOf(id)); snprintf(b, sizeof(b), "%d x %d", s_Bh.kills[id], s_Bh.deaths[id]);
    const ImVec2 as = UI::Font()->CalcTextSizeA(fs, FLT_MAX, 0, a), bs = UI::Font()->CalcTextSizeA(fs * 0.8f, FLT_MAX, 0, b);
    float y = sy - fs * 2.25f;
    UI::OutlinedText(dl, fs * 0.8f, ImVec2(sx - bs.x * 0.5f, y), IM_COL32(255, 255, 255, 230), b);
    UI::OutlinedText(dl, fs, ImVec2(sx - as.x * 0.5f, y - fs), IM_COL32(60, 220, 90, 255), a);
    if(s_Bh.hitAt[id] && MP_NowMs() - s_Bh.hitAt[id] < 2500)
    {
        const char* t = "Death by the Fog!";
        const ImVec2 ts = UI::Font()->CalcTextSizeA(fs, FLT_MAX, 0, t);
        UI::OutlinedText(dl, fs, ImVec2(sx - ts.x * 0.5f, sy - fs * 5.6f), IM_COL32(220, 38, 38, 255), t);
    }
}

// ---- Add server > Gamemode > Bounty Hunter settings --------------------------------------------------------------
static void BountySettingsSection()
{
    ImGui::Indent(UI::S(20));
    if(FloatMenu::ToggleSwitch("Bloody welcome (logo + message)", &settings.bountyWelcomeOn)) SaveConfig();
    if(settings.bountyWelcomeOn)
    {
        ImGui::TextDisabled("Your welcome message ({name} = player, {server} = server, {RRGGBB} = colour, symbols like \xE2\x98\xA0 \xE2\x9D\xA4 \xE2\x9C\x93 \xCF\x80 \xE2\x88\x9A are fine)");
        FloatMenu::TextField("Welcome message", settings.bountyWelcome, "Bounty Hunter welcome", MP_CHAT_LEN - 1,
                             [](const char* t, void*) { MP_CopyStr(settings.bountyWelcome, t, sizeof(settings.bountyWelcome)); SaveConfig(); });
    }
    if(FloatMenu::ToggleSwitch("Kill sound (sound_010.mp3)", &settings.bountySound)) SaveConfig();
    ImGui::Unindent(UI::S(20));
}
#endif   // MP_BOUNTY_TEST
