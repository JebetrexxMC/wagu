// MP 4.13.22: GTA HUD settings - see hudlayout.h.
#include <mod/amlmod.h>
#include <mod/logger.h>
#include <mod/config.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <dlfcn.h>
#include <strings.h>
#include "hudlayout.h"

namespace
{
struct CRect { float left, bottom, right, top; };      // the game's CRect (x1, y1, x2, y2)
struct CRGBA { uint8_t r, g, b, a; };
struct CVector2D { float x, y; };
struct FontDetails { CRGBA color; float scaleX, scaleY; };   // the head of CFont::Details (engine/Font.h)

static const char* const kKeys[HudLayout::PART_COUNT] = {
    "HUD_PlayerInfo", "HUD_BarHealth", "HUD_BarArmor", "HUD_BarBreath", "HUD_WeaponIcon", "HUD_Ammo", "HUD_Money",
    "HUD_MoneyChange", "HUD_Clock", "HUD_WantedStar", "HUD_Radar", "HUD_VehName", "HUD_AreaName", "HUD_Radio", "HUD_Subtitles" };
static const char* const kNames[HudLayout::PART_COUNT] = {
    "Player info (all)", "Health bar", "Armor bar", "Breath bar", "Weapon icon", "Ammo", "Money", "Money change",
    "Clock", "Wanted stars", "Radar", "Vehicle name", "Area name", "Radio station", "Subtitles" };

HudLayout::Cfg s_Cfg[HudLayout::PART_COUNT];
bool  s_bOn = false, s_bHooked = false, s_bWant = false;
int   s_nHooks = 0;
void* s_hGame = nullptr;
float s_fW = 1280, s_fH = 720;
FontDetails* s_pDetails = nullptr;

// the part stack while a part draws (PlayerInfo > Health bar ...)
struct Ctx { int part; bool anchored; float ax, ay; bool alphaOnly; };   // 5.00.06: alphaOnly = the radar (moved by its widget rect)
Ctx  s_Stack[4]; int s_nDepth = 0;
bool s_bInInfo = false;   // inside CWidgetPlayerInfo::Draw (for the text parts: money, clock, ammo)

inline bool Identity(const HudLayout::Cfg& c)
{ return c.show && c.x == 0 && c.y == 0 && c.z == 100 && c.size == 100 && c.w == 100 && c.h == 100 && c.alpha == 100; }
inline bool Active() { return s_bOn && s_nDepth > 0; }

void Push(int p, bool alphaOnly = false) { if(s_nDepth < 4) { s_Stack[s_nDepth].part = p; s_Stack[s_nDepth].anchored = false; s_Stack[s_nDepth].alphaOnly = alphaOnly; } ++s_nDepth; }

// 5.00.06: the Compact layout (the default) in weapon-icon heights, measured from the player's "correct" HUD photo
// against the mobile one: clock 0.22 lower, money 0.02 higher, the bars 0.14 higher. Icon heights, not % of the
// screen, so it fits every screen shape; the icon size comes from DrawWeaponIcon's rect (last frame).
int   s_nPreset = HudLayout::PRESET_COMPACT;
float s_fIconH = 0;
float CompactDy(int part)
{
    if(s_nPreset != HudLayout::PRESET_COMPACT) return 0;
    float k = 0;
    switch(part)
    {
        case HudLayout::CLOCK:        k =  0.22f; break;
        case HudLayout::MONEY:        k = -0.02f; break;
        case HudLayout::MONEY_CHANGE: k = -0.02f; break;
        case HudLayout::BAR_HEALTH: case HudLayout::BAR_ARMOR: case HudLayout::BAR_BREATH: k = -0.14f; break;
        default: return 0;
    }
    const float ih = (s_fIconH > 4 && s_fIconH < s_fH * 0.5f) ? s_fIconH : s_fH * 0.115f;
    return k * ih;
}
void Pop() { if(s_nDepth > 0) --s_nDepth; }

// one point through the stack: the innermost part first, then its parents
void Xform(float& x, float& y)
{
    const int n = s_nDepth < 4 ? s_nDepth : 4;
    for(int i = n - 1; i >= 0; --i)
    {
        Ctx& c = s_Stack[i];
        if(c.alphaOnly) continue;
        const HudLayout::Cfg& g = s_Cfg[c.part];
        if(!c.anchored) { c.anchored = true; c.ax = x; c.ay = y; }
        const float k = g.size * g.z * 0.0001f, sx = k * g.w * 0.01f, sy = k * g.h * 0.01f;
        x = c.ax + (x - c.ax) * sx + g.x * 0.01f * s_fW;
        y = c.ay + (y - c.ay) * sy + g.y * 0.01f * s_fH + CompactDy(c.part);
    }
}
void Scale(float& sx, float& sy)
{
    const int n = s_nDepth < 4 ? s_nDepth : 4;
    for(int i = 0; i < n; ++i)
    {
        if(s_Stack[i].alphaOnly) continue;
        const HudLayout::Cfg& g = s_Cfg[s_Stack[i].part];
        const float k = g.size * g.z * 0.0001f; sx *= k * g.w * 0.01f; sy *= k * g.h * 0.01f;
    }
}
float Alpha()
{
    float a = 1; const int n = s_nDepth < 4 ? s_nDepth : 4;
    for(int i = 0; i < n; ++i) a *= s_Cfg[s_Stack[i].part].alpha * 0.01f;
    return a < 0 ? 0 : (a > 1 ? 1 : a);
}
inline CRGBA A(const CRGBA& c, float a) { CRGBA o = c; o.a = (uint8_t)(c.a * a); return o; }
CRect XRect(const CRect& r)
{
    float x1 = r.left, y1 = r.bottom, x2 = r.right, y2 = r.top;
    Xform(x1, y1); Xform(x2, y2);
    // 4.13.33: a part that came out far off / bigger than the screen (a wrong anchor, an odd value in [GTAHUD])
    // is drawn where the game put it - it was a grey band stretched over the top of the screen
    const float lim = 2.0f;
    if(!(x1 == x1) || !(y1 == y1) || !(x2 == x2) || !(y2 == y2) || fabsf(x2 - x1) > s_fW * lim || fabsf(y2 - y1) > s_fH * lim
       || x1 < -s_fW || x1 > s_fW * 2 || y1 < -s_fH || y1 > s_fH * 2) return r;
    return { x1, y1, x2, y2 };
}
// 4.13.34: a transformed polygon with a point far off the screen (or NaN) is drawn as the game made it - the
// "spaghetti" lines across the HUD
bool SaneVerts(const float* t, int n)
{
    for(int i = 0; i < n; ++i)
    {
        const float x = t[i * 2], y = t[i * 2 + 1];
        if(!(x == x) || !(y == y) || x < -s_fW || x > s_fW * 2 || y < -s_fH || y > s_fH * 2) return false;
    }
    return true;
}
bool Hidden(int p) { return s_bOn && !s_Cfg[p].show; }

// ---- the part hooks --------------------------------------------------------------------------------------
typedef void (*FnV)(void*);
typedef void (*FnBar)(void*, int, float);
typedef void (*FnIcon)(void*, void*, CRect, float);
typedef void (*FnS)();
FnV    oInfoDraw = nullptr, oWanted = nullptr, oRadarW = nullptr, oRadio = nullptr;
FnBar  oHealth = nullptr, oArmor = nullptr, oBreath = nullptr;
FnIcon oIcon = nullptr;
FnS    oHudRadar = nullptr, oArea = nullptr, oVeh = nullptr, oSubs = nullptr;
FnV    oRadarUpd = nullptr;

#define PART_WRAP(part, call) do { if(!s_bOn) { call; return; } if(Hidden(part)) return; Push(part); call; Pop(); } while(0)
// 5.00.06: a top level part (not inside another one) starts and ends with an empty stack - a part left on the
// stack moved everything drawn after it: the pause menu map (half the screen, the blips gone), camera / mission text
#define PART_TOP(part, call) do { if(!s_bOn) { s_nDepth = 0; call; return; } s_nDepth = 0; if(Hidden(part)) return; Push(part); call; s_nDepth = 0; } while(0)
void hInfoDraw(void* w) { if(!s_bOn) { s_nDepth = 0; oInfoDraw(w); return; } s_nDepth = 0; if(Hidden(HudLayout::PLAYERINFO)) return; Push(HudLayout::PLAYERINFO); s_bInInfo = true; oInfoDraw(w); s_bInInfo = false; s_nDepth = 0; }
void hHealth(void* w, int a, float b) { PART_WRAP(HudLayout::BAR_HEALTH, oHealth(w, a, b)); }
void hArmor(void* w, int a, float b)  { PART_WRAP(HudLayout::BAR_ARMOR, oArmor(w, a, b)); }
void hBreath(void* w, int a, float b) { PART_WRAP(HudLayout::BAR_BREATH, oBreath(w, a, b)); }
void hIcon(void* w, void* ped, CRect r, float f) { const float ih = fabsf(r.top - r.bottom); if(ih == ih) s_fIconH = ih; PART_WRAP(HudLayout::WEAPON_ICON, oIcon(w, ped, r, f)); }
void hWanted(void* w) { PART_WRAP(HudLayout::WANTED, oWanted(w)); }
// 5.00.06: the radar is no longer moved sprite by sprite. Its mask, map pieces, blips and ring don't all go through
// the same CSprite2d calls, so they came apart: the green "spaghetti" (map texture outside the circle). It moves the
// way Advanced Settings does it: CWidgetRadar::Update -> the widget's screen rect (everything is drawn from it).
// While it draws only its alpha / show is applied.
inline bool RadarAlpha() { return s_Cfg[HudLayout::RADAR].alpha < 100; }
void hRadarW(void* w)
{
    if(!s_bOn) { s_nDepth = 0; oRadarW(w); return; }
    s_nDepth = 0; if(Hidden(HudLayout::RADAR)) return;
    if(RadarAlpha()) Push(HudLayout::RADAR, true);
    oRadarW(w); s_nDepth = 0;
}
void hHudRadar()
{
    if(!s_bOn || s_nDepth > 0) { oHudRadar(); return; }   // inside CWidgetRadar::Draw (already handled) / off
    if(Hidden(HudLayout::RADAR)) return;
    if(RadarAlpha()) Push(HudLayout::RADAR, true);
    oHudRadar(); s_nDepth = 0;
}
// CWidget (aml-psdk game_sa/other/Widgets.h, size 0x90 / 0xA8): m_RectScreen at 0x20 (32 bit) / 0x2C (64 bit), the
// touch rect m_RectPaddedScreen at 0x34 / 0x40. 0x20 is what Advanced Settings writes on 32 bit.
const size_t kRectOff = sizeof(void*) == 8 ? 0x2C : 0x20, kPadOff = sizeof(void*) == 8 ? 0x40 : 0x34;
CRect s_RadarBase, s_RadarBasePad, s_RadarSet, s_RadarSetPad; bool s_bRadarSet = false; void* s_pRadarW = nullptr;
inline bool SameRect(const CRect& a, const CRect& b) { return a.left == b.left && a.right == b.right && a.top == b.top && a.bottom == b.bottom; }
CRect MoveRect(const CRect& r, const HudLayout::Cfg& g)
{
    const float k = g.size * g.z * 0.0001f, sx = k * g.w * 0.01f, sy = k * g.h * 0.01f;
    const float cx = (r.left + r.right) * 0.5f + g.x * 0.01f * s_fW, cy = (r.bottom + r.top) * 0.5f + g.y * 0.01f * s_fH;
    const float hx = (r.right - r.left) * 0.5f * sx, hy = (r.bottom - r.top) * 0.5f * sy;
    return { cx - hx, cy + hy, cx + hx, cy - hy };
}
void hRadarUpd(void* w)
{
    oRadarUpd(w);
    if(!w) return;
    CRect& rc = *(CRect*)((char*)w + kRectOff);
    CRect& pd = *(CRect*)((char*)w + kPadOff);
    // the game may or may not rebuild the rect every frame: what we wrote last time is not the base
    if(s_bRadarSet && w == s_pRadarW && SameRect(rc, s_RadarSet)) { rc = s_RadarBase; if(SameRect(pd, s_RadarSetPad)) pd = s_RadarBasePad; }
    s_bRadarSet = false;
    const HudLayout::Cfg& g = s_Cfg[HudLayout::RADAR];
    if(!s_bOn || (g.x == 0 && g.y == 0 && g.z == 100 && g.size == 100 && g.w == 100 && g.h == 100)) return;
    const float wd = rc.right - rc.left, ht = rc.bottom - rc.top;
    if(!(wd == wd) || !(ht == ht) || fabsf(wd) < 4 || fabsf(ht) < 4 || fabsf(wd) > s_fW || fabsf(ht) > s_fH) return;   // no sane rect: leave it
    s_RadarBase = rc; s_RadarBasePad = pd; s_pRadarW = w;
    rc = MoveRect(rc, g); pd = MoveRect(pd, g);
    s_RadarSet = rc; s_RadarSetPad = pd; s_bRadarSet = true;
}
void hArea() { PART_TOP(HudLayout::AREA_NAME, oArea()); }
void hVeh()  { PART_TOP(HudLayout::VEH_NAME, oVeh()); }
void hSubs() { PART_TOP(HudLayout::SUBTITLES, oSubs()); }
void hRadio(void* m) { PART_TOP(HudLayout::RADIO, oRadio(m)); }

// ---- the primitives ----------------------------------------------------------------------------------------
typedef void (*FnSV2)(const CRect&, const CRGBA&, const CRGBA&, const CRGBA&, const CRGBA&);
typedef void (*FnSV4)(const CRect&, const CRGBA&, const CRGBA&, const CRGBA&, const CRGBA&, float, float, float, float, float, float, float, float);
typedef void (*FnSV3)(int, float*, float*, const CRGBA&);
typedef void (*FnSV6)(int, float*, CRGBA*);
typedef void (*FnMask)(unsigned int, float*, float);
typedef void (*FnPrint)(float, float, uint16_t*);
FnSV2 oSV2 = nullptr; FnSV4 oSV4 = nullptr; FnSV3 oSV3 = nullptr; FnSV6 oSV6 = nullptr; FnMask oMask = nullptr; FnPrint oPrint = nullptr;

void hSV2(const CRect& r, const CRGBA& a, const CRGBA& b, const CRGBA& c, const CRGBA& d)
{
    if(!Active()) { oSV2(r, a, b, c, d); return; }
    const float k = Alpha(); const CRect x = XRect(r);
    oSV2(x, A(a, k), A(b, k), A(c, k), A(d, k));
}
void hSV4(const CRect& r, const CRGBA& a, const CRGBA& b, const CRGBA& c, const CRGBA& d, float u1, float v1, float u2, float v2, float u3, float v3, float u4, float v4)
{
    if(!Active()) { oSV4(r, a, b, c, d, u1, v1, u2, v2, u3, v3, u4, v4); return; }
    const float k = Alpha(); const CRect x = XRect(r);
    oSV4(x, A(a, k), A(b, k), A(c, k), A(d, k), u1, v1, u2, v2, u3, v3, u4, v4);
}
void hSV3(int n, float* v, float* tc, const CRGBA& col)
{
    if(!Active() || n <= 0 || n > 64 || !v) { oSV3(n, v, tc, col); return; }
    float t[128]; for(int i = 0; i < n; ++i) { t[i * 2] = v[i * 2]; t[i * 2 + 1] = v[i * 2 + 1]; Xform(t[i * 2], t[i * 2 + 1]); }
    if(!SaneVerts(t, n)) { oSV3(n, v, tc, col); return; }
    oSV3(n, t, tc, A(col, Alpha()));
}
void hSV6(int n, float* v, CRGBA* cols)
{
    if(!Active() || n <= 0 || n > 64 || !v) { oSV6(n, v, cols); return; }
    float t[128]; CRGBA c[64]; const float k = Alpha();
    for(int i = 0; i < n; ++i) { t[i * 2] = v[i * 2]; t[i * 2 + 1] = v[i * 2 + 1]; Xform(t[i * 2], t[i * 2 + 1]); if(cols) c[i] = A(cols[i], k); }
    if(!SaneVerts(t, n)) { oSV6(n, v, cols); return; }
    oSV6(n, t, cols ? c : nullptr);
}
void hMask(unsigned int n, float* v, float z)
{
    if(!Active() || n == 0 || n > 64 || !v) { oMask(n, v, z); return; }
    float t[128]; for(unsigned int i = 0; i < n; ++i) { t[i * 2] = v[i * 2]; t[i * 2 + 1] = v[i * 2 + 1]; Xform(t[i * 2], t[i * 2 + 1]); }
    if(!SaneVerts(t, (int)n)) { oMask(n, v, z); return; }
    oMask(n, t, z);
}
// the text parts of the player info: money "$00000123", money change "+$50" / "-$50", clock "12:34", ammo "30-120"
int TextPart(const uint16_t* s)
{
    if(!s) return -1;
    int i = 0; while(s[i] == ' ') ++i;
    const uint16_t c0 = s[i], c1 = c0 ? s[i + 1] : 0;
    if(c0 == '$') return HudLayout::MONEY;
    if((c0 == '+' || c0 == '-') && c1 == '$') return HudLayout::MONEY_CHANGE;
    bool digits = false, colon = false;
    for(int k = i; s[k] && k < i + 16; ++k) { if(s[k] >= '0' && s[k] <= '9') digits = true; else if(s[k] == ':') colon = true; }
    if(colon && digits) return HudLayout::CLOCK;
    if(digits) return HudLayout::AMMO;
    return -1;
}
void hPrint(float x, float y, uint16_t* s)
{
    if(!s_bOn) { oPrint(x, y, s); return; }
    bool pushed = false;
    if(s_bInInfo && s_nDepth == 1)
    {
        const int p = TextPart(s);
        if(p >= 0) { if(Hidden(p)) return; Push(p); pushed = true; }
    }
    if(!Active()) { oPrint(x, y, s); return; }
    Xform(x, y);
    if(s_pDetails)
    {
        const FontDetails keep = *s_pDetails;
        Scale(s_pDetails->scaleX, s_pDetails->scaleY);
        s_pDetails->color = A(keep.color, Alpha());
        oPrint(x, y, s);
        s_pDetails->scaleX = keep.scaleX; s_pDetails->scaleY = keep.scaleY; s_pDetails->color = keep.color;
    }
    else oPrint(x, y, s);
    if(pushed) Pop();
}

template<typename F> bool H(const char* sym, F hook, F* orig)
{
    void* f = (void*)aml->GetSym(s_hGame, sym);
    if(!f) { logger->Info("GTA HUD: %s not in this game", sym); return false; }
    if(!aml->Hook(f, (void*)hook, (void**)orig) || !*orig) { logger->Info("GTA HUD: hook failed (%s)", sym); return false; }
    ++s_nHooks; return true;
}
bool s_bAdvSet = false;
// 4.13.34: ARM's Advanced Settings (libAdvancedSettings.SA.so) moves the same HUD parts through the same game
// functions. With both on, every HUD sprite was moved twice ("spaghetti" lines from the weapon icon / bars) and
// a change could hang the game. MP leaves the HUD to Advanced Settings when it is installed.
bool AdvancedSettingsLoaded()
{
    if(s_bAdvSet) return true;
    if(void* h = dlopen("libAdvancedSettings.SA.so", RTLD_NOW | RTLD_NOLOAD)) { dlclose(h); s_bAdvSet = true; }
    return s_bAdvSet;
}
void InstallHooks()
{
    if(s_bHooked || !s_hGame) return;
    if(AdvancedSettingsLoaded())
    {
        static bool logged = false;
        if(!logged) { logged = true; logger->Info("GTA HUD: Advanced Settings (libAdvancedSettings.SA.so) is installed - it controls the HUD, MP's custom HUD stays off (no double hooks)"); }
        s_bOn = false; return;
    }
    s_bHooked = true;
    s_pDetails = (FontDetails*)aml->GetSym(s_hGame, "_ZN5CFont7DetailsE");
    // the primitives first: a part hook without them only hides / shows
    H("_ZN9CSprite2d11SetVerticesERK5CRectRK5CRGBAS5_S5_S5_", &hSV2, &oSV2);
    H("_ZN9CSprite2d11SetVerticesERK5CRectRK5CRGBAS5_S5_S5_ffffffff", &hSV4, &oSV4);
    H("_ZN9CSprite2d11SetVerticesEiPfS0_RK5CRGBA", &hSV3, &oSV3);
    H("_ZN9CSprite2d11SetVerticesEiPfP5CRGBA", &hSV6, &oSV6);
    H("_ZN9CSprite2d15SetMaskVerticesEiPff", &hMask, &oMask);
    H("_ZN5CFont11PrintStringEffPt", &hPrint, &oPrint);
    H("_ZN17CWidgetPlayerInfo4DrawEv", &hInfoDraw, &oInfoDraw);
    H("_ZN17CWidgetPlayerInfo15RenderHealthBarEif", &hHealth, &oHealth);
    H("_ZN17CWidgetPlayerInfo14RenderArmorBarEif", &hArmor, &oArmor);
    H("_ZN17CWidgetPlayerInfo15RenderBreathBarEif", &hBreath, &oBreath);
    H("_ZN17CWidgetPlayerInfo14DrawWeaponIconEP4CPed5CRectf", &hIcon, &oIcon);
    H("_ZN17CWidgetPlayerInfo10DrawWantedEv", &hWanted, &oWanted);
    H("_ZN12CWidgetRadar4DrawEv", &hRadarW, &oRadarW);
    H("_ZN12CWidgetRadar6UpdateEv", &hRadarUpd, &oRadarUpd);   // 5.00.06: radar position / size
    H("_ZN4CHud9DrawRadarEv", &hHudRadar, &oHudRadar);
    H("_ZN4CHud12DrawAreaNameEv", &hArea, &oArea);
    H("_ZN4CHud15DrawVehicleNameEv", &hVeh, &oVeh);
    H("_ZN4CHud13DrawSubtitlesEv", &hSubs, &oSubs);
    H("_ZN20CAERadioTrackManager23DisplayRadioStationNameEv", &hRadio, &oRadio);
    logger->Info("GTA HUD: custom HUD on, %d game functions hooked (font details %s)", s_nHooks, s_pDetails ? "ok" : "-");
}
void Defaults(HudLayout::Cfg& c) { c.show = true; c.x = c.y = 0; c.z = c.size = c.w = c.h = c.alpha = 100; }
float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
struct InitDefaults { InitDefaults() { for(auto& c : s_Cfg) Defaults(c); } } s_InitDefaults;
}   // namespace

const char* HudLayout::PartName(int p) { return p >= 0 && p < PART_COUNT ? kNames[p] : ""; }
const char* HudLayout::PartKey(int p) { return p >= 0 && p < PART_COUNT ? kKeys[p] : ""; }
HudLayout::Cfg& HudLayout::Get(int p) { return s_Cfg[p >= 0 && p < PART_COUNT ? p : 0]; }
void HudLayout::Reset(int p) { if(p >= 0 && p < PART_COUNT) Defaults(s_Cfg[p]); }
bool HudLayout::Enabled() { return s_bOn; }
int  HudLayout::HookCount() { return s_bHooked ? s_nHooks : 0; }
void HudLayout::SetEnabled(bool on) { if(on && AdvancedSettingsLoaded()) { s_bOn = false; return; } s_bOn = on; if(on) { s_bWant = true; InstallHooks(); } }
bool HudLayout::Conflict() { return AdvancedSettingsLoaded(); }

// 5.00.04: measured from the two HUD screenshots (the mobile layout vs the compact one), in weapon-icon heights:
// the clock is ~0.27 lower, the bars ~0.19 higher; the weapon icon is ~11.5 % of the screen height.
const char* HudLayout::PresetName(int preset)
{
    static const char* const k[PRESET_COUNT] = { "Game default", "Compact" };
    return preset >= 0 && preset < PRESET_COUNT ? k[preset] : "";
}
void HudLayout::ApplyPreset(int preset)
{
    for(int p = 0; p < PART_COUNT; ++p) Defaults(s_Cfg[p]);
    s_nPreset = preset == PRESET_COMPACT ? PRESET_COMPACT : PRESET_GAME;   // 5.00.06: Compact = CompactDy(), not % offsets
    SetEnabled(s_nPreset == PRESET_COMPACT);
    Save();
}
int HudLayout::CurrentPreset() { return s_nPreset; }
void HudLayout::SetScreen(float w, float h)
{
    if(w > 16 && h > 16) { s_fW = w; s_fH = h; }
    static bool once = false; if(!once) { once = true; AdvsAutoApply(); }   // 5.00.08: every mod is loaded by the first frame
}
void HudLayout::Init(void* gtasa) { s_hGame = gtasa; if(s_bWant) InstallHooks(); }

void HudLayout::Load()
{
    if(!::cfg) return;
    for(int p = 0; p < PART_COUNT; ++p)
    {
        Cfg& c = s_Cfg[p]; char k[64];
        snprintf(k, sizeof(k), "%s_Show", kKeys[p]);   c.show  = ::cfg->GetBool(k, true, "GTAHUD");
        snprintf(k, sizeof(k), "%s_X", kKeys[p]);      c.x     = Clamp(::cfg->GetFloat(k, 0, "GTAHUD"), -100, 100);
        snprintf(k, sizeof(k), "%s_Y", kKeys[p]);      c.y     = Clamp(::cfg->GetFloat(k, 0, "GTAHUD"), -100, 100);
        snprintf(k, sizeof(k), "%s_Z", kKeys[p]);      c.z     = Clamp(::cfg->GetFloat(k, 100, "GTAHUD"), 10, 400);
        snprintf(k, sizeof(k), "%s_Size", kKeys[p]);   c.size  = Clamp(::cfg->GetFloat(k, 100, "GTAHUD"), 10, 400);
        snprintf(k, sizeof(k), "%s_Width", kKeys[p]);  c.w     = Clamp(::cfg->GetFloat(k, 100, "GTAHUD"), 10, 400);
        snprintf(k, sizeof(k), "%s_Height", kKeys[p]); c.h     = Clamp(::cfg->GetFloat(k, 100, "GTAHUD"), 10, 400);
        snprintf(k, sizeof(k), "%s_Alpha", kKeys[p]);  c.alpha = Clamp(::cfg->GetFloat(k, 100, "GTAHUD"), 0, 100);
    }
    bool on = ::cfg->GetBool("CustomHud", false, "GTAHUD");
    bool any = false; for(auto& c : s_Cfg) if(!Identity(c)) any = true;
    // 5.00.06: Compact is the default HUD (the PC-like stack the player asked for). An ini from 5.00.05 or older has
    // no Preset: 5.00.04's Compact (clock +3 %, bars -2.2 %) becomes the new one; never customized -> Compact too.
    int preset = ::cfg->GetInt("Preset", -1, "GTAHUD");
    if(preset < 0)
    {
        const bool old54 = s_Cfg[CLOCK].y == 3.0f && s_Cfg[BAR_HEALTH].y == -2.2f;
        if(old54) { s_Cfg[CLOCK].y = 0; s_Cfg[BAR_HEALTH].y = s_Cfg[BAR_ARMOR].y = s_Cfg[BAR_BREATH].y = 0; any = false; for(auto& c : s_Cfg) if(!Identity(c)) any = true; }
        if(old54 || !on || !any) { preset = PRESET_COMPACT; on = true; }
        else preset = PRESET_GAME;   // their own custom HUD stays as it was
    }
    s_nPreset = preset == PRESET_COMPACT ? PRESET_COMPACT : PRESET_GAME;
    SetEnabled(on && (any || s_nPreset == PRESET_COMPACT));   // all default + the game layout = nothing hooked
    s_bOn = on && !AdvancedSettingsLoaded();
    AdvsAutoApply();   // 5.00.08
}
void HudLayout::Save()
{
    if(!::cfg) return;
    Config::BeginBatch();   // 4.13.34: one ini write instead of 120 (the game froze on every HUD change)
    auto B = [](const char* k, bool v) { ConfigEntry* e = ::cfg->Bind(k, v, "GTAHUD"); e->SetBool(v); delete e; };
    auto F = [](const char* k, float v) { ConfigEntry* e = ::cfg->Bind(k, v, "GTAHUD"); e->SetFloat(v); delete e; };
    B("CustomHud", s_bOn);
    { ConfigEntry* e = ::cfg->Bind("Preset", s_nPreset, "GTAHUD"); e->SetInt(s_nPreset); delete e; }   // 5.00.06
    for(int p = 0; p < PART_COUNT; ++p)
    {
        const Cfg& c = s_Cfg[p]; char k[64];
        snprintf(k, sizeof(k), "%s_Show", kKeys[p]);   B(k, c.show);
        snprintf(k, sizeof(k), "%s_X", kKeys[p]);      F(k, c.x);
        snprintf(k, sizeof(k), "%s_Y", kKeys[p]);      F(k, c.y);
        snprintf(k, sizeof(k), "%s_Z", kKeys[p]);      F(k, c.z);
        snprintf(k, sizeof(k), "%s_Size", kKeys[p]);   F(k, c.size);
        snprintf(k, sizeof(k), "%s_Width", kKeys[p]);  F(k, c.w);
        snprintf(k, sizeof(k), "%s_Height", kKeys[p]); F(k, c.h);
        snprintf(k, sizeof(k), "%s_Alpha", kKeys[p]);  F(k, c.alpha);
    }
    ::cfg->EndBatch();
}

// ---- 5.00.08: Compact HUD through Advanced Settings' own ini ------------------------------------------------
// Advanced Settings units: Pos / Size = % of the screen. Bars: right edge PosX, grow left by SizeX, top PosY,
// height SizeY. Weapon icon: centre PosX / PosY, half width SizeX, half height SizeY. Text: PosY = top,
// Orientation 2 = right aligned at PosX. Clock right above the money, health / armor / breath bars right under,
// all just left of the weapon icon (Screenshots/IMG_20261117_172345.jpg). Same text as docs/hud/AdvancedSettings.SA.ini.
static const char kAdvsIni[] = R"MPINI([About]
PresetBy = MP Compact (correct HUD)

[HUD_PlayerInfo]
ForceDrawing = 0

[HUD_WeaponIcon]
DrawBehind = 0
PosX = 92.000000
PosY = 11.000000
SizeX = 3.800000
SizeY = 8.500000

[HUD_Ammo]
DisplayAbove9999 = 0
BGColor = 0 0 0 0
Color = 172 203 241 255
CalcWeaponRect = 1
Proportional = 1
FontStyle = 6
Orientation = 0
FontEdge = 1
AnimatedMult = 1
PosX = 92.000000
PosY = 20.300000
SizeX = 0.500000
SizeY = 1.300000

[HUD_Clock]
Format = %02d:%02d
BGColor = 0 0 0 0
Color = 225 225 225 255
Proportional = 0
Orientation = 2
FontStyle = 5
FontEdge = 1
PosX = 87.200000
PosY = 2.600000
SizeX = 0.550000
SizeY = 1.500000

[HUD_Money]
ColorMinus = 180 25 29 255
ColorPlus = 54 104 44 255
BGColor = 0 0 0 0
FontStyle = 5
Proportional = 0
Orientation = 2
FontEdge = 1
DropShadow = 0
PosX = 87.200000
PosY = 9.600000
SizeX = 0.550000
SizeY = 1.450000

[HUD_BarHealth]
BorderMult = 0.200000
BorderSide = 30
Color = 180 25 29 255
ConstWidth = 0
DrawMode = 0
FlashItem = 1
PCBehaviour = 0
Percent = 0
PosX = 87.200000
PosY = 16.600000
SizeX = 16.000000
SizeY = 2.200000
Vertical = 0

[HUD_BarArmor]
AlwaysShow = 0
BorderMult = 0.200000
BorderSide = 30
Color = 225 225 225 255
ConstWidth = 0
DrawMode = 0
FlashItem = 1
Percent = 0
PosX = 87.200000
PosY = 20.400000
SizeX = 16.000000
SizeY = 2.200000
Vertical = 0

[HUD_BarBreath]
AlwaysShow = 0
BorderMult = 0.200000
BorderSide = 30
Color = 137 162 192 255
DrawMode = 0
FlashItem = 1
Percent = 0
PosX = 87.200000
PosY = 24.200000
SizeX = 16.000000
SizeY = 2.200000
Vertical = 0

[HUD_WantedStar]
AlwaysShowShadow = 0
Color = 144 98 16 255
BGColor = 0 0 0 255
FontStyle = 4
FontEdge = 1
Orientation = 2
Proportional = 1
Padding = 0.150000
PosX = 96.000000
PosY = 27.800000
SizeX = 0.500000
SizeY = 1.500000
Vertical = 0
)MPINI";
#include <string>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>
namespace
{
std::string Trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while(a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    while(b > a && (s[b-1] == ' ' || s[b-1] == '\t' || s[b-1] == '\r' || s[b-1] == '\n')) --b;
    return s.substr(a, b - a);
}
bool SameI(const std::string& a, const std::string& b) { return a.size() == b.size() && strncasecmp(a.c_str(), b.c_str(), a.size()) == 0; }
void SplitLines(const std::string& t, std::vector<std::string>& out)
{
    size_t p = 0;
    while(p < t.size()) { size_t e = t.find('\n', p); if(e == std::string::npos) e = t.size(); std::string l = t.substr(p, e - p); if(!l.empty() && l.back() == '\r') l.pop_back(); out.push_back(l); p = e + 1; }
}
bool ReadAll(const char* path, std::string& out)
{
    FILE* f = fopen(path, "rb"); if(!f) return false;
    char buf[4096]; size_t n; out.clear();
    while((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    fclose(f); return true;
}
bool WriteAll(const char* path, const std::string& t)
{
    std::string tmp = std::string(path) + ".mp_tmp";
    FILE* f = fopen(tmp.c_str(), "wb"); if(!f) return false;
    const bool ok = fwrite(t.data(), 1, t.size(), f) == t.size(); fclose(f);
    if(!ok || rename(tmp.c_str(), path) != 0) { remove(tmp.c_str()); return false; }
    return true;
}
bool Exists(const char* p) { struct stat st; return stat(p, &st) == 0; }
struct AdvsKey { std::string sec, key, val; bool done; };
void ParseIni(const std::string& t, std::vector<AdvsKey>& out)
{
    std::vector<std::string> lines; SplitLines(t, lines); std::string sec;
    for(auto& l0 : lines)
    {
        std::string l = Trim(l0); if(l.empty() || l[0] == ';' || l[0] == '#') continue;
        if(l[0] == '[') { size_t e = l.find(']'); sec = Trim(l.substr(1, e == std::string::npos ? std::string::npos : e - 1)); continue; }
        size_t eq = l.find('='); if(eq == std::string::npos) continue;
        out.push_back({ sec, Trim(l.substr(0, eq)), Trim(l.substr(eq + 1)), false });
    }
}
}   // namespace

const char* HudLayout::AdvsIniText() { return kAdvsIni; }
const char* HudLayout::AdvsIniPath()
{
    static char path[512];
    const char* dir = aml ? aml->GetConfigPath() : NULL;
    if(!dir || !*dir) { path[0] = 0; return path; }
    const size_t n = strlen(dir); const char* sep = (n && dir[n-1] == '/') ? "" : "/";
    // the 64-bit build's ini, if that is the one on this phone; else the 32-bit name (ARM's mod id)
    snprintf(path, sizeof(path), "%s%sAdvancedSettings.SA64.ini", dir, sep);
    if(Exists(path)) return path;
    snprintf(path, sizeof(path), "%s%sAdvancedSettings.SA.ini", dir, sep);
    return path;
}
bool HudLayout::AdvsApplied()
{
    const char* ini = AdvsIniPath(); if(!*ini) return false;
    return Exists((std::string(ini) + ".mp_orig").c_str());
}
bool HudLayout::AdvsApplyCompact()
{
    const char* ini = AdvsIniPath(); if(!*ini) return false;
    std::string cur; const bool had = ReadAll(ini, cur);
    const std::string orig = std::string(ini) + ".mp_orig";
    if(!Exists(orig.c_str()) && !WriteAll(orig.c_str(), had ? cur : std::string())) { logger->Info("GTA HUD: cannot back up %s", ini); return false; }
    std::vector<AdvsKey> want; ParseIni(kAdvsIni, want);
    std::vector<std::string> lines; SplitLines(cur, lines);
    while(!lines.empty() && Trim(lines.back()).empty()) lines.pop_back();
    // 1) replace the keys that are there already (in their section)
    std::string sec;
    for(auto& l0 : lines)
    {
        std::string l = Trim(l0); if(l.empty() || l[0] == ';' || l[0] == '#') continue;
        if(l[0] == '[') { size_t e = l.find(']'); sec = Trim(l.substr(1, e == std::string::npos ? std::string::npos : e - 1)); continue; }
        size_t eq = l.find('='); if(eq == std::string::npos) continue;
        const std::string k = Trim(l.substr(0, eq));
        for(auto& w : want) if(!w.done && SameI(w.sec, sec) && SameI(w.key, k)) { l0 = w.key + " = " + w.val; w.done = true; break; }
    }
    // 2) the missing keys at the end of their section (or a new section at the end)
    for(auto& w : want)
    {
        if(w.done) continue;
        int secLine = -1, last = -1; std::string s2;
        for(int i = 0; i < (int)lines.size(); ++i)
        {
            std::string l = Trim(lines[i]);
            if(!l.empty() && l[0] == '[') { size_t e = l.find(']'); s2 = Trim(l.substr(1, e == std::string::npos ? std::string::npos : e - 1)); if(SameI(s2, w.sec)) { secLine = i; last = i; } continue; }
            if(secLine >= 0 && SameI(s2, w.sec) && !l.empty()) last = i;
        }
        const std::string kv = w.key + " = " + w.val;
        if(secLine < 0) { if(!lines.empty()) lines.push_back(""); lines.push_back("[" + w.sec + "]"); lines.push_back(kv); }
        else lines.insert(lines.begin() + last + 1, kv);
        w.done = true;
    }
    std::string out; for(auto& l : lines) { out += l; out += '\n'; }
    if(!WriteAll(ini, out)) { logger->Info("GTA HUD: cannot write %s", ini); return false; }
    logger->Info("GTA HUD: Compact HUD written to Advanced Settings (%s, your own ini kept as .mp_orig)", ini);
    return true;
}
bool HudLayout::AdvsRestore()
{
    const char* ini = AdvsIniPath(); if(!*ini) return false;
    const std::string orig = std::string(ini) + ".mp_orig";
    std::string t; if(!ReadAll(orig.c_str(), t)) return false;
    if(t.empty()) remove(ini);   // there was no ini before MP: Advanced Settings makes its default one again
    else if(!WriteAll(ini, t)) return false;
    remove(orig.c_str());
    logger->Info("GTA HUD: your own Advanced Settings ini is back (%s)", ini);
    return true;
}
void HudLayout::AdvsAutoApply()
{
    if(!::cfg || !AdvancedSettingsLoaded() || s_nPreset != PRESET_COMPACT) return;
    if(::cfg->GetBool("AdvsCompactDone", false, "GTAHUD")) return;   // once; after that only the GTA HUD buttons
    if(AdvsApplied() || AdvsApplyCompact())
    {
        ConfigEntry* e = ::cfg->Bind("AdvsCompactDone", true, "GTAHUD"); e->SetBool(true); delete e;
    }
}
