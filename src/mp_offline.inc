// 5.02.40: offline play - no internet, no mobile data, no router.
// Phone 1 (host) turns its Mobile Hotspot on (mobile data may be off), phone 2 joins that hotspot over Wi-Fi.
// Both are then on one local network: phone 1 hosts the MP server inside GTA SA (the server is built into
// libMP, no Termux, no separate server app), phone 2 finds it with the LAN sweep (broadcast + the /24 +
// the usual hotspot gateways 192.168.43.1 ...) and joins it by its local IP. Nothing goes over the internet.
//   Lan tab: "Offline play" card (network state, this phone's IP, the host's IP, Host / Find host and join)
//   /offline            status + how to      /offline host   host your server for the hotspot
//   /offline join [ip[:port]]   find the host on this hotspot (or connect to that IP)   /offline stop
// With no internet the launcher opens on the Lan tab (once per game start).

struct OffNet
{
    bool any = false;        // a Wi-Fi / hotspot / USB / BT tether network is up
    bool hotspot = false;    // this phone gives the hotspot (it is the host network)
    bool cellular = false;   // a mobile data link is up
    char myIp[16] = "", hostIp[16] = "", ifname[16] = "";
};
static OffNet   s_Off;
static uint64_t s_nOffAt = 0, s_nOffNoNetSince = 0;
static int      s_nOffFind = 0;          // 1 = "Find host and join" waits for the LAN sweep
static uint32_t s_nOffFindVer = 0;
static uint64_t s_nOffFindUntil = 0;

static bool OffIsApName(const char* n)
{   // interfaces Android uses for its own hotspot / tethering
    static const char* const k[] = { "ap", "swlan", "softap", "ap_br", "wigig", "rndis", "usb", "ncm", "bt-pan", "tether", "wlan1", "wlan2" };
    for(const char* p : k) if(!strncmp(n, p, strlen(p))) return true;
    return false;
}

static uint32_t OffRouteGw(const char* ifname)
{   // the gateway of that interface from /proc/net/route (= the hotspot phone); 0 = not readable
    FILE* f = fopen("/proc/net/route", "r");
    if(!f) return 0;
    char line[256], ifc[32]; unsigned dst, gw, flags; uint32_t out = 0;
    if(fgets(line, sizeof(line), f))
        while(fgets(line, sizeof(line), f))
            if(sscanf(line, "%31s %x %x %x", ifc, &dst, &gw, &flags) == 4 && !strcmp(ifc, ifname) && gw && (flags & 2))
            { out = ntohl(gw); if(!dst) break; }
    fclose(f);
    return out;
}

static void OffIp(uint32_t ip, char* out) { in_addr a; a.s_addr = htonl(ip); inet_ntop(AF_INET, &a, out, 16); }

static void OffRefresh(uint64_t now, bool force = false)
{
    if(!force && s_nOffAt && now - s_nOffAt < 2000) return;
    s_nOffAt = now;
    OffNet o;
    for(auto& f : LAN::Interfaces())
    {
        if(f.cellular) { o.cellular = true; continue; }
        const bool ap = OffIsApName(f.name) || ((f.ip & 0xFF) == 1 && f.mask >= 0xFFFFFF00u && strncmp(f.name, "wlan0", 5));
        if(o.any && (o.hotspot || !ap)) continue;   // the hotspot interface wins, else the first Wi-Fi
        o.any = true; o.hotspot = ap;
        MP_CopyStr(o.ifname, f.name, sizeof(o.ifname));
        OffIp(f.ip, o.myIp);
        if(ap) MP_CopyStr(o.hostIp, o.myIp, sizeof(o.hostIp));
        else { uint32_t gw = OffRouteGw(f.name); OffIp(gw ? gw : ((f.ip & f.mask) | 1u), o.hostIp); }
    }
    s_Off = o;
    if(Online::IsConnected()) s_nOffNoNetSince = 0;
    else if(!s_nOffNoNetSince) s_nOffNoNetSince = now;
}

// no internet (the MP lobby can't be reached) for a few seconds, but a local network is up
static bool OffNoInternet() { return s_nOffNoNetSince && !s_Off.cellular && MP_NowMs() - s_nOffNoNetSince > 12000; }

static void OffHost()
{
    OffRefresh(MP_NowMs(), true);
    if(!s_Off.any) { SysMsg(COL_RED, "Turn on your Mobile Hotspot first (mobile data can stay off), then host."); return; }
    if(s_Server.IsRunning())
    {
        SysMsg(COL_GREEN, "You already host \"%s\". Friends on your hotspot join %s:%d (Lan tab > Find host and join).", s_Server.GetName(), s_Off.myIp, s_Server.GetPort());
        return;
    }
    MP::SetHostLan(true, true);
    if(s_Off.hotspot) SysMsg(COL_SYSTEM, "Offline server on your hotspot: friends connect their Wi-Fi to it and join %s:%d.", s_Off.myIp, settings.port);
    else SysMsg(COL_SYSTEM, "Server on this Wi-Fi: friends on the same Wi-Fi join %s:%d.", s_Off.myIp, settings.port);
}

static void OffFind()
{
    OffRefresh(MP_NowMs(), true);
    if(!s_Off.any) { SysMsg(COL_RED, "Connect your Wi-Fi to the host phone's hotspot first (mobile data can stay off)."); return; }
    if(IsBusy() || s_nOffFind) { SysMsg(COL_YELLOW, "Please wait..."); return; }
    { std::lock_guard<std::mutex> lk(s_LanMutex); s_nOffFindVer = s_nNearbyVersion; }
    s_nOffFind = 1; s_nOffFindUntil = MP_NowMs() + 15000;
    s_nNearbyTime = MP_NowMs();
    SysMsg(COL_SYSTEM, "Looking for the host on this network (%s)...", s_Off.hostIp[0] ? s_Off.hostIp : "Wi-Fi");
    StartJob(JOB_SCAN, false, "", MP_DEFAULT_PORT);
}

static void OffFindDone(const std::vector<LanServer>& list)
{
    const LanServer* best = nullptr; int n = 0, oldVer = 0;
    for(auto& sv : list)
    {
        if(sv.token) continue;   // internet lobby rows: not this network
        if(s_Server.IsRunning() && sv.port == s_Server.GetPort() && (!strcmp(sv.ip, s_Off.myIp) || IsLocalHost(sv.ip))) continue;   // ourselves
        if(sv.proto != MP_PROTO_VERSION) { ++oldVer; continue; }
        ++n;
        if(!best || (!strcmp(sv.ip, s_Off.hostIp) && strcmp(best->ip, s_Off.hostIp)) || (sv.players > best->players && strcmp(best->ip, s_Off.hostIp))) best = &sv;
    }
    if(!best)
    {
        if(oldVer) SysMsg(COL_RED, "The host runs another MP version. Both phones need the same MP version (" MP_VERSION_STR ").");
        else SysMsg(COL_RED, "No MP server on this network yet. On the host phone: hotspot on, then Lan tab > Host offline server (or /offline host).");
        return;
    }
    if(best->locked)
    {
        bool saved = false;
        for(int i = 0; i < ServerList::Count(); ++i)
            if(ServerEntry* e = ServerList::Get(i)) if(e->port == best->port && !strcasecmp(e->name, best->name) && e->password[0]) saved = true;
        if(!saved) { SysMsg(COL_YELLOW, "\"%s\" (%s:%d) has a password: tap it in the Lan list and type it.", best->name, best->ip, best->port); return; }
    }
    if(n > 1) SysMsg(COL_SYSTEM, "%d servers on this network - joining \"%s\" (the others are in the Lan list).", n, best->name);
    SysMsg(COL_GREEN, "Found \"%s\" at %s:%d - joining (no internet used).", best->name, best->ip, best->port);
    char ip[16], name[32]; MP_CopyStr(ip, best->ip, sizeof(ip)); MP_CopyStr(name, best->name, sizeof(name));
    MP::JoinServer(ip, best->port, name);
}

static void OfflineTick(uint64_t now)
{
    OffRefresh(now);
    if(s_nOffFind != 1) return;
    std::vector<LanServer> list; uint32_t ver;
    { std::lock_guard<std::mutex> lk(s_LanMutex); ver = s_nNearbyVersion; if(ver != s_nOffFindVer) list = s_Nearby; }
    if(ver != s_nOffFindVer) { s_nOffFind = 0; OffFindDone(list); return; }
    if(now > s_nOffFindUntil) { s_nOffFind = 0; SysMsg(COL_RED, "The search did not finish. Tap Find host and join again."); return; }
    if(!IsSearching() && !IsBusy() && now + 13000 > s_nOffFindUntil) StartJob(JOB_SCAN, false, "", MP_DEFAULT_PORT);   // the first one was refused (another job ran)
}

static void OffHelp()
{
    SysMsg(COL_SYSTEM, "{FFA500}Offline play{FFFFFF} - no internet, no mobile data, no router needed:");
    SysMsg(COL_SYSTEM, "1. Host phone: Mobile data OFF, Mobile Hotspot ON. In GTA: Lan tab > Host offline server (/offline host).");
    SysMsg(COL_SYSTEM, "2. Friend: Mobile data OFF, Wi-Fi ON, connect to the host's hotspot. Lan tab > Find host and join (/offline join).");
    SysMsg(COL_SYSTEM, "3. Not found? /offline join 192.168.43.1 (the host's IP, shown on its Lan tab). Same MP version on both phones.");
    SysMsg(COL_SYSTEM, "Keep the host's screen on. The default and featured servers need internet (Discord login); your own server does not.");
}

static bool OfflineCommand(const char* cmd, const char* arg)
{
    if(strcmp(cmd, "offline") && strcmp(cmd, "hotspot")) return false;
    OffRefresh(MP_NowMs(), true);
    char a[80] = ""; MP_CopyStr(a, arg ? arg : "", sizeof(a));
    char* sp = strchr(a, ' '); const char* rest = "";
    if(sp) { *sp = 0; rest = sp + 1; while(*rest == ' ') ++rest; }
    if(!strcasecmp(a, "host")) OffHost();
    else if(!strcasecmp(a, "stop")) { if(s_Server.IsRunning()) MP::SetHostLan(false); else SysMsg(COL_SYSTEM, "You don't host a server."); }
    else if(!strcasecmp(a, "join"))
    {
        if(!*rest) { OffFind(); return true; }
        char ip[64]; MP_CopyStr(ip, rest, sizeof(ip));
        int port = settings.port > MP_DEFAULT_PORT ? settings.port : MP_DEFAULT_PORT + 1;
        if(char* c = strchr(ip, ':')) { *c = 0; port = atoi(c + 1); }
        if(char* c = strchr(ip, ' ')) *c = 0;
        ConnectLocalIp(ip, port);
    }
    else if(!strcasecmp(a, "help")) OffHelp();
    else
    {
        if(!s_Off.any) SysMsg(COL_YELLOW, "No Wi-Fi or hotspot is on%s.", s_Off.cellular ? " (only mobile data)" : "");
        else if(s_Off.hotspot) SysMsg(COL_GREEN, "Hotspot ON (%s): this phone is the host network, your IP %s.", s_Off.ifname, s_Off.myIp);
        else SysMsg(COL_GREEN, "Wi-Fi (%s): your IP %s, the hotspot / router is %s.", s_Off.ifname, s_Off.myIp, s_Off.hostIp);
        SysMsg(COL_SYSTEM, "Internet: %s (not needed for your own server).", Online::IsConnected() ? "yes" : "no");
        if(s_Server.IsRunning()) SysMsg(COL_SYSTEM, "You host \"%s\" on %s:%d (%d players).", s_Server.GetName(), s_Off.myIp[0] ? s_Off.myIp : "127.0.0.1", s_Server.GetPort(), s_Server.GetPlayerCount());
        SysMsg(COL_SYSTEM, "/offline host | join [ip[:port]] | stop | help");
    }
    return true;
}

// Lan tab card (mp_launcher.inc): returns its height
// 5.02.41: no orange outline, and the height follows the font: title, info line, gap, then the buttons
// (a fixed 150 px card put the info line on top of the buttons at bigger UI scales)
static float OfflineCard(float w)
{
    const float pad = UI::S(12), bh = UI::S(54), lh = ImGui::GetTextLineHeightWithSpacing();
    const float h = pad + lh * 2 + UI::S(10) + bh + pad + UI::S(6);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h - UI::S(6)), IM_COL32(16, 20, 26, 200), UI::S(6));
    const bool inet = Online::IsConnected();
    char l1[160], l2[200];
    snprintf(l1, sizeof(l1), "OFFLINE PLAY  -  hotspot / same Wi-Fi, no internet needed      Internet: %s", inet ? "yes" : "no");
    if(!s_Off.any) snprintf(l2, sizeof(l2), "No Wi-Fi / hotspot. Host: Mobile Hotspot ON. Friend: Wi-Fi ON and connect to it. Mobile data can be off.");
    else if(s_Server.IsRunning()) snprintf(l2, sizeof(l2), "You host \"%s\": friends join %s:%d  (%d players)%s", s_Server.GetName(), s_Off.myIp, s_Server.GetPort(), s_Server.GetPlayerCount(), s_Off.hotspot ? "  - hotspot ON" : "");
    else if(s_Off.hotspot) snprintf(l2, sizeof(l2), "Hotspot ON (%s) - your IP %s. Tap Host offline server, friends then tap Find host and join.", s_Off.ifname, s_Off.myIp);
    else snprintf(l2, sizeof(l2), "Wi-Fi (%s) - your IP %s, host phone %s. Tap Find host and join.", s_Off.ifname, s_Off.myIp, s_Off.hostIp);
    dl->AddText(ImVec2(p.x + pad, p.y + pad), IM_COL32(255, 165, 0, 255), l1);
    dl->PushClipRect(p, ImVec2(p.x + w - pad, p.y + pad + lh * 2), true);
    dl->AddText(ImVec2(p.x + pad, p.y + pad + lh), IM_COL32(230, 230, 230, 255), l2);
    dl->PopClipRect();
    ImGui::SetCursorScreenPos(ImVec2(p.x + pad, p.y + pad + lh * 2 + UI::S(10)));
    const float bw = (w - pad * 4) / 3;
    ImGui::PushID("offcard");
    if(ImGui::Button(s_Server.IsRunning() ? "Stop my server" : "Host offline server", ImVec2(bw, bh))) { if(s_Server.IsRunning()) MP::SetHostLan(false); else OffHost(); }
    ImGui::SameLine(0, pad);
    if(ImGui::Button(s_nOffFind ? "Searching..." : "Find host and join", ImVec2(bw, bh)) && !s_nOffFind) OffFind();
    ImGui::SameLine(0, pad);
    if(ImGui::Button("How to (chat)", ImVec2(bw, bh))) OffHelp();
    ImGui::PopID();
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
    return h;
}
