// VICE Loader: the San Andreas MP launcher (src/mp_launcher.inc) rebuilt for GTA Vice City as a "display over other
// apps" overlay. Same look (vl_kit.inc is copied from the SA launcher) and the same pages as San Andreas MP: server list
// (Internet / Featured / Favorites / Lan), Add server, Chat (text field + MP virtual keyboard), Players, Voice chat,
// MonetLoader, Help, Mods, Settings (Player / Display / Chat / Sound / Connection) and About.
// No login / authentication: CONNECT joins directly.
//
// 4.13.64: the floating bar is the San Andreas one ([⋮] [chat] [mic] [MonetLoader], OverlayService.java); ⋮ opens
// the SA dropdown (Help / Server / Player list) drawn here (VL_MODE_MENU). The in-app VICE City LAN server page is
// gone: the VICE City LAN server runs 24/7 (server/run_24_7.sh port 7777, GitHub Actions) and is listed in Internet
// through the online lobby, like the 24/7 servers of San Andreas. Joining a lobby server goes through Online's relay
// proxy on 127.0.0.1 (or straight to the 24/7 machine when it answers over direct UDP).
//
// It drives the game through the VICE Loader bridge (src/vc/vc_overlay.inc, UDP 127.0.0.1:47790).
#include "ui_common.h"
#include "vkeyboard.h"
#include "emoji.h"
#include "vl.h"
#include "net/protocol.h"
#include "net/server.h"     // MP_NowMs
#include "net/serverlist.h"
#include "net/lan.h"
#include "net/online.h"
#include "imgui_internal.h"
#include <mod/logger.h>
#include <float.h>
#include <math.h>
#include <time.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <atomic>
#include <deque>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>

#include "hud.h"
#include "vl_kit.inc"
#include "server_style.h"   // 4.13.64: country flags (flags.png) + gamemode colours of the San Andreas server list

// ======================================================================= prefs (files/viceloader.ini)
struct VlPrefs
{
    char  nick[MP_NAME_LEN] = "Player";
    float opacity = 1.0f, scale = 1.0f;       // Display
    float dim = 0.6f;                         // Display: how dark the game is behind the launcher
    bool  showBar = true;                     // Display: the floating bar (off = only the notification opens VICE Loader)
    float barSize = 1.0f;                     // Display: floating bar size
    float chatSize = 1.0f;                    // Chat: text size on the Chat page
    bool  chatTime = true;                    // Chat: time in front of every line
    bool  chatColors = true;                  // Chat: {RRGGBB} colours + coloured player names
    int   chatKeep = 200;                     // Chat: lines kept
    bool  click = true;                       // Sound: global_click when a button is pressed (like San Andreas MP)
    float clickVol = 0.8f;
    bool  showSA = false;                     // Connection: San Andreas servers in the lists too (the game refuses them)
    bool  lobbyAuto = true;                   // Connection: lowest-ping lobby region
    int   region = REGION_ASIA;
};
static VlPrefs s_P;
static std::string PrefPath() { return std::string(VL_FilesDir()) + "/viceloader.ini"; }
static void PrefsSave()
{
    FILE* f = fopen(PrefPath().c_str(), "w"); if(!f) return;
    fprintf(f, "nick=%s\nopacity=%.2f\nscale=%.2f\ndim=%.2f\nshowBar=%d\nbarSize=%.2f\nchatSize=%.2f\nchatTime=%d\nchatColors=%d\nchatKeep=%d\n"
               "click=%d\nclickVol=%.2f\nshowSA=%d\nlobbyAuto=%d\nregion=%d\n",
            s_P.nick, s_P.opacity, s_P.scale, s_P.dim, s_P.showBar ? 1 : 0, s_P.barSize, s_P.chatSize, s_P.chatTime ? 1 : 0, s_P.chatColors ? 1 : 0,
            s_P.chatKeep, s_P.click ? 1 : 0, s_P.clickVol, s_P.showSA ? 1 : 0, s_P.lobbyAuto ? 1 : 0, s_P.region);
    fclose(f);
    char b[64];
    snprintf(b, sizeof(b), "%d %.2f %.2f", s_P.showBar ? 1 : 0, s_P.barSize, s_P.click ? s_P.clickVol : 0.0f);
    VL_JavaCall("prefs", b);   // the floating bar + the click sound live in Java
}
static void PrefsLoad()
{
    FILE* f = fopen(PrefPath().c_str(), "r");
    if(f)
    {
        char l[256];
        while(fgets(l, sizeof(l), f))
        {
            l[strcspn(l, "\r\n")] = 0; char* v = strchr(l, '='); if(!v) continue; *v++ = 0;
            if(!strcmp(l, "nick") && v[0]) MP_CopyStr(s_P.nick, v, sizeof(s_P.nick));
            else if(!strcmp(l, "opacity")) s_P.opacity = (float)atof(v);
            else if(!strcmp(l, "scale")) s_P.scale = (float)atof(v);
            else if(!strcmp(l, "dim")) s_P.dim = (float)atof(v);
            else if(!strcmp(l, "showBar")) s_P.showBar = atoi(v) != 0;
            else if(!strcmp(l, "barSize")) s_P.barSize = (float)atof(v);
            else if(!strcmp(l, "chatSize")) s_P.chatSize = (float)atof(v);
            else if(!strcmp(l, "chatTime")) s_P.chatTime = atoi(v) != 0;
            else if(!strcmp(l, "chatColors")) s_P.chatColors = atoi(v) != 0;
            else if(!strcmp(l, "chatKeep")) s_P.chatKeep = atoi(v);
            else if(!strcmp(l, "click")) s_P.click = atoi(v) != 0;
            else if(!strcmp(l, "clickVol")) s_P.clickVol = (float)atof(v);
            else if(!strcmp(l, "showSA")) s_P.showSA = atoi(v) != 0;
            else if(!strcmp(l, "lobbyAuto")) s_P.lobbyAuto = atoi(v) != 0;
            else if(!strcmp(l, "region")) s_P.region = atoi(v);
        }
        fclose(f);
    }
    s_P.opacity = ImClamp(s_P.opacity, 0.4f, 1.0f); s_P.scale = ImClamp(s_P.scale, 0.6f, 1.4f); s_P.dim = ImClamp(s_P.dim, 0.0f, 0.95f);
    s_P.barSize = ImClamp(s_P.barSize, 0.6f, 1.6f); s_P.chatSize = ImClamp(s_P.chatSize, 0.6f, 1.6f); s_P.chatKeep = ImClamp(s_P.chatKeep, 20, 500);
    s_P.clickVol = ImClamp(s_P.clickVol, 0.0f, 1.0f); if(s_P.region < 0 || s_P.region >= REGION_COUNT) s_P.region = REGION_ASIA;
}
float VL_UserScale() { return s_P.scale; }

// ======================================================================= bridge to the game (vc_overlay.inc)
struct VlChat { uint32_t abgr; std::string text; time_t at; };
struct VlPlayer { int id, score, ping; uint32_t color; int health, armour, flags; std::string name; };   // 4.13.64: + colour / HP / armour / flags (PLAYERS2)
static std::atomic<int> s_nSock(-1);
static std::atomic<uint64_t> s_nGameSeen(0);
static bool s_bGameOnline = false; static int s_nGamePort = 0, s_nMyId = -1, s_nMic = 0;
static std::string s_GameHost, s_GameName, s_GameServer;
static std::deque<VlChat> s_Chat;
static std::vector<VlPlayer> s_Players;
static bool GameLinked() { uint64_t g = s_nGameSeen.load(); return g && MP_NowMs() - g < 5000; }

static void BrSend(const char* fmt, ...)
{
    const int s = s_nSock.load(); if(s < 0) return;
    char b[1024]; va_list ap; va_start(ap, fmt); int n = vsnprintf(b, sizeof(b), fmt, ap); va_end(ap);
    if(n <= 0) return; if(n >= (int)sizeof(b)) n = sizeof(b) - 1;
    sockaddr_in a; memset(&a, 0, sizeof(a)); a.sin_family = AF_INET; a.sin_port = htons(VL_BRIDGE_PORT); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sendto(s, b, n, MSG_DONTWAIT, (sockaddr*)&a, sizeof(a));
}
static void BrOpen()
{
    if(s_nSock.load() >= 0) return;
    int s = socket(AF_INET, SOCK_DGRAM, 0); if(s < 0) return;
    sockaddr_in a; memset(&a, 0, sizeof(a)); a.sin_family = AF_INET; a.sin_port = 0; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if(bind(s, (sockaddr*)&a, sizeof(a)) != 0) { close(s); return; }
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
    int big = 512 * 1024; setsockopt(s, SOL_SOCKET, SO_RCVBUF, &big, sizeof(big));   // the chat waits here while nothing draws
    s_nSock = s;
}
// 4.13.64: the keep-alive runs on its own thread. Before, HELLO / PING were only sent while the overlay drew a frame,
// so when Android slowed the hidden overlay down the game stopped sending after 15 s and the chat "stopped".
static void BrKeepAlive()
{
    std::thread([] {
        for(;;)
        {
            BrOpen();
            BrSend(GameLinked() ? "PING" : "HELLO");
            usleep(GameLinked() ? 3000 * 1000 : 2000 * 1000);
        }
    }).detach();
}
static void AddChat(uint32_t abgr, const std::string& t)
{
    s_Chat.push_back({ abgr, UI::EmojiToPUA(t.c_str()), time(nullptr) });
    while((int)s_Chat.size() > s_P.chatKeep) s_Chat.pop_front();
}
static void BrHandle(char* m)
{
    if(!strncmp(m, "STATE ", 6))
    {
        int on = 0, port = 0, id = -1, off = 0;
        if(sscanf(m + 6, "%d %d %d %n", &on, &port, &id, &off) < 3) return;
        char* p = m + 6 + off; char* t1 = strchr(p, '\t'); char* t2 = t1 ? strchr(t1 + 1, '\t') : nullptr;
        if(t1) *t1 = 0; if(t2) *t2 = 0;
        s_bGameOnline = on != 0; s_nGamePort = port; s_nMyId = id;
        s_GameHost = p; s_GameName = t1 ? t1 + 1 : ""; s_GameServer = t2 ? t2 + 1 : "";
    }
    else if(!strncmp(m, "CHAT ", 5))
    {
        uint32_t c = (uint32_t)strtoul(m + 5, nullptr, 16); const char* t = strchr(m + 5, ' ');
        AddChat(c, t ? t + 1 : "");
    }
    else if(!strncmp(m, "PLAYERS ", 8))
    {
        s_Players.clear();
        for(char* l = strtok(m + 8, "\n"); l; l = strtok(nullptr, "\n"))
        {
            VlPlayer pl{}; int off = 0;
            pl.color = 0xFFFFFFFF;
            if(sscanf(l, "%d,%d,%d,%n", &pl.id, &pl.score, &pl.ping, &off) >= 3) { pl.name = l + off; s_Players.push_back(pl); }
        }
    }
    else if(!strncmp(m, "PLAYERS2 ", 9))
    {   // 4.13.64: id,score,ping,colour,hp,armour,flags,name
        s_Players.clear();
        for(char* l = strtok(m + 9, "\n"); l; l = strtok(nullptr, "\n"))
        {
            VlPlayer pl{}; int off = 0; unsigned col = 0xFFFFFFFF;
            if(sscanf(l, "%d,%d,%d,%x,%d,%d,%d,%n", &pl.id, &pl.score, &pl.ping, &col, &pl.health, &pl.armour, &pl.flags, &off) >= 7 && off > 0)
            { pl.color = col; pl.name = l + off; s_Players.push_back(pl); }
        }
    }
    else if(!strncmp(m, "MIC ", 4)) { s_nMic = atoi(m + 4); VL_JavaCall("mic", m + 4); }
    else if(!strncmp(m, "BYE", 3)) s_nGameSeen = 0;
}
static void BrPoll()
{
    BrOpen(); const int s = s_nSock.load(); if(s < 0) return;
    for(int k = 0; k < 64; ++k)
    {
        char b[1500]; sockaddr_in from; socklen_t fl = sizeof(from);
        int n = recvfrom(s, b, sizeof(b) - 1, 0, (sockaddr*)&from, &fl);
        if(n <= 0) break;
        if(from.sin_addr.s_addr != htonl(INADDR_LOOPBACK) || ntohs(from.sin_port) != VL_BRIDGE_PORT) continue;
        b[n] = 0; s_nGameSeen = MP_NowMs(); BrHandle(b);
    }
}

// ======================================================================= server lists
struct VlRow
{
    int kind;            // 0 saved, 1 found on the LAN, 2 online lobby (internet / 24/7)
    int idx;             // saved: ServerList index
    char name[32], host[64], mode[MP_MODE_LEN], tags[MP_TAGS_LEN], pass[MP_PASSWORD_LEN + 1], desc[MP_DESC_LEN];
    int port, players, maxPlayers, ping; bool online, locked, fav, vc, dedicated; uint32_t token;
    char cc[MP_CC_LEN], lang[MP_LANG_LEN]; int modeCol, region;   // 4.13.64: Location flag, Language, gamemode colour, lobby region
};
static std::mutex s_LanMx;
static std::vector<LanServer> s_Lan;
static std::atomic<bool> s_bScanning(false);
static uint64_t s_nScanAt = 0;
static void ScanLan()
{
    if(s_bScanning) return;
    s_bScanning = true; s_nScanAt = MP_NowMs();
    std::thread([] {
        std::vector<uint16_t> ports = { VL_LAN_PORT, MP_DEFAULT_PORT, 7778, 8192 };
        std::vector<LanServer> got = LAN::Discover(ports, 1500, true);
        { std::lock_guard<std::mutex> lk(s_LanMx); s_Lan = got; }
        s_bScanning = false;
    }).detach();
}
static bool IsVcTags(const char* t) { return t && !strncasecmp(t, MP_GAME_VC_TAG, 2) && (t[2] == 0 || t[2] == ','); }
// d/<token> "v1|cc|modeCol|mode|tags|desc|players" (the SA launcher's LParseDetails)
static void ParseDetails(const std::string& d, VlRow& r)
{
    if(d.compare(0, 3, "v1|")) return;
    std::vector<std::string> f; size_t at = 3;
    for(int i = 0; i < 6; ++i) { size_t e = i < 5 ? d.find('|', at) : std::string::npos; f.push_back(d.substr(at, e == std::string::npos ? std::string::npos : e - at)); if(e == std::string::npos) break; at = e + 1; }
    while(f.size() < 6) f.push_back("");
    if(!f[0].empty()) MP_CopyStr(r.cc, f[0].c_str(), sizeof(r.cc));
    if(atoi(f[1].c_str())) r.modeCol = atoi(f[1].c_str());
    if(!f[2].empty()) MP_CopyStr(r.mode, f[2].c_str(), sizeof(r.mode));
    if(!f[3].empty()) MP_CopyStr(r.tags, f[3].c_str(), sizeof(r.tags));
    if(!f[4].empty()) MP_CopyStr(r.desc, f[4].c_str(), sizeof(r.desc));
}
static bool IsVcName(const char* n) { return n && strcasestr(n, "vice city") != nullptr; }

enum { VT_INTERNET, VT_FEATURED, VT_FAV, VT_LAN, VT_ADD, VT_CHAT, VT_PLAYERS, VT_VOICE, VT_MONET, VT_HELP, VT_MODS, VT_SETTINGS, VT_ABOUT, VT_COUNT };
static int  s_nTab = VT_INTERNET;
static char s_szSearch[32] = "";
static char s_szSel[96] = "";
static int  s_nMode = VL_MODE_BUBBLE;
static std::string s_Bundle;   // "name|bytes\n" of assets/bundle (from Java)
static float s_BarX = 0, s_BarY = 0, s_BarW = 0, s_BarH = 0;   // the Java floating bar (screen px) - the dropdown goes under it
static int  s_nMenuFrame = 0;
static int s_nPage = 0, s_nPageFrame = 0, s_nPlSel = -1;   // 0 Player list, 1 Help

static void RowKey(const VlRow& r, char* o, size_t n) { snprintf(o, n, "%d|%s|%s|%d", r.kind == 1 ? 1 : 0, r.name, r.host, r.port); }
static void OnlineRows(std::vector<VlRow>& rows, bool featuredOnly)
{
    std::vector<LanServer> live = Online::Snapshot();
    for(auto& s : live)
    {
        VlRow r{}; r.kind = 2; r.idx = -1; r.token = s.token;
        MP_CopyStr(r.name, s.name, sizeof(r.name)); MP_CopyStr(r.host, s.ip, sizeof(r.host));
        MP_CopyStr(r.mode, s.mode, sizeof(r.mode)); MP_CopyStr(r.tags, s.tags, sizeof(r.tags)); MP_CopyStr(r.desc, s.desc, sizeof(r.desc));
        MP_CopyStr(r.cc, s.cc, sizeof(r.cc)); MP_CopyStr(r.lang, s.lang, sizeof(r.lang)); r.modeCol = s.modeCol; r.region = s.region;
        std::string d; if(s.token && Online::Details(s.token, d)) ParseDetails(d, r);
        if(!r.mode[0]) MP_CopyStr(r.mode, "Freeroam", sizeof(r.mode));
        r.port = s.port; r.players = s.players; r.maxPlayers = s.maxPlayers; r.ping = s.ping; r.online = true; r.locked = s.locked;
        r.dedicated = s.dedicated != 0;
        r.vc = IsVcTags(r.tags) || (r.port == VL_LAN_PORT && IsVcName(r.name));
        if(r.dedicated && r.vc)
        {   // the VICE City LAN server (24/7): its text, like the San Andreas featured servers (LFeaturedDetails)
            if(!r.cc[0]) MP_CopyStr(r.cc, MP_CC_GLOBAL, sizeof(r.cc));
            if(!r.tags[0] || !strcasecmp(r.tags, MP_GAME_VC_TAG)) MP_CopyStr(r.tags, "vc,freeroam,deathmatch,vehicles,voice,24/7", sizeof(r.tags));
            if(!r.desc[0]) MP_CopyStr(r.desc, "The VICE City LAN server, always online: free roam Vice City with everyone - cars, weapons, deathmatch and voice chat. No login, just CONNECT.", sizeof(r.desc));
            if(!r.lang[0]) MP_CopyStr(r.lang, "English", sizeof(r.lang));
        }
        if(!r.vc && !s_P.showSA) continue;
        if(featuredOnly && !r.dedicated) continue;
        rows.push_back(r);
    }
}
static void BuildRows(int tab, std::vector<VlRow>& rows)
{
    if(tab == VT_LAN)
    {
        std::lock_guard<std::mutex> lk(s_LanMx);
        for(auto& s : s_Lan)
        {
            VlRow r{}; r.kind = 1; r.idx = -1;
            MP_CopyStr(r.name, s.name, sizeof(r.name)); MP_CopyStr(r.host, s.ip, sizeof(r.host)); MP_CopyStr(r.mode, s.mode[0] ? s.mode : "Freeroam", sizeof(r.mode));
            MP_CopyStr(r.tags, s.tags, sizeof(r.tags)); MP_CopyStr(r.desc, s.desc, sizeof(r.desc));
            MP_CopyStr(r.cc, s.cc, sizeof(r.cc)); MP_CopyStr(r.lang, s.lang, sizeof(r.lang)); r.modeCol = s.modeCol; r.region = s.region;
            r.port = s.port; r.players = s.players; r.maxPlayers = s.maxPlayers; r.ping = s.ping; r.online = true; r.locked = s.locked; r.vc = IsVcTags(s.tags);
            if(!r.vc && !s_P.showSA) continue;
            rows.push_back(r);
        }
    }
    else
    {
        if(tab == VT_INTERNET || tab == VT_FEATURED) OnlineRows(rows, tab == VT_FEATURED);
        if(tab != VT_FEATURED)
            for(int i = 0; i < ServerList::Count(); ++i)
            {
                ServerEntry* e = ServerList::Get(i); if(!e) continue;
                if(tab == VT_FAV && !e->fav) continue;
                VlRow r{}; r.kind = 0; r.idx = i;
                MP_CopyStr(r.name, e->name, sizeof(r.name)); MP_CopyStr(r.host, e->host, sizeof(r.host));
                const char* mode = e->liveMode[0] ? e->liveMode : e->mode;
                MP_CopyStr(r.mode, mode[0] ? mode : "Freeroam", sizeof(r.mode));
                MP_CopyStr(r.tags, e->liveTags[0] ? e->liveTags : e->tags, sizeof(r.tags));
                MP_CopyStr(r.pass, e->password, sizeof(r.pass));
                r.port = e->port; r.players = e->players; r.maxPlayers = e->maxPlayers; r.ping = e->ping; r.online = e->online; r.locked = e->locked; r.fav = e->fav;
                r.vc = IsVcTags(r.tags) || !e->online;   // an offline saved server: we can't tell, keep it
                if(!r.vc && !s_P.showSA) continue;
                rows.push_back(r);
            }
    }
    if(s_szSearch[0])
        rows.erase(std::remove_if(rows.begin(), rows.end(), [](const VlRow& r) { return !strcasestr(r.name, s_szSearch) && !strcasestr(r.host, s_szSearch) && !strcasestr(r.mode, s_szSearch); }), rows.end());
    // 24/7 servers first (the VICE City LAN server), then online, then by players
    std::stable_sort(rows.begin(), rows.end(), [](const VlRow& a, const VlRow& b) {
        if(a.online != b.online) return a.online;
        if(a.dedicated != b.dedicated) return a.dedicated;
        return a.players > b.players; });
}
static bool OnRow(const VlRow& r)
{
    if(!s_bGameOnline) return false;
    if(r.kind == 2) return !strcmp(s_GameServer.c_str(), r.name) || (s_GameHost == r.host && s_nGamePort == r.port);   // via the relay proxy the game sees 127.0.0.1
    return s_nGamePort == r.port && (!strcasecmp(s_GameHost.c_str(), r.host) || (s_GameHost == "127.0.0.1" && r.kind == 1 && LAN::IsPrivateAddress(r.host)));
}
static void Join(const VlRow& r)
{
    if(!GameLinked())
    {
        AddChat(0xFF6060FF, "GTA Vice City is not running with MP yet - opening the game. Join again when it is up.");
        VL_JavaCall("launch", "");
        return;
    }
    BrSend("NAME %s", s_P.nick);
    if(r.kind == 2 && Online::IsRelayIp(r.host))
    {
        uint32_t ip = 0; int rtt = 0; uint16_t local = 0;
        if(Online::DirectAddr(r.host, (uint16_t)r.port, &ip, &rtt))   // a 24/7 machine that answers over plain UDP
        {
            in_addr a; a.s_addr = ip; char b[32]; inet_ntop(AF_INET, &a, b, sizeof(b));
            BrSend("JOIN %d %s\t%s", r.port, b, r.pass);
        }
        else if(Online::OpenProxy(r.host, (uint16_t)r.port, &local))   // the game talks to our proxy, the proxy to the lobby relay
            BrSend("JOIN %d 127.0.0.1\t%s", (int)local, r.pass);
        else { AddChat(0xFF6060FF, "The online lobby is not connected yet - try again in a few seconds."); return; }
    }
    else BrSend("JOIN %d %s\t%s", r.port, r.host, r.pass);
    s_nMode = VL_MODE_BUBBLE;   // back to the game
}
static void Leave() { BrSend("LEAVE"); Online::CloseProxy(); }

// ======================================================================= keyboard callbacks
static void OnSearch(const char* t, void*) { MP_CopyStr(s_szSearch, t ? t : "", sizeof(s_szSearch)); }
static void OnNick(const char* t, void*) { if(t && t[0]) { MP_CopyStr(s_P.nick, t, sizeof(s_P.nick)); PrefsSave(); BrSend("NAME %s", s_P.nick); } }
static void OnChatSubmit(const char* t, void*) { if(t && t[0]) BrSend("CHAT %s", t); }
static void OpenChatKeyboard() { VKeyboard::Open("Say", "", MP_CHAT_LEN, OnChatSubmit); VKeyboard::SetChat(true); }
static char s_AddName[32] = "", s_AddHost[64] = "", s_AddPort[8] = "7777", s_AddPass[MP_PASSWORD_LEN + 1] = "", s_AddMsg[96] = "";
static void OnAddName(const char* t, void*) { MP_CopyStr(s_AddName, t ? t : "", sizeof(s_AddName)); }
static void OnAddHost(const char* t, void*) { MP_CopyStr(s_AddHost, t ? t : "", sizeof(s_AddHost)); }
static void OnAddPort(const char* t, void*) { MP_CopyStr(s_AddPort, t ? t : "", sizeof(s_AddPort)); }
static void OnAddPass(const char* t, void*) { MP_CopyStr(s_AddPass, t ? t : "", sizeof(s_AddPass)); }

// ======================================================================= widgets
static void Label(const char* t, ImU32 col = kVDim, float sc = 0.9f)
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    ImVec2 sz = UI::Font()->CalcTextSizeA(ImGui::GetFontSize() * sc, FLT_MAX, w, t);
    SocText(ImGui::GetWindowDrawList(), p, col, t, sc, w);
    ImGui::Dummy(ImVec2(w, sz.y + UI::S(4)));
}
static bool Field(const char* id, const char* label, const char* value, const char* ph, VKeyboard::SubmitFn fn, int maxLen, bool numeric = false)
{
    Label(label);
    float w = ImMin(ImGui::GetContentRegionAvail().x, UI::S(760));
    if(LInputBox(id, value, ph, w, UI::S(62))) { VKeyboard::Open(label, value, maxLen, fn, nullptr, numeric); return true; }
    ImGui::Dummy(ImVec2(1, UI::S(8)));
    return false;
}
static void Heading(const char* t)
{
    ImGui::Dummy(ImVec2(1, UI::S(10)));
    Label(t, kVTeal, 0.95f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddLine(p, ImVec2(p.x + ImGui::GetContentRegionAvail().x, p.y), kVLine, UI::S(1));
    ImGui::Dummy(ImVec2(1, UI::S(8)));
}
static bool Slider(const char* id, const char* label, float* v, float lo, float hi, const char* fmt = "%.2f")
{
    Label(label);
    ImGui::SetNextItemWidth(ImMin(ImGui::GetContentRegionAvail().x, UI::S(760)));
    bool ch = ImGui::SliderFloat(id, v, lo, hi, fmt);
    ImGui::Dummy(ImVec2(1, UI::S(4)));
    return ch;
}
static bool Check(const char* label, bool* v) { bool ch = ImGui::Checkbox(label, v); ImGui::Dummy(ImVec2(1, UI::S(4))); return ch; }
// a small white mic (the SA-MP mobile voice button look), slashed while off
static void DrawMic(ImDrawList* dl, ImVec2 c, float r, ImU32 col, bool slash)
{
    const float w = r * 0.42f, h = r * 0.62f;
    dl->AddRectFilled(ImVec2(c.x - w, c.y - r * 0.95f), ImVec2(c.x + w, c.y - r * 0.95f + h * 1.55f), col, w);
    dl->PathArcTo(ImVec2(c.x, c.y - r * 0.1f), r * 0.66f, 0.15f, IM_PI - 0.15f, 16); dl->PathStroke(col, 0, r * 0.13f);
    dl->AddLine(ImVec2(c.x, c.y + r * 0.56f), ImVec2(c.x, c.y + r * 0.86f), col, r * 0.13f);
    dl->AddLine(ImVec2(c.x - r * 0.34f, c.y + r * 0.88f), ImVec2(c.x + r * 0.34f, c.y + r * 0.88f), col, r * 0.13f);
    if(slash) dl->AddLine(ImVec2(c.x - r * 0.8f, c.y - r * 0.85f), ImVec2(c.x + r * 0.8f, c.y + r * 0.85f), col, r * 0.13f);
}
// chat line with SA-MP colour codes "{FFFF00}SERVER{FFFFFF}: hi" (the code is not shown), wrapped to the width
static bool ChatColCode(const char* p, const char* e, ImU32& out)
{
    if(e - p < 8 || p[0] != '{' || p[7] != '}') return false;
    unsigned v = 0;
    for(int i = 1; i <= 6; ++i) { char c = p[i]; int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; if(d < 0) return false; v = v * 16 + d; }
    out = IM_COL32((v >> 16) & 255, (v >> 8) & 255, v & 255, 255);
    return true;
}
static void ChatLineWidget(const std::string& text, ImU32 base, float fs, float W)
{
    struct Seg { std::string t; ImU32 c; };
    std::vector<Seg> segs; ImU32 c = base; std::string cur;
    const char* p = text.c_str(); const char* e = p + text.size();
    while(p < e)
    {
        ImU32 nc;
        if(s_P.chatColors && *p == '{' && ChatColCode(p, e, nc)) { if(!cur.empty()) { segs.push_back({ cur, c }); cur.clear(); } c = nc; p += 8; continue; }
        if(!s_P.chatColors && *p == '{' && ChatColCode(p, e, nc)) { p += 8; continue; }
        cur += *p++;
    }
    if(!cur.empty()) segs.push_back({ cur, c });
    ImFont* f = UI::Font(); ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 o = ImGui::GetCursorScreenPos(); float x = 0, y = 0; const float lh = fs * 1.18f;
    for(auto& s : segs)   // word wrap across the coloured parts
    {
        const char* q = s.t.c_str(); const char* qe = q + s.t.size();
        while(q < qe)
        {
            const char* we = q; while(we < qe && *we != ' ') ++we; if(we < qe) ++we;   // one word + its space
            float ww = f->CalcTextSizeA(fs, FLT_MAX, 0, q, we).x;
            if(x > 0 && x + ww > W) { x = 0; y += lh; }
            if(ww > W)   // a very long word: let ImGui wrap it by characters
            {
                const char* end = f->CalcWordWrapPositionA(fs / f->FontSize, q, we, W - x);
                if(end == q) end = q + 1;
                dl->AddText(f, fs, ImVec2(o.x + x + UI::S(1), o.y + y + UI::S(1)), IM_COL32(0, 0, 0, 160), q, end);
                dl->AddText(f, fs, ImVec2(o.x + x, o.y + y), s.c, q, end);
                q = end; x = 0; y += lh; continue;
            }
            dl->AddText(f, fs, ImVec2(o.x + x + UI::S(1), o.y + y + UI::S(1)), IM_COL32(0, 0, 0, 160), q, we);   // SA chat outline / shadow
            dl->AddText(f, fs, ImVec2(o.x + x, o.y + y), s.c, q, we);
            x += ww; q = we;
        }
    }
    ImGui::Dummy(ImVec2(W, y + lh + UI::S(2)));
}
// SA-MP player colours (by player id) for the "name:" part of white chat lines
static ImU32 PlayerColor(int id)
{
    static const ImU32 k[] = { IM_COL32(255,128,0,255), IM_COL32(0,200,255,255), IM_COL32(120,230,80,255), IM_COL32(255,90,180,255), IM_COL32(255,220,60,255),
                               IM_COL32(170,120,255,255), IM_COL32(60,230,200,255), IM_COL32(255,100,90,255), IM_COL32(150,200,255,255), IM_COL32(230,160,255,255) };
    return k[(id < 0 ? 0 : id) % (int)(sizeof(k) / sizeof(k[0]))];
}
// ======================================================================= server table (the SA LTable: 4.13.64 same columns + detail strip)
// Lock | Server | Gamemode chip | Tags | Language | Players | Map | Ping | Location flag, and a tap opens the CS:GO detail
// strip under the row: Server Description + IP + Favorite, gamemode + tags, All players, preview picture,
// Server location + Ping stability graph (src/mp_launcher.inc LTable)
static std::map<std::string, std::vector<int>> s_PingHist;   // row key -> last 40 pings (1 a second)
static bool s_bTestSelFirst = false;   // emulator test hook (--es open detail): open the first row's detail strip
static ImU32 VlModeColor(const VlRow& r)
{
    if(r.modeCol > 0 && r.modeCol < kModeColCount) return kModeCols[r.modeCol].col;
    const char* m = r.mode[0] ? r.mode : "Freeroam"; unsigned h = 5381; for(const char* c = m; *c; ++c) h = h * 33 + (unsigned char)tolower((unsigned char)*c);
    return !strcasecmp(m, "Freeroam") ? kModeCols[0].col : kModeCols[1 + h % (kModeColCount - 1)].col;
}
static void VlStar(ImDrawList* dl, ImVec2 c, float r, ImU32 col, bool filled)
{
    ImVec2 p[10];
    for(int i = 0; i < 10; ++i) { float an = -IM_PI * 0.5f + i * IM_PI / 5, rr = (i & 1) ? r * 0.45f : r; p[i] = ImVec2(c.x + cosf(an) * rr, c.y + sinf(an) * rr); }
    if(filled) for(int i = 0; i < 10; i += 2) dl->AddTriangleFilled(c, p[i], p[(i + 1) % 10], col), dl->AddTriangleFilled(c, p[(i + 9) % 10], p[i], col);
    dl->AddPolyline(p, 10, col, ImDrawFlags_Closed, UI::S(1.5f));
}
static void VlWrap(ImDrawList* dl, ImVec2 p, float w, float maxY, ImU32 col, const char* txt, float sc = 0.86f)
{
    dl->PushClipRect(p, ImVec2(p.x + w, maxY), true);
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * sc, p, col, txt, nullptr, w);
    dl->PopClipRect();
}
// the host's preview picture (p/<token> in the lobby); the VICE City LAN server / no picture: the Vice City backdrop
static void* VlPreview(const VlRow& r, ImVec2* uv)
{
    *uv = ImVec2(1, 1);
    if(r.token)
    {
        static std::map<uint32_t, std::pair<uint64_t, std::string>> s_Pv;   // token -> last check, texture key ("" = none)
        auto& c = s_Pv[r.token];
        const uint64_t now = MP_NowMs();
        if(!c.first || now - c.first > 5000)
        {
            c.first = now;
            std::string jpg;
            if(!Online::Preview(r.token, jpg) || jpg.empty()) c.second.clear();
            else
            {
                uint32_t h = 2166136261u; for(unsigned char ch : jpg) h = (h ^ ch) * 16777619u;
                char k[48]; snprintf(k, sizeof(k), "pv_%08x_%08x", r.token, h);
                if(c.second != k) { c.second = k; VL_TextureMem(k, jpg); }
            }
        }
        if(!c.second.empty()) if(void* t = VL_TextureMem(c.second.c_str(), std::string())) return t;
    }
    return HUD::Texture("ui_bg.jpg", uv);
}
// who is inside: live from the game when we are on it, else what the host publishes (d/<token> field 6)
static void VlDetailPlayers(const VlRow& r, bool on, std::vector<std::string>& out)
{
    out.clear();
    if(on) { for(auto& p : s_Players) out.push_back(p.name); return; }
    std::string d;
    if(!r.token || !Online::Details(r.token, d) || d.compare(0, 3, "v1|")) return;
    size_t at = 3; for(int i = 0; i < 5 && at != std::string::npos; ++i) { at = d.find('|', at); if(at != std::string::npos) ++at; }
    if(at == std::string::npos) return;
    const std::string pl = d.substr(at);
    size_t p = 0;
    while(p < pl.size()) { size_t e = pl.find(',', p); if(e == std::string::npos) e = pl.size(); if(e > p) out.push_back(pl.substr(p, e - p)); p = e + 1; }
}
static void Table(std::vector<VlRow>& rows, float w, float h)
{
    ImGui::BeginChild("##vltable", ImVec2(w, h), false);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rh = UI::S(70), hh = UI::S(46), detH = UI::S(300);
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    // the same columns as San Andreas; a narrow (portrait) panel drops Tags / Language / Map
    const bool wide = w > UI::S(1250);
    const float cLock = 0, cName = UI::S(56);
    const float cMode = w * (wide ? 0.30f : 0.40f), cTags = w * 0.43f, cLang = w * 0.555f;
    const float cPl = w * (wide ? 0.64f : 0.62f), cMap = w * 0.735f, cPing = w * (wide ? 0.85f : 0.78f), cLoc = w * (wide ? 0.92f : 0.89f);
    dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + hh), IM_COL32(0, 0, 0, 70));
    {
        const float fy = p0.y + (hh - ImGui::GetFontSize() * 0.8f) * 0.5f;
        SocText(dl, ImVec2(p0.x + cName, fy), kVDim, "SERVER", 0.8f);
        SocText(dl, ImVec2(p0.x + cMode, fy), kVDim, "GAMEMODE", 0.8f);
        if(wide) { SocText(dl, ImVec2(p0.x + cTags, fy), kVDim, "TAGS", 0.8f); SocText(dl, ImVec2(p0.x + cLang, fy), kVDim, "LANGUAGE", 0.8f); SocText(dl, ImVec2(p0.x + cMap, fy), kVDim, "MAP", 0.8f); }
        SocText(dl, ImVec2(p0.x + cPl, fy), kVDim, "PLAYERS", 0.8f);
        SocText(dl, ImVec2(p0.x + cPing, fy), kVDim, "PING", 0.8f);
        SocText(dl, ImVec2(p0.x + cLoc, fy), kVDim, "LOC.", 0.8f);
    }
    ImGui::Dummy(ImVec2(w, hh));
    if(rows.empty())
    {
        ImGui::Dummy(ImVec2(1, UI::S(30)));
        const char* t = s_nTab == VT_LAN ? (s_bScanning ? "Looking for Vice City servers on your Wi-Fi / hotspot..." : "No Vice City server on this Wi-Fi / hotspot. Tap refresh.")
                       : s_nTab == VT_FAV ? "No favourite servers yet - tap a server, then Favorite."
                       : !Online::IsConnected() ? "Connecting to the online lobby..." : s_nTab == VT_FEATURED ? "No 24/7 Vice City server online right now." : "No Vice City server online. Add one with Add server.";
        Label(t, kVDim, 1.0f);
    }
    char key[96];
    {   // ping stability graph samples (every row, once a second)
        static uint64_t last = 0; const uint64_t now = MP_NowMs();
        if(now - last >= 1000)
        {
            last = now;
            for(auto& r : rows) { if(!r.online || r.ping < 0) continue; RowKey(r, key, sizeof(key)); auto& v = s_PingHist[key]; v.push_back(r.ping); if(v.size() > 40) v.erase(v.begin()); }
            if(s_PingHist.size() > 600) s_PingHist.clear();
        }
    }
    const float W = ImGui::GetContentRegionAvail().x;
    if(s_bTestSelFirst && !rows.empty()) { s_bTestSelFirst = false; RowKey(rows[0], s_szSel, sizeof(s_szSel)); }
    for(size_t i = 0; i < rows.size(); ++i)
    {
        VlRow& r = rows[i]; RowKey(r, key, sizeof(key));
        bool sel = !strcmp(key, s_szSel); const bool on = OnRow(r);
        ImVec2 a = ImGui::GetCursorScreenPos(), b(a.x + W, a.y + rh);
        ImGui::PushID((int)i);
        if(ImGui::InvisibleButton("##row", ImVec2(W, rh)))
        {
            static double s_dLast = 0; static char s_Last[96] = "";
            if(sel && !strcmp(s_Last, key) && ImGui::GetTime() - s_dLast < 0.45) Join(r);   // double tap = connect
            else if(sel) { s_szSel[0] = 0; sel = false; }                                     // tap again = close the details
            else { MP_CopyStr(s_szSel, key, sizeof(s_szSel)); sel = true; }
            MP_CopyStr(s_Last, key, sizeof(s_Last)); s_dLast = ImGui::GetTime();
        }
        const bool held = ImGui::IsItemActive();
        const ImU32 mc = VlModeColor(r);
        // row: slate base, the gamemode colour stripe, 24/7 rows with the gamemode highlight (San Andreas featured rows)
        dl->AddRectFilled(a, b, (i & 1) ? IM_COL32(30, 34, 40, 110) : IM_COL32(44, 49, 56, 110));
        if(r.dedicated) dl->AddRectFilledMultiColor(a, b, VAlpha(mc, 0.30f), VAlpha(mc, 0.04f), VAlpha(mc, 0.04f), VAlpha(mc, 0.30f));
        dl->AddRectFilled(a, ImVec2(a.x + UI::S(4), b.y), mc);
        if(sel) dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 26));
        else if(held) dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 14));
        dl->AddLine(ImVec2(a.x, b.y), b, IM_COL32(0, 0, 0, 90));
        const float ty = a.y + (rh - ImGui::GetTextLineHeight()) * 0.5f;
        if(r.locked) LPadlock(dl, ImVec2(a.x + cLock + UI::S(30), a.y + rh * 0.5f - UI::S(3)), UI::S(20), true, IM_COL32(230, 230, 230, 255));
        // name (+ online dot, favourite star, pink VICE CITY 24/7 tag) and a dim second line
        dl->PushClipRect(ImVec2(a.x + cName, a.y), ImVec2(a.x + cMode - UI::S(10), b.y), true);
        float nx = a.x + cName;
        if(on) { dl->AddCircleFilled(ImVec2(nx + UI::S(7), ty - UI::S(10) + ImGui::GetTextLineHeight() * 0.5f), UI::S(6), IM_COL32(90, 200, 90, 255)); nx += UI::S(22); }
        if(r.fav) { VlStar(dl, ImVec2(nx + UI::S(9), ty - UI::S(10) + ImGui::GetTextLineHeight() * 0.5f), UI::S(9), IM_COL32(240, 200, 60, 255), true); nx += UI::S(24); }
        dl->AddText(ImVec2(nx, ty - UI::S(10)), (r.online || sel) ? IM_COL32_WHITE : IM_COL32(175, 180, 186, 255), r.name);
        if(r.dedicated)
        {
            float tx = nx + ImGui::CalcTextSize(r.name).x + UI::S(12), th = UI::S(28);
            const char* tg = r.vc ? "VICE CITY 24/7" : "24/7"; float tw = SocTextW(tg, 0.62f) + UI::S(16);
            ImVec2 ta(tx, ty - UI::S(8)), tb(tx + tw, ty - UI::S(8) + th);
            dl->AddRectFilled(ta, tb, IM_COL32(255, 64, 160, 60)); dl->AddRect(ta, tb, IM_COL32(255, 92, 180, 200));
            SocText(dl, ImVec2(ta.x + UI::S(8), ta.y + (th - ImGui::GetFontSize() * 0.62f) * 0.5f), IM_COL32(255, 160, 210, 255), tg, 0.62f);
        }
        {
            char sub[160];
            if(r.kind == 2) snprintf(sub, sizeof(sub), "%s%s%s", r.dedicated ? "24/7 server" : "internet", r.vc ? "  -  GTA Vice City" : "  -  San Andreas", on ? "   - you are here" : "");
            else snprintf(sub, sizeof(sub), "%s:%d%s%s", r.host, r.port, r.vc ? "" : "   (San Andreas)", on ? "   - you are here" : "");
            SocText(dl, ImVec2(a.x + cName, ty + UI::S(20)), on ? IM_COL32(95, 211, 95, 255) : kVDim, sub, 0.70f);
        }
        dl->PopClipRect();
        {   // gamemode chip (square, the gamemode colour)
            const float ch = UI::S(30), maxW = (wide ? cTags : cPl) - cMode - UI::S(12);
            const float tw = ImMin(ImGui::CalcTextSize(r.mode).x, maxW - UI::S(16));
            ImVec2 ca(a.x + cMode, a.y + rh * 0.5f - ch * 0.5f), cb(ca.x + tw + UI::S(16), ca.y + ch);
            dl->AddRectFilled(ca, cb, mc); dl->AddRect(ca, cb, IM_COL32(255, 255, 255, 40));
            dl->PushClipRect(ca, cb, true);
            dl->AddText(ImVec2(ca.x + UI::S(8), ca.y + (ch - ImGui::GetTextLineHeight()) * 0.5f), IM_COL32_WHITE, r.mode);
            dl->PopClipRect();
        }
        const float sfs = ImGui::GetFontSize() * 0.82f;
        if(wide)
        {
            std::string t2; for(const char* c = r.tags[0] ? r.tags : "-"; *c; ++c) { if(*c == ',') t2 += ", "; else t2 += *c; }
            dl->PushClipRect(ImVec2(a.x + cTags, a.y), ImVec2(a.x + cLang - UI::S(10), b.y), true);
            dl->AddText(ImGui::GetFont(), sfs, ImVec2(a.x + cTags, a.y + (rh - sfs) * 0.5f), IM_COL32(160, 166, 174, 255), t2.c_str());
            dl->PopClipRect();
            dl->PushClipRect(ImVec2(a.x + cLang, a.y), ImVec2(a.x + cPl - UI::S(10), b.y), true);
            dl->AddText(ImGui::GetFont(), sfs, ImVec2(a.x + cLang, a.y + (rh - sfs) * 0.5f), IM_COL32(190, 196, 204, 255), r.lang[0] ? r.lang : "-");
            dl->PopClipRect();
            dl->PushClipRect(ImVec2(a.x + cMap, a.y), ImVec2(a.x + cPing - UI::S(8), b.y), true);
            dl->AddText(ImVec2(a.x + cMap, ty), IM_COL32(200, 204, 210, 255), r.vc ? "Vice City" : "San Andreas");
            dl->PopClipRect();
        }
        char t[48];
        if(r.online && r.players >= 0) snprintf(t, sizeof(t), "%d/%d", r.players, r.maxPlayers); else snprintf(t, sizeof(t), "-");
        const bool full = r.online && r.maxPlayers > 0 && r.players >= r.maxPlayers;
        dl->AddText(ImVec2(a.x + cPl, ty), full ? kVRed : (r.players > 0 ? IM_COL32(230, 200, 90, 255) : IM_COL32(200, 204, 210, 255)), t);
        if(r.online && r.ping >= 0) snprintf(t, sizeof(t), "%d", r.ping); else snprintf(t, sizeof(t), r.online ? "-" : "offline");
        const ImU32 pc = !r.online ? kVRed : r.ping < 0 ? IM_COL32(150, 150, 150, 255) : r.ping < 80 ? IM_COL32(110, 200, 90, 255) : r.ping < 150 ? IM_COL32(230, 200, 90, 255) : kVRed;
        dl->AddText(ImVec2(a.x + cPing, ty), pc, t);
        {
            const float fh = rh * 0.40f, fw = fh * 4.0f / 3.0f;
            ImVec2 fa(a.x + cLoc, a.y + (rh - fh) * 0.5f);
            LDrawFlag(dl, LCountryIndex(r.cc), fa, ImVec2(fa.x + fw, fa.y + fh));
        }
        if(sel)
        {   // ---- the CS:GO detail strip (San Andreas) ----
            ImVec2 d = ImGui::GetCursorScreenPos();
            const float dh = wide ? detH : detH * 1.55f;
            ImGui::Dummy(ImVec2(W, dh));
            ImVec2 e(d.x + W, d.y + dh);
            dl->AddRectFilled(d, e, IM_COL32(24, 27, 32, 200));
            dl->AddRect(a, e, IM_COL32(235, 238, 242, 220), 0.0f, 0, UI::S(2));   // white frame (row + details)
            const float pad = UI::S(14), lh = ImGui::GetTextLineHeight();
            const ImU32 hc = IM_COL32(150, 157, 166, 255), tc = IM_COL32(222, 226, 231, 255);
            // wide: 5 columns in one strip; narrow: 2 rows (description / gamemode / players, then preview / location + ping)
            float c1, c2, c3, c4, c5, y0 = d.y + pad, yA = e.y, yB = e.y;
            if(wide) { c1 = d.x + pad; c2 = d.x + W * 0.34f; c3 = d.x + W * 0.52f; c4 = d.x + W * 0.67f; c5 = d.x + W * 0.84f; }
            else { c1 = d.x + pad; c2 = d.x + W * 0.45f; c3 = d.x + W * 0.70f; c4 = d.x + pad; c5 = d.x + W * 0.50f; yA = d.y + dh * 0.52f; }
            const float r2y = wide ? y0 : yA + pad;   // the preview / location row
            // 1: description, IP, favourite
            dl->AddText(ImVec2(c1, y0), hc, "Server Description");
            VlWrap(dl, ImVec2(c1, y0 + lh + UI::S(6)), c2 - c1 - pad, yA - lh * 2.6f, tc, r.desc[0] ? r.desc : "No description.");
            {
                char ip[96];
                if(r.kind == 2) snprintf(ip, sizeof(ip), "IP: %s lobby, port %d", r.dedicated ? "24/7 internet" : "internet", r.port);
                else snprintf(ip, sizeof(ip), "IP: %s:%d%s", r.host, r.port, r.kind == 1 ? " (Wi-Fi)" : "");
                dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.86f, ImVec2(c1, yA - pad - lh), hc, ip);
                if(r.kind == 0)
                {   // saved server: Favorite (star) - the only kind kept on this phone
                    ImVec2 sa(c2 - pad - UI::S(150), yA - pad - lh * 2.6f), sb(c2 - pad, yA - pad - lh * 1.4f);
                    ImGui::SetCursorScreenPos(sa);
                    if(ImGui::InvisibleButton("##fav", ImVec2(sb.x - sa.x, sb.y - sa.y))) { ServerList::SetFav(r.idx, !r.fav); ServerList::Save(); r.fav = !r.fav; }
                    dl->AddRectFilled(sa, sb, ImGui::IsItemActive() ? IM_COL32(255, 255, 255, 40) : IM_COL32(255, 255, 255, 14));
                    dl->AddText(ImVec2(sa.x + UI::S(10), sa.y + (sb.y - sa.y - lh) * 0.5f), tc, "Favorite");
                    VlStar(dl, ImVec2(sb.x - UI::S(22), (sa.y + sb.y) * 0.5f), UI::S(11), IM_COL32(240, 200, 60, 255), r.fav);
                }
            }
            // 2: gamemode + tags
            dl->AddText(ImVec2(c2, y0), hc, r.mode);
            {
                std::string tl; for(const char* c = r.tags[0] ? r.tags : "-"; *c; ++c) { if(*c == ',') tl += ", "; else tl += *c; }
                dl->AddText(ImVec2(c2, y0 + lh * 1.3f), hc, "Tags");
                VlWrap(dl, ImVec2(c2, y0 + lh * 2.4f), c3 - c2 - pad, yA - pad, tc, tl.c_str());
            }
            // 3: all players
            {
                std::vector<std::string> pl; VlDetailPlayers(r, on, pl);
                const float cEnd = wide ? c4 : e.x;
                char hd[48]; snprintf(hd, sizeof(hd), "All players (%d)", r.players >= 0 ? r.players : (int)pl.size());
                dl->AddText(ImVec2(c3, y0), hc, hd);
                float yy = y0 + lh + UI::S(8); int shown = 0;
                dl->PushClipRect(ImVec2(c3, d.y), ImVec2(cEnd - pad, yA), true);
                for(auto& n : pl)
                {
                    if(yy + lh > yA - pad - lh) { char more[32]; snprintf(more, sizeof(more), "+%d more", (int)pl.size() - shown); dl->AddText(ImVec2(c3, yy), hc, more); break; }
                    dl->AddRectFilled(ImVec2(c3, yy + lh * 0.2f), ImVec2(c3 + lh * 0.6f, yy + lh * 0.8f), kVTeal);
                    dl->AddText(ImVec2(c3 + lh, yy), tc, n.c_str()); yy += lh * 1.15f; ++shown;
                }
                if(pl.empty()) dl->AddText(ImVec2(c3, yy), hc, r.players > 0 ? "Join to see who is playing." : "Nobody inside yet.");
                dl->PopClipRect();
            }
            if(!wide) dl->AddLine(ImVec2(d.x + pad, yA), ImVec2(e.x - pad, yA), kVLine, UI::S(1));
            // 4: preview picture
            {
                ImVec2 uv(1, 1); ImVec2 pa(c4, r2y), pb(c5 - pad, yB - pad);
                const float ph = (pb.x - pa.x) * 0.5f; if(ph < pb.y - pa.y) { pa.y += (pb.y - pa.y - ph) * 0.5f; pb.y = pa.y + ph; }
                if(void* bt = VlPreview(r, &uv)) LImageCover(dl, bt, uv, pa, pb, IM_COL32_WHITE);
                else { dl->AddRectFilled(pa, pb, IM_COL32(24, 27, 32, 255)); dl->AddText(ImVec2(pa.x + UI::S(10), pa.y + UI::S(8)), hc, "No preview"); }
                dl->AddRect(pa, pb, IM_COL32(255, 255, 255, 50));
            }
            // 5: Server location + ping stability graph
            {
                const char* cn = LCountryName(r.cc);
                char loc[96]; snprintf(loc, sizeof(loc), "Server location: %s", cn ? cn : (r.kind == 2 ? Online::RegionLabel(r.region) : (r.kind == 1 ? "this Wi-Fi" : "unknown")));
                const float fh = lh, fw = fh * 4.0f / 3.0f;
                LDrawFlag(dl, LCountryIndex(r.cc), ImVec2(c5, r2y), ImVec2(c5 + fw, r2y + fh));
                dl->PushClipRect(ImVec2(c5, r2y - UI::S(4)), ImVec2(e.x - UI::S(6), yB), true);
                dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.82f, ImVec2(c5 + fw + UI::S(6), r2y + UI::S(2)), tc, loc);
                const std::vector<int>* hv = nullptr; auto it = s_PingHist.find(key); if(it != s_PingHist.end()) hv = &it->second;
                ImVec2 ga(c5, r2y + lh * 2.2f), gb(e.x - pad, yB - pad - lh * 1.2f);
                dl->AddRectFilled(ga, gb, IM_COL32(20, 23, 27, 255)); dl->AddRect(ga, gb, IM_COL32(255, 255, 255, 40));
                dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.78f, ImVec2(c5, ga.y - lh * 0.95f), hc, "Ping stability");
                int mx = 100, sum = 0, mn = 99999, mxp = 0; const int n = hv ? (int)hv->size() : 0;
                for(int k = 0; k < n; ++k) { int v = (*hv)[k]; mx = ImMax(mx, v); sum += v; mn = ImMin(mn, v); mxp = ImMax(mxp, v); }
                const float bw = (gb.x - ga.x) / 40.0f;
                for(int k = 0; k < n; ++k)
                {
                    const int v = (*hv)[k]; const float bh = (gb.y - ga.y - UI::S(4)) * ImClamp((float)v / mx, 0.04f, 1.0f);
                    const float x = gb.x - (n - k) * bw;
                    const ImU32 bc = v < 80 ? IM_COL32(96, 186, 80, 255) : v < 150 ? IM_COL32(220, 190, 70, 255) : IM_COL32(210, 70, 56, 255);
                    dl->AddRectFilled(ImVec2(x + 1, gb.y - bh), ImVec2(x + bw - 1, gb.y - 1), bc);
                }
                char st[64];
                if(n) snprintf(st, sizeof(st), "avg %d ms   jitter %d ms", sum / n, mxp - mn); else snprintf(st, sizeof(st), r.online ? "no ping yet" : "offline");
                dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.78f, ImVec2(c5, yB - pad - lh), hc, st);
                dl->PopClipRect();
            }
            // CONNECT for this server right in the strip (San Andreas: CONNECT in the bottom bar does the same)
            ImGui::SetCursorScreenPos(ImVec2(d.x, e.y));
            ImGui::Dummy(ImVec2(W, 0));
        }
        ImGui::PopID();
    }
    LDragScroll();
    ImGui::EndChild();
}

// ======================================================================= pages
static void PageAdd(float w)
{
    Label("Add a Vice City server (its owner gives you the IP or host name and the port). Saved on this phone, shown in "
          "Internet and Favorites - the same as + Add server in San Andreas MP.", kVDim, 0.85f);
    ImGui::Dummy(ImVec2(1, UI::S(6)));
    Field("an", "Server name", s_AddName, "My Vice City server", OnAddName, 31);
    Field("ah", "IP / host", s_AddHost, "192.168.1.5  or  vc.example.com", OnAddHost, 63);
    Field("ap", "Port", s_AddPort, "7777", OnAddPort, 6, true);
    Field("aw", "Password (empty = none)", s_AddPass, "none", OnAddPass, MP_PASSWORD_LEN);
    if(VBtn("addsv", "ADD SERVER", UI::S(320), UI::S(66), 1, s_AddHost[0] != 0, IC_PLUS))
    {
        int port = atoi(s_AddPort); if(port <= 0 || port > 65535) port = VL_LAN_PORT;
        if(ServerList::Add(s_AddName[0] ? s_AddName : s_AddHost, s_AddHost, port, s_P.region, s_AddPass))
        {
            int i = ServerList::FindByHostPort(s_AddHost, port);
            if(i >= 0) ServerList::SetDetails(i, "", 0, "Freeroam", MP_GAME_VC_TAG, "");
            ServerList::Save(); ServerList::Refresh();
            snprintf(s_AddMsg, sizeof(s_AddMsg), "Saved %s:%d", s_AddHost, port);
            s_AddName[0] = s_AddHost[0] = s_AddPass[0] = 0; s_nTab = VT_INTERNET;
        }
        else snprintf(s_AddMsg, sizeof(s_AddMsg), "Could not save (list full or a wrong address)");
    }
    if(s_AddMsg[0]) Label(s_AddMsg, kVTeal);
    ImGui::Dummy(ImVec2(1, UI::S(20)));
    Label("Saved servers", IM_COL32_WHITE, 1.0f);
    for(int i = 0; i < ServerList::Count(); ++i)
    {
        ServerEntry* e = ServerList::Get(i); if(!e) continue;
        char t[160]; snprintf(t, sizeof(t), "%s   %s:%d", e->name, e->host, e->port);
        ImGui::PushID(i);
        if(VBtn("rm", "", UI::S(62), UI::S(52), 3, true, IC_TRASH)) { ServerList::Remove(i); ServerList::Save(); ImGui::PopID(); break; }
        ImGui::SameLine(0, UI::S(14));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + UI::S(10)); Label(t, kVText, 0.9f);
        ImGui::PopID();
    }
}
static ImU32 ChatBase(const VlChat& c) { return IM_COL32(c.abgr & 0xFF, (c.abgr >> 8) & 0xFF, (c.abgr >> 16) & 0xFF, 255); }   // Chat() colours are ABGR
static void PageChat(float w, float h)
{
    const float inH = UI::S(66);
    ImGui::BeginChild("##chatlog", ImVec2(w, h - inH - UI::S(16)), false);
    const float fs = ImGui::GetFontSize() * 0.9f * s_P.chatSize, W = ImGui::GetContentRegionAvail().x;
    for(auto& c : s_Chat)
    {
        std::string line;
        if(s_P.chatTime) { char tb[16]; struct tm lt; localtime_r(&c.at, &lt); strftime(tb, sizeof(tb), "{8C96A0}[%H:%M] ", &lt); line = tb; }
        ImU32 base = ChatBase(c);
        // a white "name: text" line from a player: the name in that player's colour (like the San Andreas chat)
        if(s_P.chatColors && (c.abgr & 0xFFFFFF) == 0xFFFFFF)
        {
            size_t colon = c.text.find(": ");
            if(colon != std::string::npos && colon < MP_NAME_LEN)
                for(auto& pl : s_Players)
                    if(pl.name.size() == colon && !c.text.compare(0, colon, pl.name))
                    {
                        ImU32 pc = PlayerColor(pl.id); char cb[12];
                        snprintf(cb, sizeof(cb), "{%02X%02X%02X}", (pc >> IM_COL32_R_SHIFT) & 255, (pc >> IM_COL32_G_SHIFT) & 255, (pc >> IM_COL32_B_SHIFT) & 255);
                        line += cb; line += pl.name; line += "{FFFFFF}"; line += c.text.substr(colon); goto built;
                    }
        }
        { char cb[12]; snprintf(cb, sizeof(cb), "{%02X%02X%02X}", (base >> IM_COL32_R_SHIFT) & 255, (base >> IM_COL32_G_SHIFT) & 255, (base >> IM_COL32_B_SHIFT) & 255); line += cb; }
        line += c.text;
    built:
        ChatLineWidget(line, base, fs, W);
    }
    if(s_Chat.empty()) Label(GameLinked() ? "No chat yet." : "Start GTA Vice City (with MP) - the chat shows here.", kVDim, 0.9f);
    static size_t s_nLast = 0;
    if(s_Chat.size() != s_nLast) { s_nLast = s_Chat.size(); ImGui::SetScrollHereY(1.0f); }
    LDragScroll();
    ImGui::EndChild();
    ImGui::Dummy(ImVec2(1, UI::S(8)));
    float bw = UI::S(180);
    if(LInputBox("say", "", "Say something...  (or a /command)", w - bw - UI::S(12), inH, IC_CHAT)) OpenChatKeyboard();
    ImGui::SameLine(0, UI::S(12));
    if(VBtn("sayb", "SEND", bw, inH, 1, true, IC_SEND)) OpenChatKeyboard();
}
static void PagePlayers(float w)
{
    char t[160];
    snprintf(t, sizeof(t), "%s   -   %d player%s", s_bGameOnline ? s_GameServer.c_str() : "Not on a server", (int)s_Players.size(), s_Players.size() == 1 ? "" : "s");
    Label(t, IM_COL32_WHITE, 1.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + UI::S(46)), IM_COL32(0, 0, 0, 70));
    SocText(dl, ImVec2(p.x + UI::S(16), p.y + UI::S(10)), kVDim, "ID", 0.8f);
    SocText(dl, ImVec2(p.x + UI::S(110), p.y + UI::S(10)), kVDim, "NAME", 0.8f);
    SocText(dl, ImVec2(p.x + w * 0.65f, p.y + UI::S(10)), kVDim, "SCORE", 0.8f);
    SocText(dl, ImVec2(p.x + w * 0.82f, p.y + UI::S(10)), kVDim, "PING", 0.8f);
    ImGui::Dummy(ImVec2(w, UI::S(50)));
    for(auto& pl : s_Players)
    {
        p = ImGui::GetCursorScreenPos();
        bool me = pl.id == s_nMyId;
        if(me) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + UI::S(56)), IM_COL32(126, 178, 226, 30));
        char b[16]; snprintf(b, sizeof(b), "%d", pl.id); dl->AddText(ImVec2(p.x + UI::S(16), p.y + UI::S(12)), kVDim, b);
        dl->AddText(ImVec2(p.x + UI::S(110), p.y + UI::S(12)), me ? kVTeal : PlayerColor(pl.id), pl.name.c_str());
        snprintf(b, sizeof(b), "%d", pl.score); dl->AddText(ImVec2(p.x + w * 0.65f, p.y + UI::S(12)), kVText, b);
        snprintf(b, sizeof(b), "%d", pl.ping); dl->AddText(ImVec2(p.x + w * 0.82f, p.y + UI::S(12)), kVText, b);
        ImGui::Dummy(ImVec2(w, UI::S(56)));
    }
    if(s_Players.empty()) Label(GameLinked() ? "Join a server to see its players." : "Start GTA Vice City (with MP) first.", kVDim, 0.9f);
}
static void PageVoice(float w)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    const float r = UI::S(56);
    ImVec2 c(p.x + r + UI::S(10), p.y + r + UI::S(10));
    const bool on = s_nMic >= 2;
    dl->AddCircleFilled(c, r, on ? IM_COL32(235, 235, 235, 240) : IM_COL32(0, 0, 0, 135), 32);
    if(s_nMic == 3) dl->AddCircle(c, r + UI::S(4), IM_COL32(60, 220, 90, 255), 32, UI::S(4));
    DrawMic(dl, c, r * 0.55f, on ? IM_COL32(10, 10, 10, 255) : (s_nMic == 0 ? IM_COL32(255, 255, 255, 110) : IM_COL32_WHITE), !on);
    ImGui::SetCursorScreenPos(ImVec2(c.x + r + UI::S(30), p.y + UI::S(14)));
    ImGui::BeginGroup();
    static const char* st[4] = { "Not on a server", "Microphone off", "Microphone on", "Talking" };
    Label(GameLinked() ? st[ImClamp(s_nMic, 0, 3)] : "GTA Vice City is not running", IM_COL32_WHITE, 1.1f);
    if(VBtn("mic", on ? "TURN MIC OFF" : "TURN MIC ON", UI::S(360), UI::S(66), on ? 3 : 1, GameLinked() && s_nMic > 0, IC_PLAY)) BrSend("MIC");
    ImGui::EndGroup();
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + r * 2 + UI::S(30)));
    Heading("VOICE CHAT");
    Label("Proximity voice chat of MP: players near you hear you (louder up close). The mic button on the floating bar does "
          "the same - white = on, green ring = you are talking. Vice City can't open the microphone itself, so MP uses the "
          "MP Voice helper app when needed (it asks once).", kVText, 0.88f);
}
static void PageMonet(float w)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float fs = ImGui::GetFontSize();
    ImVec2 a = ImGui::GetCursorScreenPos(), b(a.x + w, a.y + fs * 3.8f);
    VPill(dl, a, b, IM_COL32(24, 27, 32, 200), VAlpha(kVTeal, 0.45f));
    dl->AddRectFilled(ImVec2(a.x, a.y + UI::S(10)), ImVec2(a.x + UI::S(5), b.y - UI::S(10)), kVTeal);
    dl->AddText(UI::Font(), fs * 1.3f, ImVec2(a.x + UI::S(26), a.y + fs * 0.5f), IM_COL32_WHITE, "MonetLoader for MP 3.9.1-mp");
    dl->AddText(ImVec2(a.x + UI::S(26), a.y + fs * 2.2f), kVDim, "built into libMP32.so / libMP64.so - no separate libmonetloader.so");
    ImGui::Dummy(ImVec2(w, fs * 3.8f + UI::S(12)));
    Heading("HOW IT WORKS");
    Label("MonetLoader (Lua scripts, moonloader/ folder) is inside libMP since 4.13.62: one file, nothing to install apart from "
          "libMP. The built-in copy has mimgui fixed (mimgui/cdefs.lua is built in, so 'undefined symbol: luaJIT_BC_mimgui_cdefs' "
          "is gone).", kVText, 0.88f);
    Label("An old separate libmonetloader.so in the mods folder is moved to libmonetloader.so.old by MP 4.13.64 on the first "
          "start, so it never runs twice - restart the game once when MP tells you.", kVText, 0.88f);
    Heading("SCRIPTS");
    Label("Put .lua scripts in Android/data/<game>/files/moonloader (or the mods folder's moonloader/). They load with the game. "
          "MonetLoader scripts are for San Andreas; in Vice City MP keeps the loader idle.", kVText, 0.88f);
}
static void PageHelp(float w)
{
    Heading("FLOATING BAR");
    Label("[ : ]  menu: Help, Server (this launcher), Player list - the same dropdown as San Andreas MP.", kVText, 0.88f);
    Label("[chat]  the MP chat text field + virtual keyboard over the game (Vice City already shows the chat text itself).", kVText, 0.88f);
    Label("[mic]  voice chat on / off.      [moon]  MonetLoader.      Drag the bar to move it (saved).", kVText, 0.88f);
    Heading("PLAYING");
    Label("1. Open GTA Vice City with MP installed (AML mods folder).  2. Tap [ : ] > Server.  3. Pick a server (the VICE City "
          "LAN server runs 24/7 and is always on top of Internet) and tap CONNECT. No login - you join straight away.", kVText, 0.88f);
    Label("Type /help in the chat for the server's own commands (they come from the server's gamemode).", kVText, 0.88f);
    Heading("NOTHING SHOWS?");
    Label("Allow 'Display over other apps' for VICE Loader (Android settings). The game link turns green in Settings > Connection "
          "when GTA Vice City runs MP 4.13.64 or newer.", kVText, 0.88f);
}
static void PageMods(float w)
{
    Label("Inside VICE Loader (the same mod set as San Andreas MP):", IM_COL32_WHITE, 1.0f);
    ImGui::Dummy(ImVec2(1, UI::S(6)));
    const char* names[][2] = {
        { "AML (AndroidModLoader)", "libAML.so - loads the mods into the game" },
        { "SAUtils / VCUtils", "libSAUtils.so - menus, settings and helpers for the mods" },
        { "MP (libMP)", "libMP32.so / libMP64.so - multiplayer, voice chat, VICE Loader bridge, built-in MonetLoader for MP 3.9.1-mp" },
        { "Dear ImGui 1.89.7", "the launcher / overlay UI (this window)" },
        { "Online lobby (HiveMQ)", "the Internet list and the relay to the 24/7 VICE City LAN server" } };
    for(auto& n : names) { Label(n[0], kVText, 0.95f); Label(n[1], kVDim, 0.78f); ImGui::Dummy(ImVec2(1, UI::S(4))); }
    ImGui::Dummy(ImVec2(1, UI::S(10)));
    Label("Files in the app (assets/bundle):", IM_COL32_WHITE, 0.95f);
    Label(s_Bundle.empty() ? "-" : s_Bundle.c_str(), kVDim, 0.75f);
    ImGui::Dummy(ImVec2(1, UI::S(10)));
    if(VBtn("exp", "EXPORT TO DOWNLOADS", UI::S(460), UI::S(66), 1, true, IC_BOX)) VL_JavaCall("export", "");
    Label("Copies everything to Download/ViceLoader: put the .so files in the GTA Vice City AML mods folder "
          "(Android/data/com.rockstargames.gtavc/mods).", kVDim, 0.78f);
}
static int s_nSetSec = 0;
static void PageSettings(float w)
{
    static const char* secs[] = { "Player", "Display", "Chat", "Sound", "Connection" };
    float x0 = ImGui::GetCursorPosX(), used = 0;
    for(int i = 0; i < 5; ++i)
    {
        float bw = ImMax(UI::S(150), SocTextW(secs[i], 1.0f) + UI::S(40));
        if(i && used + UI::S(8) + bw <= w) { ImGui::SameLine(0, UI::S(8)); used += UI::S(8); }   // 4.13.64: wraps on a narrow screen
        else if(i) { ImGui::SetCursorPosX(x0); used = 0; }
        if(VBtn(secs[i], secs[i], bw, UI::S(56), s_nSetSec == i ? 1 : 0)) s_nSetSec = i;
        used += bw;
    }
    ImGui::SetCursorPosX(x0);
    ImGui::Dummy(ImVec2(1, UI::S(8)));
    bool ch = false;
    switch(s_nSetSec)
    {
    case 0:
        Heading("PLAYER");
        Field("nk", "Nickname", s_P.nick, "Player", OnNick, MP_NAME_LEN - 1);
        Label("No login in VICE Loader: this name is used on every Vice City server.", kVDim, 0.8f);
        break;
    case 1:
        Heading("DISPLAY");
        ch |= Slider("##op", "Launcher opacity", &s_P.opacity, 0.4f, 1.0f);
        ch |= Slider("##sc", "UI size", &s_P.scale, 0.6f, 1.4f);
        ch |= Slider("##dim", "Darken the game behind the launcher", &s_P.dim, 0.0f, 0.95f);
        ch |= Check("Show the floating bar", &s_P.showBar);
        ch |= Slider("##bs", "Floating bar size", &s_P.barSize, 0.6f, 1.6f);
        if(VBtn("barreset", "RESET BAR POSITION", UI::S(420), UI::S(62), 0, true, IC_REFRESH)) VL_JavaCall("barreset", "");
        break;
    case 2:
        Heading("CHAT");
        ch |= Slider("##cs", "Chat text size", &s_P.chatSize, 0.6f, 1.6f);
        ch |= Check("Time in front of every line", &s_P.chatTime);
        ch |= Check("Colours ({RRGGBB} codes + player name colours)", &s_P.chatColors);
        { float k = (float)s_P.chatKeep; if(Slider("##ck", "Lines kept", &k, 20, 500, "%.0f")) { s_P.chatKeep = (int)k; ch = true; } }
        if(VBtn("clr", "CLEAR CHAT", UI::S(300), UI::S(62), 3, !s_Chat.empty(), IC_TRASH)) s_Chat.clear();
        break;
    case 3:
        Heading("SOUND");
        ch |= Check("Click sound on buttons (global_click, like San Andreas MP)", &s_P.click);
        ch |= Slider("##cv", "Click volume", &s_P.clickVol, 0.0f, 1.0f);
        if(VBtn("test", "TEST", UI::S(200), UI::S(62), 0)) VL_JavaCall("click", "");
        break;
    default:
    {
        Heading("CONNECTION");
        ch |= Check("Show San Andreas servers too (the game can't join them)", &s_P.showSA);
        ch |= Check("Online lobby: lowest-ping region", &s_P.lobbyAuto);
        if(!s_P.lobbyAuto)
            for(int i = 0; i < REGION_COUNT; ++i)
            {
                if(i) ImGui::SameLine(0, UI::S(8));
                if(VBtn(g_MPRegionNames[i], g_MPRegionNames[i], UI::S(170), UI::S(56), s_P.region == i ? 1 : 0)) { s_P.region = i; ch = true; }
            }
        char t[200];
        snprintf(t, sizeof(t), "Online lobby: %s %s", Online::IsConnected() ? "connected -" : "connecting...", Online::BrokerName());
        Label(t, Online::IsConnected() ? IM_COL32(95, 211, 95, 255) : kVDim, 0.85f);
        snprintf(t, sizeof(t), "Game link: %s", GameLinked() ? "connected (127.0.0.1:47790)" : "waiting for GTA Vice City with MP 4.13.64+");
        Label(t, GameLinked() ? IM_COL32(95, 211, 95, 255) : kVDim, 0.85f);
        ImGui::Dummy(ImVec2(1, UI::S(8)));
        if(VBtn("lgame", "LAUNCH GTA VICE CITY", UI::S(460), UI::S(66), 1, true, IC_PLAY)) VL_JavaCall("launch", "");
        ImGui::SameLine(0, UI::S(14));
        if(VBtn("leave", "DISCONNECT", UI::S(300), UI::S(66), 3, s_bGameOnline, IC_STOP)) Leave();
        break;
    }
    }
    if(ch) PrefsSave();
}
#define VL_ABOUT_YOUTUBE "https://youtube.com/@thirtyfoxmc"
#define VL_ABOUT_DISCORD "https://discord.gg/BbZ5xkmuYH"
#define VL_ABOUT_WEBSITE "https://mpsamp-open.github.io"
static void PageAbout(float W)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float fs = ImGui::GetFontSize();
    {
        ImVec2 a = ImGui::GetCursorScreenPos(), b(a.x + W, a.y + fs * 4.6f);
        VPill(dl, a, b, IM_COL32(24, 27, 32, 200), IM_COL32(255, 92, 180, 120));
        dl->AddRectFilled(ImVec2(a.x, a.y + UI::S(10)), ImVec2(a.x + UI::S(5), b.y - UI::S(10)), IM_COL32(255, 92, 180, 255));
        dl->AddText(UI::Font(), fs * 1.45f, ImVec2(a.x + UI::S(26), a.y + fs * 0.55f), IM_COL32_WHITE, "VICE Loader");
        dl->AddText(ImVec2(a.x + UI::S(26), a.y + fs * 2.45f), kVDim, "Version " VL_VERSION "   -   created by ThirtyFoxMC (ThirtyFoxMCx00E1)");
        dl->AddText(ImVec2(a.x + UI::S(26), a.y + fs * 3.35f), kVDim, "The MP Multiplayer launcher for GTA Vice City on Android");
        ImGui::Dummy(ImVec2(W, fs * 4.6f + UI::S(12)));
    }
    const float bh = UI::S(72), bw = (W - UI::S(24)) / 3.0f;
    ImVec2 r = ImGui::GetCursorScreenPos();
    if(VBtn("abyt", "YOUTUBE  @thirtyfoxmc", bw, bh, 1, true, IC_PLAY)) VL_JavaCall("url", VL_ABOUT_YOUTUBE);
    ImGui::SetCursorScreenPos(ImVec2(r.x + bw + UI::S(12), r.y));
    if(VBtn("abdc", "DISCORD SERVER", bw, bh, 2, true, IC_CHAT)) VL_JavaCall("url", VL_ABOUT_DISCORD);
    ImGui::SetCursorScreenPos(ImVec2(r.x + (bw + UI::S(12)) * 2, r.y));
    if(VBtn("abgh", "OFFICIAL WEBSITE", bw, bh, 0, true, IC_GLOBE)) VL_JavaCall("url", VL_ABOUT_WEBSITE);
    ImGui::SetCursorScreenPos(ImVec2(r.x, r.y + bh + UI::S(6)));
    Label("Website: " VL_ABOUT_WEBSITE "     YouTube: " VL_ABOUT_YOUTUBE "     Discord: " VL_ABOUT_DISCORD, kVDim, 0.75f);

    Heading("ABOUT VICE LOADER");
    Label("VICE Loader brings the San Andreas MP interface to GTA Vice City: the floating bar (menu, chat, voice, MonetLoader), "
          "the server browser with the 24/7 VICE City LAN server, the chat text field and MP virtual keyboard, the player list, "
          "voice chat and settings - drawn with Dear ImGui as a 'display over other apps' overlay, because Vice City has no "
          "floating menu of its own. No login: CONNECT joins directly.", kVText, 0.88f);
    Heading("ABOUT THIS MOD");
    Label("MP Multiplayer is an independent, non-commercial project designed, programmed and maintained by one developer, "
          "ThirtyFoxMC. The networking and sync, the server browser and launcher, the game modes, the dedicated server and "
          "the tools behind them are the result of many months of daily work and testing on real phones.", kVText, 0.88f);
    Heading("AUTHORSHIP AND CREDIT");
    Label("MP Multiplayer and VICE Loader, their code, interface, artwork and names are the original work of ThirtyFoxMC. All "
          "rights are reserved by the author. You may share the official download link and make videos and reviews as long as "
          "the author is credited. Re-uploads need the author's permission and visible credit; removing the author's name, "
          "renaming it as a new project or selling it is not allowed. Edited versions must say they are based on MP "
          "Multiplayer by ThirtyFoxMC.", kVText, 0.88f);
    Heading("THIRD-PARTY COMPONENTS");
    Label("GTA Vice City and GTA San Andreas are the property of Rockstar Games; MP is a fan-made mod and is not affiliated with "
          "or endorsed by Rockstar Games or Take-Two Interactive. MP uses AndroidModLoader, SAUtils, MonetLoader (OSS), Dear "
          "ImGui, mbedTLS, Opus, SQLite and other open-source libraries under their own licences.", kVText, 0.88f);
    Label("(c) ThirtyFoxMC (ThirtyFoxMCx00E1). All rights reserved.   powered by HiveMQ/ThirtyFoxMC", kVDim, 0.78f);
    ImGui::Dummy(ImVec2(1, UI::S(20)));
}

// ======================================================================= the ⋮ dropdown (floatmenu.cpp RenderDropdown)
static void DropdownIcon(ImDrawList* dl, int kind, ImVec2 c, float s, ImU32 col)
{
    switch(kind)
    {
        case 0:
        {
            dl->AddCircle(c, s * 0.5f, col, 20, UI::S(2.5f));
            ImVec2 ts = UI::Font()->CalcTextSizeA(s * 0.75f, FLT_MAX, 0, "?");
            dl->AddText(UI::Font(), s * 0.75f, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), col, "?");
            break;
        }
        case 1:
            for(int k = 0; k < 3; ++k)
            {
                float y = c.y - s * 0.45f + k * s * 0.32f;
                dl->AddRect(ImVec2(c.x - s * 0.45f, y), ImVec2(c.x + s * 0.45f, y + s * 0.24f), col, UI::S(3), 0, UI::S(2.2f));
                dl->AddCircleFilled(ImVec2(c.x + s * 0.28f, y + s * 0.12f), UI::S(2.4f), col);
            }
            break;
        default:
            dl->AddCircleFilled(ImVec2(c.x - s * 0.18f, c.y - s * 0.18f), s * 0.17f, col);
            dl->AddCircleFilled(ImVec2(c.x + s * 0.22f, c.y - s * 0.12f), s * 0.14f, col);
            dl->PathArcTo(ImVec2(c.x - s * 0.18f, c.y + s * 0.42f), s * 0.32f, IM_PI, IM_PI * 2, 14); dl->PathFillConvex(col);
            dl->PathArcTo(ImVec2(c.x + s * 0.24f, c.y + s * 0.42f), s * 0.25f, IM_PI, IM_PI * 2, 14); dl->PathFillConvex(col);
            break;
    }
}
static void Dropdown(const ImVec2& ds)
{
    const float rowH = UI::S(74), w = UI::S(380);
    float x = ImClamp(s_BarX, UI::S(8), ds.x - w - UI::S(8));
    float y = s_BarH > 0 ? s_BarY + s_BarH + UI::S(10) : UI::S(100);
    if(y + rowH * 3 + UI::S(20) > ds.y) y = ImMax(UI::S(8), s_BarY - rowH * 3 - UI::S(30));   // the bar is low on the screen: open upwards
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, rowH * 3 + UI::S(20)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(UI::S(10), UI::S(10)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, UI::S(14));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0.66f));
    if(ImGui::Begin("##VLDropdown", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar))
    {
        static const char* names[3] = { "Help", "Server", "Player list" };
        static const int   tabs[3] = { VT_HELP, VT_INTERNET, VT_PLAYERS };
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for(int k = 0; k < 3; ++k)
        {
            ImGui::PushID(k);
            ImVec2 p = ImGui::GetCursorScreenPos();
            float rw = ImGui::GetContentRegionAvail().x;
            bool hit = ImGui::InvisibleButton("##dd", ImVec2(rw, rowH));
            if(ImGui::IsItemActive()) dl->AddRectFilled(p, ImVec2(p.x + rw, p.y + rowH), IM_COL32(255, 255, 255, 40), UI::S(12));
            DropdownIcon(dl, k, ImVec2(p.x + UI::S(38), p.y + rowH * 0.5f), UI::S(34), IM_COL32_WHITE);
            float fs = UI::S(30);
            dl->AddText(UI::Font(), fs, ImVec2(p.x + UI::S(76), p.y + (rowH - fs) * 0.5f), IM_COL32_WHITE, names[k]);
            if(hit)
            {   // 4.13.64: Help / Player list = San Andreas page windows over the game; Server = the MP launcher
                if(tabs[k] == VT_PLAYERS || tabs[k] == VT_HELP) { s_nPage = tabs[k] == VT_PLAYERS ? 0 : 1; s_nMode = VL_MODE_PAGE; s_nPageFrame = ImGui::GetFrameCount(); s_nPlSel = -1; }
                else { s_nTab = tabs[k]; s_nMode = VL_MODE_PANEL; }
            }
            ImGui::PopID();
        }
        if(ImGui::IsMouseClicked(0) && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && ImGui::GetFrameCount() > s_nMenuFrame + 1)
            s_nMode = VL_MODE_BUBBLE;   // tap anywhere else = close
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}
// ======================================================================= San Andreas page windows (4.13.64)
// ⋮ > Player list / Help open a window over the game like San Andreas MP (src/vc/vc_gui.inc PagePlayers / PageHelp),
// not the MP launcher. Tap outside or X = back to the game.
static ImU32 VlAbgr(uint32_t c) { return IM_COL32(c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF, 255); }
static void PageWindow(const ImVec2& ds)
{
    const bool port = ds.y > ds.x;
    const ImVec2 sz = port ? ImVec2(ds.x * 0.94f, ds.y * 0.56f) : ImVec2(ds.x * 0.50f, ds.y * 0.72f);
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, ds.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(sz, ImGuiCond_Appearing);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(UI::S(16), UI::S(12)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, UI::S(10));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(UI::S(12), UI::S(10)));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.06f, 0.07f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.10f, 0.10f, 0.11f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.10f, 0.10f, 0.11f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, 0.12f));
    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(1, 1, 1, 0.15f));
    bool open = true;
    const char* title = s_nPage == 0 ? "Player list##VLPlayers" : "Help##VLHelp";
    if(ImGui::Begin(title, &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings))
    {
        if(s_nPage == 0)
        {
            char sn[160];
            if(s_bGameOnline) snprintf(sn, sizeof(sn), "%s  -  %s", s_GameServer.empty() ? "Vice City" : s_GameServer.c_str(), s_GameHost.c_str());
            else snprintf(sn, sizeof(sn), "%s", GameLinked() ? "Not connected" : "GTA Vice City (MP) is not running");
            ImGui::TextUnformatted(sn);
            char pl[32]; snprintf(pl, sizeof(pl), "Players: %d", s_bGameOnline ? (int)s_Players.size() : 0);
            ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(pl).x); ImGui::TextUnformatted(pl);
            ImGui::Separator();
            ImGui::BeginChild("##vlsb", ImVec2(0, -UI::S(150)), false);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float rowH = UI::S(54), W = ImGui::GetContentRegionAvail().x;
            const float cName = W * 0.12f, cScore = W * 0.66f, cPing = W * 0.83f;
            {
                ImVec2 p = ImGui::GetCursorScreenPos(); const ImU32 tc = IM_COL32(200, 200, 200, 255);
                dl->AddText(ImVec2(p.x + UI::S(10), p.y), tc, "id"); dl->AddText(ImVec2(p.x + cName, p.y), tc, "name");
                dl->AddText(ImVec2(p.x + cScore, p.y), tc, "score"); dl->AddText(ImVec2(p.x + cPing, p.y), tc, "ping");
                ImGui::Dummy(ImVec2(W, ImGui::GetTextLineHeight() + UI::S(6)));
            }
            int n = 0;
            if(s_bGameOnline)
                for(auto& p : s_Players)
                {
                    ImGui::PushID(p.id);
                    ImVec2 a = ImGui::GetCursorScreenPos();
                    if(ImGui::InvisibleButton("##row", ImVec2(W, rowH))) s_nPlSel = s_nPlSel == p.id ? -1 : p.id;
                    const bool sel = s_nPlSel == p.id, me = (p.flags & 16) || p.id == s_nMyId;
                    if(sel) dl->AddRectFilled(a, ImVec2(a.x + W, a.y + rowH), IM_COL32(190, 25, 25, 215), UI::S(8));
                    else if(n % 2 == 1) dl->AddRectFilled(a, ImVec2(a.x + W, a.y + rowH), IM_COL32(255, 255, 255, 12), UI::S(8));
                    const float ty = a.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f;
                    char b[96]; snprintf(b, sizeof(b), "%d", p.id);
                    dl->AddText(ImVec2(a.x + UI::S(10), ty), IM_COL32_WHITE, b);
                    snprintf(b, sizeof(b), "%s%s%s", p.name.c_str(), me ? " (you)" : "", (p.flags & 1) ? "  (paused)" : "");
                    dl->PushClipRect(ImVec2(a.x + cName, a.y), ImVec2(a.x + cScore - UI::S(50), a.y + rowH), true);
                    dl->AddText(ImVec2(a.x + cName, ty), sel || me ? IM_COL32_WHITE : VlAbgr(p.color), b);
                    dl->PopClipRect();
                    const ImVec2 vc(a.x + cScore - UI::S(34), a.y + rowH * 0.5f);
                    if(p.flags & 4) UI::DrawSpeaker(dl, vc, UI::S(13), IM_COL32(230, 230, 230, 255), 0, true);
                    else if(p.flags & 2) UI::DrawSpeaker(dl, vc, UI::S(13), IM_COL32_WHITE, 2, false);
                    snprintf(b, sizeof(b), "%d", p.score); dl->AddText(ImVec2(a.x + cScore, ty), IM_COL32_WHITE, b);
                    snprintf(b, sizeof(b), "%d", p.ping);  dl->AddText(ImVec2(a.x + cPing, ty), IM_COL32_WHITE, b);
                    ImGui::PopID(); ++n;
                }
            if(!n) ImGui::TextDisabled("Not connected. ... > Server > pick a Vice City server > CONNECT.");
            LDragScroll();
            ImGui::EndChild();
            ImGui::Separator();
            const VlPlayer* sp = nullptr; for(auto& p : s_Players) if(p.id == s_nPlSel) sp = &p;
            if(sp && !(sp->flags & 16) && sp->id != s_nMyId)
            {
                ImGui::Text("%s  |  HP %d  |  armour %d%s", sp->name.c_str(), sp->health, sp->armour, (sp->flags & 8) ? "  |  in a car" : "");
                const bool muted = (sp->flags & 4) != 0;
                ImGui::PushStyleColor(ImGuiCol_Button, muted ? ImVec4(0.70f, 0.12f, 0.12f, 0.95f) : ImVec4(0.18f, 0.18f, 0.18f, 0.95f));
                if(ImGui::Button(muted ? "Unmute voice##vm" : "Mute voice##vm", ImVec2(-1, UI::S(64))))
                {
                    BrSend("MUTE %d", sp->id);
                    for(auto& p : s_Players) if(p.id == sp->id) p.flags ^= 4;   // the next PLAYERS2 confirms it
                }
                ImGui::PopStyleColor();
            }
            else ImGui::TextDisabled("Tap a player to see their health or mute their voice.");
        }
        else
        {
            ImGui::BeginChild("##vlhelp", ImVec2(0, 0), false);
            PageHelp(ImGui::GetContentRegionAvail().x);
            LDragScroll();
            ImGui::EndChild();
        }
        if(ImGui::IsMouseClicked(0) && !ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)
           && !ImGui::IsAnyItemActive() && ImGui::GetFrameCount() > s_nPageFrame + 1)
            open = false;   // tap outside the window = back to the game
    }
    ImGui::End();
    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar(3);
    if(!open) s_nMode = VL_MODE_BUBBLE;
}
// ======================================================================= region pin (the San Andreas LRegionPicker, 4.13.64)
static bool s_bRegionPick = false; static uint64_t s_nRegionToggleMs = 0;
static ImVec2 s_vRegionBtn(0, 0); static float s_fRegionBtn = 0;
static void RegionChoose(int i)   // i < 0 = Auto
{
    if(i < 0) { s_P.lobbyAuto = true; const int best = Online::BestRegion(); if(best >= 0) s_P.region = best; }
    else { s_P.lobbyAuto = false; s_P.region = i; }
    PrefsSave();
    Online::SetRegionPrefs(s_P.region, s_P.lobbyAuto);
    char m[96]; snprintf(m, sizeof(m), i < 0 ? "Region: Auto (lowest ping)" : "Region: %s", i < 0 ? "" : Online::RegionLabel(i));
    VL_JavaCall("toast", m);
}
static void RegionPicker()
{
    if(!s_bRegionPick) return;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float fs1 = ImMax(UI::S(26), ImGui::GetFontSize() * 1.08f), fs2 = ImMax(UI::S(19), ImGui::GetFontSize() * 0.86f);
    const float rowH = ImMax(UI::S(62), fs1 * 2.0f), gap = UI::S(6), pw = UI::S(12);
    float wantW;
    {
        ImFont* f = ImGui::GetFont();
        wantW = pw * 2 + UI::S(34) + f->CalcTextSizeA(fs1, FLT_MAX, 0, "North America - Los Angeles").x + UI::S(28) + f->CalcTextSizeA(fs2, FLT_MAX, 0, "offline  |  888 servers").x + UI::S(16);
    }
    const float w = ImMin(ImMax(ImMax(UI::S(470), ds.x * 0.26f), wantW), ds.x - UI::S(20));
    const float h = pw * 2 + (REGION_COUNT + 1) * rowH + REGION_COUNT * gap;
    float x = s_vRegionBtn.x + s_fRegionBtn - w, y = s_vRegionBtn.y + s_fRegionBtn + UI::S(8);
    if(x < UI::S(10)) x = UI::S(10);
    if(x + w > ds.x - UI::S(10)) x = ds.x - UI::S(10) - w;
    if(y + h > ds.y - UI::S(10)) y = ImMax(UI::S(10), ds.y - UI::S(10) - h);
    int cnt[REGION_COUNT] = { 0 }, all = 0;
    for(auto& sv : Online::Snapshot()) if(sv.token != 0 && sv.region >= 0 && sv.region < REGION_COUNT) { ++cnt[sv.region]; ++all; }
    const int cur = Online::CurrentRegion();
    ImGui::SetNextWindowPos(ImVec2(x, y)); ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pw, pw));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, UI::S(1));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(36 / 255.f, 40 / 255.f, 46 / 255.f, 250 / 255.f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 200 / 255.f));
    ImGui::SetNextWindowFocus();
    if(ImGui::Begin("##VLRegionPick", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetWindowPos();
        ImFont* rf = ImGui::GetFont();
        for(int i = -1; i < REGION_COUNT; ++i)
        {
            ImVec2 a(p.x + pw, p.y + pw + (i + 1) * (rowH + gap)), b(a.x + w - pw * 2, a.y + rowH);
            ImGui::SetCursorScreenPos(a);
            ImGui::PushID(i + 100);
            const bool tap = ImGui::InvisibleButton("##reg", ImVec2(b.x - a.x, rowH)), held = ImGui::IsItemActive();
            ImGui::PopID();
            const bool sel = i < 0 ? s_P.lobbyAuto : (!s_P.lobbyAuto && s_P.region == i);
            dl->AddRectFilled(a, b, ((i + 1) & 1) ? IM_COL32(30, 34, 40, 170) : IM_COL32(44, 49, 56, 170));
            if(sel) { dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 26)); dl->AddRectFilled(a, ImVec2(a.x + UI::S(4), b.y), kVTeal); }
            if(held) dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 14));
            char l1[48], l2[64];
            if(i < 0)
            {
                const int best = Online::BestRegion();
                snprintf(l1, sizeof(l1), "Auto");
                if(best >= 0) snprintf(l2, sizeof(l2), "%s  |  %d servers", Online::RegionLabel(best), all); else snprintf(l2, sizeof(l2), "lowest ping  |  %d", all);
            }
            else
            {
                snprintf(l1, sizeof(l1), "%s", Online::RegionLabel(i));
                const int ms = Online::RegionPingMs(i), same = Online::RegionSameAs(i);
                if(same >= 0) snprintf(l2, sizeof(l2), "same server as %s  |  %d", g_MPRegionNames[same], cnt[i]);
                else if(ms >= 0) snprintf(l2, sizeof(l2), "%d ms  |  %d servers", ms, cnt[i]);
                else snprintf(l2, sizeof(l2), "%s  |  %d servers", ms == -2 ? "offline" : "-- ms", cnt[i]);
            }
            const ImVec2 s2 = rf->CalcTextSizeA(fs2, FLT_MAX, 0, l2);
            dl->PushClipRect(a, ImVec2(b.x - UI::S(24) - s2.x, b.y), true);
            dl->AddText(rf, fs1, ImVec2(a.x + UI::S(34), a.y + (rowH - fs1) * 0.5f), sel ? IM_COL32_WHITE : kVText, l1);
            dl->PopClipRect();
            dl->AddText(rf, fs2, ImVec2(b.x - UI::S(16) - s2.x, a.y + (rowH - fs2) * 0.5f), kVDim, l2);
            if(i >= 0 && cur == i) dl->AddCircleFilled(ImVec2(a.x + UI::S(18), a.y + rowH * 0.5f), UI::S(6), IM_COL32(34, 197, 94, 255));   // lobby broker in use
            if(tap) { if(!sel) RegionChoose(i); s_bRegionPick = false; }
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
    if(s_bRegionPick && ImGui::IsMouseClicked(0) && MP_NowMs() - s_nRegionToggleMs > 300
       && !ImGui::IsMouseHoveringRect(ImVec2(x, y), ImVec2(x + w, y + h), false)
       && !ImGui::IsMouseHoveringRect(s_vRegionBtn, ImVec2(s_vRegionBtn.x + s_fRegionBtn, s_vRegionBtn.y + s_fRegionBtn), false))
        s_bRegionPick = false;   // tap anywhere else closes the list
}
// ======================================================================= the launcher
static void Launcher(float dw, float dh)
{
    const ImVec2 ds(dw, dh);
    const float mx = ds.x * 0.025f, my = ds.y * 0.03f;
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    bg->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(0, 0, 0, (int)(255 * s_P.dim)));
    VBackdrop(bg, ImVec2(mx, my), ImVec2(ds.x - mx, ds.y - my));
    ImGui::SetNextWindowPos(ImVec2(mx, my));
    ImGui::SetNextWindowSize(ImVec2(ds.x - mx * 2, ds.y - my * 2));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, UI::S(1));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0.6f));
    ImGui::Begin("##VLLauncher", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    const float pad = UI::S(16);
    VGlass(dl, wp, ImVec2(wp.x + ws.x, wp.y + ws.y));
    VPushWidgetStyle();

    // ---- title bar ----
    const float titleH = UI::S(70);
    {
        float tx = wp.x + pad + UI::S(6), ty = wp.y + (titleH - ImGui::GetTextLineHeight() * 1.25f) * 0.5f;
        dl->AddText(UI::Font(), ImGui::GetFontSize() * 1.25f, ImVec2(tx, ty), IM_COL32_WHITE, "PLAY");
        float px = tx + ImGui::CalcTextSize("PLAY").x * 1.25f + UI::S(22), ty2 = wp.y + (titleH - ImGui::GetTextLineHeight()) * 0.5f;
        const char* sub = "MP Launcher  v" VL_VERSION "  -  GTA Vice City (VICE Loader)";
        dl->AddText(ImVec2(px, ty2), kVDim, sub);
        ImGui::SetCursorScreenPos(ImVec2(wp.x + ws.x - titleH - UI::S(6), wp.y));
        if(LToolButton("close", IC_CLOSE, nullptr, titleH)) s_nMode = VL_MODE_BUBBLE;
        // the player (no login: just the nickname) + game link dot
        const char* st = GameLinked() ? (s_bGameOnline ? "In game - online" : "In game") : "Game not running";
        float right = wp.x + ws.x - titleH - UI::S(24), nw = ImMax(ImGui::CalcTextSize(s_P.nick).x, ImGui::CalcTextSize(st).x * 0.78f);
        float nx = right - nw;
        dl->AddText(ImVec2(nx, wp.y + UI::S(8)), IM_COL32_WHITE, s_P.nick);
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.78f, ImVec2(nx, wp.y + UI::S(40)), GameLinked() ? IM_COL32(95, 211, 95, 255) : IM_COL32(150, 150, 150, 255), st);
        dl->AddCircleFilled(ImVec2(nx - UI::S(20), wp.y + titleH * 0.5f), UI::S(7), GameLinked() ? IM_COL32(67, 181, 129, 255) : IM_COL32(116, 127, 141, 255), 16);
        const char* wm = "powered by HiveMQ/ThirtyFoxMC";
        float wx = nx - UI::S(60) - ImGui::CalcTextSize(wm).x * 0.8f;
        if(wx > px + ImGui::CalcTextSize(sub).x + UI::S(30)) dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.8f, ImVec2(wx, ty2 + UI::S(3)), IM_COL32(95, 211, 95, 110), wm);
        dl->AddLine(ImVec2(wp.x, wp.y + titleH), ImVec2(wp.x + ws.x, wp.y + titleH), kVLine, UI::S(1));
    }
    // ---- data ----
    ServerList::Poll();
    if(MP_NowMs() - ServerList::LastRefreshMs() > 10000) ServerList::Refresh();
    if(!s_bScanning && MP_NowMs() - s_nScanAt > 15000) ScanLan();
    const bool listTab = s_nTab == VT_INTERNET || s_nTab == VT_FEATURED || s_nTab == VT_FAV || s_nTab == VT_LAN;
    std::vector<VlRow> rows; if(listTab) BuildRows(s_nTab, rows);
    const VlRow* sel = nullptr;
    { char key[96]; for(auto& r : rows) { RowKey(r, key, sizeof(key)); if(!strcmp(key, s_szSel)) { sel = &r; break; } } }

    // ---- top tabs + tools ----
    const float tbH = UI::S(78), btnH = UI::S(60);
    const float sideW = ImMax(UI::S(300), ws.x * 0.17f);
    {
        float y = wp.y + titleH + (tbH - btnH) * 0.5f, x = wp.x + UI::S(10) + sideW + UI::S(18);
        static const char* const kTitleMax = "MonetLoader";   // the longest page title
        if(!listTab)
        {
            static const char* const kNames[VT_COUNT] = { "", "", "", "", "Add server", "Chat", "Players", "Voice chat", "MonetLoader", "Help", "Mods", "Settings", "About" };
            dl->AddText(ImVec2(x, y + (btnH - ImGui::GetTextLineHeight()) * 0.5f), IM_COL32_WHITE, kNames[s_nTab]);
        }
        else
        {
            struct { const char* t; int tab; } tabs[4] = { { "Internet", VT_INTERNET }, { "Featured", VT_FEATURED }, { "Favorites", VT_FAV }, { "Lan", VT_LAN } };
            // 4.13.64: the tab padding shrinks on a narrow (portrait) panel so "Lan" never runs under the
            // refresh / region pin / gear / about buttons
            const float toolsX = wp.x + ws.x - pad - btnH * 4 - UI::S(6) * 4 - UI::S(8);
            float textW = 0; for(int i = 0; i < 4; ++i) textW += ImGui::CalcTextSize(tabs[i].t).x + UI::S(6);
            const float tpad = ImClamp((toolsX - x - textW) / 4.0f, UI::S(10), UI::S(44));
            for(int i = 0; i < 4; ++i)
            {
                ImVec2 ts = ImGui::CalcTextSize(tabs[i].t); float tw = ts.x + tpad;
                ImVec2 ta(x, y), tb(x + tw, y + btnH);
                ImGui::SetCursorScreenPos(ta); ImGui::PushID(950 + i);
                bool on = s_nTab == tabs[i].tab;
                if(ImGui::InvisibleButton("##ttab", ImVec2(tw, btnH), L_PRESS_ON_CLICK) && !on) { s_nTab = tabs[i].tab; s_szSel[0] = 0; if(s_nTab == VT_LAN) ScanLan(); }
                bool held = ImGui::IsItemActive(); ImGui::PopID();
                if(held) dl->AddRectFilled(ta, tb, IM_COL32(255, 255, 255, 18));
                dl->AddText(ImVec2(x + (tw - ts.x) * 0.5f, y + (btnH - ts.y) * 0.5f), on ? IM_COL32_WHITE : IM_COL32(165, 172, 180, 255), tabs[i].t);
                if(on) dl->AddRectFilled(ImVec2(ta.x + UI::S(8), tb.y - UI::S(3)), ImVec2(tb.x - UI::S(8), tb.y), IM_COL32_WHITE);
                x += tw + UI::S(6);
            }
        }
        // 4.13.64: the search box takes only the room left after the tabs (portrait / small screens: it shrinks,
        // then hides) - it was drawn over "Featured" / "Favorites" in the emulator test
        if(!listTab) x += ImGui::CalcTextSize(kTitleMax).x;
        float right = wp.x + ws.x - pad - btnH * 4 - UI::S(6) * 4;   // refresh, region pin, gear, about
        float searchW = ImMin(ImMax(UI::S(260), ws.x * 0.16f), right - UI::S(12) - x);
        if(searchW >= UI::S(150))
        {
            ImGui::SetCursorScreenPos(ImVec2(right - searchW, y));
            if(LInputBox("search", s_szSearch, "Search", searchW, btnH, IC_SEARCH)) VKeyboard::Open("Search server", s_szSearch, 31, OnSearch);
        }
        ImGui::SetCursorScreenPos(ImVec2(right, y));
        ImGui::Dummy(ImVec2(0, btnH));
        ImGui::SameLine(0, UI::S(6));
        if(LToolButton("refresh", IC_REFRESH, nullptr, btnH)) { ServerList::Refresh(); ScanLan(); }
        ImGui::SameLine(0, UI::S(6));
        // 4.13.64: the region pin of San Andreas (online lobby region: Auto / Asia / Europe / NA / Oceania)
        s_vRegionBtn = ImGui::GetCursorScreenPos(); s_fRegionBtn = btnH;
        if(LToolButton("region", IC_REGION, nullptr, btnH, true, s_bRegionPick) && MP_NowMs() - s_nRegionToggleMs > 400)
        { s_bRegionPick = !s_bRegionPick; s_nRegionToggleMs = MP_NowMs(); }
        ImGui::SameLine(0, UI::S(6));
        if(LToolButton("gear", IC_GEAR, nullptr, btnH, true, s_nTab == VT_SETTINGS)) s_nTab = VT_SETTINGS;
        ImGui::SameLine(0, UI::S(6));
        if(LToolButton("help", IC_INFO, nullptr, btnH, true, s_nTab == VT_ABOUT)) s_nTab = VT_ABOUT;
        dl->AddLine(ImVec2(wp.x, wp.y + titleH + tbH), ImVec2(wp.x + ws.x, wp.y + titleH + tbH), kVLine, UI::S(1));
    }
    // ---- body ----
    const float bodyY = wp.y + titleH + tbH + UI::S(2), barH = UI::S(88);
    const float bodyH = wp.y + ws.y - bodyY - UI::S(6) - barH;
    ImGui::SetCursorScreenPos(ImVec2(wp.x + UI::S(10), bodyY + UI::S(8)));
    ImGui::BeginChild("##vlside", ImVec2(sideW, bodyH - UI::S(10)), false);
    {
        float iw = ImGui::GetContentRegionAvail().x, ih = UI::S(70);
        LGroupLabel("SERVERS");
        if(LSideItem("inet", IC_GLOBE, "Internet", listTab, iw, ih)) { if(!listTab) s_nTab = VT_INTERNET; }
        if(LSideItem("add", IC_PLUS, "Add server", s_nTab == VT_ADD, iw, ih)) s_nTab = VT_ADD;
        LGroupLabel("PLAY");
        if(LSideItem("chat", IC_CHAT, "Chat", s_nTab == VT_CHAT, iw, ih)) s_nTab = VT_CHAT;
        if(LSideItem("pl", IC_USERS, "Players", s_nTab == VT_PLAYERS, iw, ih)) s_nTab = VT_PLAYERS;
        if(LSideItem("voice", IC_BELL, "Voice chat", s_nTab == VT_VOICE, iw, ih)) s_nTab = VT_VOICE;
        LGroupLabel("LOADER");
        if(LSideItem("monet", IC_FLAG, "MonetLoader", s_nTab == VT_MONET, iw, ih)) s_nTab = VT_MONET;
        if(LSideItem("mods", IC_BOX, "Mods", s_nTab == VT_MODS, iw, ih)) s_nTab = VT_MODS;
        if(LSideItem("set", IC_SLIDERS, "Settings", s_nTab == VT_SETTINGS, iw, ih)) s_nTab = VT_SETTINGS;
        LGroupLabel("INFO");
        if(LSideItem("help", IC_LIST, "Help", s_nTab == VT_HELP, iw, ih)) s_nTab = VT_HELP;
        if(LSideItem("about", IC_INFO, "About", s_nTab == VT_ABOUT, iw, ih)) s_nTab = VT_ABOUT;
        LDragScroll();
    }
    ImGui::EndChild();
    const float cx0 = wp.x + UI::S(10) + sideW + UI::S(8);
    dl->AddLine(ImVec2(cx0, bodyY), ImVec2(cx0, wp.y + ws.y), kVLine, UI::S(2));
    const float contentW = wp.x + ws.x - cx0 - UI::S(4);
    if(listTab)
    {
        ImGui::SetCursorScreenPos(ImVec2(cx0 + UI::S(4), bodyY + UI::S(4)));
        Table(rows, contentW - UI::S(10), bodyH - UI::S(4));
        sel = nullptr;
        { char key[96]; for(auto& r : rows) { RowKey(r, key, sizeof(key)); if(!strcmp(key, s_szSel)) { sel = &r; break; } } }
    }
    else
    {
        ImGui::SetCursorScreenPos(ImVec2(cx0 + pad, bodyY + pad));
        const float pw = contentW - pad * 2, ph = bodyH - pad;
        ImGui::BeginChild("##vlpage", ImVec2(pw, ph), false);
        switch(s_nTab)
        {
        case VT_ADD: PageAdd(pw); break;
        case VT_VOICE: PageVoice(pw); break;
        case VT_MONET: PageMonet(pw); break;
        case VT_HELP: PageHelp(pw); break;
        case VT_CHAT: PageChat(pw, ph); break;
        case VT_PLAYERS: PagePlayers(pw); break;
        case VT_MODS: PageMods(pw); break;
        case VT_SETTINGS: PageSettings(pw); break;
        default: PageAbout(pw); break;
        }
        if(s_nTab != VT_CHAT) LDragScroll();
        ImGui::EndChild();
    }
    // ---- bottom bar: status | nickname | CONNECT ----
    {
        ImVec2 ba(wp.x, wp.y + ws.y - barH), bb(wp.x + ws.x, wp.y + ws.y);
        dl->AddRectFilled(ba, bb, IM_COL32(24, 27, 32, 220));
        dl->AddLine(ba, ImVec2(bb.x, ba.y), IM_COL32(0, 0, 0, 160), UI::S(1));
        const float bh = barH - UI::S(26), by = ba.y + UI::S(13);
        char st[200];
        int n = 0, pl = 0; for(auto& r : rows) { ++n; if(r.players > 0) pl += r.players; }
        if(listTab) snprintf(st, sizeof(st), "%d server%s   %d player%s%s", n, n == 1 ? "" : "s", pl, pl == 1 ? "" : "s", s_nTab == VT_LAN ? (s_bScanning ? "   searching..." : "") : (Online::IsConnected() ? "" : "   lobby connecting..."));
        else if(s_bGameOnline) snprintf(st, sizeof(st), "Connected: %s", s_GameServer.c_str());
        else snprintf(st, sizeof(st), "%s", GameLinked() ? "Not connected" : "GTA Vice City is not running");
        dl->AddText(ImVec2(ba.x + pad + UI::S(6), by + (bh - ImGui::GetTextLineHeight()) * 0.5f), kVDim, st);
        float cw = ImMax(UI::S(300), ws.x * 0.18f), nickW = ImMax(UI::S(260), ws.x * 0.15f);
        ImGui::SetCursorScreenPos(ImVec2(bb.x - pad - cw - UI::S(14) - nickW, by));
        if(LInputBox("nick", s_P.nick, "Nickname", nickW, bh)) VKeyboard::Open("Nickname", s_P.nick, MP_NAME_LEN - 1, OnNick);
        const bool onSel = sel && OnRow(*sel);
        const char* cl = onSel ? "DISCONNECT" : "CONNECT";
        const bool can = onSel || (sel != nullptr);
        ImVec2 ca(bb.x - pad - cw, by), cb(bb.x - pad, by + bh);
        ImGui::SetCursorScreenPos(ca);
        if(!can) ImGui::BeginDisabled();
        bool hit = ImGui::InvisibleButton("##connect", ImVec2(cw, bh));
        bool held = ImGui::IsItemActive();
        if(!can) ImGui::EndDisabled();
        ImU32 g0 = onSel ? IM_COL32(170, 58, 46, 255) : kVGreen, g1 = onSel ? IM_COL32(120, 36, 28, 255) : IM_COL32(44, 110, 26, 255);
        if(!can) { g0 = IM_COL32(70, 76, 84, 255); g1 = IM_COL32(52, 56, 62, 255); }
        if(held) g0 = VAlpha(g0, 0.8f);
        dl->AddRectFilledMultiColor(ca, cb, g0, g0, g1, g1);
        dl->AddRect(ca, cb, can ? IM_COL32(150, 220, 120, onSel ? 0 : 160) : IM_COL32(255, 255, 255, 30));
        ImVec2 ts = ImGui::CalcTextSize(cl);
        dl->AddText(UI::Font(), ImGui::GetFontSize() * 1.1f, ImVec2((ca.x + cb.x - ts.x * 1.1f) * 0.5f, (ca.y + cb.y - ts.y * 1.1f) * 0.5f), can ? IM_COL32_WHITE : IM_COL32(150, 150, 150, 255), cl);
        if(hit && can) { if(onSel) Leave(); else Join(*sel); }
    }
    VPopWidgetStyle();
    ImGui::End();
    ImGui::PopStyleColor(3); ImGui::PopStyleVar(3);
    RegionPicker();
}

// ======================================================================= entry points (vl_main.cpp)
void VL_PanelInit()
{
    PrefsLoad();
    ServerList::Load((std::string(VL_FilesDir()) + "/vc_servers.txt").c_str());
    BrOpen();
    BrKeepAlive();
    Online::SetRegionPrefs(s_P.region, s_P.lobbyAuto);
    Online::Start((std::string(VL_FilesDir()) + "/broker.txt").c_str());   // the Internet list (24/7 VICE City LAN server) + relay
    AddChat(0xFFFFC080, "VICE Loader " VL_VERSION ": the chat of GTA Vice City MP shows here.");
    PrefsSave();   // hands the bar / click prefs to Java
}
void VL_Open(int mode)
{
    if(mode != VL_MODE_CHAT && VKeyboard::IsOpen()) VKeyboard::Close();   // 4.13.64: a bar button / page closes the chat keyboard first
    if(mode == VL_MODE_CHAT) { s_nMode = VL_MODE_CHAT; OpenChatKeyboard(); }
    else if(mode == VL_MODE_BUBBLE) s_nMode = VL_MODE_BUBBLE;   // back to the game (only the floating bar)
    else if(mode == VL_MODE_MENU) { s_nMode = s_nMode == VL_MODE_MENU ? VL_MODE_BUBBLE : VL_MODE_MENU; s_nMenuFrame = ImGui::GetFrameCount(); }
    else if(mode == 200 || mode == 201) { s_nPage = mode - 200; s_nMode = VL_MODE_PAGE; s_nPageFrame = ImGui::GetFrameCount(); s_nPlSel = -1; }   // 4.13.64: 200 Player list, 201 Help window
    else if(mode == VL_MODE_PAGE) { s_nMode = VL_MODE_PAGE; s_nPageFrame = ImGui::GetFrameCount(); }
    else if(mode >= 100) { s_nTab = ImClamp(mode - 100, 0, VT_COUNT - 1); s_nMode = VL_MODE_PANEL; }   // a page straight from the bar (100 + tab)
    else s_nMode = VL_MODE_PANEL;
}
int VL_Mode() { return s_nMode; }
void VL_SetPref(const char* key, const char* value)
{
    if(!strcmp(key, "bundle")) s_Bundle = value ? value : "";
    else if(!strcmp(key, "tab") && value) { s_nTab = ImClamp(atoi(value), 0, VT_COUNT - 1); s_nMode = VL_MODE_PANEL; }
    else if(!strcmp(key, "test") && value)
    {   // emulator test hooks (MainActivity --es open region | detail)
        s_nMode = VL_MODE_PANEL; s_nTab = VT_INTERNET;
        if(!strcmp(value, "region")) { s_bRegionPick = true; s_nRegionToggleMs = MP_NowMs(); }
        else if(!strcmp(value, "detail")) s_bTestSelFirst = true;
    }
    else if(!strcmp(key, "crashtest")) { logger->Info("crash test (native)"); volatile int* p = nullptr; *p = 1; }   // checks vl_crash.cpp
    else if(!strcmp(key, "mic")) { if(GameLinked()) BrSend("MIC"); else AddChat(0xFF6060FF, "Voice chat: start GTA Vice City with MP and join a server first."); }
    else if(!strcmp(key, "bar") && value) sscanf(value, "%f %f %f %f", &s_BarX, &s_BarY, &s_BarW, &s_BarH);
}
void VL_PanelFrame(float w, float h)
{
    BrPoll();
    Online::SetRegionPrefs(s_P.region, s_P.lobbyAuto);
    ImGui::GetStyle().Alpha = s_P.opacity;
    if(s_nMode == VL_MODE_PANEL) Launcher(w, h);
    else if(s_nMode == VL_MODE_MENU) Dropdown(ImVec2(w, h));
    else if(s_nMode == VL_MODE_PAGE) PageWindow(ImVec2(w, h));
    if(VKeyboard::IsOpen()) VKeyboard::Render();
    else if(s_nMode == VL_MODE_CHAT) s_nMode = VL_MODE_BUBBLE;   // the chat keyboard closed: back to the game
    // 4.13.64: global_click like San Andreas MP (ui/hud.cpp GlobalClickTick): a button / key was just pressed
    ImGuiContext& g = *GImGui;
    if(s_P.click && g.ActiveId != 0 && g.ActiveIdIsJustActivated && !g.MovingWindow) VL_JavaCall("click", "");
}
