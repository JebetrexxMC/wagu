// MP 5.01.11: the game's settings table for the neon Settings page (see mobilesettings.h). The layout and the side
// effects are PCMenu's (PCMenu/source/jni/main.cpp ReadSetting / WriteSetting / ApplySideEffect): SAUtils moves the
// table (pNewSettings, 1200 entries) and points the game's GOT slot at it, so the table is read through that slot.
#include <mod/amlmod.h>
#include <mod/logger.h>
#include <dlfcn.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/uio.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "game/mobilesettings.h"

#ifdef AML32
    #define MS_STRIDE  0x20
    #define MS_NAMES   0x04
    #define MS_VALUE   0x08
    #define MS_MIN     0x10
    #define MS_MAX     0x14
    #define MS_VISIBLE 0x18
    #define MS_TYPE    0x1C
    #define MS_GOT     0x679A40
#else
    #define MS_STRIDE  0x28
    #define MS_NAMES   0x08
    #define MS_VALUE   0x10
    #define MS_MIN     0x18
    #define MS_MAX     0x1C
    #define MS_VISIBLE 0x20
    #define MS_TYPE    0x24
    #define MS_GOT     0x851498
#endif

extern uintptr_t pGameLib;

namespace
{
    bool      s_bInit = false;
    uintptr_t s_GotSlot = 0, s_Orig = 0;
    void (*fnSetupLayout)() = nullptr;
    void (*fnApplyAudio)() = nullptr;
    void (*fnRetune)(void*, signed char) = nullptr;
    void (*fnSetHRTF)(void*, int) = nullptr;
    void *s_pAudioEngine = nullptr, *s_pAEHw = nullptr;
    unsigned short* (*fnTextGet)(void*, const char*) = nullptr;
    void* s_pTheText = nullptr;
    void (*fnSave)() = nullptr;
    const char* s_szSave = "no save function found (the game saves its settings when you leave its own Options)";

    struct SAUSetting
    {
        int nSettingId; unsigned char eType, byteItemType; char szName[32];
        void (*fnOnValueChange)(int, int, void*); const char* (*fnOnValueDraw)(int, void*); void* fnOnButtonPressed;
        int nInitVal, nSavedVal, nMaxVal; void* pOwnData;
    };
    void** s_pSauVec = nullptr; int s_nSauTries = 0;

    bool SafeCopy(const void* addr, void* out, size_t len)
    {
        if(!addr) return false;
        struct iovec local = { out, len }, remote = { (void*)addr, len };
        return syscall(__NR_process_vm_readv, (long)getpid(), &local, 1L, &remote, 1L, 0L) == (long)len;
    }
    uint8_t* Base()
    {
        uintptr_t p = 0;
        if(s_GotSlot && SafeCopy((void*)s_GotSlot, &p, sizeof(p)) && p)
        {   // the slot really points at a settings table (SFX volume 0..N)
            int mn = -1, mx = -1;
            if(SafeCopy((uint8_t*)p + 11 * MS_STRIDE + MS_MIN, &mn, 4) && SafeCopy((uint8_t*)p + 11 * MS_STRIDE + MS_MAX, &mx, 4) && mn == 0 && mx > 0 && mx <= 1000) return (uint8_t*)p;
        }
        return (uint8_t*)s_Orig;
    }
    int MaxId() { uint8_t* b = Base(); return (b && (uintptr_t)b != s_Orig) ? 1199 : 36; }   // SAUtils' table has room for 1200
    bool Raw(int id, uint8_t* buf)
    {
        if(id < 0 || id > MaxId()) return false;
        uint8_t* b = Base(); if(!b) return false;
        return SafeCopy(b + (size_t)id * MS_STRIDE, buf, MS_STRIDE);
    }
    SAUSetting* SauFind(int id, SAUSetting* copy)
    {
        if(id < 37) return nullptr;
        if(!s_pSauVec && s_nSauTries < 8)
        {
            ++s_nSauTries;
            void* h = dlopen("libSAUtils.so", RTLD_NOW | RTLD_NOLOAD);
            if(h) { s_pSauVec = (void**)dlsym(h, "gMoreSettings"); dlclose(h); if(s_pSauVec) logger->Info("Neon settings: SAUtils mod settings (gMoreSettings %p)", (void*)s_pSauVec); }
        }
        if(!s_pSauVec) return nullptr;
        SAUSetting** b = nullptr; SAUSetting** e = nullptr;
        if(!SafeCopy(s_pSauVec, &b, sizeof(b)) || !SafeCopy(s_pSauVec + 1, &e, sizeof(e)) || !b || e < b || e - b > 1200) return nullptr;
        for(SAUSetting** p = b; p < e; ++p)
        {
            SAUSetting* a = nullptr;
            if(!SafeCopy(p, &a, sizeof(a)) || !a || !SafeCopy(a, copy, sizeof(*copy))) continue;
            if(id >= 0 && copy->nSettingId == id) return a;
        }
        return nullptr;
    }
    // GXT text (16-bit) -> ASCII upper case (the neon atlases are ASCII)
    std::string Gxt(const char* key)
    {
        std::string out;
        if(!key || !key[0]) return out;
        if(fnTextGet && s_pTheText)
        {
            unsigned short* w = fnTextGet(s_pTheText, key);
            unsigned short c = 0;
            for(int i = 0; w && i < 60 && SafeCopy(w + i, &c, 2) && c; ++i)
            {
                if(c == '~') { int j = i + 1; unsigned short d = 0; while(j < i + 4 && SafeCopy(w + j, &d, 2) && d && d != '~') ++j; if(d == '~') { i = j; continue; } }
                if(c >= 32 && c < 127) out += (char)c;
                else if(c >= 128) out += '?';
            }
            bool junk = out.empty() || out.find('?') != std::string::npos;
            if(!junk) { for(auto& ch : out) if(ch >= 'a' && ch <= 'z') ch = (char)(ch - 32); return out; }
        }
        out = key;
        for(auto& ch : out) if(ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
        return out;
    }
    void SideEffect(int id)
    {
        GameSettings::View v;
        switch(id)
        {
            case 9:  if(fnSetupLayout) fnSetupLayout(); break;
            case 11: case 12: if(fnApplyAudio) fnApplyAudio(); break;
            case 14: if(fnRetune && s_pAudioEngine && GameSettings::Read(14, v)) fnRetune(s_pAudioEngine, (signed char)v.value); break;
            case 33: if(fnSetHRTF && s_pAEHw && GameSettings::Read(33, v)) fnSetHRTF(s_pAEHw, v.value); break;
            default: break;
        }
    }

    struct Stock { int id, tab; const char* label; };
    const Stock kStock[] = {
        { 3, 0, "Steering" }, { 23, 0, "Steering sensitivity" }, { 18, 0, "Bike controls" }, { 6, 0, "Targeting" },
        { 8, 0, "Tilt steering" }, { 9, 0, "Touch layout" }, { 15, 0, "Invert look" }, { 25, 0, "Look sensitivity X" },
        { 26, 0, "Look sensitivity Y" }, { 31, 0, "Mouse sensitivity X" }, { 32, 0, "Mouse sensitivity Y" },
        { 34, 0, "Controller scheme" }, { 29, 0, "Vibration" }, { 35, 0, "Auto climb" },
        { 10, 1, "Camera height" }, { 21, 1, "Cinematic camera" }, { 24, 1, "Subtitles" }, { 17, 1, "Language" },
        { 0, 2, "Visual quality" }, { 1, 2, "Resolution" }, { 2, 2, "Draw distance" }, { 5, 2, "Shadows" },
        { 7, 2, "Car reflections" }, { 4, 2, "Traffic density" }, { 16, 2, "Brightness" }, { 19, 2, "HUD" },
        { 30, 2, "Frame limiter" },
        { 11, 3, "Sound effects volume" }, { 12, 3, "Radio volume" }, { 13, 3, "Radio auto tune" },
        { 14, 3, "Default radio station" }, { 33, 3, "3D audio (HRTF)" },
    };
}

bool GameSettings::Init(void* h)
{
    if(s_bInit) return true;
    if(!h || !pGameLib) return false;
    s_Orig = (uintptr_t)aml->GetSym(h, "_ZN14MobileSettings8settingsE");
#ifdef AML32
    if(*(uint32_t*)(pGameLib + 0x202020) == 0xE8BDB001) s_GotSlot = pGameLib + MS_GOT;   // GTA SA 2.00 (SAUtils' check)
#else
    s_GotSlot = pGameLib + MS_GOT;
#endif
    *(void**)&fnSetupLayout = (void*)aml->GetSym(h, "_ZN15CTouchInterface18SetupLayoutObjectsEv");
    *(void**)&fnApplyAudio  = (void*)aml->GetSym(h, "_Z23Menu_ApplyAudioSettingsv");
    *(void**)&fnRetune      = (void*)aml->GetSym(h, "_ZN12CAudioEngine11RetuneRadioEa");
    *(void**)&fnSetHRTF     = (void*)aml->GetSym(h, "_ZN16CAEAudioHardware7SetHRTFEi");
    s_pAudioEngine = (void*)aml->GetSym(h, "AudioEngine");
    s_pAEHw        = (void*)aml->GetSym(h, "AEAudioHardware");
    *(void**)&fnTextGet = (void*)aml->GetSym(h, "_ZN5CText3GetEPKc");
    s_pTheText = (void*)aml->GetSym(h, "TheText");
    static const char* const kSave[] = { "_ZN14MobileSettings4SaveEv", "_ZN14MobileSettings12SaveSettingsEv", "_ZN14MobileSettings13SaveToStorageEv", "_Z12SaveSettingsv", "_Z17Menu_SaveSettingsv" };
    for(const char* s : kSave) if(void* f = (void*)aml->GetSym(h, s)) { *(void**)&fnSave = f; s_szSave = s; break; }
    s_bInit = s_Orig || s_GotSlot;
    logger->Info("Neon settings: table %p (GOT slot %p, game's own %p), text %p, save %s", (void*)Base(), (void*)s_GotSlot, (void*)s_Orig, (void*)fnTextGet, s_szSave);
    return s_bInit;
}
bool GameSettings::Ready() { return s_bInit && Base() != nullptr; }
const char* GameSettings::SaveInfo() { return s_szSave; }

bool GameSettings::Read(int id, View& v)
{
    uint8_t buf[64];
    if(!Raw(id, buf)) return false;
    memcpy(&v.value, buf + MS_VALUE, 4); memcpy(&v.vmin, buf + MS_MIN, 4); memcpy(&v.vmax, buf + MS_MAX, 4); memcpy(&v.type, buf + MS_TYPE, 4);
    return v.vmax >= v.vmin && v.vmax - v.vmin <= 100000;
}
bool GameSettings::Write(int id, int val)
{
    View v; if(!Read(id, v)) return false;
    if(val < v.vmin) val = v.vmin; if(val > v.vmax) val = v.vmax;
    volatile int* p = (volatile int*)(Base() + (size_t)id * MS_STRIDE + MS_VALUE);
    if(*p == val) return false;
    const int old = *p;
    *p = val;
    SideEffect(id);
    SAUSetting cp; if(SAUSetting* a = SauFind(id, &cp))
    {   // what SAUtils does when its settings screen closes: the mod's callback + its saved value
        if(cp.fnOnValueChange) cp.fnOnValueChange(cp.nSavedVal, val, cp.pOwnData);
        a->nSavedVal = val;
    }
    (void)old;
    return true;
}
void GameSettings::Step(int id, int dir)
{
    View v; if(!Read(id, v)) return;
    if(v.type == 1)
    {
        int step = (v.vmax - v.vmin) / 20; if(step < 1) step = 1;
        Write(id, v.value + (dir < 0 ? -step : step));
        return;
    }
    int nv = v.value + (dir < 0 ? -1 : 1);
    if(nv > v.vmax) nv = v.vmin; if(nv < v.vmin) nv = v.vmax;
    Write(id, nv);
}
void GameSettings::SetFraction(int id, float f)
{
    View v; if(!Read(id, v)) return;
    if(f < 0) f = 0; if(f > 1) f = 1;
    Write(id, v.vmin + (int)(f * (v.vmax - v.vmin) + 0.5f));
}
std::string GameSettings::ValueText(int id)
{
    View v; if(!Read(id, v)) return "-";
    char num[32];
    SAUSetting cp;
    if(SauFind(id, &cp) && cp.fnOnValueDraw)
    {
        const char* t = cp.fnOnValueDraw(v.value, cp.pOwnData);
        char b[48] = { 0 };
        if(t && SafeCopy(t, b, 1)) { for(int i = 0; i < 47; ++i) { if(!SafeCopy(t + i, b + i, 1) || !b[i]) break; } b[47] = 0; if(b[0]) return Gxt(b); }
    }
    if(v.type == 1)
    {
        const int r = v.vmax - v.vmin;
        snprintf(num, sizeof(num), "%d%%", r > 0 ? (v.value - v.vmin) * 100 / r : 0);
        return num;
    }
    snprintf(num, sizeof(num), "%d", v.value);
    uint8_t buf[64]; const char** names = nullptr;
    if(!Raw(id, buf)) return num;
    memcpy(&names, buf + MS_NAMES, sizeof(names));
    const int idx = v.value - v.vmin;
    if(!names || idx < 0 || idx > 63) return (v.vmin == 0 && v.vmax == 1) ? std::string(v.value ? "ON" : "OFF") : std::string(num);
    const char* p = nullptr; char key[64] = { 0 };
    if(!SafeCopy(names + idx, &p, sizeof(p)) || !p) return num;
    for(int i = 0; i < 63; ++i) { if(!SafeCopy(p + i, key + i, 1) || !key[i]) break; }
    key[63] = 0;
    if(!key[0]) return num;
    std::string t = Gxt(key);
    return t.empty() ? std::string(num) : t;
}
void GameSettings::Items(std::vector<Item>& out)
{
    out.clear();
    if(!Ready()) return;
    for(const Stock& s : kStock)
    {
        uint8_t buf[64]; View v;
        if(!Raw(s.id, buf) || !Read(s.id, v) || v.vmax <= v.vmin) continue;
        if(!buf[MS_VISIBLE]) continue;   // the game hides it on this device
        Item it; it.id = s.id; it.tab = s.tab; it.kind = v.type == 1 ? K_SLIDER : K_TOGGLE;
        snprintf(it.label, sizeof(it.label), "%s", s.label);
        out.push_back(it);
    }
    if(MaxId() < 37) return;   // SAUtils is not there (or did not move the table)
    SAUSetting tmp; SauFind(-1, &tmp);   // looks gMoreSettings up
    if(!s_pSauVec) return;
    SAUSetting** b = nullptr; SAUSetting** e = nullptr;
    if(!SafeCopy(s_pSauVec, &b, sizeof(b)) || !SafeCopy(s_pSauVec + 1, &e, sizeof(e)) || !b || e < b || e - b > 1200) return;
    for(SAUSetting** p = b; p < e; ++p)
    {
        SAUSetting* a = nullptr; SAUSetting cp;
        if(!SafeCopy(p, &a, sizeof(a)) || !a || !SafeCopy(a, &cp, sizeof(cp))) continue;
        if(cp.byteItemType == 2 || cp.nSettingId < 37 || cp.nSettingId > 1199) continue;   // buttons open the mod's own screen: not here
        View v; if(!Read(cp.nSettingId, v)) continue;
        Item it; it.id = cp.nSettingId;
        it.tab = cp.eType == 0 ? 0 : cp.eType == 1 ? 1 : cp.eType == 2 ? 2 : cp.eType == 3 ? 3 : cp.eType == 4 ? 1 : 5;
        it.kind = cp.byteItemType == 1 ? K_SLIDER : K_TOGGLE;
        cp.szName[31] = 0;
        std::string l = Gxt(cp.szName);
        snprintf(it.label, sizeof(it.label), "%s", l.c_str());
        out.push_back(it);
    }
}
void GameSettings::Save()
{
    if(fnSave) { fnSave(); logger->Info("Neon settings: saved (%s)", s_szSave); }
}
