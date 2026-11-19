// 5.02: open.mp / SA-MP crossplay, built into libMP (net/sampgate.cpp, open.mp's RakNet).
//  - "Join a PC server": libMP starts a private MP server on 127.0.0.1 (random password, 1 slot) and a
//    SampGate in the REMOTE role that joins the PC open.mp / SA-MP 0.3.7 server as a SA-MP client.
//    The phone joins that private server as usual, so every feature (chat, dialogs, textdraws, objects,
//    3D labels, other players, death / respawn) runs through the normal MP code paths.
//  - "Let PC players join": a SampGate in the SERVER role on your hosted server's SA-MP port (7777):
//    PC players (SA-MP 0.3.7 client / open.mp launcher) join it and see the phones, and the other way.
// Included by mp.cpp (needs s_Server, client, SysMsg, LeaveServer, DoConnect, s_szJoinPw, settings).

static MPServer  s_SampProxy;            // the private local server of a PC join
static SampGate  s_SampGate;             // REMOTE role (the phone on a PC server)
static SampGate  s_HostGate;             // SERVER role (PC players on the server this phone hosts)
static uint16_t  s_nSampProxyPort = 0;
static uint64_t  s_nSampStartAt = 0, s_nSampEndAt = 0, s_nSampAwayAt = 0;
static bool      s_bSampEndSaid = false;
static char      s_szPcAddr[80] = "";
static char      s_szPcPw[33] = "";
static uint64_t  s_nHostGateRetryAt = 0;

// query of the typed PC server (a thread: the UI never waits for UDP)
static std::mutex     s_PcQMutex;
static SampQueryInfo  s_PcQ;
static std::atomic<int> s_nPcQState{0};   // 0 none, 1 asking, 2 answered, 3 no answer
static char           s_szPcQAddr[80] = "";

static bool SampProxyActive() { return s_SampGate.Running(); }
static bool ConnectedToSampProxy()
{
    return s_SampGate.Running() && client.GetState() != NET_DISCONNECTED && IsLocalHost(client.GetHost()) && client.GetPort() == s_nSampProxyPort;
}

static void StopPcJoin(const char* why)
{
    if(ConnectedToSampProxy()) LeaveServer(nullptr);
    s_SampGate.Stop();
    s_SampProxy.Stop();
    s_nSampProxyPort = 0; s_nSampStartAt = s_nSampEndAt = s_nSampAwayAt = 0; s_bSampEndSaid = false;
    if(why) SysMsg(COL_YELLOW, "%s", why);
}

static void PcQueryStart(const char* addr)
{
    char h[64]; uint16_t p = 0;
    if(!SampGate::ParseAddr(addr, h, sizeof(h), &p)) { SysMsg(COL_RED, "Type the PC server as IP:port (e.g. 51.89.1.2:7777)."); return; }
    if(s_nPcQState == 1) return;
    s_nPcQState = 1; MP_CopyStr(s_szPcQAddr, addr, sizeof(s_szPcQAddr));
    std::string hs(h);
    std::thread([hs, p]() {
        SampQueryInfo qi; const bool ok = SampGate::Query(hs.c_str(), p, qi, 2000);
        { std::lock_guard<std::mutex> lk(s_PcQMutex); s_PcQ = qi; }
        s_nPcQState = ok ? 2 : 3;
    }).detach();
}

static void StartPcJoin(const char* addr, const char* pw)
{
    char h[64]; uint16_t p = 0;
    if(!SampGate::ParseAddr(addr, h, sizeof(h), &p)) { SysMsg(COL_RED, "Type the PC server as IP:port (e.g. 51.89.1.2:7777)."); return; }
    if(!LAN::HasNetwork()) { s_bHasNetwork = false; OfflineMsg(); return; }
    if(IsBusy()) { SysMsg(COL_YELLOW, "Please wait..."); return; }
    if(CheatMenuBlocks(h, false)) return;
    if(s_SampGate.Running()) StopPcJoin(nullptr);
    LeaveServer(nullptr);
    // the private server: 127.0.0.1 only matters to this phone; a random password keeps LAN players out
    char spw[MP_PASSWORD_LEN + 1]; snprintf(spw, sizeof(spw), "pc%08X", (unsigned)(MP_NowMs() * 2654435761u) ^ (unsigned)rand());
    s_SampProxy.SetPersistent(true);
    s_SampProxy.SetPassword(spw);
    char nm[64]; snprintf(nm, sizeof(nm), "open.mp %s:%u", h, p);
    bool ok = false;
    for(uint16_t port = 19700; port < 19740 && !ok; ++port)
    {
        if(s_Server.IsRunning() && s_Server.GetPort() == port) continue;
        if(s_SampProxy.Start(port, nm, ServerLog, 2, settings.region)) { ok = true; s_nSampProxyPort = port; }
    }
    if(!ok) { SysMsg(COL_RED, "Could not open the local crossplay port."); return; }
    if(!s_SampGate.StartRemote(&s_SampProxy, h, p, pw && *pw ? pw : nullptr))
    { s_SampProxy.Stop(); s_nSampProxyPort = 0; SysMsg(COL_RED, "Could not start the open.mp connection."); return; }
    MP_CopyStr(settings.pcServer, addr, sizeof(settings.pcServer)); SaveConfig();
    s_nSampStartAt = MP_NowMs(); s_nSampEndAt = s_nSampAwayAt = 0; s_bSampEndSaid = false;
    s_bConnectingDefault = false;
    MP_CopyStr(s_szJoinPw, spw, sizeof(s_szJoinPw));
    SysMsg(COL_SYSTEM, "Joining the PC server %s:%u (open.mp / SA-MP) as %s ...", h, p, SampGate::SampName(settings.name).c_str());
    JoinOverlayStart();
    DoConnect("127.0.0.1", s_nSampProxyPort);
}

// every game frame (ProcessLan)
static void CrossplayTick(uint64_t now)
{
    if(s_SampGate.Running())
    {
        if(s_SampGate.Ended())
        {
            if(!s_bSampEndSaid) { s_bSampEndSaid = true; s_nSampEndAt = now; SysMsg(COL_YELLOW, "PC server: %s", s_SampGate.Status().c_str()); }
            else if(now - s_nSampEndAt > 4000) StopPcJoin(nullptr);   // the reason stays readable for a moment
        }
        else if(now - s_nSampStartAt > 15000)
        {   // you left (or joined another server): the PC connection closes too
            if(!ConnectedToSampProxy()) { if(!s_nSampAwayAt) s_nSampAwayAt = now; else if(now - s_nSampAwayAt > 2500) StopPcJoin("Left the PC server."); }
            else s_nSampAwayAt = 0;
        }
    }
    // SERVER role: follows the server this phone hosts
    const bool want = settings.hostPcGate && s_Server.IsRunning() && !s_Server.HasBot();
    if(s_HostGate.Running() && (!want || s_HostGate.Port() != (uint16_t)settings.hostPcPort)) { s_HostGate.Stop(); s_nHostGateRetryAt = now + 500; }
    else if(want && !s_HostGate.Running() && now >= s_nHostGateRetryAt)
    {
        if(s_Server.GetPort() == settings.hostPcPort)
        { SysMsg(COL_RED, "Your server already uses port %d - pick another SA-MP port for PC players.", settings.hostPcPort); s_nHostGateRetryAt = now + 30000; return; }
        if(s_HostGate.StartServer(&s_Server, (uint16_t)settings.hostPcPort, s_Server.GetName(), std::max(1, std::min(s_Server.GetMaxPlayers(), 100))))
            SysMsg(COL_GREEN, "PC players can join \"%s\" with SA-MP 0.3.7 / open.mp on UDP port %d.", s_Server.GetName(), settings.hostPcPort);
        else { SysMsg(COL_RED, "SA-MP port %d is busy - PC crossplay is off for now.", settings.hostPcPort); s_nHostGateRetryAt = now + 30000; }
    }
}

static void OnPcAddr(const char* t, void*) { MP_CopyStr(s_szPcAddr, t ? t : "", sizeof(s_szPcAddr)); char* c = s_szPcAddr; while(*c == ' ') memmove(c, c + 1, strlen(c)); }
static void OnPcPw(const char* t, void*) { MP_CopyStr(s_szPcPw, t ? t : "", sizeof(s_szPcPw)); }
static void OnPcPort(const char* t, void*) { const int p = atoi(t ? t : ""); if(p > 0 && p < 65536) { settings.hostPcPort = p; SaveConfig(); } }

// Add server page: "open.mp / SA-MP (PC crossplay)"
static void CrossplaySection()
{
    ImGui::Separator();
    ImGui::TextUnformatted("open.mp / SA-MP  (PC crossplay)");
    ImGui::TextDisabled("Join PC servers (SA-MP 0.3.7 / open.mp) from the phone, or let PC players join your server.");
    if(!s_szPcAddr[0] && settings.pcServer[0]) MP_CopyStr(s_szPcAddr, settings.pcServer, sizeof(s_szPcAddr));
    FloatMenu::TextField("PC server  (IP:port)", s_szPcAddr, "e.g. 51.89.1.2:7777", 79, OnPcAddr);
    FloatMenu::TextField("PC server password (optional)", s_szPcPw, "Password", 32, OnPcPw, nullptr, false, true);
    if(FloatMenu::BigButton(s_nPcQState == 1 ? "Asking the server..." : "Server info")) PcQueryStart(s_szPcAddr);
    if((s_nPcQState == 2 || s_nPcQState == 3) && !strcmp(s_szPcQAddr, s_szPcAddr))
    {
        if(s_nPcQState == 3) ImGui::TextColored(kYellowTxt, "No answer from %s (offline, or the query port is blocked).", s_szPcQAddr);
        else
        {
            SampQueryInfo q; { std::lock_guard<std::mutex> lk(s_PcQMutex); q = s_PcQ; }
            ImGui::TextColored(kGreenTxt, "%s", q.hostname.c_str());
            ImGui::Text("%s  |  %d/%d players  |  ping %d  |  %s%s", q.gamemode.c_str(), q.players, q.maxPlayers, q.ping, q.version.empty() ? "SA-MP" : q.version.c_str(), q.password ? "  |  password" : "");
        }
    }
    if(s_SampGate.Running())
    {
        ImGui::TextColored(s_SampGate.Ended() ? kYellowTxt : kGreenTxt, "PC server: %s", s_SampGate.Status().c_str());
        if(FloatMenu::BigButton("Leave the PC server", kLeaveCol)) StopPcJoin("Left the PC server.");
    }
    else if(FloatMenu::BigButton("Join PC server")) StartPcJoin(s_szPcAddr, s_szPcPw);

    bool on = settings.hostPcGate;
    char lbl[96]; snprintf(lbl, sizeof(lbl), "Let PC players join my server (SA-MP port %d)", settings.hostPcPort);
    if(FloatMenu::ToggleSwitch(lbl, &on)) { settings.hostPcGate = on; SaveConfig(); }
    if(settings.hostPcGate)
    {
        char pb[8]; snprintf(pb, sizeof(pb), "%d", settings.hostPcPort);
        FloatMenu::TextField("SA-MP port (UDP)", pb, "SA-MP port", 5, OnPcPort, nullptr, true);
        if(s_HostGate.Running()) ImGui::TextColored(kGreenTxt, "%s", s_HostGate.Status().c_str());
        else if(!s_Server.IsRunning()) ImGui::TextDisabled("Starts with the server you host on this phone.");
        ImGui::TextDisabled("PC players add  your-IP:%d  in SA-MP 0.3.7 or the open.mp launcher (same Wi-Fi, or forward the UDP port).", settings.hostPcPort);
    }
}
