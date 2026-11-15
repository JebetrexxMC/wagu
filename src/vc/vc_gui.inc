// 4.13.54: the San Andreas MP interface in GTA Vice City (included by vc_layer.cpp inside namespace VC, after the
// menu functions).
//
// Before this, Vice City had no ImGui at all: [MP] printed "1. Players 2. Server ..." into the chat and opened the
// phone keyboard with "MP menu - type the number" (2 digits). Server list dialogs and the class pick worked the
// same way. Now the same pieces San Andreas uses run here too:
//   - the RenderWare ImGui renderer (imgui_impl_renderware.cpp): libGTAVC.so is the same War Drum RenderWare build
//     and exports the same RW names, so InitRenderWareFunctions() fills them from libGTAVC.so
//   - FloatMenu (src/ui/floatmenu.cpp): the top bar  [ ... ][ chat ][ mic ][ MonetLoader ]  - same pills, same
//     drag, the ... dropdown (Help / Server / Player list)
//   - the MP Launcher look (src/ui/mpkit.h): the Server page is the full-screen launcher (server list with live
//     players / ping, Add server, Settings, About, nickname + CONNECT), the Player list and Help pages
//   - voice chat (src/audio/voice.cpp, Opus + OpenSL): the mic pill and its panel, distance volume
//   - SA-MP dialogs (list / message) and the VC:MP class pick as ImGui windows
// Text (server address, nickname, chat) still uses the phone keyboard. If a RW function is missing the layer
// stays on the old chat menu, so nothing can crash because of it.
} // namespace VC

#include "imgui.h"
#include "imgui_internal.h"
#include <mutex>
#include <atomic>
#include "../ui/floatmenu.h"
#include "../ui/ui_common.h"
#include "../ui/mpkit.h"
#include "../audio/voice.h"
#include "../net/serverlist.h"
#include "../../imgui/fredoka.h"   // static: this file gets its own copy (49 KB)
// the shared embedded ImGui renderer (global names, also used by the San Andreas code - which never runs in VC)
extern void*  pGameHandle;
extern float* nearScreenZ;
extern float* recipNearClip;
extern void (*SetScissorRect)(float*);
extern unsigned char arialData[766656];          // imgui/arial.h (compiled in src/ui/hud.cpp)
void InitRenderWareFunctions();
bool ImGui_ImplRenderWare_CreateDeviceObjects();
void ImGui_ImplRenderWare_NewFrame();
void ImGui_ImplRenderWare_RenderDrawData(ImDrawData* draw_data);
namespace MonetEmbed { void* Handle(); }
struct RwRaster;
extern RwRaster* g_FontRaster;   // imgui_impl_renderware.cpp

namespace VC
{
static bool     s_bGuiTried = false;
static std::atomic<bool> s_bGuiReady{false};   // read by the touch thread
static uint64_t s_nGuiLast = 0;
static float    s_fVcNearZ = 0.0f, s_fVcRecipNear = 1.0f;   // used when the CSprite2d globals are not exported
static bool     s_bVoicePanel = false, s_bMonetPage = false, s_bMicAsked = false, s_bVoiceInit = false;
static bool     s_bVoiceMuted[256] = {};
static uint64_t s_nVoiceHeardAt = 0;
static float    s_fVoiceVol = 1.0f;
static int      s_nListSel = -1;          // Player list selection
static bool     s_bListLoaded = false;

// ---- touch: AND_TouchEvent (Android UI thread) -> queue -> ImGui on the game thread ------------------------------
enum { TQ_POS, TQ_DOWN, TQ_UP };
struct VcTouchQ { int kind; float x, y; };
static std::mutex             s_TqMx;
static std::vector<VcTouchQ>  s_Tq;
static std::mutex             s_HitMx;
static std::vector<ImRect>    s_Hit;      // last frame's MP windows (hit test on the touch thread)
static std::recursive_mutex   s_TouchMx;
static bool  s_bSwallow[16] = {};
static int   s_nMouseFinger = -1, s_nClearMouse = 0;
static bool  s_bTouchHooked = false;
static bool  s_bScaleTouch = false; static float s_fTouchSX = 1.f, s_fTouchSY = 1.f;
static inline void TQ(int k, float x = 0, float y = 0)
{
    std::lock_guard<std::mutex> lk(s_TqMx);
    if(s_Tq.size() > 512) s_Tq.erase(s_Tq.begin(), s_Tq.begin() + 256);
    s_Tq.push_back({ k, x, y });
}
static void MapTouch(int& x, int& y)
{   // the game may render below the screen's resolution: the touches come in device pixels (like San Andreas)
    const int rw = ScreenW ? ScreenW() : 0, rh = ScreenH ? ScreenH() : 0;
    if(rw <= 0 || rh <= 0) return;
    if(!s_bScaleTouch && (x > rw + 2 || y > rh + 2))
    {
        int dw = 0, dh = 0; aml->GetDisplaySize(&dw, &dh);
        if(dw < dh) { int t = dw; dw = dh; dh = t; }
        if(dw > rw && dh > rh) { s_bScaleTouch = true; s_fTouchSX = (float)rw / dw; s_fTouchSY = (float)rh / dh; logger->Info("Vice City: touch scaling %dx%d -> %dx%d", dw, dh, rw, rh); }
    }
    if(s_bScaleTouch) { x = (int)(x * s_fTouchSX); y = (int)(y * s_fTouchSY); }
}
static bool HitTest(float x, float y)
{
    std::lock_guard<std::mutex> lk(s_HitMx);
    for(const ImRect& r : s_Hit) if(r.Contains(ImVec2(x, y))) return true;
    return false;
}
// true = MP used the touch (the game must not see it)
static bool GuiTouch(int type, int finger, int x, int y)
{
    if(!s_bGuiReady.load() || finger < 0 || finger >= 16) return false;
    std::lock_guard<std::recursive_mutex> lk(s_TouchMx);
    const bool rel = type == 1 || type == 4;
    // 1) the top bar pills (tap / drag), like San Andreas
    if(FloatMenu::OnTouch(rel ? 1 : type, finger, (float)x, (float)y))
    {
        s_bSwallow[finger] = !rel;
        if(rel && finger == s_nMouseFinger) { TQ(TQ_UP); s_nMouseFinger = -1; }
        return true;
    }
    // 2) ImGui windows (launcher, pages, dialogs)
    if(type == 2)
    {
        if(s_nMouseFinger >= 0 && (finger == s_nMouseFinger || FloatMenu::IsModal())) { TQ(TQ_UP); s_nMouseFinger = -1; }   // a lost release
        const bool want = FloatMenu::IsModal() || HitTest((float)x, (float)y);
        if(s_nMouseFinger < 0) { TQ(TQ_POS, (float)x, (float)y); TQ(TQ_DOWN); s_nMouseFinger = finger; }
        if(want) { s_bSwallow[finger] = true; return true; }
        return false;
    }
    if(type == 3) { if(finger == s_nMouseFinger) TQ(TQ_POS, (float)x, (float)y); return s_bSwallow[finger]; }
    if(rel)
    {
        if(finger == s_nMouseFinger) { TQ(TQ_POS, (float)x, (float)y); TQ(TQ_UP); s_nMouseFinger = -1; }
        const bool sw = s_bSwallow[finger]; s_bSwallow[finger] = false;
        return sw;
    }
    return false;
}
DECL_HOOKv(VC_AND_TouchEvent, int type, int finger, int x, int y)
{
    int mx = x, my = y; MapTouch(mx, my);
    if(GuiTouch(type, finger, mx, my)) return;
    VC_AND_TouchEvent(type, finger, x, y);
}
static void PollTouchFallback()
{   // no AND_TouchEvent hook: read finger 0 (the game also gets these touches)
    if(s_bTouchHooked || !PointerGetButton || !PointerGetCoords) return;
    static bool was = false;
    const int st = PointerGetButton(0, 0);
    const bool down = st == 2 || st == 3;
    int x = 0, y = 0; float pr = 0; PointerGetCoords(0, &x, &y, &pr);
    if(down && !was) GuiTouch(2, 0, x, y);
    else if(down) GuiTouch(3, 0, x, y);
    else if(was) GuiTouch(1, 0, x, y);
    was = down;
}
static void DrainTouches()
{
    static std::vector<VcTouchQ> q;
    { std::lock_guard<std::mutex> lk(s_TqMx); q.swap(s_Tq); }
    ImGuiIO& io = ImGui::GetIO();
    if(s_nClearMouse > 0 && q.empty() && --s_nClearMouse == 0) io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);   // no hover after a tap
    for(const VcTouchQ& e : q)
    {
        if(e.kind == TQ_POS) io.AddMousePosEvent(e.x, e.y);
        else if(e.kind == TQ_DOWN) { io.AddMouseButtonEvent(0, true); s_nClearMouse = 0; }
        else { io.AddMouseButtonEvent(0, false); s_nClearMouse = 3; }
    }
    q.clear();
}
static void PublishHits()
{
    static std::vector<ImRect> tmp; tmp.clear();
    ImGuiContext* g = ImGui::GetCurrentContext();
    for(ImGuiWindow* w : g->Windows)
    {
        if(!w->WasActive || w->Hidden || (w->Flags & (ImGuiWindowFlags_NoMouseInputs | ImGuiWindowFlags_Tooltip))) continue;
        tmp.push_back(w->Rect());
    }
    std::lock_guard<std::mutex> lk(s_HitMx); s_Hit.swap(tmp);
}

// ---- voice chat (same engine as San Andreas) ------------------------------------------------------------------------
static void VcVoiceSend(const void* pkt, int len) { if(s_Client.GetState() == NET_CONNECTED) s_Client.Send(pkt, len); }   // audio thread
static void VoiceEnsure() { if(!s_bVoiceInit) { s_bVoiceInit = true; Voice::Init(VcVoiceSend); } }
static void VcVoicePacket(int id, uint16_t seq, uint8_t flags, const uint8_t* opus, int len)
{
    if(id < 0 || id >= 256 || id == s_nMyId || !s_Players[id].used || s_bVoiceMuted[id]) return;
    s_nVoiceHeardAt = MP_NowMs();
    VoiceEnsure();
    Voice::OnPacket(id, seq, flags, opus, len);
}
static void VcVoiceReset()
{
    if(!s_bVoiceInit) return;
    if(Voice::MicOn()) Voice::StopMic();
    Voice::ResetAll(); Voice::SetOutput(false);
    memset(s_bVoiceMuted, 0, sizeof(s_bVoiceMuted));
}
static int VoiceMicState() { if(!Online()) return 0; if(!s_bVoiceInit || !Voice::MicOn()) return 1; return Voice::MicTalking() ? 3 : 2; }
static void VoiceMicToggle()
{
    VoiceEnsure();
    if(!Online()) { Chat(0xFF60E0FF, "Voice chat works on a server: connect first, then tap the mic."); return; }
    if(Voice::MicOn()) { Voice::StopMic(); Chat(0xFFC0C0C0, "*** Microphone off."); return; }
    if(!Voice::Available()) { Chat(0xFF6060FF, "Voice chat is not available on this phone (%s).", Voice::LastError()); return; }
    if(Voice::MicPermission() == Voice::PERM_ASK && !s_bMicAsked) { s_bMicAsked = true; Voice::RequestMicPermission(); Chat(0xFF60E0FF, "Allow the microphone, then tap the mic again."); return; }
    if(!Voice::StartMic((uint8_t)s_nMyId)) { Chat(0xFF6060FF, "Could not turn on the microphone: %s.", Voice::LastError()); return; }
    if(Voice::UsingHelper()) { Chat(0xFFC0C0C0, "*** Vice City can't use the microphone itself, the MP Voice app does it for you (install MP-Voice.apk from the MP package)."); Voice::OpenHelperApp(); return; }
    Chat(0xFF80FF80, "*** Microphone on: players near you hear you (louder up close, nothing past %d m).", (int)MP_VOICE_RANGE);
}
static void VoiceProcess()
{
    if(!Online()) { if(s_bVoiceInit) { if(Voice::MicOn()) Voice::StopMic(); Voice::SetOutput(false); } return; }
    const bool heard = s_nVoiceHeardAt && MP_NowMs() - s_nVoiceHeardAt < 60000;
    if(!s_bVoiceInit || !Voice::Available()) return;
    Voice::SetOutput(heard || Voice::MicOn());
    Voice::SetVolume(s_fVoiceVol);
    void* me = FindPlayerPed ? FindPlayerPed() : nullptr;
    const float* mp = me ? MatPos(me) : nullptr;
    for(int i = 0; i < 256; ++i)
    {
        const Remote& r = s_Players[i];
        if(!r.used || i == s_nMyId) continue;
        float g = 0.f;
        if(!s_bVoiceMuted[i] && mp && r.hasSync)
        {
            const float dx = r.cur[0] - mp[0], dy = r.cur[1] - mp[1], dz = r.cur[2] - mp[2];
            g = Voice::DistanceGain(sqrtf(dx * dx + dy * dy + dz * dz), MP_VOICE_RANGE);
        }
        Voice::SetGain(i, g);
    }
}

// ---- bar pills -------------------------------------------------------------------------------------------------------
static void PillChat(void*) { if(!s_bTyping) Ask(ASK_CHAT, "Chat - or a /command", "", MP_CHAT_LEN - 1, false); }
static void PillVoice(void*) { s_bVoicePanel = !s_bVoicePanel; VoiceEnsure(); }
static void PillMonet(void*)
{
    if(FloatMenu::IsPageOpen(FloatMenu::PAGE_MONET)) FloatMenu::ClosePage(FloatMenu::PAGE_MONET); else FloatMenu::OpenPage(FloatMenu::PAGE_MONET);
    s_bMonetPage = FloatMenu::IsPageOpen(FloatMenu::PAGE_MONET);
}

static ImVec4 Rgba(uint32_t c) { return ImVec4(((c >> 24) & 255) / 255.f, ((c >> 16) & 255) / 255.f, ((c >> 8) & 255) / 255.f, 1.f); }
static ImVec4 Abgr(uint32_t c) { return ImVec4((c & 255) / 255.f, ((c >> 8) & 255) / 255.f, ((c >> 16) & 255) / 255.f, 1.f); }

// ---- the launcher (⋮ -> Server) --------------------------------------------------------------------------------------
enum { VT_SERVERS = 0, VT_ADD, VT_SETTINGS, VT_ABOUT };
static int  s_nVTab = VT_SERVERS;
static int  s_nVSel = -1;
static char s_szAddHost[64] = "", s_szAddName[32] = "", s_szAddPass[MP_PASSWORD_LEN + 1] = "";
static int  s_nAddPort = 7777;
// launcher text fields go through the phone keyboard: these asks come back in PollTyping as ASK_L*
static void LauncherConnect(const char* host, int port, const char* pass)
{
    MP_CopyStr(cfg.host, host, sizeof(cfg.host)); cfg.port = port > 0 && port < 65536 ? port : 7777;
    MP_CopyStr(cfg.password, pass ? pass : "", sizeof(cfg.password));
    SaveSettings();
    Chat(0xFF80FF80, "Joining %s:%d", cfg.host, cfg.port);
    Reconnect();
    FloatMenu::ClosePage(FloatMenu::PAGE_SERVER);
}
static void LoadVcServers()
{
    if(s_bListLoaded) return;
    s_bListLoaded = true;
    char path[640]; snprintf(path, sizeof(path), "%s/vc_servers.txt", s_szDataDir);   // Vice City servers only (not the SA list)
    ServerList::Load(path);
    if(cfg.host[0] && ServerList::FindByHostPort(cfg.host, cfg.port) < 0) { ServerList::Add("My Vice City server", cfg.host, cfg.port, 0, cfg.password); ServerList::Save(); }
    ServerList::Refresh();
}
static void LTitle(ImDrawList* dl, ImVec2 wp, ImVec2 ws, float titleH, bool* open)
{
    const float pad = UI::S(16);
    float tx = wp.x + pad + UI::S(6), ty = wp.y + (titleH - ImGui::GetTextLineHeight() * 1.25f) * 0.5f;
    dl->AddText(UI::Font(), ImGui::GetFontSize() * 1.25f, ImVec2(tx, ty), IM_COL32_WHITE, "PLAY");
    float px = tx + ImGui::CalcTextSize("PLAY").x * 1.25f + UI::S(22);
    float ty2 = wp.y + (titleH - ImGui::GetTextLineHeight()) * 0.5f;
    dl->AddText(ImVec2(px, ty2), MPKit::Dim(), "MP Launcher  v" MP_VERSION_STR "  -  GTA Vice City");
    ImGui::SetCursorScreenPos(ImVec2(wp.x + ws.x - titleH - UI::S(6), wp.y));
    if(MPKit::ToolButton("close", MPKit::IC_CLOSE, nullptr, titleH)) *open = false;
    dl->AddLine(ImVec2(wp.x, wp.y + titleH), ImVec2(wp.x + ws.x, wp.y + titleH), MPKit::Line(), UI::S(1));
}
static void LServers(float W, float H)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rowH = UI::S(62);
    const float cName = 0, cPl = W * 0.52f, cPing = W * 0.68f, cMode = W * 0.80f;
    {   // column titles
        ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddText(ImVec2(p.x + cName + UI::S(12), p.y), MPKit::Dim(), "HostName");
        dl->AddText(ImVec2(p.x + cPl, p.y), MPKit::Dim(), "Players");
        dl->AddText(ImVec2(p.x + cPing, p.y), MPKit::Dim(), "Ping");
        dl->AddText(ImVec2(p.x + cMode, p.y), MPKit::Dim(), "Mode");
        ImGui::Dummy(ImVec2(W, ImGui::GetTextLineHeight() + UI::S(8)));
    }
    ImGui::BeginChild("##vcrows", ImVec2(W, H - ImGui::GetTextLineHeight() - UI::S(10)), false);
    const int n = ServerList::Count();
    for(int i = 0; i < n; ++i)
    {
        ServerEntry* e = ServerList::Get(i);
        if(!e) continue;
        ImGui::PushID(i);
        ImVec2 a = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        if(ImGui::InvisibleButton("##r", ImVec2(w, rowH)))
        {
            if(s_nVSel == i) LauncherConnect(e->host, e->port, e->password);   // tap again = join (like San Andreas)
            else s_nVSel = i;
        }
        const bool sel = s_nVSel == i;
        if(sel) dl->AddRectFilled(a, ImVec2(a.x + w, a.y + rowH), IM_COL32(255, 255, 255, 225));
        else if(i % 2) dl->AddRectFilled(a, ImVec2(a.x + w, a.y + rowH), IM_COL32(255, 255, 255, 10));
        const ImU32 tc = sel ? IM_COL32(15, 15, 15, 255) : MPKit::Text();
        const float ty = a.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f;
        char b[96];
        dl->AddCircleFilled(ImVec2(a.x + UI::S(14), a.y + rowH * 0.5f), UI::S(5), e->online ? IM_COL32(76, 200, 60, 255) : IM_COL32(120, 120, 120, 255));
        dl->AddText(ImVec2(a.x + UI::S(30), ty), tc, e->name);
        if(e->online) snprintf(b, sizeof(b), "%d / %d", e->players, e->maxPlayers); else snprintf(b, sizeof(b), "-");
        dl->AddText(ImVec2(a.x + cPl, ty), tc, b);
        if(e->online) snprintf(b, sizeof(b), "%d", e->ping); else snprintf(b, sizeof(b), "-");
        dl->AddText(ImVec2(a.x + cPing, ty), tc, b);
        dl->AddText(ImVec2(a.x + cMode, ty), sel ? tc : MPKit::Dim(), e->liveMode[0] ? e->liveMode : (e->mode[0] ? e->mode : "Vice City"));
        ImGui::PopID();
    }
    if(!n) ImGui::TextDisabled("No Vice City servers yet. Add one in + Add server (mp_server --game vc).");
    ImGui::EndChild();
}
static void LAdd(float W)
{
    MPKit::GroupLabel("ADD A VICE CITY SERVER");
    ImGui::TextWrapped("Vice City phones join Vice City servers only (mp_server --game vc). San Andreas servers refuse Vice City players.");
    const float bh = UI::S(70);
    char l[160];
    snprintf(l, sizeof(l), "Address:  %s", s_szAddHost[0] ? s_szAddHost : "(tap to type)");
    if(MPKit::Btn("ah", l, W, bh, 0)) Ask(ASK_LHOST, "Server address (IP or host)", s_szAddHost, sizeof(s_szAddHost) - 1, false);
    snprintf(l, sizeof(l), "Port:  %d", s_nAddPort);
    if(MPKit::Btn("ap", l, W, bh, 0)) { char p[8]; snprintf(p, sizeof(p), "%d", s_nAddPort); Ask(ASK_LPORT, "Port", p, 5, true); }
    snprintf(l, sizeof(l), "Name:  %s", s_szAddName[0] ? s_szAddName : "(optional)");
    if(MPKit::Btn("an", l, W, bh, 0)) Ask(ASK_LNAME, "Server name", s_szAddName, sizeof(s_szAddName) - 1, false);
    snprintf(l, sizeof(l), "Password:  %s", s_szAddPass[0] ? "********" : "(none)");
    if(MPKit::Btn("aw", l, W, bh, 0)) Ask(ASK_LPASS, "Join password (empty = none)", s_szAddPass, sizeof(s_szAddPass) - 1, false);
    ImGui::Dummy(ImVec2(1, UI::S(8)));
    const float hw = (W - UI::S(12)) * 0.5f;
    if(MPKit::Btn("aadd", "ADD SERVER", hw, bh, 1, s_szAddHost[0] != 0, MPKit::IC_PLUS))
    {
        if(ServerList::Add(s_szAddName[0] ? s_szAddName : s_szAddHost, s_szAddHost, s_nAddPort, 0, s_szAddPass)) { ServerList::Save(); ServerList::Refresh(); s_nVTab = VT_SERVERS; Chat(0xFF80FF80, "Server added"); }
        else Chat(0xFF6060FF, "Could not add the server (list full or bad address)");
    }
    ImGui::SameLine(0, UI::S(12));
    if(MPKit::Btn("ajoin", "ADD AND CONNECT", hw, bh, 0, s_szAddHost[0] != 0, MPKit::IC_PLAY))
    {
        ServerList::Add(s_szAddName[0] ? s_szAddName : s_szAddHost, s_szAddHost, s_nAddPort, 0, s_szAddPass); ServerList::Save();
        LauncherConnect(s_szAddHost, s_nAddPort, s_szAddPass);
    }
    if(s_nVSel >= 0 && s_nVSel < ServerList::Count())
    {
        ImGui::Dummy(ImVec2(1, UI::S(10)));
        ServerEntry* e = ServerList::Get(s_nVSel);
        snprintf(l, sizeof(l), "Remove \"%s\" from the list", e ? e->name : "?");
        if(MPKit::Btn("arem", l, W, bh, 3, true, MPKit::IC_TRASH)) { ServerList::Remove(s_nVSel); ServerList::Save(); s_nVSel = -1; }
    }
}
static void LSettings(float W)
{
    MPKit::GroupLabel("DISPLAY");
    bool b = cfg.blips != 0, t = cfg.nameTags != 0;
    if(FloatMenu::ToggleSwitch("Players on the map (markers)", &b)) { cfg.blips = b; SaveSettings(); }
    if(FloatMenu::ToggleSwitch("Name tags + health bars", &t)) { cfg.nameTags = t; SaveSettings(); }
    ImGui::SetNextItemWidth(W * 0.5f);
    if(ImGui::SliderFloat("Chat text size", &cfg.textScale, 0.6f, 2.0f, "%.2f")) SaveSettings();
    MPKit::GroupLabel("VOICE");
    ImGui::SetNextItemWidth(W * 0.5f);
    ImGui::SliderFloat("Voice volume", &s_fVoiceVol, 0.0f, 2.0f, "%.1f");
    MPKit::GroupLabel("PLAYER");
    char l[96]; snprintf(l, sizeof(l), "Nickname:  %s", cfg.name);
    if(MPKit::Btn("snick", l, W, UI::S(70), 0, true, MPKit::IC_USER)) Ask(ASK_NAME, "Your name", cfg.name, MP_NAME_LEN - 1, false);
    if(!s_VcClasses.empty() && MPKit::Btn("scls", "Choose class", W, UI::S(70), 0)) { s_bGuiClass = true; FloatMenu::ClosePage(FloatMenu::PAGE_SERVER); }
}
static void VcLauncherText(int what, const std::string& t)   // the phone keyboard answered an Add server field
{
    char b[96]; MP_CopyStr(b, t.c_str(), sizeof(b)); Trim(b);
    switch(what)
    {
    case ASK_LHOST:
    {   // "host:port" works too
        char* c = strrchr(b, ':');
        if(c) { const int p = atoi(c + 1); if(p > 0 && p < 65536) s_nAddPort = p; *c = 0; }
        MP_CopyStr(s_szAddHost, b, sizeof(s_szAddHost)); break;
    }
    case ASK_LPORT: { const int p = atoi(b); if(p > 0 && p < 65536) s_nAddPort = p; break; }
    case ASK_LNAME: MP_CopyStr(s_szAddName, b, sizeof(s_szAddName)); break;
    case ASK_LPASS: MP_CopyStr(s_szAddPass, t.c_str(), sizeof(s_szAddPass)); break;
    default: break;
    }
}
static void RenderVcLauncher(void* openPtr)
{
    bool* open = (bool*)openPtr;
    LoadVcServers();
    ServerList::Poll();
    if(MP_NowMs() - ServerList::LastRefreshMs() > 10000) ServerList::Refresh();
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float mx = ds.x * 0.025f, my = ds.y * 0.03f;
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(0, 0, 0, 150));
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(mx, my), ImVec2(ds.x - mx, ds.y - my), IM_COL32(10, 12, 18, 200));
    ImGui::SetNextWindowPos(ImVec2(mx, my));
    ImGui::SetNextWindowSize(ImVec2(ds.x - mx * 2, ds.y - my * 2));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, UI::S(1));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0.6f));
    if(!ImGui::Begin("##MPLauncher", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse))
    { ImGui::End(); ImGui::PopStyleColor(3); ImGui::PopStyleVar(3); return; }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    const float pad = UI::S(16), titleH = UI::S(70), tbH = UI::S(78), btnH = UI::S(60), barH = UI::S(88);
    MPKit::Glass(dl, wp, ImVec2(wp.x + ws.x, wp.y + ws.y));
    MPKit::PushWidgetStyle();
    LTitle(dl, wp, ws, titleH, open);
    const float sideW = ImMax(UI::S(300), ws.x * 0.17f);
    {   // toolbar: page name ............ refresh  help
        const float y = wp.y + titleH + (tbH - btnH) * 0.5f, x = wp.x + UI::S(10) + sideW + UI::S(18);
        static const char* kNames[] = { "Vice City servers", "Add server", "Settings", "About" };
        dl->AddText(ImVec2(x, y + (btnH - ImGui::GetTextLineHeight()) * 0.5f), IM_COL32_WHITE, kNames[s_nVTab]);
        ImGui::SetCursorScreenPos(ImVec2(wp.x + ws.x - pad - btnH * 2 - UI::S(6), y));
        if(MPKit::ToolButton("refresh", MPKit::IC_REFRESH, nullptr, btnH)) ServerList::Refresh();
        ImGui::SameLine(0, UI::S(6));
        if(MPKit::ToolButton("help", MPKit::IC_INFO, nullptr, btnH)) FloatMenu::OpenPage(FloatMenu::PAGE_HELP);
        dl->AddLine(ImVec2(wp.x, wp.y + titleH + tbH), ImVec2(wp.x + ws.x, wp.y + titleH + tbH), MPKit::Line(), UI::S(1));
    }
    const float bodyY = wp.y + titleH + tbH + UI::S(2), bodyH = wp.y + ws.y - bodyY - UI::S(6) - barH;
    ImGui::SetCursorScreenPos(ImVec2(wp.x + UI::S(10), bodyY + UI::S(8)));
    ImGui::BeginChild("##vside", ImVec2(sideW, bodyH - UI::S(10)), false);
    {
        const float iw = ImGui::GetContentRegionAvail().x, ih = UI::S(70);
        MPKit::GroupLabel("SERVERS");
        if(MPKit::SideItem("srv", MPKit::IC_GLOBE, "Internet", s_nVTab == VT_SERVERS, iw, ih)) s_nVTab = VT_SERVERS;
        if(MPKit::SideItem("add", MPKit::IC_PLUS, "Add server", s_nVTab == VT_ADD, iw, ih)) s_nVTab = VT_ADD;
        MPKit::GroupLabel("PLAYERS");
        if(MPKit::SideItem("pl", MPKit::IC_USERS, "Player list", false, iw, ih)) FloatMenu::OpenPage(FloatMenu::PAGE_PLAYERS);
        MPKit::GroupLabel("SETTINGS");
        if(MPKit::SideItem("set", MPKit::IC_SLIDERS, "Settings", s_nVTab == VT_SETTINGS, iw, ih)) s_nVTab = VT_SETTINGS;
        if(MPKit::SideItem("about", MPKit::IC_INFO, "About", s_nVTab == VT_ABOUT, iw, ih)) s_nVTab = VT_ABOUT;
    }
    ImGui::EndChild();
    const float cx = wp.x + UI::S(10) + sideW + UI::S(18), contentW = wp.x + ws.x - cx - pad;
    ImGui::SetCursorScreenPos(ImVec2(cx, bodyY + UI::S(8)));
    ImGui::BeginChild("##vbody", ImVec2(contentW, bodyH - UI::S(10)), false);
    switch(s_nVTab)
    {
    case VT_SERVERS:  LServers(contentW - UI::S(4), bodyH - UI::S(16)); break;
    case VT_ADD:      LAdd(contentW - UI::S(24)); break;
    case VT_SETTINGS: LSettings(contentW - UI::S(24)); break;
    default:          MPKit::AboutPage(contentW - UI::S(32)); break;
    }
    ImGui::EndChild();
    {   // bottom bar: status ........ nickname  CONNECT
        const float y0 = wp.y + ws.y - barH;
        dl->AddLine(ImVec2(wp.x, y0), ImVec2(wp.x + ws.x, y0), MPKit::Line(), UI::S(1));
        char st[200];
        if(Online()) snprintf(st, sizeof(st), "Connected to %s:%d", cfg.host, cfg.port);
        else if(cfg.host[0] && !s_bManualOff) snprintf(st, sizeof(st), "Joining %s:%d ...", cfg.host, cfg.port);
        else snprintf(st, sizeof(st), "Not connected");
        dl->AddText(ImVec2(wp.x + pad + UI::S(6), y0 + (barH - ImGui::GetTextLineHeight()) * 0.5f), Online() ? IM_COL32(95, 211, 95, 255) : MPKit::Dim(), st);
        const float bh = UI::S(64), cw = UI::S(300), nw = UI::S(360), by = y0 + (barH - bh) * 0.5f;
        ImGui::SetCursorScreenPos(ImVec2(wp.x + ws.x - pad - cw - nw - UI::S(12), by));
        char nl[64]; snprintf(nl, sizeof(nl), "%s", cfg.name);
        if(MPKit::Btn("nick", nl, nw, bh, 0, true, MPKit::IC_USER)) Ask(ASK_NAME, "Your name", cfg.name, MP_NAME_LEN - 1, false);
        ImGui::SameLine(0, UI::S(12));
        ServerEntry* sel = s_nVSel >= 0 && s_nVSel < ServerList::Count() ? ServerList::Get(s_nVSel) : nullptr;
        if(Online() && !sel)
        { if(MPKit::Btn("disc", "DISCONNECT", cw, bh, 2, true, MPKit::IC_STOP)) MenuPick(7); }
        else if(MPKit::Btn("conn", "CONNECT", cw, bh, 1, sel || cfg.host[0], MPKit::IC_PLAY))
        {
            if(sel) LauncherConnect(sel->host, sel->port, sel->password);
            else LauncherConnect(cfg.host, cfg.port, cfg.password);
        }
    }
    MPKit::PopWidgetStyle();
    ImGui::End();
    ImGui::PopStyleColor(3); ImGui::PopStyleVar(3);
}

// ---- pages: Player list / Help / MonetLoader -------------------------------------------------------------------------
static void PagePlayers(void* openPtr)
{
    bool* open = (bool*)openPtr;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, ds.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(ds.x * 0.50f, ds.y * 0.72f), ImGuiCond_FirstUseEver);
    if(!ImGui::Begin("Player list##MPPlayers", open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) { ImGui::End(); return; }
    int count = Online() ? 1 : 0; for(auto& r : s_Players) if(r.used) ++count;
    {
        char sn[160]; if(Online()) snprintf(sn, sizeof(sn), "%s%s%s", s_bVcmp ? s_VcInfo.gameName : "", s_bVcmp ? "  -  " : "", cfg.host); else snprintf(sn, sizeof(sn), "Not connected");
        ImGui::TextUnformatted(sn);
        char pl[32]; snprintf(pl, sizeof(pl), "Players: %d", count);
        ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(pl).x); ImGui::TextUnformatted(pl);
    }
    ImGui::Separator();
    ImGui::BeginChild("##sb", ImVec2(0, -UI::S(170)), false);
    const float rowH = UI::S(54), W = ImGui::GetContentRegionAvail().x;
    const float cName = W * 0.12f, cScore = W * 0.66f, cPing = W * 0.83f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    {
        ImVec2 p = ImGui::GetCursorScreenPos(); const ImU32 tc = IM_COL32(200, 200, 200, 255);
        dl->AddText(ImVec2(p.x + UI::S(10), p.y), tc, "id"); dl->AddText(ImVec2(p.x + cName, p.y), tc, "name");
        dl->AddText(ImVec2(p.x + cScore, p.y), tc, "score"); dl->AddText(ImVec2(p.x + cPing, p.y), tc, "ping");
        ImGui::Dummy(ImVec2(W, ImGui::GetTextLineHeight() + UI::S(6)));
    }
    auto row = [&](int id, const char* name, ImU32 col, int score, int ping, bool talking, bool muted, int n)
    {
        ImGui::PushID(id);
        ImVec2 a = ImGui::GetCursorScreenPos();
        if(ImGui::InvisibleButton("##row", ImVec2(W, rowH))) s_nListSel = s_nListSel == id ? -1 : id;
        const bool sel = s_nListSel == id;
        if(sel) dl->AddRectFilled(a, ImVec2(a.x + W, a.y + rowH), IM_COL32(190, 25, 25, 215), UI::S(8));
        else if(n % 2 == 0) dl->AddRectFilled(a, ImVec2(a.x + W, a.y + rowH), IM_COL32(255, 255, 255, 12), UI::S(8));
        const float ty = a.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f;
        char b[24]; snprintf(b, sizeof(b), "%d", id);
        dl->AddText(ImVec2(a.x + UI::S(10), ty), IM_COL32_WHITE, b);
        dl->AddText(ImVec2(a.x + cName, ty), sel ? IM_COL32_WHITE : col, name);
        const ImVec2 vc(a.x + cScore - UI::S(34), a.y + rowH * 0.5f);
        if(muted) UI::DrawSpeaker(dl, vc, UI::S(13), IM_COL32(230, 230, 230, 255), 0, true);
        else if(talking) UI::DrawSpeaker(dl, vc, UI::S(13), IM_COL32_WHITE, 2, false);
        snprintf(b, sizeof(b), "%d", score); dl->AddText(ImVec2(a.x + cScore, ty), IM_COL32_WHITE, b);
        snprintf(b, sizeof(b), "%d", ping);  dl->AddText(ImVec2(a.x + cPing, ty), IM_COL32_WHITE, b);
        ImGui::PopID();
    };
    int n = 0;
    if(Online())
    {
        std::string me = std::string(cfg.name) + " (you)";
        row(s_nMyId, me.c_str(), IM_COL32_WHITE, s_nKills, s_Client.GetPing(), s_bVoiceInit && Voice::MicTalking(), false, ++n);
    }
    for(int i = 0; i < 256; ++i)
    {
        const Remote& r = s_Players[i];
        if(!r.used) continue;
        std::string nm = r.name;
        if(s_PlayerTeam[i] < VCMP_MAX_TEAMS) { nm += "  ["; nm += VcTeamName(s_PlayerTeam[i]); nm += "]"; }
        if(r.paused) nm += "  (paused)";
        const ImVec4 c = Abgr(r.color ? r.color : 0xFFFFFFFF);
        row(i, nm.c_str(), ImGui::ColorConvertFloat4ToU32(c), r.score, r.ping, s_bVoiceInit && Voice::IsSpeaking(i), s_bVoiceMuted[i], ++n);
    }
    if(!n) ImGui::TextDisabled("Not connected. ... > Server > pick a Vice City server > CONNECT.");
    ImGui::EndChild();
    ImGui::Separator();
    const int i = s_nListSel;
    if(i >= 0 && i < 256 && i != s_nMyId && s_Players[i].used)
    {
        const Remote& r = s_Players[i];
        ImGui::Text("%s  |  HP %d  |  armour %d%s", r.name, r.health, r.armour, r.inCar ? "  |  in a car" : "");
        const bool muted = s_bVoiceMuted[i];
        ImGui::PushStyleColor(ImGuiCol_Button, muted ? ImVec4(0.70f, 0.12f, 0.12f, 0.95f) : ImVec4(0.18f, 0.18f, 0.18f, 0.95f));
        if(ImGui::Button(muted ? "Unmute voice##vm" : "Mute voice##vm", ImVec2(-1, UI::S(64)))) { s_bVoiceMuted[i] = !muted; if(s_bVoiceInit && !muted) Voice::ResetSpeaker(i); }
        ImGui::PopStyleColor();
    }
    else ImGui::TextDisabled("Tap a player to see their health or mute their voice.");
    if(!s_VcClasses.empty() && ImGui::Button("Choose class##plcls", ImVec2(-1, UI::S(60)))) { s_bGuiClass = true; *open = false; }
    ImGui::End();
}
static void PageHelp(void* openPtr)
{
    bool* open = (bool*)openPtr;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, ds.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(ds.x * 0.50f, ds.y * 0.72f), ImGuiCond_FirstUseEver);
    if(!ImGui::Begin("Help##MPHelp", open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) { ImGui::End(); return; }
    ImGui::TextUnformatted("MP " MP_VERSION_STR " - multiplayer for GTA Vice City");
    ImGui::Separator();
    ImGui::TextWrapped("Top bar: the dots open Help, Server and Player list. The chat icon opens the keyboard (type / for the server's commands). The mic is voice chat. The moon is MonetLoader. Drag the bar to move it.");
    ImGui::TextWrapped("Server: the MP Launcher. Pick a Vice City server (tap it, tap it again or CONNECT), add your own in + Add server, change your nickname at the bottom, Settings and About on the left.");
    ImGui::TextWrapped("Player list: everybody on the server with score and ping. Tap a player to mute their voice.");
    ImGui::TextWrapped("Server dialogs (lists, messages) open as windows: tap a row, then the button. Class servers (VC:MP style) show the class list when you join - or Player list > Choose class.");
    ImGui::Separator();
    ImGui::TextWrapped("Vice City servers: mp_server --game vc. San Andreas servers do not take Vice City players (and the other way round).");
    ImGui::TextDisabled("Website: mpsamp-open.github.io");
    ImGui::End();
}
static void PageMonet(void* openPtr)
{
    bool* open = (bool*)openPtr;
    s_bMonetPage = *open;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, ds.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(ds.x * 0.50f, ds.y * 0.55f), ImGuiCond_FirstUseEver);
    if(!ImGui::Begin("MonetLoader##MPMonet", open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) { ImGui::End(); return; }
    void* h = MonetEmbed::Handle();
    if(h) ImGui::TextColored(ImVec4(0.5f, 1, 0.5f, 1), "MonetLoader is running");
    else
    {
        ImGui::TextColored(ImVec4(1, 0.75f, 0.4f, 1), "MonetLoader is not running in Vice City");
        ImGui::TextWrapped("MonetLoader's Lua API (moonloader, sampfuncs, SA-MP events) is built on GTA San Andreas' memory and opcodes, so MP starts it in San Andreas only. Vice City scripts would crash the game with it.");
        ImGui::TextWrapped("Scripts folder (San Andreas): files/TYFMC/MP/moonloader  (.lua / .luac)");
    }
    ImGui::End();
}
static void VoicePanel(float sw, float sh)
{
    if(!s_bVoicePanel) return;
    ImGui::SetNextWindowPos(ImVec2(sw * 0.5f, sh * 0.32f), ImGuiCond_Appearing, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(UI::S(640), 0), ImGuiCond_Appearing);
    if(!ImGui::Begin("Voice chat##MPVoice", &s_bVoicePanel, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize)) { ImGui::End(); return; }
    const bool on = s_bVoiceInit && Voice::MicOn();
    ImGui::TextWrapped(Online() ? "Players near you hear you: louder up close, nothing past 40 m." : "Connect to a server first.");
    ImGui::PushStyleColor(ImGuiCol_Button, on ? ImVec4(0.75f, 0.15f, 0.15f, 0.95f) : ImVec4(0.20f, 0.55f, 0.25f, 0.95f));
    if(ImGui::Button(on ? "Microphone off" : "Microphone on", ImVec2(-1, UI::S(70)))) VoiceMicToggle();
    ImGui::PopStyleColor();
    if(on) { ImGui::ProgressBar(Voice::MicLevel(), ImVec2(-1, UI::S(18)), ""); }
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##vvol", &s_fVoiceVol, 0.0f, 2.0f, "Volume %.1f");
    if(s_bVoiceInit && Voice::Available())
    {
        std::string t;
        for(int i = 0; i < 256; ++i) if(s_Players[i].used && Voice::IsSpeaking(i)) { if(!t.empty()) t += ", "; t += s_Players[i].name; }
        if(!t.empty()) ImGui::TextColored(ImVec4(0.5f, 1, 0.5f, 1), "Talking: %s", t.c_str());
    }
    else if(s_bVoiceInit) ImGui::TextDisabled("Voice: %s", Voice::LastError());
    ImGui::End();
}

// ---- SA-MP dialogs and the class pick ---------------------------------------------------------------------------------
static void SendDialog(int response, int item, const char* input)
{
    PktDialogResp r; memset(&r, 0, sizeof(r));
    r.type = PKT_DIALOG_RESPONSE; r.dialogId = s_Dialog.id; r.response = (uint8_t)response; r.listItem = (int16_t)item;
    if(input) MP_CopyStr(r.input, input, sizeof(r.input));
    s_Client.Send(&r, sizeof(r));
    s_Dialog = PendingDialog();
}
static void GuiClass(float sw, float sh)
{
    if(!s_bGuiClass) return;
    if(s_VcClasses.empty()) { s_bGuiClass = false; return; }
    ImGui::SetNextWindowPos(ImVec2(sw * 0.5f, sh * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(UI::S(700), 0), ImVec2(sw * 0.9f, sh * 0.85f));
    char title[64]; snprintf(title, sizeof(title), "%s - pick a class###vcclass", s_VcInfo.gameName[0] ? s_VcInfo.gameName : "Class");
    if(!ImGui::Begin(title, &s_bGuiClass, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) { ImGui::End(); return; }
    for(auto& k : s_VcClasses)
    {
        char w[96] = ""; int n = 0;
        for(int i = 0; i < 3; ++i) if(k.weapon[i] && k.weapon[i] < 34) n += snprintf(w + n, sizeof(w) - n, "%s%s", n ? ", " : "", g_VcWeaponName[k.weapon[i]]);
        char lbl[200]; snprintf(lbl, sizeof(lbl), "%d. %s - skin %d - %s##cls%d", k.idx + 1, VcTeamName(k.team), k.skin, n ? w : "no weapons", (int)k.idx);
        ImGui::PushStyleColor(ImGuiCol_Text, k.team < VCMP_MAX_TEAMS ? Rgba(g_VcmpTeamColor[k.team]) : ImVec4(1, 1, 1, 1));
        if(k.idx == s_nMyClass) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.49f, 0.70f, 0.89f, 0.45f));
        const bool pick = ImGui::Button(lbl, ImVec2(-FLT_MIN, UI::S(66)));
        if(k.idx == s_nMyClass) ImGui::PopStyleColor();
        ImGui::PopStyleColor();
        if(pick) { SpawnClass(k.idx); s_bGuiClass = false; }
    }
    ImGui::End();
}
static void GuiDialog(float sw, float sh)
{
    if(!s_Dialog.open || !s_Dialog.gui) return;
    ImGui::SetNextWindowPos(ImVec2(sw * 0.5f, sh * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(UI::S(640), 0), ImVec2(sw * 0.9f, sh * 0.85f));
    char title[96]; snprintf(title, sizeof(title), "%s###vcdlg%u", s_Dialog.caption.empty() ? "Dialog" : s_Dialog.caption.c_str(), (unsigned)s_Dialog.id);
    if(!ImGui::Begin(title, nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) { ImGui::End(); return; }
    const bool list = s_Dialog.style != DIALOG_STYLE_MSGBOX;
    if(list)
    {
        const float rowH = UI::S(58);
        const int shown = s_Dialog.lines.size() > 8 ? 8 : (int)s_Dialog.lines.size();
        if(!s_Dialog.header.empty()) { std::string h = s_Dialog.header; for(auto& c : h) if(c == '\t') c = ' '; ImGui::TextDisabled("%s", h.c_str()); }
        const float w = ImMin(sw * 0.7f, UI::S(1000));
        ImGui::BeginChild("##rows", ImVec2(w, rowH * shown + UI::S(10)), true);
        for(int i = 0; i < (int)s_Dialog.lines.size(); ++i)
        {
            std::string row = s_Dialog.lines[i]; for(auto& c : row) if(c == '\t') c = ' ';
            ImGui::PushID(i);
            if(ImGui::Selectable(row.c_str(), s_Dialog.sel == i, 0, ImVec2(0, rowH - ImGui::GetStyle().ItemSpacing.y))) s_Dialog.sel = i;
            ImGui::PopID();
        }
        ImGui::EndChild();
    }
    else for(auto& l : s_Dialog.lines) ImGui::TextWrapped("%s", l.c_str());
    ImGui::Separator();
    const float bw = UI::S(260);
    if(ImGui::Button(s_Dialog.b1.c_str(), ImVec2(bw, UI::S(66)))) { SendDialog(1, list ? s_Dialog.sel : -1, nullptr); ImGui::End(); return; }
    if(!s_Dialog.b2.empty())
    {
        ImGui::SameLine();
        if(ImGui::Button(s_Dialog.b2.c_str(), ImVec2(bw, UI::S(66)))) { SendDialog(0, list ? s_Dialog.sel : -1, nullptr); ImGui::End(); return; }
    }
    ImGui::End();
}

// ---- start + frame -------------------------------------------------------------------------------------------------
// 4.13.55: the font atlas as a Vice City texture, the way the vc-android-sdk examples make one
// (RwRasterCreate(w, h, 32, rwRASTERTYPETEXTURE) + RwRasterLock + RGBA copy). San Andreas's RwImage path
// (RwImageFindRasterFormat / RwRasterSetFromImage) is only the fallback.
static bool VcFontTexture()
{
    ::RwRaster* (*rCreate)(int, int, int, int) = nullptr;
    unsigned char* (*rLock)(::RwRaster*, unsigned char, int) = nullptr;
    ::RwRaster* (*rUnlock)(::RwRaster*) = nullptr;
    *(void**)&rCreate = (void*)aml->GetSym(s_hGame, "_Z14RwRasterCreateiiii");
    *(void**)&rLock   = (void*)aml->GetSym(s_hGame, "_Z12RwRasterLockP8RwRasterhi");
    *(void**)&rUnlock = (void*)aml->GetSym(s_hGame, "_Z14RwRasterUnlockP8RwRaster");
    if(!rCreate || !rLock || !rUnlock) { logger->Info("Vice City: no RwRasterLock - the font texture uses RwImage"); return false; }
    ImGuiIO& io = ImGui::GetIO();
    unsigned char* px = nullptr; int w = 0, h = 0, bpp = 0;
    io.Fonts->GetTexDataAsRGBA32(&px, &w, &h, &bpp);
    if(!px || w <= 0 || h <= 0) return false;
    ::RwRaster* r = rCreate(w, h, 32, 4);
    if(!r) { logger->Error("Vice City: RwRasterCreate(%d, %d) failed", w, h); return false; }
    unsigned char* dst = rLock(r, 0, 1);
    if(!dst) { logger->Error("Vice City: RwRasterLock failed"); return false; }   // (the raster is left; it is small)
    memcpy(dst, px, (size_t)w * h * 4);
    rUnlock(r);
    ::g_FontRaster = r;
    io.Fonts->TexID = (ImTextureID)r;
    logger->Info("Vice City: ImGui font texture %dx%d (raster lock)", w, h);
    return true;
}

static bool GuiInit(float sw, float sh)
{
    s_bGuiTried = true;
    static const char* kNeed[] = {
        "_Z28RwIm2DRenderIndexedPrimitive15RwPrimitiveTypeP14RwOpenGLVertexiPti",
        "_Z16RwRenderStateSet13RwRenderStatePv",
        "_Z14RwRasterCreateiiii",   // 4.13.55: the RwImage functions are only needed by the fallback texture path
    };
    for(const char* n : kNeed)
        if(!aml->GetSym(s_hGame, n)) { logger->Error("Vice City: %s not found - the MP menu stays in the chat", n); return false; }
    if(!pGameHandle) pGameHandle = s_hGame;   // San Andreas is not loaded here: the RW pointers come from libGTAVC.so
    InitRenderWareFunctions();
    nearScreenZ   = (float*)aml->GetSym(s_hGame, "_ZN9CSprite2d11NearScreenZE");
    recipNearClip = (float*)aml->GetSym(s_hGame, "_ZN9CSprite2d13RecipNearClipE");
    if(!nearScreenZ)
    {
        float (*getNear)() = nullptr;
        *(void**)&getNear = (void*)aml->GetSym(s_hGame, "_Z20RwIm2DGetNearScreenZv");
        if(getNear) s_fVcNearZ = getNear();
        nearScreenZ = &s_fVcNearZ;
    }
    if(!recipNearClip) recipNearClip = &s_fVcRecipNear;
    // 4.13.55: no CWidget::SetScissor on Vice City. It reads the rectangle in another layout than San Andreas, so every
    // ImGui window was clipped away (nothing of MP showed, and the old text buttons are hidden while ImGui runs).
    // The backend clips on the CPU instead when SetScissorRect is null.
    SetScissorRect = nullptr;

    UI::SetBaseScale(sh / 1080.0f);   // UI::S() like San Andreas (HUD::Init does not run in Vice City)
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_IsTouchScreen;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    io.DisplaySize = ImVec2(sw, sh);
    {   // the San Andreas system font: Fredoka + Arial for the other letters
        ImFontConfig fc; fc.FontDataOwnedByAtlas = false;
        ImFont* f = io.Fonts->AddFontFromMemoryTTF((void*)fredokaData, (int)sizeof(fredokaData), UI::S(34.0f * 1.08f), &fc, io.Fonts->GetGlyphRangesDefault());
        ImFontConfig mc = fc; mc.MergeMode = true;
        if(f) io.Fonts->AddFontFromMemoryTTF((void*)arialData, (int)sizeof(arialData), UI::S(34.0f), &mc, io.Fonts->GetGlyphRangesCyrillic());
        else  io.Fonts->AddFontFromMemoryTTF((void*)arialData, (int)sizeof(arialData), UI::S(34.0f), &fc, io.Fonts->GetGlyphRangesCyrillic());
    }
    FloatMenu::ApplyStyle();
    if(!VcFontTexture() && !ImGui_ImplRenderWare_CreateDeviceObjects())
    {
        logger->Error("Vice City: the ImGui font texture failed - the MP menu stays in the chat");
        ImGui::DestroyContext(); return false;
    }
    // the San Andreas top bar:  [ ... ][ chat ][ mic ][ moon ]   ... = Help / Server (launcher) / Player list
    FloatMenu::AddDotsButton();
    FloatMenu::AddBarButton("Chat", PillChat, nullptr, &s_bTyping);
    FloatMenu::AddMicButton(PillVoice, VoiceMicState);
    FloatMenu::AddIconButton(FloatMenu::ICON_MONET, PillMonet, nullptr, &s_bMonetPage);
    FloatMenu::SetPage(FloatMenu::PAGE_PLAYERS, PagePlayers);
    FloatMenu::SetPage(FloatMenu::PAGE_HELP, PageHelp);
    FloatMenu::SetPage(FloatMenu::PAGE_MONET, PageMonet);
    FloatMenu::SetServerPageRenderer(RenderVcLauncher);
    if(aml->GetSym(s_hGame, "_Z14AND_TouchEventiiii")) { HOOKSYM(VC_AND_TouchEvent, s_hGame, "_Z14AND_TouchEventiiii"); s_bTouchHooked = true; }
    logger->Info("Vice City: San Andreas MP interface ready (%.0fx%.0f, scissor %s, touch %s)", sw, sh, SetScissorRect ? "yes" : "no", s_bTouchHooked ? "hook" : "polled");
    s_bGuiReady.store(true);
    return true;
}

// 4.13.56: the ImGui overlay never showed on Vice City phones (4.13.54 / 4.13.55), so it's off and Vice City uses the
// CFont text buttons + text menus again ([ Chat ] [ Players ] [ MP ] [ Class ]). Set to true to try ImGui again.
static constexpr bool kVcImGui = false;

// one ImGui frame, from CHud::Draw. false = no ImGui (the CFont buttons are drawn instead)
static bool GuiFrame(float sw, float sh, bool online)
{
    if(!kVcImGui) { s_bGuiOk = false; return false; }
    if(!s_bGuiTried) s_bGuiOk = GuiInit(sw, sh);
    if(!s_bGuiOk) return false;
    ImGuiIO& io = ImGui::GetIO();
    const uint64_t now = MP_NowMs();
    io.DeltaTime = s_nGuiLast && now > s_nGuiLast ? (now - s_nGuiLast) / 1000.0f : 1.0f / 30.0f;
    if(io.DeltaTime > 0.5f) io.DeltaTime = 0.5f;
    s_nGuiLast = now;
    io.DisplaySize = ImVec2(sw, sh);
    FloatMenu::SetGameRunning();
    PollTouchFallback();
    DrainTouches();
    ImGui_ImplRenderWare_NewFrame();
    if(!io.Fonts->TexID) { logger->Error("Vice City: no ImGui font texture - back to the chat menu"); s_bGuiOk = false; s_bGuiReady.store(false); return false; }
    if(s_bScoreboard) { s_bScoreboard = false; FloatMenu::OpenPage(FloatMenu::PAGE_PLAYERS); }   // old toggles -> the Player list
    if(s_bGuiMenu) { s_bGuiMenu = false; FloatMenu::OpenPage(FloatMenu::PAGE_SERVER); }          // [MP] -> the launcher
    ImGui::NewFrame();
    FloatMenu::Render();
    VoicePanel(sw, sh);
    if(online) { GuiClass(sw, sh); GuiDialog(sw, sh); }
    ImGui::Render();
    ImDrawData* dd = ImGui::GetDrawData();
    ImGui_ImplRenderWare_RenderDrawData(dd);
    static int s_nLogged = 0;
    if(s_nLogged < 3 && dd) { ++s_nLogged; logger->Info("Vice City: ImGui frame %d: %d lists, %d vertices, display %.0fx%.0f, near z %.3f", s_nLogged, dd->CmdListsCount, dd->TotalVtxCount, sw, sh, nearScreenZ ? *nearScreenZ : -1.f); }
    PublishHits();
    return true;
}
