// VICE Loader: the San Andreas MP launcher (src/mp_launcher.inc) rebuilt for GTA Vice City as a "display over other
// apps" overlay. Same look (vl_kit.inc is copied from the SA launcher), same pages: server list (Internet / Favorites
// / Lan), Add server, the VICE City LAN server, Chat (text field + MP virtual keyboard), Players, Mods, Settings, About.
// No login / authentication: CONNECT joins directly.
//
// It drives the game through the VICE Loader bridge (src/vc/vc_overlay.inc, UDP 127.0.0.1:47790) and can host the
// VICE City LAN server itself (MP's own server, net/server.cpp, in Vice City mode) - a different server from the San
// Andreas one: own name, port 7777, own files, only Vice City phones can join.
#include "ui_common.h"
#include "vkeyboard.h"
#include "emoji.h"
#include "vl.h"
#include "net/protocol.h"
#include "net/server.h"
#include "net/serverlist.h"
#include "net/lan.h"
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

// ======================================================================= prefs (files/viceloader.ini)
struct VlPrefs
{
    char  nick[MP_NAME_LEN] = "Player";
    char  hostName[32] = VL_LAN_NAME;
    int   hostPort = VL_LAN_PORT, hostMax = 50;
    float opacity = 1.0f, scale = 1.0f;
    bool  showSA = false;      // San Andreas servers in the lists too (the game refuses them; off = Vice City only)
    bool  hostAuto = false;    // start the VICE City LAN server when VICE Loader starts
};
static VlPrefs s_P;
static std::string PrefPath() { return std::string(VL_FilesDir()) + "/viceloader.ini"; }
static void PrefsSave()
{
    FILE* f = fopen(PrefPath().c_str(), "w"); if(!f) return;
    fprintf(f, "nick=%s\nhostName=%s\nhostPort=%d\nhostMax=%d\nopacity=%.2f\nscale=%.2f\nshowSA=%d\nhostAuto=%d\n",
            s_P.nick, s_P.hostName, s_P.hostPort, s_P.hostMax, s_P.opacity, s_P.scale, s_P.showSA ? 1 : 0, s_P.hostAuto ? 1 : 0);
    fclose(f);
}
static void PrefsLoad()
{
    FILE* f = fopen(PrefPath().c_str(), "r"); if(!f) return;
    char l[256];
    while(fgets(l, sizeof(l), f))
    {
        l[strcspn(l, "\r\n")] = 0; char* v = strchr(l, '='); if(!v) continue; *v++ = 0;
        if(!strcmp(l, "nick") && v[0]) MP_CopyStr(s_P.nick, v, sizeof(s_P.nick));
        else if(!strcmp(l, "hostName") && v[0]) MP_CopyStr(s_P.hostName, v, sizeof(s_P.hostName));
        else if(!strcmp(l, "hostPort")) s_P.hostPort = atoi(v);
        else if(!strcmp(l, "hostMax")) s_P.hostMax = atoi(v);
        else if(!strcmp(l, "opacity")) s_P.opacity = (float)atof(v);
        else if(!strcmp(l, "scale")) s_P.scale = (float)atof(v);
        else if(!strcmp(l, "showSA")) s_P.showSA = atoi(v) != 0;
        else if(!strcmp(l, "hostAuto")) s_P.hostAuto = atoi(v) != 0;
    }
    fclose(f);
    if(s_P.hostPort <= 0 || s_P.hostPort > 65535) s_P.hostPort = VL_LAN_PORT;
    s_P.hostMax = ImClamp(s_P.hostMax, 2, MP_STD_MAX_PLAYERS);
    s_P.opacity = ImClamp(s_P.opacity, 0.4f, 1.0f); s_P.scale = ImClamp(s_P.scale, 0.6f, 1.4f);
}
float VL_UserScale() { return s_P.scale; }

// ======================================================================= bridge to the game (vc_overlay.inc)
struct VlChat { uint32_t argb; std::string text; };
struct VlPlayer { int id, score, ping; std::string name; };
static int  s_nSock = -1;
static uint64_t s_nGameSeen = 0, s_nNextHello = 0, s_nNextPing = 0;
static bool s_bGameOnline = false; static int s_nGamePort = 0, s_nMyId = -1;
static std::string s_GameHost, s_GameName, s_GameServer;
static bool s_bGameLan = false; static int s_nGameLanPort = 0; static std::string s_GameLanCount, s_GameLanName;
static std::deque<VlChat> s_Chat;
static std::vector<VlPlayer> s_Players;
static bool GameLinked() { return s_nGameSeen && MP_NowMs() - s_nGameSeen < 5000; }

static void BrSend(const char* fmt, ...)
{
    if(s_nSock < 0) return;
    char b[1024]; va_list ap; va_start(ap, fmt); int n = vsnprintf(b, sizeof(b), fmt, ap); va_end(ap);
    if(n <= 0) return; if(n >= (int)sizeof(b)) n = sizeof(b) - 1;
    sockaddr_in a; memset(&a, 0, sizeof(a)); a.sin_family = AF_INET; a.sin_port = htons(VL_BRIDGE_PORT); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sendto(s_nSock, b, n, MSG_DONTWAIT, (sockaddr*)&a, sizeof(a));
}
static void BrOpen()
{
    if(s_nSock >= 0) return;
    int s = socket(AF_INET, SOCK_DGRAM, 0); if(s < 0) return;
    sockaddr_in a; memset(&a, 0, sizeof(a)); a.sin_family = AF_INET; a.sin_port = 0; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if(bind(s, (sockaddr*)&a, sizeof(a)) != 0) { close(s); return; }
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
    s_nSock = s;
}
static void AddChat(uint32_t argb, const std::string& t) { s_Chat.push_back({ argb, UI::EmojiToPUA(t.c_str()) }); while(s_Chat.size() > 200) s_Chat.pop_front(); }
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
            if(sscanf(l, "%d,%d,%d,%n", &pl.id, &pl.score, &pl.ping, &off) >= 3) { pl.name = l + off; s_Players.push_back(pl); }
        }
    }
    else if(!strncmp(m, "LAN ", 4))
    {
        int on = 0, port = 0, off = 0; char cnt[24] = "";
        if(sscanf(m + 4, "%d %d %23s %n", &on, &port, cnt, &off) >= 3) { s_bGameLan = on != 0; s_nGameLanPort = port; s_GameLanCount = cnt; s_GameLanName = m + 4 + off; }
    }
    else if(!strncmp(m, "BYE", 3)) s_nGameSeen = 0;
}
static void BrPoll()
{
    BrOpen(); if(s_nSock < 0) return;
    for(int k = 0; k < 32; ++k)
    {
        char b[1500]; sockaddr_in from; socklen_t fl = sizeof(from);
        int n = recvfrom(s_nSock, b, sizeof(b) - 1, 0, (sockaddr*)&from, &fl);
        if(n <= 0) break;
        if(from.sin_addr.s_addr != htonl(INADDR_LOOPBACK) || ntohs(from.sin_port) != VL_BRIDGE_PORT) continue;
        b[n] = 0; s_nGameSeen = MP_NowMs(); BrHandle(b);
    }
    const uint64_t now = MP_NowMs();
    if(!GameLinked()) { if(now >= s_nNextHello) { s_nNextHello = now + 2000; BrSend("HELLO"); } }
    else if(now >= s_nNextPing) { s_nNextPing = now + 3000; BrSend("PING"); }
}

// ======================================================================= the VICE City LAN server hosted in this app
static MPServer s_Host;
static std::string s_HostLog;
static std::mutex s_HostLogMx;
static void HostLog(const char* msg)
{
    logger->Info("[VICE City LAN] %s", msg);
    std::lock_guard<std::mutex> lk(s_HostLogMx);
    s_HostLog += msg; s_HostLog += "\n";
    if(s_HostLog.size() > 6000) s_HostLog.erase(0, s_HostLog.size() - 5000);
}
static bool HostStart()
{
    if(s_Host.IsRunning()) s_Host.Stop();
    std::string dir = std::string(VL_FilesDir()) + "/vclan";
    mkdir(dir.c_str(), 0700);
    if(chdir(dir.c_str()) != 0) logger->Error("VICE Loader: no folder %s", dir.c_str());
    s_Host.SetGame(MP_GAME_VC);
    s_Host.SetPersistent(true);
    s_Host.SetPassword("");
    s_Host.SetModFile((dir + "/vclan_moderators.txt").c_str());
    if(!s_Host.VcmpLoad((dir + "/vclan_vcmp.ini").c_str())) HostLog("no vclan_vcmp.ini: free roam");
    s_Host.SetDetails("", 0, "Freeroam", MP_GAME_VC_TAG ",lan", "Vice City LAN server hosted by VICE Loader");
    if(!s_Host.Start((uint16_t)s_P.hostPort, s_P.hostName, HostLog, s_P.hostMax)) { HostLog("port busy - the game or another app hosts on it"); return false; }
    HostLog("VICE City LAN server is up");
    return true;
}
static void HostStop() { if(s_Host.IsRunning()) { s_Host.Stop(); HostLog("stopped"); } }

// ======================================================================= server lists
struct VlRow
{
    int kind;            // 0 saved, 1 found on the LAN
    int idx;             // saved: ServerList index
    char name[32], host[64], mode[MP_MODE_LEN], tags[MP_TAGS_LEN], pass[MP_PASSWORD_LEN + 1];
    int port, players, maxPlayers, ping; bool online, locked, fav, vc;
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

enum { VT_INTERNET, VT_FAV, VT_LAN, VT_ADD, VT_VCLAN, VT_CHAT, VT_PLAYERS, VT_MODS, VT_SETTINGS, VT_ABOUT };
static int  s_nTab = VT_INTERNET;
static char s_szSearch[32] = "";
static char s_szSel[96] = "";
static int  s_nMode = VL_MODE_BUBBLE;
static std::string s_Bundle;   // "name|bytes\n" of assets/bundle (from Java)
static bool s_bLanding = true;

static void RowKey(const VlRow& r, char* o, size_t n) { snprintf(o, n, "%d|%s|%s|%d", r.kind, r.name, r.host, r.port); }
static void BuildRows(int tab, std::vector<VlRow>& rows)
{
    if(tab == VT_LAN)
    {
        std::lock_guard<std::mutex> lk(s_LanMx);
        for(auto& s : s_Lan)
        {
            VlRow r{}; r.kind = 1; r.idx = -1;
            MP_CopyStr(r.name, s.name, sizeof(r.name)); MP_CopyStr(r.host, s.ip, sizeof(r.host)); MP_CopyStr(r.mode, s.mode[0] ? s.mode : "Freeroam", sizeof(r.mode));
            MP_CopyStr(r.tags, s.tags, sizeof(r.tags));
            r.port = s.port; r.players = s.players; r.maxPlayers = s.maxPlayers; r.ping = s.ping; r.online = true; r.locked = s.locked; r.vc = IsVcTags(s.tags);
            if(!r.vc && !s_P.showSA) continue;
            rows.push_back(r);
        }
    }
    else
    {
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
        rows.erase(std::remove_if(rows.begin(), rows.end(), [](const VlRow& r) { return !strcasestr(r.name, s_szSearch) && !strcasestr(r.host, s_szSearch); }), rows.end());
    std::stable_sort(rows.begin(), rows.end(), [](const VlRow& a, const VlRow& b) { if(a.online != b.online) return a.online; return a.players > b.players; });
}
static bool OnRow(const VlRow& r) { return s_bGameOnline && s_nGamePort == r.port && (!strcasecmp(s_GameHost.c_str(), r.host) || (s_GameHost == "127.0.0.1" && r.kind == 1 && LAN::IsPrivateAddress(r.host))); }
static void Join(const char* host, int port, const char* pass)
{
    if(!GameLinked())
    {
        AddChat(0xFFFF6060, "GTA Vice City is not running with MP yet - opening the game. Join again when it is up.");
        VL_JavaCall("launch", "");
        return;
    }
    BrSend("NAME %s", s_P.nick);
    BrSend("JOIN %d %s\t%s", port, host, pass ? pass : "");
    s_nMode = VL_MODE_BUBBLE;   // back to the game
}

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
static void OnHostName(const char* t, void*) { if(t && t[0]) { MP_CopyStr(s_P.hostName, t, sizeof(s_P.hostName)); PrefsSave(); } }
static void OnHostPort(const char* t, void*) { int p = t ? atoi(t) : 0; if(p > 0 && p < 65536) { s_P.hostPort = p; PrefsSave(); } }
static void OnHostMax(const char* t, void*) { int p = t ? atoi(t) : 0; if(p >= 2) { s_P.hostMax = ImMin(p, (int)MP_STD_MAX_PLAYERS); PrefsSave(); } }

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
static void Card(float h, const char* title)
{
    ImVec2 p = ImGui::GetCursorScreenPos(); float w = ImGui::GetContentRegionAvail().x;
    VSlate(ImGui::GetWindowDrawList(), p, ImVec2(p.x + w, p.y + h), 0.9f);
    SocText(ImGui::GetWindowDrawList(), ImVec2(p.x + UI::S(18), p.y + UI::S(12)), IM_COL32_WHITE, title, 1.0f);
    ImGui::Dummy(ImVec2(w, UI::S(56)));
}

// ======================================================================= server table (the SA LTable look, fewer columns)
static void Table(std::vector<VlRow>& rows, float w, float h)
{
    ImGui::BeginChild("##vltable", ImVec2(w, h), false);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rh = UI::S(78), hh = UI::S(46);
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float cName = UI::S(70), cMode = w * 0.52f, cPl = w * 0.70f, cPing = w * 0.84f;
    dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + hh), IM_COL32(0, 0, 0, 70));
    const float fy = p0.y + (hh - ImGui::GetFontSize() * 0.8f) * 0.5f;
    SocText(dl, ImVec2(p0.x + cName, fy), kVDim, "SERVER", 0.8f);
    SocText(dl, ImVec2(p0.x + cMode, fy), kVDim, "GAMEMODE", 0.8f);
    SocText(dl, ImVec2(p0.x + cPl, fy), kVDim, "PLAYERS", 0.8f);
    SocText(dl, ImVec2(p0.x + cPing, fy), kVDim, "PING", 0.8f);
    ImGui::Dummy(ImVec2(w, hh));
    if(rows.empty())
    {
        ImGui::Dummy(ImVec2(1, UI::S(30)));
        const char* t = s_nTab == VT_LAN ? (s_bScanning ? "Looking for Vice City servers on your Wi-Fi / hotspot..." : "No Vice City LAN server found. Host one in VICE City LAN, or tap refresh.")
                       : s_nTab == VT_FAV ? "No favourite servers yet - tap the star on a server." : "No servers saved. Add one with Add server.";
        Label(t, kVDim, 1.0f);
    }
    char key[96];
    for(size_t i = 0; i < rows.size(); ++i)
    {
        VlRow& r = rows[i]; RowKey(r, key, sizeof(key));
        bool sel = !strcmp(key, s_szSel), on = OnRow(r);
        ImVec2 a = ImGui::GetCursorScreenPos(), b(a.x + w, a.y + rh);
        ImGui::PushID((int)i);
        if(ImGui::InvisibleButton("##row", ImVec2(w - UI::S(80), rh)))
        {
            static double s_dLast = 0; static char s_Last[96] = "";
            if(sel && !strcmp(s_Last, key) && ImGui::GetTime() - s_dLast < 0.45) Join(r.host, r.port, r.pass);   // double tap = connect
            MP_CopyStr(s_szSel, key, sizeof(s_szSel)); MP_CopyStr(s_Last, key, sizeof(s_Last)); s_dLast = ImGui::GetTime();
        }
        bool held = ImGui::IsItemActive();
        ImGui::SameLine(0, 0);
        bool star = r.kind == 0 && ImGui::InvisibleButton("##fav", ImVec2(UI::S(80), rh));
        ImGui::PopID();
        if(star) { ServerList::SetFav(r.idx, !r.fav); ServerList::Save(); r.fav = !r.fav; }
        if(sel) { dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 34)); dl->AddRectFilled(a, ImVec2(a.x + UI::S(4), b.y), kVTeal); }
        else if(held) dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 16));
        else if(i & 1) dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 6));
        dl->AddLine(ImVec2(a.x, b.y), b, kVLine, UI::S(1));
        const float ty = a.y + (rh - ImGui::GetTextLineHeight()) * 0.5f;
        LPadlock(dl, ImVec2(a.x + UI::S(36), a.y + rh * 0.5f), UI::S(13), r.locked, r.locked ? kVText : IM_COL32(120, 128, 136, 255));
        dl->PushClipRect(ImVec2(a.x + cName, a.y), ImVec2(a.x + cMode - UI::S(10), b.y), true);
        dl->AddText(ImVec2(a.x + cName, ty - UI::S(10)), r.online ? IM_COL32_WHITE : kVDim, r.name);
        char sub[96]; snprintf(sub, sizeof(sub), "%s:%d%s%s", r.host, r.port, r.vc ? "" : "   (San Andreas)", on ? "   - you are here" : "");
        SocText(dl, ImVec2(a.x + cName, ty + UI::S(22)), on ? IM_COL32(95, 211, 95, 255) : kVDim, sub, 0.72f);
        dl->PopClipRect();
        {
            const float ch = UI::S(38), cw = ImMin(SocTextW(r.mode, 0.75f) + UI::S(24), cPl - cMode - UI::S(16));
            ImVec2 ca(a.x + cMode, a.y + (rh - ch) * 0.5f), cb(ca.x + cw, ca.y + ch);
            dl->AddRectFilled(ca, cb, IM_COL32(126, 178, 226, 40)); dl->AddRect(ca, cb, IM_COL32(126, 178, 226, 120));
            dl->PushClipRect(ca, cb, true);
            SocText(dl, ImVec2(ca.x + UI::S(12), ca.y + (ch - ImGui::GetFontSize() * 0.75f) * 0.5f), kVText, r.mode, 0.75f);
            dl->PopClipRect();
        }
        char pl[24]; if(r.online) snprintf(pl, sizeof(pl), "%d / %d", r.players, r.maxPlayers); else snprintf(pl, sizeof(pl), "-");
        dl->AddText(ImVec2(a.x + cPl, ty), kVText, pl);
        char pg[16]; if(r.online && r.ping >= 0) snprintf(pg, sizeof(pg), "%d", r.ping); else snprintf(pg, sizeof(pg), r.online ? "?" : "offline");
        dl->AddText(ImVec2(a.x + cPing, ty), !r.online ? kVRed : r.ping < 80 ? IM_COL32(95, 211, 95, 255) : r.ping < 160 ? IM_COL32(230, 200, 80, 255) : kVRed, pg);
        if(r.kind == 0)
        {
            ImVec2 c(b.x - UI::S(40), a.y + rh * 0.5f), q[10];
            for(int k = 0; k < 10; ++k) { float ang = -1.5708f + k * 0.6283f, rr = (k & 1) ? UI::S(7) : UI::S(16); q[k] = ImVec2(c.x + cosf(ang) * rr, c.y + sinf(ang) * rr); }
            if(r.fav) dl->AddConvexPolyFilled(q, 10, IM_COL32(240, 200, 70, 255));   // concave: drawn as an outline + fill
            dl->AddPolyline(q, 10, r.fav ? IM_COL32(240, 200, 70, 255) : kVDim, ImDrawFlags_Closed, UI::S(2));
        }
    }
    LDragScroll();
    ImGui::EndChild();
}

// ======================================================================= pages
static void PageAdd(float w)
{
    Label("Add a Vice City server (its owner gives you the IP or host name and the port). Saved on this phone.", kVDim, 0.85f);
    ImGui::Dummy(ImVec2(1, UI::S(6)));
    Field("an", "Server name", s_AddName, "My Vice City server", OnAddName, 31);
    Field("ah", "IP / host", s_AddHost, "192.168.1.5  or  vc.example.com", OnAddHost, 63);
    Field("ap", "Port", s_AddPort, "7777", OnAddPort, 6, true);
    Field("aw", "Password (empty = none)", s_AddPass, "none", OnAddPass, MP_PASSWORD_LEN);
    if(VBtn("addsv", "ADD SERVER", UI::S(320), UI::S(66), 1, s_AddHost[0] != 0, IC_PLUS))
    {
        int port = atoi(s_AddPort); if(port <= 0 || port > 65535) port = VL_LAN_PORT;
        if(ServerList::Add(s_AddName[0] ? s_AddName : s_AddHost, s_AddHost, port, REGION_ASIA, s_AddPass))
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
static void PageVcLan(float w)
{
    char t[256];
    Label("The VICE City LAN server is MP's own server in Vice City mode - a different server from the San Andreas one "
          "(own name, port, players and files). Friends on the same Wi-Fi or your hotspot find it in Lan.", kVDim, 0.85f);
    ImGui::Dummy(ImVec2(1, UI::S(8)));
    Card(UI::S(470), "Host in VICE Loader  (keeps running while the overlay runs)");
    ImGui::Indent(UI::S(18));
    Field("hn", "Server name", s_P.hostName, VL_LAN_NAME, OnHostName, 31);
    char port[8], mx[8]; snprintf(port, sizeof(port), "%d", s_P.hostPort); snprintf(mx, sizeof(mx), "%d", s_P.hostMax);
    Field("hp", "Port", port, "7777", OnHostPort, 6, true);
    Field("hm", "Max players", mx, "50", OnHostMax, 3, true);
    if(!s_Host.IsRunning()) { if(VBtn("hst", "START SERVER", UI::S(320), UI::S(66), 1, true, IC_PLAY)) HostStart(); }
    else
    {
        if(VBtn("hsp", "STOP SERVER", UI::S(320), UI::S(66), 2, true, IC_STOP)) HostStop();
        ImGui::SameLine(0, UI::S(14));
        if(VBtn("hjn", "JOIN IT", UI::S(260), UI::S(66), 0, true, IC_PLAY)) Join("127.0.0.1", s_Host.GetPort(), "");
    }
    ImGui::Unindent(UI::S(18));
    ImGui::Dummy(ImVec2(1, UI::S(10)));
    if(s_Host.IsRunning()) snprintf(t, sizeof(t), "Running: \"%s\" on port %d  -  %d / %d players", s_Host.GetName(), (int)s_Host.GetPort(), s_Host.GetPlayerCount(), s_Host.GetMaxPlayers());
    else snprintf(t, sizeof(t), "Not running");
    Label(t, s_Host.IsRunning() ? IM_COL32(95, 211, 95, 255) : kVDim, 0.95f);
    {
        std::string ips;
        for(auto& f : LAN::Interfaces()) { in_addr ia; ia.s_addr = htonl(f.ip); ips += inet_ntoa(ia); ips += "  "; }
        snprintf(t, sizeof(t), "This phone: %s", ips.empty() ? "no Wi-Fi / hotspot" : ips.c_str());
        Label(t, kVDim, 0.85f);
    }
    ImGui::Dummy(ImVec2(1, UI::S(16)));
    Card(UI::S(250), "Host inside the game  (MP in GTA Vice City runs it)");
    ImGui::Indent(UI::S(18));
    snprintf(t, sizeof(t), "%s", !GameLinked() ? "GTA Vice City is not running" : s_bGameLan ? "" : "Not running");
    if(GameLinked() && s_bGameLan) snprintf(t, sizeof(t), "Running: \"%s\" on port %d  -  %s players", s_GameLanName.c_str(), s_nGameLanPort, s_GameLanCount.c_str());
    Label(t, s_bGameLan ? IM_COL32(95, 211, 95, 255) : kVDim, 0.95f);
    if(!s_bGameLan) { if(VBtn("gst", "START IN GAME", UI::S(320), UI::S(66), 1, GameLinked() && !s_Host.IsRunning(), IC_PLAY)) BrSend("LANSTART %d %d %s", s_P.hostPort, s_P.hostMax, s_P.hostName); }
    else if(VBtn("gsp", "STOP IN GAME", UI::S(320), UI::S(66), 2, true, IC_STOP)) BrSend("LANSTOP");
    if(s_Host.IsRunning()) Label("(the app's server already uses the port - stop it first to host in the game)", kVDim, 0.75f);
    ImGui::Unindent(UI::S(18));
    ImGui::Dummy(ImVec2(1, UI::S(16)));
    Label("Server log", IM_COL32_WHITE, 0.95f);
    std::lock_guard<std::mutex> lk(s_HostLogMx);
    Label(s_HostLog.empty() ? "-" : s_HostLog.c_str(), kVDim, 0.75f);
}
static void PageChat(float w, float h)
{
    const float inH = UI::S(66);
    ImGui::BeginChild("##chatlog", ImVec2(w, h - inH - UI::S(16)), false);
    for(auto& c : s_Chat)
    {
        ImU32 col = IM_COL32((c.argb >> 16) & 0xFF, (c.argb >> 8) & 0xFF, c.argb & 0xFF, 255);
        Label(c.text.c_str(), col, 0.9f);
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
    snprintf(t, sizeof(t), "%s", s_bGameOnline ? s_GameServer.c_str() : "Not on a server");
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
        dl->AddText(ImVec2(p.x + UI::S(110), p.y + UI::S(12)), me ? kVTeal : kVText, pl.name.c_str());
        snprintf(b, sizeof(b), "%d", pl.score); dl->AddText(ImVec2(p.x + w * 0.65f, p.y + UI::S(12)), kVText, b);
        snprintf(b, sizeof(b), "%d", pl.ping); dl->AddText(ImVec2(p.x + w * 0.82f, p.y + UI::S(12)), kVText, b);
        ImGui::Dummy(ImVec2(w, UI::S(56)));
    }
}
static void PageMods(float w)
{
    Label("Inside VICE Loader (the same mod set as San Andreas MP):", IM_COL32_WHITE, 1.0f);
    ImGui::Dummy(ImVec2(1, UI::S(6)));
    const char* names[][2] = {
        { "AML (AndroidModLoader)", "libAML.so - loads the mods into the game" },
        { "SAUtils / VCUtils", "libSAUtils.so - menus, settings and helpers for the mods" },
        { "MP (libMP)", "libMP32.so / libMP64.so - multiplayer, VICE City LAN server, VICE Loader bridge" },
        { "MonetLoader for MP 3.9.1", "libmonetloader.so - Lua scripts (moonloader/), mimgui fixed" },
        { "Dear ImGui 1.89.7", "the launcher / overlay UI (this window)" },
        { "PSDK / Pawn AMX / SQLite", "SA-MP / VC:MP gamemodes in the VICE City LAN server" } };
    for(auto& n : names) { Label(n[0], kVText, 0.95f); Label(n[1], kVDim, 0.78f); ImGui::Dummy(ImVec2(1, UI::S(4))); }
    ImGui::Dummy(ImVec2(1, UI::S(10)));
    Label("Files in the app (assets/bundle):", IM_COL32_WHITE, 0.95f);
    Label(s_Bundle.empty() ? "-" : s_Bundle.c_str(), kVDim, 0.75f);
    ImGui::Dummy(ImVec2(1, UI::S(10)));
    if(VBtn("exp", "EXPORT TO DOWNLOADS", UI::S(460), UI::S(66), 1, true, IC_BOX)) VL_JavaCall("export", "");
    Label("Copies everything to Download/ViceLoader: put the .so files in the GTA Vice City AML mods folder "
          "(Android/data/com.rockstargames.gtavc/mods) and moonloader/ next to them.", kVDim, 0.78f);
}
static void PageSettings(float w)
{
    Field("nk", "Nickname", s_P.nick, "Player", OnNick, MP_NAME_LEN - 1);
    Label("Overlay opacity");
    ImGui::SetNextItemWidth(ImMin(w, UI::S(760)));
    if(ImGui::SliderFloat("##op", &s_P.opacity, 0.4f, 1.0f, "%.2f")) PrefsSave();
    Label("UI size");
    ImGui::SetNextItemWidth(ImMin(w, UI::S(760)));
    if(ImGui::SliderFloat("##sc", &s_P.scale, 0.6f, 1.4f, "%.2f")) PrefsSave();
    ImGui::Dummy(ImVec2(1, UI::S(8)));
    if(ImGui::Checkbox("Show San Andreas servers too", &s_P.showSA)) PrefsSave();
    if(ImGui::Checkbox("Start the VICE City LAN server with VICE Loader", &s_P.hostAuto)) PrefsSave();
    ImGui::Dummy(ImVec2(1, UI::S(14)));
    if(VBtn("lgame", "LAUNCH GTA VICE CITY", UI::S(460), UI::S(66), 1, true, IC_PLAY)) VL_JavaCall("launch", "");
    ImGui::SameLine(0, UI::S(14));
    if(VBtn("leave", "DISCONNECT", UI::S(300), UI::S(66), 3, s_bGameOnline, IC_STOP)) BrSend("LEAVE");
    ImGui::Dummy(ImVec2(1, UI::S(14)));
    char t[200]; snprintf(t, sizeof(t), "Game link: %s", GameLinked() ? "connected (127.0.0.1:47790)" : "waiting for GTA Vice City with MP 4.13.63+");
    Label(t, GameLinked() ? IM_COL32(95, 211, 95, 255) : kVDim, 0.85f);
}
static void PageAbout(float w)
{
    Label("VICE Loader " VL_VERSION, IM_COL32_WHITE, 1.3f);
    Label("The San Andreas MP launcher for GTA Vice City - server list, LAN, chat, virtual keyboard, players and the "
          "VICE City LAN server - drawn as a display-over-apps overlay with Dear ImGui, because Vice City has no "
          "floating menu of its own. No login: CONNECT joins directly.", kVText, 0.9f);
    ImGui::Dummy(ImVec2(1, UI::S(10)));
    Label("Bubble: tap = launcher, hold = chat keyboard, drag = move it.", kVDim, 0.85f);
    Label("MP, MonetLoader for MP, AML, SAUtils, ImGui - github.com/ThirtyFoxMCx00E1", kVDim, 0.85f);
    Label("powered by HiveMQ/ThirtyFoxMC", IM_COL32(95, 211, 95, 160), 0.8f);
}

// ======================================================================= the launcher
static void Launcher(float dw, float dh)
{
    const ImVec2 ds(dw, dh);
    const float mx = ds.x * 0.025f, my = ds.y * 0.03f;
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    bg->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(0, 0, 0, (int)(150 * s_P.opacity)));
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
    const bool listTab = s_nTab == VT_INTERNET || s_nTab == VT_FAV || s_nTab == VT_LAN;
    std::vector<VlRow> rows; if(listTab) BuildRows(s_nTab, rows);
    const VlRow* sel = nullptr;
    { char key[96]; for(auto& r : rows) { RowKey(r, key, sizeof(key)); if(!strcmp(key, s_szSel)) { sel = &r; break; } } }

    // ---- top tabs + tools ----
    const float tbH = UI::S(78), btnH = UI::S(60);
    const float sideW = ImMax(UI::S(300), ws.x * 0.17f);
    {
        float y = wp.y + titleH + (tbH - btnH) * 0.5f, x = wp.x + UI::S(10) + sideW + UI::S(18);
        if(!listTab)
        {
            static const char* const kNames[] = { "", "", "", "Add server", "VICE City LAN server", "Chat", "Players", "Mods", "Settings", "About" };
            dl->AddText(ImVec2(x, y + (btnH - ImGui::GetTextLineHeight()) * 0.5f), IM_COL32_WHITE, kNames[s_nTab]);
        }
        else
        {
            struct { const char* t; int tab; } tabs[3] = { { "Internet", VT_INTERNET }, { "Favorites", VT_FAV }, { "Lan", VT_LAN } };
            for(int i = 0; i < 3; ++i)
            {
                ImVec2 ts = ImGui::CalcTextSize(tabs[i].t); float tw = ts.x + UI::S(44);
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
        float searchW = ImMax(UI::S(260), ws.x * 0.16f);
        float rx = wp.x + ws.x - pad - btnH * 3 - UI::S(6) * 3 - searchW;
        ImGui::SetCursorScreenPos(ImVec2(rx, y));
        if(LInputBox("search", s_szSearch, "Search", searchW, btnH, IC_SEARCH)) VKeyboard::Open("Search server", s_szSearch, 31, OnSearch);
        ImGui::SameLine(0, UI::S(6));
        if(LToolButton("refresh", IC_REFRESH, nullptr, btnH)) { ServerList::Refresh(); ScanLan(); }
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
        LGroupLabel("VICE CITY");
        if(LSideItem("vclan", IC_CLOUD, "VICE City LAN", s_nTab == VT_VCLAN, iw, ih)) s_nTab = VT_VCLAN;
        if(LSideItem("chat", IC_CHAT, "Chat", s_nTab == VT_CHAT, iw, ih)) s_nTab = VT_CHAT;
        if(LSideItem("pl", IC_USERS, "Players", s_nTab == VT_PLAYERS, iw, ih)) s_nTab = VT_PLAYERS;
        LGroupLabel("LOADER");
        if(LSideItem("mods", IC_BOX, "Mods", s_nTab == VT_MODS, iw, ih)) s_nTab = VT_MODS;
        if(LSideItem("set", IC_SLIDERS, "Settings", s_nTab == VT_SETTINGS, iw, ih)) s_nTab = VT_SETTINGS;
        LGroupLabel("INFO");
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
        case VT_VCLAN: PageVcLan(pw); break;
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
        if(listTab) snprintf(st, sizeof(st), "%d server%s   %d player%s%s", n, n == 1 ? "" : "s", pl, pl == 1 ? "" : "s", s_bScanning ? "   searching..." : "");
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
        if(hit && can) { if(onSel) BrSend("LEAVE"); else Join(sel->host, sel->port, sel->pass); }
    }
    VPopWidgetStyle();
    ImGui::End();
    ImGui::PopStyleColor(3); ImGui::PopStyleVar(3);
}

// ======================================================================= entry points (vl_main.cpp)
void VL_PanelInit()
{
    PrefsLoad();
    ServerList::Load((std::string(VL_FilesDir()) + "/vc_servers.txt").c_str());
    BrOpen();
    if(s_P.hostAuto) HostStart();
    AddChat(0xFF80C0FF, "VICE Loader " VL_VERSION ": the chat of GTA Vice City MP shows here.");
}
void VL_Open(int mode)
{
    if(mode == VL_MODE_CHAT) { s_nMode = VL_MODE_CHAT; OpenChatKeyboard(); }
    else s_nMode = VL_MODE_PANEL;
}
int VL_Mode() { return s_nMode; }
void VL_SetPref(const char* key, const char* value)
{
    if(!strcmp(key, "bundle")) s_Bundle = value ? value : "";
    else if(!strcmp(key, "tab") && value) { s_nTab = atoi(value); s_nMode = VL_MODE_PANEL; }
}
void VL_PanelFrame(float w, float h)
{
    BrPoll();
    ImGui::GetStyle().Alpha = s_P.opacity;
    if(s_nMode == VL_MODE_PANEL) Launcher(w, h);
    if(VKeyboard::IsOpen()) VKeyboard::Render();
    else if(s_nMode == VL_MODE_CHAT) s_nMode = VL_MODE_BUBBLE;   // the chat keyboard closed: back to the game
}
