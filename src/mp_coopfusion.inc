// 5.02.79: CoopFusion32 crossplay, built into libMP (net/coopgate.cpp). The CoopFusion32 mod is not changed.
// "Join a co-op server": libMP starts a private MP server on 127.0.0.1 (random password, 2 slots) and a CoopGate
// that joins the CoopFusion32 server (TCP, default port 6767) as a co-op client. The phone joins the private server
// as usual, so the co-op players are MP remote players on it (nametags, kill feed, damage) and the phone's
// movement / shots / hits go to the co-op server. AML Multiplayer's Co-op tab joins through here
// (christe_join = "coop|<host>|<port>|<name>").
// Included by mp.cpp after mp_crossplay.inc (needs s_Server, SysMsg, LeaveServer, DoConnect, s_szJoinPw, settings).

static MPServer  s_CoopProxy;
static CoopGate  s_CoopGate;
static uint16_t  s_nCoopProxyPort = 0;
static uint64_t  s_nCoopStartAt = 0, s_nCoopEndAt = 0, s_nCoopAwayAt = 0;
static bool      s_bCoopEndSaid = false, s_bCoopJoinedSaid = false;
static char      s_szCoopAddr[96] = "";

static bool ConnectedToCoopProxy()
{
    return s_nCoopProxyPort && client.GetState() != NET_DISCONNECTED && IsLocalHost(client.GetHost()) && client.GetPort() == s_nCoopProxyPort;
}
static void StopCoopJoin(const char* why)
{
    if(ConnectedToCoopProxy()) LeaveServer(nullptr);
    s_nCoopProxyPort = 0; s_nCoopStartAt = s_nCoopEndAt = s_nCoopAwayAt = 0; s_bCoopEndSaid = s_bCoopJoinedSaid = false;
    if(why) SysMsg(COL_YELLOW, "%s", why);
    if(!s_CoopGate.Running() && !s_CoopProxy.IsRunning()) { s_CoopGate.Stop(); return; }
    if(logger) logger->Info("[coop] leaving the CoopFusion32 server %s", s_szCoopAddr);
    s_CoopGate.Stop();    // a TCP socket + its thread: closes at once
    s_CoopProxy.Stop();
}

static void StartCoopJoin(const char* host, int port, const char* label)
{
    if(!host || !*host) { SysMsg(COL_RED, "No co-op server address."); return; }
    if(port <= 0 || port > 65535) port = 6767;
    if(!LAN::HasNetwork() && strcmp(host, "127.0.0.1")) { s_bHasNetwork = false; OfflineMsg(); return; }
    if(IsBusy()) { SysMsg(COL_YELLOW, "Please wait..."); return; }
    if(s_SampGate.Running()) StopPcJoin(nullptr);
    if(s_CoopGate.Running() || s_CoopProxy.IsRunning()) StopCoopJoin(nullptr);
    LeaveServer(nullptr);
    char spw[MP_PASSWORD_LEN + 1]; snprintf(spw, sizeof(spw), "cf%08X", (unsigned)(MP_NowMs() * 2654435761u) ^ (unsigned)rand());
    s_CoopProxy.SetPersistent(true);
    s_CoopProxy.SetPassword(spw);
    char nm[64]; snprintf(nm, sizeof(nm), "Co-op %s", label && *label ? label : host);
    bool ok = false;
    for(uint16_t p = 19760; p < 19800 && !ok; ++p)
    {
        if(s_Server.IsRunning() && s_Server.GetPort() == p) continue;
        if(s_CoopProxy.Start(p, nm, ServerLog, 2, settings.region)) { ok = true; s_nCoopProxyPort = p; }
    }
    if(!ok) { SysMsg(COL_RED, "Could not open the local co-op port."); return; }
    if(!s_CoopGate.Start(&s_CoopProxy, host, (uint16_t)port, settings.name))
    { s_CoopProxy.Stop(); s_nCoopProxyPort = 0; SysMsg(COL_RED, "Could not start the co-op connection."); return; }
    snprintf(s_szCoopAddr, sizeof(s_szCoopAddr), "%s:%d", host, port);
    s_nCoopStartAt = MP_NowMs(); s_nCoopEndAt = s_nCoopAwayAt = 0; s_bCoopEndSaid = s_bCoopJoinedSaid = false;
    s_bConnectingDefault = false;
    MP_CopyStr(s_szJoinPw, spw, sizeof(s_szJoinPw));
    if(logger) logger->Info("[coop] joining the CoopFusion32 server %s as %s", s_szCoopAddr, settings.name);
    SysMsg(COL_SYSTEM, "Joining the co-op server %s (CoopFusion32 crossplay) ...", s_szCoopAddr);
    JoinOverlayStart();
    DoConnect("127.0.0.1", s_nCoopProxyPort);
}

// every game frame (from CrossplayTick)
static void CoopFusionTick(uint64_t now)
{
    if(!s_nCoopProxyPort && !s_CoopGate.Running()) return;
    if(s_CoopGate.Joined() && !s_bCoopJoinedSaid && ConnectedToCoopProxy())
    {
        s_bCoopJoinedSaid = true;
        SysMsg(COL_GREEN, "On the co-op server %s: %d co-op player(s). Combat is synced with CoopFusion32 players.", s_szCoopAddr, s_CoopGate.Players());
    }
    if(s_CoopGate.Ended() || (!s_CoopGate.Running() && s_nCoopProxyPort))
    {
        if(!s_bCoopEndSaid) { s_bCoopEndSaid = true; s_nCoopEndAt = now; SysMsg(COL_YELLOW, "Co-op server: %s", s_CoopGate.Status().c_str()); }
        else if(now - s_nCoopEndAt > 4000) StopCoopJoin(nullptr);
        return;
    }
    if(now - s_nCoopStartAt > 15000)
    {   // you left (or joined another server): the co-op connection closes too
        if(!ConnectedToCoopProxy()) { if(!s_nCoopAwayAt) s_nCoopAwayAt = now; else if(now - s_nCoopAwayAt > 2500) StopCoopJoin("Left the co-op server."); }
        else s_nCoopAwayAt = 0;
    }
}
