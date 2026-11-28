// 5.02.80: MP live feed -> Discord. Every player's own events on any server (joined / left / mission started or
// passed / chat) go to the MP Discord bot over the social link (public MQTT, topic wagu/feed1/e). The bot posts
// them in the live feed channel as a webhook message under the player's Discord username (no avatar, no profile).
// Only your own events are sent (never other players' chat); commands (/...) never. Off: MP/discord_feed_off.txt.
#define DCFEED_TOPIC "wagu/feed1/e"
struct DcFeedEv { std::string json; uint64_t at; };
static std::deque<DcFeedEv> s_DcFeedQ;
static uint64_t s_nDcFeedFileAt = 0, s_nDcFeedMisAt = 0;
static bool s_bDcFeedOff = false, s_bDcFeedMis = false;
static char s_szDcFeedMis[8] = "";
static std::string DcJson(const std::string& v)
{
    std::string o; o.reserve(v.size() + 8);
    for(unsigned char c : v)
    {
        if(c == '"' || c == '\\') { o += '\\'; o += (char)c; }
        else if(c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
        else o += (char)c;
    }
    return o;
}
static std::string DcStripCols(const char* s)
{   // {RRGGBB} colour codes out
    std::string o;
    for(const char* p = s ? s : ""; *p; ++p)
    {
        if(*p == '{' && strlen(p) >= 8 && p[7] == '}') { bool hex = true; for(int i = 1; i < 7; ++i) if(!isxdigit((unsigned char)p[i])) hex = false; if(hex) { p += 7; continue; } }
        o += *p;
    }
    return o;
}
static bool DcFeedOff(uint64_t now)
{
    if(!s_nDcFeedFileAt || now - s_nDcFeedFileAt > 10000)
    {
        s_nDcFeedFileAt = now;
        char p[512]; snprintf(p, sizeof(p), "%s/discord_feed_off.txt", s_szDataDir);
        s_bDcFeedOff = access(p, F_OK) == 0;
    }
    return s_bDcFeedOff;
}
static void DcFeed(const char* type, const char* text)
{
    const uint64_t now = MP_NowMs();
    if(DcFeedOff(now)) return;
    std::string disc = !s_Auth.discordName.empty() ? s_Auth.discordName : AuthFileValue("auth_service.txt", "discord");
    const std::string user = !s_AuthUser.empty() ? s_AuthUser : std::string();
    char id[24]; snprintf(id, sizeof(id), "%08x%08x", (unsigned)(now & 0xFFFFFFFFu), (unsigned)lrand48());
    std::string j = "{\"v\":1,\"id\":\"" + std::string(id) + "\",\"t\":\"" + type + "\",\"name\":\"" + DcJson(DcStripCols(settings.name)) +
        "\",\"user\":\"" + DcJson(user) + "\",\"disc\":\"" + DcJson(disc) + "\",\"srv\":\"" + DcJson(DcStripCols(s_szCurServer)) +
        "\",\"msg\":\"" + DcJson(DcStripCols(text ? text : "")) + "\",\"ver\":\"" MP_VERSION_STR "\"}";
    if(s_DcFeedQ.size() >= 32) s_DcFeedQ.pop_front();
    s_DcFeedQ.push_back({ j, now });
}
static void DcFeedTick(uint64_t now)
{
    if(!s_DcFeedQ.empty() && Online::SocLinkUp())
    {
        int n = 0;
        while(!s_DcFeedQ.empty() && n++ < 4)
        {   // older than 2 min (the link was down): dropped, a late "joined" is wrong
            if(now - s_DcFeedQ.front().at < 120000) Online::SocLinkPub(DCFEED_TOPIC, s_DcFeedQ.front().json, false);
            s_DcFeedQ.pop_front();
        }
    }
    // missions (story / co-op / gamemode missions on the server)
    if(client.GetState() == NET_CONNECTED && Game::IsReady() && now - s_nDcFeedMisAt > 2000)
    {
        s_nDcFeedMisAt = now;
        bool on = false, cut = false; char nm[8] = "";
        Game::GetMissionState(on, cut, nm);
        if(on != s_bDcFeedMis)
        {
            char t[64];
            if(on) { snprintf(t, sizeof(t), "started a mission%s%s", nm[0] ? ": " : "", nm); MP_CopyStr(s_szDcFeedMis, nm, sizeof(s_szDcFeedMis)); }
            else snprintf(t, sizeof(t), "is out of the mission%s%s", s_szDcFeedMis[0] ? " " : "", s_szDcFeedMis);
            s_bDcFeedMis = on;
            DcFeed("mission", t);
        }
    }
    else if(client.GetState() != NET_CONNECTED) s_bDcFeedMis = false;
}
