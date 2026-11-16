// 4.13.63: VICE Loader bridge + VICE City LAN server (included by vc_layer.cpp inside namespace VC, before Process)
//
// VICE Loader (viceloader/, com.thirtyfoxmc.viceloader) is a separate app that draws the San Andreas MP launcher
// (server list, chat text field, virtual keyboard, players, settings, about) as a "display over other apps" overlay
// on top of GTA Vice City. It talks to this file over UDP on 127.0.0.1 (the phone itself, nothing leaves it):
//
//   overlay -> game  (port VC_OVERLAY_PORT)          game -> overlay (the address the last HELLO came from)
//     HELLO  (first packet: the chat so far is sent back once)  STATE <online 0/1> <port> <myid> <host> \t <name> \t <server name>
//     CHAT <text>       (/commands too)                 CHAT <colour hex, ABGR like Chat()> <text>
//     JOIN <port> <host> \t <password>                  PLAYERS <id>,<score>,<ping>,<name>\n...   (every second)
//     NAME <name>                                       LAN <0/1> <port> <players>/<max> <name>
//     LEAVE                                             BYE
//     LANSTART <port> <maxPlayers> <name>  (the VICE City LAN server on this phone)
//     LANSTOP
//     MIC    (4.13.64: the mic pill - voice chat on / off)  MIC <0 off-server, 1 off, 2 on, 3 talking>  (every second)
//     PING   (keep-alive every 3 s; the game stops sending 15 s after the last packet)
//
// The VICE City LAN server is MP's own server (net/server.cpp) in Vice City mode (SetGame(MP_GAME_VC)): only
// Vice City phones can join it, it answers LAN discovery like the San Andreas LAN server, but it is a different
// server (own port, default 7777, own name "VICE City LAN server", own files vclan_*.txt) - never the SA one.
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#define VC_OVERLAY_PORT 47790
#define VC_LAN_DEFAULT_PORT 7777
#define VC_LAN_DEFAULT_NAME "VICE City LAN server"

static int         s_nOvSock = -1;
static sockaddr_in s_OvPeer;
static bool        s_bOvPeer = false;
static uint64_t    s_nOvLastHello = 0, s_nOvNextState = 0;
static MPServer    s_VcLan;            // the VICE City LAN server (one per phone, separate from San Andreas hosting)
static bool        s_bVcLanJoin = false;

static void OvSend(const char* fmt, ...)
{
    if(s_nOvSock < 0 || !s_bOvPeer) return;
    if(MP_NowMs() - s_nOvLastHello > 15000) { s_bOvPeer = false; return; }   // the overlay closed
    char buf[1400]; va_list ap; va_start(ap, fmt); int n = vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    if(n <= 0) return;
    if(n >= (int)sizeof(buf)) n = sizeof(buf) - 1;
    sendto(s_nOvSock, buf, n, MSG_DONTWAIT, (sockaddr*)&s_OvPeer, sizeof(s_OvPeer));
}
static void OverlayChat(uint32_t color, const char* text) { OvSend("CHAT %08X %s", color, text); }

static void OvOpen()
{
    if(s_nOvSock >= 0) return;
    static uint64_t s_nTried = 0;
    if(MP_NowMs() - s_nTried < 5000) return;
    s_nTried = MP_NowMs();
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if(s < 0) return;
    int one = 1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in a; memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET; a.sin_port = htons(VC_OVERLAY_PORT); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if(bind(s, (sockaddr*)&a, sizeof(a)) != 0) { close(s); return; }
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
    s_nOvSock = s;
    logger->Info("Vice City: VICE Loader bridge on 127.0.0.1:%d", VC_OVERLAY_PORT);
}

static void VcLanLog(const char* msg) { logger->Info("[VICE City LAN] %s", msg); }
static void VcLanState()
{
    OvSend("LAN %d %d %d/%d %s", s_VcLan.IsRunning() ? 1 : 0, s_VcLan.IsRunning() ? (int)s_VcLan.GetPort() : 0,
           s_VcLan.IsRunning() ? s_VcLan.GetPlayerCount() : 0, s_VcLan.IsRunning() ? s_VcLan.GetMaxPlayers() : 0,
           s_VcLan.IsRunning() ? s_VcLan.GetName() : VC_LAN_DEFAULT_NAME);
}
static bool StartVcLan(const char* name, int port, int maxPlayers)
{
    if(!name || !name[0]) name = VC_LAN_DEFAULT_NAME;
    if(port <= 0 || port > 65535) port = VC_LAN_DEFAULT_PORT;
    if(maxPlayers < 2 || maxPlayers > MP_STD_MAX_PLAYERS) maxPlayers = 50;
    if(s_VcLan.IsRunning()) s_VcLan.Stop();
    s_VcLan.SetGame(MP_GAME_VC);
    s_VcLan.SetPersistent(true);
    s_VcLan.SetPassword("");
    {
        char f[640];
        snprintf(f, sizeof(f), "%s/vclan_moderators.txt", s_szDataDir); s_VcLan.SetModFile(f);
        snprintf(f, sizeof(f), "%s/vclan_vcmp.ini", s_szDataDir);
        if(!s_VcLan.VcmpLoad(f)) logger->Info("[VICE City LAN] no %s: free roam", f);
    }
    s_VcLan.SetDetails("", 0, "Freeroam", MP_GAME_VC_TAG ",lan", "Vice City LAN server hosted by a phone (VICE Loader)");
    if(!s_VcLan.Start((uint16_t)port, name, VcLanLog, maxPlayers))
    {
        Chat(0xFF6060FF, "VICE City LAN server: port %d is busy", port);
        return false;
    }
    Chat(0xFF80FF80, "VICE City LAN server \"%s\" is up on port %d (Wi-Fi / hotspot)", name, port);
    // join it ourselves
    MP_CopyStr(cfg.host, "127.0.0.1", sizeof(cfg.host)); cfg.port = port; cfg.password[0] = 0;
    SaveSettings();
    if(s_Client.GetState() != NET_DISCONNECTED) s_Client.Disconnect(true);
    DropAll();
    s_bManualOff = false; s_bJoinTried = false; s_nNextJoin = 0; s_bVcLanJoin = true;
    VcLanState();
    return true;
}
static void StopVcLan()
{
    if(!s_VcLan.IsRunning()) return;
    if(s_bVcLanJoin && s_Client.GetState() != NET_DISCONNECTED) s_Client.Disconnect(true);
    DropAll();
    s_VcLan.Stop(); s_bVcLanJoin = false;
    Chat(0xFFC0C0C0, "VICE City LAN server stopped");
    VcLanState();
}

static void OvState()
{
    OvSend("STATE %d %d %d %s\t%s\t%s", (s_Client.GetState() == NET_CONNECTED && s_nMyId >= 0) ? 1 : 0, cfg.port, s_nMyId,
           cfg.host, cfg.name, s_Client.GetServerName());
}
static void OvPlayers()
{
    std::string out = "PLAYERS ";
    char b[96];
    snprintf(b, sizeof(b), "%d,0,0,%s\n", s_nMyId, cfg.name); out += b;
    for(int i = 0; i < 256; ++i)
    {
        const Remote& r = s_Players[i];
        if(!r.used || i == s_nMyId) continue;
        snprintf(b, sizeof(b), "%d,%d,%d,%s\n", i, r.score, r.ping, r.name);
        if(out.size() + strlen(b) > 1300) break;
        out += b;
    }
    OvSend("%s", out.c_str());
}

static void SendChatText(const char* text);
static int  VoiceMicState();    // vc_gui.inc (4.13.64: the mic pill of VICE Loader's floating bar)
static void VoiceMicToggle();   // vc_gui.inc
static void OvHandle(char* m, bool fresh)
{
    if(!strncmp(m, "PING", 4)) { if(!fresh) return; }   // keep-alive (a new overlay gets the state below)
    else if(!strncmp(m, "HELLO", 5))
    {
        if(!fresh) { OvState(); VcLanState(); return; }   // the same overlay again: no second copy of the chat
        OvState(); VcLanState();
        for(auto& c : s_Chat) OverlayChat(c.color, c.text.c_str());
    }
    else if(!strncmp(m, "CHAT ", 5))
    {
        if(s_Client.GetState() == NET_CONNECTED && s_nMyId >= 0) SendChatText(m + 5);
        else Chat(0xFFC0C0C0, "Not on a server yet - join one in VICE Loader first");
    }
    else if(!strncmp(m, "JOIN ", 5))
    {
        int port = atoi(m + 5); char* p = strchr(m + 5, ' ');
        if(!p) return;
        ++p; char* tab = strchr(p, '\t'); const char* pass = "";
        if(tab) { *tab = 0; pass = tab + 1; }
        Trim(p);
        if(!p[0]) return;
        MP_CopyStr(cfg.host, p, sizeof(cfg.host)); cfg.port = port > 0 && port < 65536 ? port : 7777;
        MP_CopyStr(cfg.password, pass, sizeof(cfg.password));
        SaveSettings();
        Chat(0xFF80FF80, "Joining %s:%d", cfg.host, cfg.port);
        if(s_Client.GetState() != NET_DISCONNECTED) s_Client.Disconnect(true);
        DropAll();
        s_bManualOff = false; s_bJoinTried = false; s_nNextJoin = 0;
    }
    else if(!strncmp(m, "NAME ", 5))
    {
        char nb[MP_NAME_LEN]; MP_CopyStr(nb, m + 5, sizeof(nb)); Trim(nb);
        if(nb[0] && strcmp(nb, cfg.name))
        {
            MP_CopyStr(cfg.name, nb, sizeof(cfg.name)); SaveSettings();
            if(s_Client.GetState() != NET_DISCONNECTED) { s_Client.Disconnect(true); DropAll(); s_bJoinTried = false; s_nNextJoin = 0; }
        }
    }
    else if(!strncmp(m, "LEAVE", 5))
    {
        if(s_Client.GetState() != NET_DISCONNECTED) s_Client.Disconnect(true);
        DropAll(); s_bManualOff = true;
        Chat(0xFFC0C0C0, "Disconnected (VICE Loader)");
    }
    else if(!strncmp(m, "LANSTART ", 9))
    {
        int port = 0, maxp = 0, off = 0;
        if(sscanf(m + 9, "%d %d %n", &port, &maxp, &off) >= 2) StartVcLan(m + 9 + off, port, maxp);
    }
    else if(!strncmp(m, "LANSTOP", 7)) StopVcLan();
    else if(!strncmp(m, "MIC", 3)) { VoiceMicToggle(); OvSend("MIC %d", VoiceMicState()); }   // 4.13.64: VICE Loader mic pill
    OvState();
}

static void OverlayProcess()
{
    OvOpen();
    if(s_nOvSock < 0) return;
    for(int k = 0; k < 16; ++k)
    {
        char buf[1024]; sockaddr_in from; socklen_t fl = sizeof(from);
        int n = recvfrom(s_nOvSock, buf, sizeof(buf) - 1, 0, (sockaddr*)&from, &fl);
        if(n <= 0) break;
        if(from.sin_addr.s_addr != htonl(INADDR_LOOPBACK)) continue;   // the phone itself only
        buf[n] = 0;
        while(n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
        const bool fresh = !s_bOvPeer || from.sin_port != s_OvPeer.sin_port || MP_NowMs() - s_nOvLastHello > 15000;
        s_OvPeer = from; s_bOvPeer = true; s_nOvLastHello = MP_NowMs();
        OvHandle(buf, fresh);
    }
    const uint64_t now = MP_NowMs();
    if(s_bOvPeer && now >= s_nOvNextState) { s_nOvNextState = now + 1000; OvState(); OvPlayers(); VcLanState(); OvSend("MIC %d", VoiceMicState()); }
}
