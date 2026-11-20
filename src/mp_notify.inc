// 5.00.07 (included by mp.cpp): the notification system (gamemode GM_NOTIFY).
//   Default server (Broker MQQ): always on, only its [ADMIN] / [MOD] send (they run Server management).
//   Custom servers: Add server > Gamemodes > Notification system; the host owner (+ [ADMIN] / [MOD]) sends.
// Staff type in the chat (the suggestions under the chat box list every key, the logos and the times):
//   /notify embed:Server event logo:trophy message:Race at the airport in 10 minutes timer:15
//   /notify embed:Double pay logo:money message:Every job pays x2 start:2:00 PM end:3:00 PM
//   /notify clear
// embed = title, logo = the icon (dropdown), message = the text, timer = seconds on screen (3-600),
// start / end = a time (12 h with AM / PM, or 24 h, or "now"); typing "start:" / "end:" opens a time picker.
// Everybody on the server gets a card that slides in from the left side: an icon, the title, a bar that
// runs out with the time, and the message, in the Broker MQQ red + brown on the default server (the
// server's HUD colour on custom servers). notify.mp3 plays once when it pops up: streamed from the wagu
// repo on GitHub (Host/Media) into memory, like dz_ambient / war_ambient - it is never put in files/TYFMC/MP.

#define NOTIFY_SOUND_URL1 "https://raw.githubusercontent.com/JebetrexxMC/wagu/main/Host/Media/notify.mp3"
#define NOTIFY_SOUND_URL2 "https://jebetrexxmc.github.io/wagu/Host/Media/notify.mp3"
#define NOTIFY_IN_MS   420
#define NOTIFY_OUT_MS  320
#define NOTIFY_SHOW_MAX 5

struct NotifyLogoInfo { const char* key; const char* desc; };
static const NotifyLogoInfo kNotifyLogos[NLOGO_COUNT] = {
    { "info",      "i - news, information" },
    { "warning",   "! - warning, rules" },
    { "heart",     "heart - health, healing" },
    { "car",       "car - vehicles, rentals, races" },
    { "trophy",    "trophy - events, prizes, levels" },
    { "drop",      "drop - effects, drugs, water" },
    { "star",      "star - VIP, specials" },
    { "money",     "money - pay, bonus, shop" },
    { "shield",    "shield - police, safe zone, protection" },
    { "clock",     "clock - timer, schedule, restart" },
    { "bell",      "bell - reminder" },
    { "skull",     "skull - deathmatch, danger, zombies" },
    { "gift",      "gift - giveaway, Christmas" },
    { "megaphone", "megaphone - announcement" },
};
static int NotifyLogoFind(const char* s)
{
    if(!s || !*s) return -1;
    for(int i = 0; i < NLOGO_COUNT; ++i) if(!strcasecmp(kNotifyLogos[i].key, s)) return i;
    if(isdigit((unsigned char)s[0])) { int v = atoi(s); if(v >= 0 && v < NLOGO_COUNT) return v; }
    size_t n = strlen(s);
    for(int i = 0; i < NLOGO_COUNT; ++i) if(!strncasecmp(kNotifyLogos[i].key, s, n)) return i;   // "troph", "mega"
    return -1;
}

// ---- where it is on --------------------------------------------------------------------------
static bool NotifyFeaturedOther() { return BcwOnServer() || Mw3OnServer() || RpOnServer() || DzOnServer() || LgOnServer(); }
static bool NotifyOnServer()
{
    if(client.GetState() != NET_CONNECTED || NotifyFeaturedOther()) return false;
    return MP::IsOnDefault() || MP::ModeOn(GM_NOTIFY);
}
static bool NotifyCanSend()
{
    if(client.GetState() != NET_CONNECTED) return false;
    if(MP::IsOnDefault()) return MP::IsLocalAdmin() || MP::IsLocalMod() || MP::IsDefaultAdmin() || (s_SrvSet.have && s_SrvSet.s.can);
    return AdmLocalStaff() || (s_SrvSet.have && s_SrvSet.s.can);
}

// ---- colours: Broker MQQ red + brown on the default server, the server HUD colour elsewhere -----
struct NotifyTheme { ImU32 stripe, stripe2, bar, bar2, barBg, fill, tag; const char* tagText; };
static NotifyTheme NotifyThemeNow()
{
    NotifyTheme t;
    if(MP::IsOnDefault())
    {   // the colours of the Broker MQQ title (mp_timber.inc DefaultDrawLogo)
        t.stripe = IM_COL32(220, 38, 38, 255); t.stripe2 = IM_COL32(176, 112, 64, 255);
        t.bar = IM_COL32(220, 38, 38, 255); t.bar2 = IM_COL32(176, 112, 64, 255); t.barBg = IM_COL32(70, 18, 14, 230);
        t.fill = IM_COL32(12, 6, 5, 228); t.tag = IM_COL32(196, 132, 84, 255); t.tagText = "BROKER MQQ";
        return t;
    }
    const HudPal& p = HudPalette();
    t.stripe = p.border; t.stripe2 = p.accent; t.bar = p.border; t.bar2 = p.accent;
    t.barBg = (p.fill & 0x00FFFFFF) | 0xE6000000; t.fill = IM_COL32(8, 8, 10, 228); t.tag = p.accent; t.tagText = nullptr;
    return t;
}
static ImU32 NotifyIconCol(int logo)
{
    if(MP::IsOnDefault())
    {   // red / light red / brown, like the title logo
        static const ImU32 k[NLOGO_COUNT] = {
            IM_COL32(255, 255, 255, 255), IM_COL32(248, 90, 60, 255), IM_COL32(230, 40, 40, 255), IM_COL32(196, 132, 84, 255),
            IM_COL32(212, 150, 90, 255), IM_COL32(255, 255, 255, 255), IM_COL32(248, 113, 113, 255), IM_COL32(196, 132, 84, 255),
            IM_COL32(220, 38, 38, 255), IM_COL32(255, 255, 255, 255), IM_COL32(212, 150, 90, 255), IM_COL32(255, 255, 255, 255),
            IM_COL32(230, 40, 40, 255), IM_COL32(248, 90, 60, 255) };
        return k[logo < NLOGO_COUNT ? logo : 0];
    }
    static const ImU32 k[NLOGO_COUNT] = {
        IM_COL32(255, 255, 255, 255), IM_COL32(250, 204, 21, 255), IM_COL32(239, 68, 68, 255), IM_COL32(59, 130, 246, 255),
        IM_COL32(250, 204, 21, 255), IM_COL32(255, 255, 255, 255), IM_COL32(250, 204, 21, 255), IM_COL32(74, 222, 128, 255),
        IM_COL32(96, 165, 250, 255), IM_COL32(255, 255, 255, 255), IM_COL32(250, 204, 21, 255), IM_COL32(229, 231, 235, 255),
        IM_COL32(244, 63, 94, 255), IM_COL32(251, 146, 60, 255) };
    return k[logo < NLOGO_COUNT ? logo : 0];
}

// ---- the logos (drawn, no textures) ------------------------------------------------------------
static void NotifyIcon(ImDrawList* dl, ImVec2 c, float r, int logo, ImU32 col, float a)
{
    const ImU32 cc = (col & 0x00FFFFFF) | ((ImU32)(((col >> 24) & 0xFF) * a) << 24);
    const ImU32 dk = IM_COL32(0, 0, 0, (int)(235 * a));
    const float t = fmaxf(1.5f, r * 0.16f);
    switch(logo)
    {
    case NLOGO_INFO:
        dl->AddCircle(c, r * 0.9f, cc, 24, t);
        dl->AddCircleFilled(ImVec2(c.x, c.y - r * 0.42f), r * 0.13f, cc, 10);
        dl->AddRectFilled(ImVec2(c.x - r * 0.1f, c.y - r * 0.18f), ImVec2(c.x + r * 0.1f, c.y + r * 0.52f), cc, r * 0.05f);
        break;
    case NLOGO_WARNING:
        dl->AddTriangleFilled(ImVec2(c.x, c.y - r * 0.92f), ImVec2(c.x + r, c.y + r * 0.8f), ImVec2(c.x - r, c.y + r * 0.8f), cc);
        dl->AddRectFilled(ImVec2(c.x - r * 0.09f, c.y - r * 0.38f), ImVec2(c.x + r * 0.09f, c.y + r * 0.3f), dk);
        dl->AddCircleFilled(ImVec2(c.x, c.y + r * 0.55f), r * 0.11f, dk, 8);
        break;
    case NLOGO_HEART:
        dl->AddCircleFilled(ImVec2(c.x - r * 0.42f, c.y - r * 0.25f), r * 0.48f, cc, 16);
        dl->AddCircleFilled(ImVec2(c.x + r * 0.42f, c.y - r * 0.25f), r * 0.48f, cc, 16);
        dl->AddTriangleFilled(ImVec2(c.x - r * 0.88f, c.y - r * 0.08f), ImVec2(c.x + r * 0.88f, c.y - r * 0.08f), ImVec2(c.x, c.y + r * 0.85f), cc);
        break;
    case NLOGO_CAR:
        dl->AddRectFilled(ImVec2(c.x - r, c.y - r * 0.1f), ImVec2(c.x + r, c.y + r * 0.45f), cc, r * 0.15f);
        { ImVec2 p[4] = { ImVec2(c.x - r * 0.6f, c.y - r * 0.1f), ImVec2(c.x - r * 0.35f, c.y - r * 0.6f), ImVec2(c.x + r * 0.4f, c.y - r * 0.6f), ImVec2(c.x + r * 0.7f, c.y - r * 0.1f) };
          dl->AddConvexPolyFilled(p, 4, cc); }
        dl->AddCircleFilled(ImVec2(c.x - r * 0.55f, c.y + r * 0.5f), r * 0.26f, dk, 12); dl->AddCircleFilled(ImVec2(c.x - r * 0.55f, c.y + r * 0.5f), r * 0.15f, cc, 10);
        dl->AddCircleFilled(ImVec2(c.x + r * 0.55f, c.y + r * 0.5f), r * 0.26f, dk, 12); dl->AddCircleFilled(ImVec2(c.x + r * 0.55f, c.y + r * 0.5f), r * 0.15f, cc, 10);
        break;
    case NLOGO_TROPHY:
        dl->PathArcTo(ImVec2(c.x, c.y - r * 0.55f), r * 0.62f, 0.0f, 3.14159f, 14); dl->PathFillConvex(cc);
        dl->AddRectFilled(ImVec2(c.x - r * 0.62f, c.y - r * 0.85f), ImVec2(c.x + r * 0.62f, c.y - r * 0.55f), cc);
        dl->AddCircle(ImVec2(c.x - r * 0.68f, c.y - r * 0.48f), r * 0.28f, cc, 12, t);
        dl->AddCircle(ImVec2(c.x + r * 0.68f, c.y - r * 0.48f), r * 0.28f, cc, 12, t);
        dl->AddRectFilled(ImVec2(c.x - r * 0.12f, c.y + r * 0.02f), ImVec2(c.x + r * 0.12f, c.y + r * 0.5f), cc);
        dl->AddRectFilled(ImVec2(c.x - r * 0.5f, c.y + r * 0.5f), ImVec2(c.x + r * 0.5f, c.y + r * 0.82f), cc, r * 0.08f);
        break;
    case NLOGO_DROP:
        dl->AddCircleFilled(ImVec2(c.x, c.y + r * 0.28f), r * 0.58f, cc, 20);
        dl->AddTriangleFilled(ImVec2(c.x, c.y - r * 0.95f), ImVec2(c.x + r * 0.53f, c.y + r * 0.05f), ImVec2(c.x - r * 0.53f, c.y + r * 0.05f), cc);
        break;
    case NLOGO_STAR:
        for(int i = 0; i < 5; ++i)
        {
            float a0 = -1.5708f + i * 1.25664f, a1 = a0 + 0.62832f, am = a0 - 0.62832f;
            ImVec2 o(c.x + cosf(a0) * r, c.y + sinf(a0) * r), i1(c.x + cosf(a1) * r * 0.42f, c.y + sinf(a1) * r * 0.42f), i0(c.x + cosf(am) * r * 0.42f, c.y + sinf(am) * r * 0.42f);
            dl->AddTriangleFilled(c, i0, o, cc); dl->AddTriangleFilled(c, o, i1, cc);
        }
        break;
    case NLOGO_MONEY:
        dl->AddRectFilled(ImVec2(c.x - r, c.y - r * 0.55f), ImVec2(c.x + r, c.y + r * 0.55f), cc, r * 0.12f);
        dl->AddCircleFilled(c, r * 0.36f, dk, 16);
        { ImFont* f = UI::Font(); const float fs = r * 0.62f; ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0, "$"); dl->AddText(f, fs, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), cc, "$"); }
        break;
    case NLOGO_SHIELD:
        { ImVec2 p[6] = { ImVec2(c.x, c.y - r), ImVec2(c.x + r * 0.82f, c.y - r * 0.62f), ImVec2(c.x + r * 0.72f, c.y + r * 0.2f),
                          ImVec2(c.x, c.y + r), ImVec2(c.x - r * 0.72f, c.y + r * 0.2f), ImVec2(c.x - r * 0.82f, c.y - r * 0.62f) };
          dl->AddConvexPolyFilled(p, 6, cc); }
        dl->AddLine(ImVec2(c.x - r * 0.35f, c.y), ImVec2(c.x - r * 0.05f, c.y + r * 0.3f), dk, t);
        dl->AddLine(ImVec2(c.x - r * 0.05f, c.y + r * 0.3f), ImVec2(c.x + r * 0.4f, c.y - r * 0.3f), dk, t);
        break;
    case NLOGO_CLOCK:
        dl->AddCircle(c, r * 0.9f, cc, 24, t);
        dl->AddLine(c, ImVec2(c.x, c.y - r * 0.6f), cc, t);
        dl->AddLine(c, ImVec2(c.x + r * 0.45f, c.y + r * 0.1f), cc, t);
        dl->AddCircleFilled(c, t * 0.8f, cc, 8);
        break;
    case NLOGO_BELL:
        dl->PathArcTo(ImVec2(c.x, c.y - r * 0.15f), r * 0.62f, 3.14159f, 6.28318f, 14); dl->PathFillConvex(cc);
        { ImVec2 p[4] = { ImVec2(c.x - r * 0.62f, c.y - r * 0.16f), ImVec2(c.x + r * 0.62f, c.y - r * 0.16f), ImVec2(c.x + r * 0.9f, c.y + r * 0.5f), ImVec2(c.x - r * 0.9f, c.y + r * 0.5f) };
          dl->AddConvexPolyFilled(p, 4, cc); }
        dl->AddCircleFilled(ImVec2(c.x, c.y + r * 0.68f), r * 0.18f, cc, 10);
        dl->AddCircleFilled(ImVec2(c.x, c.y - r * 0.82f), r * 0.12f, cc, 8);
        break;
    case NLOGO_SKULL:
        dl->AddCircleFilled(ImVec2(c.x, c.y - r * 0.15f), r * 0.75f, cc, 20);
        dl->AddRectFilled(ImVec2(c.x - r * 0.42f, c.y + r * 0.3f), ImVec2(c.x + r * 0.42f, c.y + r * 0.85f), cc, r * 0.1f);
        dl->AddCircleFilled(ImVec2(c.x - r * 0.3f, c.y - r * 0.1f), r * 0.2f, dk, 10);
        dl->AddCircleFilled(ImVec2(c.x + r * 0.3f, c.y - r * 0.1f), r * 0.2f, dk, 10);
        dl->AddTriangleFilled(ImVec2(c.x, c.y + r * 0.12f), ImVec2(c.x + r * 0.1f, c.y + r * 0.3f), ImVec2(c.x - r * 0.1f, c.y + r * 0.3f), dk);
        break;
    case NLOGO_GIFT:
        dl->AddRectFilled(ImVec2(c.x - r * 0.85f, c.y - r * 0.2f), ImVec2(c.x + r * 0.85f, c.y + r * 0.9f), cc, r * 0.06f);
        dl->AddRectFilled(ImVec2(c.x - r * 0.95f, c.y - r * 0.5f), ImVec2(c.x + r * 0.95f, c.y - r * 0.15f), cc, r * 0.06f);
        dl->AddRectFilled(ImVec2(c.x - r * 0.12f, c.y - r * 0.5f), ImVec2(c.x + r * 0.12f, c.y + r * 0.9f), dk);
        dl->AddCircle(ImVec2(c.x - r * 0.3f, c.y - r * 0.72f), r * 0.24f, cc, 12, t);
        dl->AddCircle(ImVec2(c.x + r * 0.3f, c.y - r * 0.72f), r * 0.24f, cc, 12, t);
        break;
    default:   // NLOGO_MEGAPHONE
        { ImVec2 p[4] = { ImVec2(c.x - r * 0.55f, c.y - r * 0.25f), ImVec2(c.x + r * 0.75f, c.y - r * 0.85f), ImVec2(c.x + r * 0.75f, c.y + r * 0.85f), ImVec2(c.x - r * 0.55f, c.y + r * 0.25f) };
          dl->AddConvexPolyFilled(p, 4, cc); }
        dl->AddRectFilled(ImVec2(c.x - r * 0.95f, c.y - r * 0.28f), ImVec2(c.x - r * 0.5f, c.y + r * 0.28f), cc, r * 0.08f);
        dl->AddLine(ImVec2(c.x - r * 0.45f, c.y + r * 0.3f), ImVec2(c.x - r * 0.2f, c.y + r * 0.85f), cc, t * 1.4f);
        break;
    }
}

// ---- notify.mp3 from GitHub (wagu Host/Media), into memory -------------------------------------
static std::atomic<int> s_NfSnd{0};   // 0 = not asked, 1 = downloading, 2 = ready, 3 = failed
static uint64_t s_NfSndFailAt = 0, s_NfSndWant = 0;   // want = play when it arrives (until then)
static void NotifySoundFetch()
{
    int st = s_NfSnd.load();
    if(st == 1 || st == 2) return;
    if(st == 3 && MP_NowMs() - s_NfSndFailAt < 60000) return;   // offline: ask again in a minute
    s_NfSnd = 1;
    std::thread([]() {
        static const char* const kUrls[] = { NOTIFY_SOUND_URL1, NOTIFY_SOUND_URL2, MP_MEDIA_URL "notify.mp3" };
        for(const char* u : kUrls)
        {
            std::string body, why;
            if(Online::HttpsFetch(u, body, 15000, why, 2u << 20) && body.size() > 1024 && Sound::DecodeMem(0, body.data(), body.size()))
            { logger->Info("notify: notify.mp3 streamed from %s (%u bytes)", u, (unsigned)body.size()); s_NfSnd = 2; return; }
            logger->Info("notify: %s failed (%s)", u, why.c_str());
        }
        s_NfSndFailAt = MP_NowMs(); s_NfSnd = 3;
    }).detach();
}
static void NotifySoundPlay()
{
    if(s_NfSnd.load() == 2 && Sound::PlayMem(0, 0.9f)) { s_NfSndWant = 0; return; }
    s_NfSndWant = MP_NowMs() + 6000;   // still downloading (or offline): play it if it comes in 6 s
    NotifySoundFetch();
}

// ---- cards ---------------------------------------------------------------------------------------
struct NotifyCard { PktNotify p; uint64_t showAt = 0, hideAt = 0; float y = -1.0f; bool popped = false; };
static std::vector<NotifyCard> s_Nf;
static void NotifyReset() { s_Nf.clear(); s_NfSndWant = 0; }

static void NotifyOnPacket(const uint8_t* d, int len)
{
    if(len < (int)sizeof(PktNotify)) return;
    PktNotify n; memcpy(&n, d, sizeof(n));
    n.title[sizeof(n.title) - 1] = 0; n.text[sizeof(n.text) - 1] = 0; n.sender[sizeof(n.sender) - 1] = 0;
    const uint64_t now = MP_NowMs();
    if(n.op == NOTIFY_DENIED)
    {
        if(n.logo) SysMsg(COL_RED, MP::IsOnDefault() ? "/notify is for the [ADMIN] and [MOD] of the default server." : "/notify is for the host owner and the staff of this server.");
        else SysMsg(COL_RED, "The notification system is off on this server (the host owner turns it on: Add server > Gamemodes > Notification system).");
        return;
    }
    if(n.op == NOTIFY_CLEAR)
    {
        for(NotifyCard& c : s_Nf) if(c.hideAt > now) c.hideAt = now;
        return;
    }
    if(n.op != NOTIFY_SHOW) return;
    if(n.logo >= NLOGO_COUNT) n.logo = NLOGO_INFO;
    const int64_t inS = (int64_t)n.start - (int64_t)n.srvNow;   // the server's clock, not this phone's
    NotifyCard c; c.p = n;
    c.showAt = now + (uint64_t)(inS > 0 ? inS * 1000 : 0);
    const int64_t endS = n.end ? (int64_t)n.end - (int64_t)n.srvNow : inS + n.timer;
    c.hideAt = now + (uint64_t)(endS > 0 ? endS * 1000 : 0);
    if(c.hideAt <= c.showAt) c.hideAt = c.showAt + (uint64_t)(n.timer ? n.timer : 10) * 1000;
    for(NotifyCard& o : s_Nf) if(o.p.id == n.id) { const bool was = o.popped; const float y = o.y; o = c; o.popped = was; o.y = y; return; }
    s_Nf.push_back(c);
    if(c.showAt > now + 1500)
    {   // a scheduled one: tell the sender it is booked
        if(n.from == myId)
        {
            time_t st = (time_t)(time(nullptr) + (c.showAt - now) / 1000); struct tm lt; localtime_r(&st, &lt);
            char b[16]; strftime(b, sizeof(b), "%I:%M %p", &lt);
            SysMsg(COL_GREEN, "Notification \"%s\" is set for %s.", n.title[0] ? n.title : "Notification", b[0] == '0' ? b + 1 : b);
        }
    }
    NotifySoundFetch();
}

static float NotifyEaseOutBack(float t) { const float c1 = 1.70158f, c3 = c1 + 1.0f; t -= 1.0f; return 1.0f + c3 * t * t * t + c1 * t * t; }

// ---- the time picker (start: / end:) ------------------------------------------------------------
static struct { bool open = false; int which = 0; int h = 12, m = 0; bool pm = false; std::string base; } s_NfPick;
static void NotifyFmtTime(time_t t, char* out, size_t n)
{
    struct tm lt; localtime_r(&t, &lt);
    int h = lt.tm_hour % 12; if(!h) h = 12;
    snprintf(out, n, "%d:%02d %s", h, lt.tm_min, lt.tm_hour < 12 ? "AM" : "PM");
}
// "now", "+10m", "+1h", "2:30 PM", "2 PM", "14:30" -> Unix seconds (0 = empty, -1 = can't read it)
static int64_t NotifyParseTime(const char* s, int64_t after)
{
    while(*s == ' ') ++s;
    if(!*s) return 0;
    const time_t now = time(nullptr);
    if(!strncasecmp(s, "now", 3)) return now;
    if(*s == '+')
    {
        int v = atoi(s + 1); if(v <= 0) return -1;
        const char* u = s + 1; while(isdigit((unsigned char)*u)) ++u;
        return now + (int64_t)v * ((*u == 'h' || *u == 'H') ? 3600 : (*u == 's' || *u == 'S') ? 1 : 60);
    }
    if(!isdigit((unsigned char)*s)) return -1;
    int h = atoi(s), m = 0;
    const char* q = s; while(isdigit((unsigned char)*q)) ++q;
    if(*q == ':' || *q == '.') { ++q; if(!isdigit((unsigned char)*q)) return -1; m = atoi(q); while(isdigit((unsigned char)*q)) ++q; }
    while(*q == ' ') ++q;
    int ap = 0; if(!strncasecmp(q, "am", 2) || !strncasecmp(q, "a.m", 3) || (*q == 'a' || *q == 'A')) ap = 1; else if(!strncasecmp(q, "pm", 2) || (*q == 'p' || *q == 'P')) ap = 2;
    if(m < 0 || m > 59) return -1;
    if(ap) { if(h < 1 || h > 12) return -1; h = h % 12 + (ap == 2 ? 12 : 0); }
    else if(h > 23) return -1;
    struct tm lt; localtime_r(&now, &lt); lt.tm_hour = h; lt.tm_min = m; lt.tm_sec = 0;
    int64_t t = (int64_t)mktime(&lt);
    const int64_t ref = after ? after : (int64_t)now - 60;
    while(t <= ref) t += 86400;   // the next time it is that time
    return t;
}
static bool NotifyEndsWithKey(const char* t, const char* key)
{
    size_t n = strlen(t), k = strlen(key);
    if(n < k || strncasecmp(t + n - k, key, k)) return false;
    return n == k || t[n - k - 1] == ' ';
}

// ---- keys: embed: logo: message: timer: start: end: ---------------------------------------------
static const char* const kNotifyKeys[] = { "embed:", "logo:", "message:", "timer:", "start:", "end:" };
static const char* const kNotifyKeyHint[] = {
    "the title of the notification", "pick the icon (info, warning, heart, car, trophy ...)", "the text everybody reads",
    "seconds on screen (3 - 600, default 10)", "when it pops up (AM / PM time picker, or now)", "when it goes away (AM / PM time picker)" };
#define NOTIFY_KEYS 6
// position of every key in the text (-1 = not there); "msg:" also counts as message:
static void NotifyFindKeys(const char* a, int* pos)
{
    for(int k = 0; k < NOTIFY_KEYS; ++k) pos[k] = -1;
    for(int i = 0; a[i]; ++i)
    {
        if(i && a[i - 1] != ' ') continue;
        for(int k = 0; k < NOTIFY_KEYS; ++k)
        {
            size_t L = strlen(kNotifyKeys[k]);
            if(!strncasecmp(a + i, kNotifyKeys[k], L)) { if(pos[k] < 0) pos[k] = i; break; }
            if(k == 2 && !strncasecmp(a + i, "msg:", 4)) { if(pos[k] < 0) pos[k] = i; break; }
        }
    }
}
static std::string NotifyValue(const char* a, const int* pos, int k)
{
    if(pos[k] < 0) return "";
    const char* v = a + pos[k];
    v = strchr(v, ':') + 1;
    int end = (int)strlen(a);
    for(int j = 0; j < NOTIFY_KEYS; ++j) if(pos[j] > pos[k] && pos[j] < end) end = pos[j];
    std::string s(v, a + end);
    while(!s.empty() && s.front() == ' ') s.erase(s.begin());
    while(!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

static bool NotifyCommand(const char* cmd, const char* arg)
{
    if(strcmp(cmd, "notify") && strcmp(cmd, "notif") && strcmp(cmd, "announce")) return false;
    if(!strcmp(cmd, "announce") && !NotifyOnServer()) return false;
    if(client.GetState() != NET_CONNECTED) { SysMsg(COL_RED, "Join a server first."); return true; }
    if(!NotifyOnServer())
    {
        SysMsg(COL_RED, NotifyFeaturedOther() ? "This featured server has no notification system." : "The notification system is off on this server (Add server > Gamemodes > Notification system).");
        return true;
    }
    if(!NotifyCanSend()) { SysMsg(COL_RED, MP::IsOnDefault() ? "/notify is for the [ADMIN] and [MOD] of the default server." : "/notify is for the host owner and the staff of this server."); return true; }
    PktNotify p; memset(&p, 0, sizeof(p)); p.type = PKT_NOTIFY; p.op = NOTIFY_SEND; p.srvNow = (uint32_t)time(nullptr);
    if(!strcasecmp(arg, "clear") || !strcasecmp(arg, "off"))
    {
        p.op = NOTIFY_CLEAR; client.Send(&p, sizeof(p)); SysMsg(COL_SYSTEM, "Notifications cleared for everyone."); return true;
    }
    if(!*arg)
    {
        SysMsg(0xC080FFFF, "/notify embed:<title> logo:<icon> message:<text> timer:<seconds> start:<time> end:<time>");
        SysMsg(0xC080FFFF, "e.g. /notify embed:Double pay logo:money message:Every job pays x2 start:2:00 PM end:3:00 PM");
        std::string l; for(int i = 0; i < NLOGO_COUNT; ++i) { if(i) l += ", "; l += kNotifyLogos[i].key; }
        SysMsg(0xC080FFFF, "logo: %s  -  /notify clear takes them all off.", l.c_str());
        return true;
    }
    int pos[NOTIFY_KEYS]; NotifyFindKeys(arg, pos);
    bool any = false; for(int k = 0; k < NOTIFY_KEYS; ++k) if(pos[k] >= 0) any = true;
    std::string title = NotifyValue(arg, pos, 0), logo = NotifyValue(arg, pos, 1), msg = NotifyValue(arg, pos, 2), timer = NotifyValue(arg, pos, 3);
    std::string st = NotifyValue(arg, pos, 4), en = NotifyValue(arg, pos, 5);
    if(!any) msg = arg;   // "/notify Server restart in 5 minutes" = just a message
    if(title.empty() && msg.empty()) { SysMsg(COL_RED, "/notify: type embed: (title) or message: (text)."); return true; }
    if(title.empty()) title = "Notification";
    int li = 0;
    if(!logo.empty()) { li = NotifyLogoFind(logo.c_str()); if(li < 0) { SysMsg(COL_RED, "/notify: no logo \"%s\" - pick one from the list under the chat box.", logo.c_str()); return true; } }
    int tm = timer.empty() ? 10 : atoi(timer.c_str());
    if(tm < 3) tm = 3; if(tm > 600) tm = 600;
    int64_t ts = NotifyParseTime(st.c_str(), 0);
    if(ts < 0) { SysMsg(COL_RED, "/notify: start:\"%s\" is no time (e.g. start:2:30 PM, start:14:30, start:now, start:+10m).", st.c_str()); return true; }
    int64_t te = NotifyParseTime(en.c_str(), ts ? ts : (int64_t)time(nullptr));
    if(te < 0) { SysMsg(COL_RED, "/notify: end:\"%s\" is no time (e.g. end:3:00 PM).", en.c_str()); return true; }
    if(te && te - (ts ? ts : time(nullptr)) > 86400) { SysMsg(COL_RED, "/notify: at most 24 hours on screen."); return true; }
    p.logo = (uint8_t)li; p.timer = (uint16_t)tm; p.start = (uint32_t)ts; p.end = (uint32_t)te;
    MP_CopyStr(p.title, title.c_str(), sizeof(p.title));
    MP_CopyStr(p.text, msg.c_str(), sizeof(p.text));
    MP_CopyStr(p.sender, settings.name, sizeof(p.sender));
    if(client.GetServerBuild() && client.GetServerBuild() < MP_NOTIFY_BUILD)
    { SysMsg(COL_YELLOW, "This server runs an older MP (it updates itself in ~10 minutes) - notifications need MP %s.", MP_VERSION_STR); return true; }
    client.Send(&p, sizeof(p));
    return true;
}

// the chat box list: every key, the logos, timer values, times
static bool NotifySuggest(const char* t, std::vector<Suggest>& out)
{
    const size_t tl = strlen(t);
    if(!NotifyOnServer() || !NotifyCanSend()) return false;
    if(tl <= 7 && !strncasecmp("/notify", t, tl) && tl >= 2)
    {
        out.push_back({ "/notify ", "embed: logo: message: timer: start: end:  -  pop-up for everyone" });
        out.push_back({ "/notify clear", "take every notification off" });
        return false;   // the other commands that match come under it
    }
    if(strncasecmp(t, "/notify ", 8)) return false;
    const char* a = t + 8;
    int pos[NOTIFY_KEYS]; NotifyFindKeys(a, pos);
    int last = -1; for(int k = 0; k < NOTIFY_KEYS; ++k) if(pos[k] >= 0 && (last < 0 || pos[k] > pos[last])) last = k;
    std::string base(t);
    auto nextKeys = [&](const std::string& head) {
        std::string h = head; while(!h.empty() && h.back() == ' ') h.pop_back();
        for(int k = 0; k < NOTIFY_KEYS; ++k) if(pos[k] < 0) out.push_back({ h + " " + kNotifyKeys[k], kNotifyKeyHint[k] });
    };
    if(last < 0)
    {
        if(!*a) out.push_back({ "/notify clear", "take every notification off" });
        nextKeys(base);
        return true;
    }
    const char* v = strchr(a + pos[last], ':') + 1;
    const bool trailing = tl && t[tl - 1] == ' ';
    const std::string head(t, v);   // up to and with "key:"
    std::string val(v); while(!val.empty() && val.front() == ' ') val.erase(val.begin());
    if(last == 1 && !trailing)
    {   // logo: dropdown
        for(int i = 0; i < NLOGO_COUNT; ++i)
            if(val.empty() || !strncasecmp(kNotifyLogos[i].key, val.c_str(), val.size())) out.push_back({ head + kNotifyLogos[i].key + " ", kNotifyLogos[i].desc });
        if(NotifyLogoFind(val.c_str()) >= 0 && !strcasecmp(kNotifyLogos[NotifyLogoFind(val.c_str())].key, val.c_str())) nextKeys(base);
        return true;
    }
    if(last == 3 && !trailing && val.empty())
    {
        static const char* kT[] = { "5", "10", "15", "30", "60", "120", "300" };
        for(const char* x : kT) out.push_back({ head + x + " ", std::string(x) + " seconds on screen" });
        return true;
    }
    if((last == 4 || last == 5) && val.empty())
    {   // quick times (the picker above the chat has every time)
        const time_t now = time(nullptr);
        int64_t from = now;
        if(last == 5) { int64_t s0 = NotifyParseTime(NotifyValue(a, pos, 4).c_str(), 0); if(s0 > 0) from = s0; }
        if(last == 4) out.push_back({ head + "now ", "right away" });
        static const int kMin[] = { 5, 15, 30, 60, 120 };
        for(int m : kMin)
        {
            time_t x = (time_t)(((from + m * 60) + 299) / 300 * 300); char b[16]; NotifyFmtTime(x, b, sizeof(b));
            char h[48]; snprintf(h, sizeof(h), last == 4 ? "in about %d min" : "%d min on screen", m);
            out.push_back({ head + b + " ", h });
        }
        return true;
    }
    if(trailing || !val.empty()) nextKeys(base);
    return true;
}

static void NotifyPickerRender()
{
    const bool chat = VKeyboard::IsOpen() && VKeyboard::IsChat();
    const char* txt = chat ? VKeyboard::GetText() : "";
    int which = 0;
    if(chat && !strncasecmp(txt, "/notify ", 8) && NotifyOnServer() && NotifyCanSend())
        which = NotifyEndsWithKey(txt, "start:") ? 1 : NotifyEndsWithKey(txt, "end:") ? 2 : 0;
    if(!which) { s_NfPick.open = false; return; }
    if(!s_NfPick.open || s_NfPick.which != which || s_NfPick.base != txt)
    {   // a new picker: the next 5 minutes (end: 30 minutes after start:)
        int64_t from = time(nullptr) + 300;
        if(which == 2)
        {
            int pos[NOTIFY_KEYS]; NotifyFindKeys(txt + 8, pos);
            int64_t s0 = NotifyParseTime(NotifyValue(txt + 8, pos, 4).c_str(), 0);
            from = (s0 > 0 ? s0 : (int64_t)time(nullptr)) + 1800;
        }
        from = (from + 299) / 300 * 300;
        time_t ft = (time_t)from; struct tm lt; localtime_r(&ft, &lt);
        s_NfPick.h = lt.tm_hour % 12; if(!s_NfPick.h) s_NfPick.h = 12; s_NfPick.m = lt.tm_min; s_NfPick.pm = lt.tm_hour >= 12;
        s_NfPick.open = true; s_NfPick.which = which; s_NfPick.base = txt;
    }
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const NotifyTheme th = NotifyThemeNow();
    const float W = UI::S(660), H = UI::S(300), bh = UI::S(70);
    ImGui::SetNextWindowPos(ImVec2((ds.x - W) * 0.5f, ds.y * 0.06f));
    ImGui::SetNextWindowSize(ImVec2(W, H));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(UI::S(16), UI::S(12)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, UI::S(10));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, UI::S(8));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::ColorConvertU32ToFloat4(th.fill));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(th.stripe));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.08f, 0.07f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.12f, 0.10f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::ColorConvertU32ToFloat4(th.stripe));
    if(ImGui::Begin("##MPNotifyTime", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar))
    {
        ImGui::SetWindowFontScale(1.15f);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(th.tag), which == 1 ? "Start time - when it pops up" : "End time - when it goes away");
        ImGui::Spacing();
        const float sw = UI::S(70), vw = UI::S(110);
        auto stepper = [&](const char* id, int& v, int lo, int hi, int step, bool two) {
            ImGui::PushID(id);
            if(ImGui::Button("-", ImVec2(sw, bh))) { v -= step; if(v < lo) v = hi - ((lo - v - 1) % (hi - lo + 1)); }
            ImGui::SameLine();
            char b[8]; snprintf(b, sizeof(b), two ? "%02d" : "%d", v);
            ImGui::Button(b, ImVec2(vw, bh));
            ImGui::SameLine();
            if(ImGui::Button("+", ImVec2(sw, bh))) { v += step; if(v > hi) v = lo + ((v - hi - 1) % (hi - lo + 1)); }
            ImGui::PopID();
        };
        stepper("h", s_NfPick.h, 1, 12, 1, false);
        ImGui::SameLine(0, UI::S(10)); ImGui::TextUnformatted(":"); ImGui::SameLine(0, UI::S(10));
        stepper("m", s_NfPick.m, 0, 59, 5, true);
        ImGui::Spacing();
        const float hw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 3) / 4.0f;
        auto toggle = [&](const char* l, bool on) {
            if(on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(th.stripe));
            bool r = ImGui::Button(l, ImVec2(hw, bh));
            if(on) ImGui::PopStyleColor();
            return r;
        };
        if(toggle("AM", !s_NfPick.pm)) s_NfPick.pm = false;
        ImGui::SameLine(); if(toggle("PM", s_NfPick.pm)) s_NfPick.pm = true;
        ImGui::SameLine();
        if(which == 1 && ImGui::Button("Now", ImVec2(hw, bh))) { std::string t = std::string(txt) + "now "; VKeyboard::SetText(t.c_str()); }
        else if(which == 2) ImGui::Dummy(ImVec2(hw, bh));
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(th.stripe2));
        if(ImGui::Button("Set", ImVec2(hw, bh)))
        {
            char b[24]; snprintf(b, sizeof(b), "%d:%02d %s ", s_NfPick.h, s_NfPick.m, s_NfPick.pm ? "PM" : "AM");
            std::string t = std::string(txt) + b; VKeyboard::SetText(t.c_str());
        }
        ImGui::PopStyleColor();
    }
    ImGui::End();
    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar(3);
}

// ---- drawing --------------------------------------------------------------------------------------
static void NotifyRender()
{
    if(client.GetState() != NET_CONNECTED) { if(!s_Nf.empty()) NotifyReset(); s_NfPick.open = false; return; }
    if(NotifyOnServer() && s_NfSnd.load() == 0) NotifySoundFetch();   // ready before the first one
    if(s_NfSndWant)
    {
        if(MP_NowMs() > s_NfSndWant) s_NfSndWant = 0;
        else if(s_NfSnd.load() == 2) { Sound::PlayMem(0, 0.9f); s_NfSndWant = 0; }
    }
    NotifyPickerRender();
    if(s_Nf.empty()) return;
    const uint64_t now = MP_NowMs();
    for(size_t i = 0; i < s_Nf.size();) { if(now > s_Nf[i].hideAt + NOTIFY_OUT_MS) s_Nf.erase(s_Nf.begin() + i); else ++i; }
    if(Game::IsPaused() || FloatMenu::IsModal() || JoinFlowActive()) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const NotifyTheme th = NotifyThemeNow();
    ImFont* f = UI::Font();
    const float W = ImClamp(ds.x * 0.25f, UI::S(440), UI::S(640));
    const float x0 = UI::S(14), stripeW = UI::S(9), iconW = UI::S(76), pad = UI::S(12), barH = UI::S(11);
    const float fsT = UI::S(30), fsM = UI::S(25), fsS = UI::S(19);
    const float cx = x0 + stripeW + iconW, cw = W - stripeW - iconW - pad;
    float y = ds.y * 0.26f;
    const float dt = ImGui::GetIO().DeltaTime > 0 ? ImGui::GetIO().DeltaTime : 0.016f;
    int shown = 0;
    for(NotifyCard& c : s_Nf)
    {
        if(now < c.showAt) continue;
        if(shown >= NOTIFY_SHOW_MAX) break;
        ++shown;
        if(!c.popped) { c.popped = true; NotifySoundPlay(); }   // notify.mp3, once
        // height: title, bar, message (wrapped, at most 4 lines), "by" line
        const char* title = c.p.title[0] ? c.p.title : "Notification";
        ImVec2 tsz = f->CalcTextSizeA(fsT, cw, cw, title);
        ImVec2 msz = c.p.text[0] ? f->CalcTextSizeA(fsM, FLT_MAX, cw, c.p.text) : ImVec2(0, 0);
        if(msz.y > fsM * 4.2f) msz.y = fsM * 4.2f;
        const float h = pad + ImMin(tsz.y, fsT * 2.1f) + UI::S(8) + barH + (c.p.text[0] ? UI::S(8) + msz.y : 0) + UI::S(6) + fsS + pad;
        if(c.y < 0) c.y = y;
        c.y += (y - c.y) * ImMin(1.0f, dt * 12.0f);   // slide up / down when one above goes away
        // in: slide from the left with a small overshoot; out: slide back + fade
        float slide = 1.0f, alpha = 1.0f;
        if(now < c.showAt + NOTIFY_IN_MS) { float t = (float)(now - c.showAt) / NOTIFY_IN_MS; slide = NotifyEaseOutBack(t); alpha = ImMin(1.0f, t * 2.5f); }
        if(now > c.hideAt) { float t = ImMin(1.0f, (float)(now - c.hideAt) / NOTIFY_OUT_MS); slide = 1.0f - t * t; alpha = 1.0f - t; }
        const float x = x0 - (W + x0 + UI::S(20)) * (1.0f - slide);
        const float yy = c.y;
        auto A = [alpha](ImU32 col) { return (col & 0x00FFFFFF) | ((ImU32)(((col >> 24) & 0xFF) * alpha) << 24); };
        // the card
        dl->AddRectFilled(ImVec2(x + UI::S(4), yy + UI::S(5)), ImVec2(x + W + UI::S(4), yy + h + UI::S(5)), A(IM_COL32(0, 0, 0, 120)), UI::S(4));
        dl->AddRectFilled(ImVec2(x, yy), ImVec2(x + W, yy + h), A(th.fill), UI::S(4));
        dl->AddRectFilledMultiColor(ImVec2(x, yy), ImVec2(x + stripeW, yy + h), A(th.stripe), A(th.stripe), A(th.stripe2), A(th.stripe2));
        // a flash of the border when it pops up
        const float fl = now < c.showAt + 900 ? 1.0f - (float)(now - c.showAt) / 900.0f : 0.0f;
        dl->AddRect(ImVec2(x, yy), ImVec2(x + W, yy + h), A((th.stripe & 0x00FFFFFF) | ((ImU32)(60 + 195 * fl) << 24)), UI::S(4), 0, UI::S(2));
        // the logo
        const float ir = UI::S(24);
        ImVec2 ic(x + stripeW + iconW * 0.5f, yy + pad + ImMin(tsz.y, fsT * 2.1f) * 0.5f + UI::S(10));
        dl->AddCircleFilled(ic, ir * 1.45f, A((th.stripe & 0x00FFFFFF) | 0x38000000), 24);
        NotifyIcon(dl, ic, ir, c.p.logo, NotifyIconCol(c.p.logo), alpha);
        // title (outlined like the chat), bar, message
        float ty = yy + pad;
        const float o = 1.5f;
        dl->PushClipRect(ImVec2(x + cx - x0, yy), ImVec2(x + W - pad * 0.5f, yy + h), true);
        for(int ox = -1; ox <= 1; ++ox) for(int oy = -1; oy <= 1; ++oy) if(ox || oy)
            dl->AddText(f, fsT, ImVec2(x + cx - x0 + ox * o, ty + oy * o), A(IM_COL32(0, 0, 0, 230)), title, nullptr, cw);
        dl->AddText(f, fsT, ImVec2(x + cx - x0, ty), A(IM_COL32_WHITE), title, nullptr, cw);
        ty += ImMin(tsz.y, fsT * 2.1f) + UI::S(8);
        const float left = c.hideAt > c.showAt ? ImClamp((float)(c.hideAt > now ? c.hideAt - now : 0) / (float)(c.hideAt - c.showAt), 0.0f, 1.0f) : 0.0f;
        const float bx = x + cx - x0;
        dl->AddRectFilled(ImVec2(bx, ty), ImVec2(bx + cw, ty + barH), A(th.barBg), barH * 0.5f);
        if(left > 0.01f) dl->AddRectFilledMultiColor(ImVec2(bx, ty), ImVec2(bx + cw * left, ty + barH), A(th.bar), A(th.bar2), A(th.bar2), A(th.bar));
        ty += barH;
        if(c.p.text[0])
        {
            ty += UI::S(8);
            dl->PushClipRect(ImVec2(bx, ty), ImVec2(bx + cw, ty + msz.y), true);
            dl->AddText(f, fsM, ImVec2(bx + 1, ty + 1), A(IM_COL32(0, 0, 0, 200)), c.p.text, nullptr, cw);
            dl->AddText(f, fsM, ImVec2(bx, ty), A(IM_COL32(235, 235, 235, 255)), c.p.text, nullptr, cw);
            dl->PopClipRect();
            ty += msz.y;
        }
        ty += UI::S(6);
        char by[96];
        if(c.p.end) { char e[16]; NotifyFmtTime((time_t)(time(nullptr) + (int64_t)(c.hideAt > now ? (c.hideAt - now) / 1000 : 0)), e, sizeof(e)); snprintf(by, sizeof(by), "%s%s%s  -  until %s", th.tagText ? th.tagText : "", th.tagText ? "  -  " : "", AdmBareName(c.p.sender), e); }
        else snprintf(by, sizeof(by), "%s%s%s", th.tagText ? th.tagText : "", th.tagText ? "  -  " : "", AdmBareName(c.p.sender));
        dl->AddText(f, fsS, ImVec2(bx, ty), A(th.tag), by);
        dl->PopClipRect();
        y += h + UI::S(10);
    }
}
